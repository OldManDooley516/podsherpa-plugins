"""Waypoint desktop prototype. Start here with Python 3.10 or newer."""

import argparse
import json
import queue
import sys
import threading
import tkinter as tk
from dataclasses import asdict
from pathlib import Path
from tkinter import filedialog, font, messagebox, ttk

from delivery import ACX, PROFILES, assess, export_wav, peak_time, scan_file
from meter import LIMIT


ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / "assets"
try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
    DropSupport = TkinterDnD.DnDWrapper
except ImportError:
    TkinterDnD = None
    DropSupport = object

BACKGROUND = "#090e13"
PANEL = "#11171d"
TEXT = "#edf2f7"
MUTED = "#c7d6e0"
MINT = "#61d4bf"
BLUE = "#29bae8"
GRID = "#304a59"
AMBER = "#ffcf85"


def reading(value, units, precision=1, signed=False):
    if value is None:
        return "—"
    sign = "+" if signed else ""
    return f"{value:{sign}.{precision}f} {units}".rstrip()


def format_report(filename, result, profile, receipt=""):
    verdict = assess(result, profile)
    layout = "Mono" if result.channels == 1 else "Stereo"
    correlation = "N/A (mono)" if result.channels == 1 else reading(result.correlation, "", 2, True)
    lines = [
        "Waypoint | Delivery report",
        f"File: {filename}",
        f"Layout: {layout}",
        f"Profile: {profile} | {verdict.spec}",
        f"Integrated loudness: {reading(result.integrated, 'LUFS')}",
        f"True peak: {reading(result.true_peak, 'dBTP')}",
        f"Loudness range: {reading(result.loudness_range, 'LU')}",
        f"Stereo correlation: {correlation}",
        f"Chapter RMS: {reading(result.rms_dbfs, 'dBFS')}",
        f"Sample peak: {reading(result.sample_peak_dbfs, 'dBFS')}",
        f"Audio: {result.codec} | {result.sample_rate} Hz | {reading(result.bitrate / 1000 if result.bitrate else None, 'kb/s', 0)}",
        f"Result: {verdict.status}",
    ]
    if result.peak_window_start is not None:
        lines.append(f"First maximum true-peak window near {peak_time(result.peak_window_start)}")
    lines.extend(f"- {note}" for note in verdict.notes)
    lines.append(verdict.plan)
    if receipt:
        lines.append(receipt)
    return "\n".join(lines)


class Waypoint(tk.Tk, DropSupport):
    def __init__(self):
        super().__init__()
        self.title("PodSherpa Waypoint · Loudness & Delivery · v0.1.3")
        self.configure(bg=BACKGROUND)
        self.geometry("1000x700")
        self.minsize(840, 635)
        self.results = queue.Queue()
        self.measurement = None
        self.path = None
        self.stamp = None
        self.receipt = ""
        self.busy = False
        self.profile = tk.StringVar(value="Auto")

        families = set(font.families(self))
        default_face = font.nametofont("TkDefaultFont").actual("family")
        self.body = next((f for f in ("Manrope Medium", "Manrope") if f in families), default_face)
        self.numbers = next((f for f in ("Manrope SemiBold", "Manrope") if f in families), default_face)
        self.title_face = next((f for f in ("Montserrat SemiBold", "Montserrat") if f in families), default_face)
        self.build_styles()
        self.panel = tk.Frame(self, bg=BACKGROUND, highlightthickness=2,
                              highlightbackground=BACKGROUND, highlightcolor=BACKGROUND)
        self.panel.pack(fill="both", expand=True)
        header = tk.Frame(self.panel, bg=BACKGROUND)
        header.pack(fill="x", padx=24, pady=(14, 12))
        self.logo = tk.PhotoImage(file=ASSETS / "podsherpa.png")
        tk.Label(header, image=self.logo, bg=BACKGROUND, borderwidth=0).pack(side="left", padx=(0, 24))
        title = tk.Frame(header, bg=BACKGROUND)
        title.pack(side="left")
        self.label(title, "Waypoint", size=32, face=self.title_face, bold=True).pack(anchor="w")
        self.label(title, "Loudness & Delivery", color=MUTED).pack(anchor="w")
        self.label(header, "v0.1.3", color=MUTED, size=14).pack(side="right")

        tools = tk.Frame(self.panel, bg=PANEL, padx=12, pady=8)
        tools.pack(fill="x", padx=24)
        self.open_button = ttk.Button(tools, text="Open export...", command=self.open_file)
        self.filename = self.label(tools, "No file selected", color=MUTED, bg=PANEL)
        self.profiles = ttk.Combobox(tools, width=15, state="readonly",
                                     textvariable=self.profile, values=PROFILES, font=(self.body, -15))
        self.profiles.pack(side="right")
        self.profiles.bind("<<ComboboxSelected>>", self.change_profile)
        self.label(tools, "Delivery profile", color=MUTED, bg=PANEL, size=14).pack(side="right", padx=(10, 8))
        self.filename.pack(side="left", padx=12, fill="x", expand=True)

        scope = tk.Frame(self.panel, bg=BACKGROUND)
        scope.pack(fill="both", expand=True, padx=24, pady=(16, 12))
        rail = tk.Frame(scope, bg=PANEL, width=206)
        rail.pack(side="right", fill="y", padx=(18, 0))
        rail.pack_propagate(False)
        self.rail = tk.Canvas(rail, bg=PANEL, highlightthickness=0)
        self.rail.pack(fill="both", expand=True)
        self.rail.bind("<Configure>", lambda event: self.draw_rail())

        self.label(scope, "Short-term loudness", size=17, bold=True).pack(anchor="w")
        self.label(scope, "3-second window  ·  LUFS", color=MUTED, size=14).pack(anchor="w")
        self.graph = tk.Canvas(scope, bg=BACKGROUND, highlightthickness=0, height=150)
        self.graph.pack(fill="both", expand=True, pady=(4, 0))
        self.empty_open = ttk.Button(self.graph, text="Open export...", command=self.open_file)
        self.graph.bind("<Configure>", lambda event: self.draw_graph())

        secondary = tk.Frame(scope, bg=BACKGROUND)
        secondary.pack(fill="x", pady=(7, 0))
        self.metric_names, self.values = [], []
        for name in ("Loudness range", "Stereo correlation"):
            card = tk.Frame(secondary, bg=PANEL, padx=12, pady=5)
            card.pack(side="left", padx=(0, 3), fill="both", expand=True)
            label = self.label(card, name, color=MUTED, bg=PANEL, size=14)
            label.pack(anchor="w")
            self.metric_names.append(label)
            value = self.label(card, "—", size=24, bold=True, bg=PANEL)
            value.pack(anchor="w")
            self.values.append(value)

        self.rule()
        review = tk.Frame(self.panel, bg=BACKGROUND)
        review.pack(fill="x", padx=24, pady=(8, 0))
        self.status = self.label(review, "Ready to scan", size=19, bold=True)
        self.status.pack(anchor="w", fill="x")
        self.detail = self.label(review, "", color=MUTED, size=14)
        self.detail.pack(anchor="w", fill="x")
        self.plan = self.label(review, "", size=15)
        self.plan.pack(anchor="w", fill="x")
        self.export_info = self.label(review, "", color=MINT, size=14)
        self.export_info.pack(anchor="w", fill="x")
        def wrap_review(event):
            for widget in (self.status, self.detail, self.plan, self.export_info):
                widget.configure(wraplength=max(250, event.width - 8))
        review.bind("<Configure>", wrap_review)
        actions = tk.Frame(self.panel, bg=BACKGROUND)
        actions.pack(fill="x", padx=24, pady=(5, 14))
        self.export_button = ttk.Button(actions, text="Export gain-adjusted WAV...",
                                        command=self.save_wav, state="disabled")
        self.export_button.pack(side="left")
        self.copy_button = ttk.Button(actions, text="Copy report", command=self.copy_report, state="disabled")
        self.copy_button.pack(side="right")
        self.drops_enabled = False
        try:
            if TkinterDnD is None:
                raise ImportError("Install the dependencies from requirements.txt to enable file drop.")
            TkinterDnD.require(self)
            self.drop_target_register(DND_FILES)
            self.dnd_bind("<<DropEnter>>", self.drag_enter)
            self.dnd_bind("<<DropPosition>>", self.drag_position)
            self.dnd_bind("<<DropLeave>>", self.drag_leave)
            self.dnd_bind("<<Drop>>", self.drop_file)
            self.drops_enabled = True
        except (ImportError, RuntimeError, OSError, tk.TclError) as error:
            self.status.configure(text="Drag-and-drop unavailable. Use Open export…", fg=AMBER)
            print(f"Waypoint file drop: {error}", file=sys.stderr)

    def build_styles(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TButton", font=(self.body, -15), padding=(12, 7), background=MINT,
                        foreground=BACKGROUND, borderwidth=1, bordercolor=MINT, focuscolor=BACKGROUND)
        style.map("TButton", background=[("disabled", PANEL), ("active", "#83e2d2")],
                  foreground=[("disabled", "#889ba6")], bordercolor=[("disabled", GRID)])
        style.configure("TCombobox", padding=5, arrowsize=14, arrowcolor=TEXT, background=PANEL,
                        fieldbackground=PANEL, foreground=TEXT, bordercolor=GRID)
        style.map("TCombobox", fieldbackground=[("readonly", PANEL)], foreground=[("readonly", TEXT)],
                  selectforeground=[("readonly", TEXT)], selectbackground=[("readonly", PANEL)])
        self.option_add("*TCombobox*Listbox.background", PANEL)
        self.option_add("*TCombobox*Listbox.foreground", TEXT)

    def rule(self):
        tk.Frame(self.panel, height=1, bg=GRID).pack(fill="x", padx=24)

    def label(self, parent, text, color=TEXT, bg=BACKGROUND, size=16, bold=False, face=None, **options):
        family = face or (self.numbers if bold else self.body)
        weight = "bold" if bold and "semibold" not in family.lower() else "normal"
        return tk.Label(parent, text=text, fg=color, bg=bg, justify="left", anchor="w",
                        font=(family, -size, weight), **options)

    def drag_position(self, event):
        return "refuse_drop" if self.busy or self.grab_current() is not None else "copy"

    def drag_enter(self, event):
        action = self.drag_position(event)
        if action == "copy":
            self.panel.configure(highlightbackground=BLUE, highlightcolor=BLUE)
        return action

    def drag_leave(self, event):
        self.panel.configure(highlightbackground=BACKGROUND, highlightcolor=BACKGROUND)

    def drop_file(self, event):
        self.drag_leave(event)
        if self.drag_position(event) != "copy":
            return "refuse_drop"
        try:
            paths = self.tk.splitlist(event.data)
            if len(paths) != 1:
                raise ValueError("Drop one file at a time.")
            path = Path(paths[0])
            if not path.is_file():
                raise ValueError("Drop an audio or video file.")
        except (TypeError, ValueError, OSError, tk.TclError) as error:
            self.status.configure(text=str(error), fg=AMBER)
            return "refuse_drop"
        self.scan_path(path)
        return "copy"

    def open_file(self):
        path = filedialog.askopenfilename(
            title="Open rendered podcast export",
            filetypes=[("Common media", "*.wav *.mp3 *.m4a *.flac *.aif *.aiff *.ogg *.opus *.aac *.mp4 *.mov *.mkv"),
                       ("All files", "*")],
        )
        if path:
            self.scan_path(Path(path))

    def scan_path(self, path):
        if self.busy:
            return

        def scanned(result):
            self.path = path
            self.measurement, self.stamp = result
            self.receipt = ""
            self.filename.configure(text=path.name)
            self.open_button.pack(side="left", before=self.filename)

        self.start_job(lambda: scan_file(path), scanned, f"Scanning {path.name}…")

    def start_job(self, task, on_success, message):
        self.busy = True
        self.draw_graph()
        for widget in (self.open_button, self.profiles, self.copy_button, self.export_button):
            widget.configure(state="disabled")
        self.status.configure(text=message, fg=TEXT)

        def worker():
            try:
                self.results.put((task(), None))
            except Exception as error:
                self.results.put((None, str(error)))

        threading.Thread(target=worker, daemon=True).start()

        def poll():
            try:
                result, error = self.results.get_nowait()
            except queue.Empty:
                self.after(100, poll)
                return
            self.busy = False
            self.open_button.configure(state="normal")
            self.profiles.configure(state="readonly")
            if error is None:
                on_success(result)
            self.show_result()
            if error is not None:
                self.status.configure(text=error, fg=AMBER)

        self.after(100, poll)

    def change_profile(self, event):
        self.receipt = ""
        self.show_result()

    def save_wav(self):
        profile = self.profile.get()
        source, measured, stamp = self.path, self.measurement, self.stamp
        path = filedialog.asksaveasfilename(
            title="Save a new 24-bit WAV master", defaultextension=".wav",
            initialdir=source.parent, initialfile=source.stem + "-Waypoint.wav",
            filetypes=[("24-bit PCM WAV", "*.wav")],
        )
        if not path:
            return
        if Path(path).exists():
            messagebox.showinfo("Choose a new filename", "Waypoint saves a new copy. Choose an unused filename.")
            return

        def verified(output):
            self.receipt = (f"Verified {Path(path).name}  ·  "
                            f"{output.integrated:.1f} LUFS  ·  {output.true_peak:.1f} dBTP")

        self.receipt = ""
        self.export_info.configure(text="")
        self.start_job(lambda: export_wav(source, path, measured, stamp, profile),
                       verified, "Rendering and remeasuring WAV…")

    def copy_report(self):
        if self.measurement is None:
            return
        report = format_report(self.filename.cget("text"), self.measurement, self.profile.get(), self.receipt)
        self.clipboard_clear()
        self.clipboard_append(report)
        self.copy_button.configure(text="Copied")

    def show_result(self):
        result = self.measurement
        self.copy_button.configure(text="Copy report", state="normal" if result else "disabled")
        acx = self.profile.get() == ACX
        names = ("Integrated", "True peak") if acx else ("Loudness range", "Stereo correlation")
        for label, name in zip(self.metric_names, names):
            label.configure(text=name)
        self.export_info.configure(text=self.receipt)
        self.export_button.configure(state="disabled")
        self.plan.configure(text="")
        if result:
            verdict = assess(result, self.profile.get())
            self.plan.configure(text=verdict.plan)
            if verdict.gain_db is not None:
                self.export_button.configure(state="normal")
            values = (
                reading(result.loudness_range, "LU"),
                "Mono file" if result.channels == 1 else reading(result.correlation, "", 2, True),
            )
            layout = "Mono file" if result.channels == 1 else "Stereo file"
            if acx:
                values = (reading(result.integrated, "LUFS"), reading(result.true_peak, "dBTP"))
            self.status.configure(text=verdict.status, fg=AMBER if verdict.notes else MINT)
            text = f"{layout}  ·  {verdict.spec}"
            if verdict.notes:
                text += "\n" + " ".join(verdict.notes)
            self.detail.configure(text=text)
        else:
            values = ("—",) * 2
            self.status.configure(text="Ready to scan", fg=TEXT)
            self.detail.configure(text="")
        for value, label in zip(values, self.values):
            label.configure(text=value)
        self.draw_graph()
        self.draw_rail()

    def draw_rail(self):
        rail = self.rail
        rail.delete("all")
        w, h = rail.winfo_width(), rail.winfo_height()
        if w < 10 or h < 100:
            return
        m = self.measurement
        if self.profile.get() == ACX:
            lanes = [("Chapter RMS", "dBFS", m.rms_dbfs if m else None, -60, 0, (-23, -18)),
                     ("Sample peak", "dBFS", m.sample_peak_dbfs if m else None, -36, 0, (-3,))]
        else:
            target = assess(m, self.profile.get()).target_lufs if m else None
            lanes = [("Integrated", "LUFS", m.integrated if m else None, -48, 0, (target,)),
                     ("True peak", "dBTP", m.true_peak if m else None, -36, 0, (LIMIT,))]
        top, bottom = 85, h - 25
        for index, (name, unit, value, low, high, targets) in enumerate(lanes):
            x = (index + 0.5) * w / 2
            rail.create_text(x, 16, text=name, fill=MUTED, font=(self.body, -14))
            rail.create_text(x, 42, text="—" if value is None else f"{value:.1f}",
                             fill=TEXT, font=(self.numbers, -25))
            rail.create_text(x, 65, text=unit, fill=MUTED, font=(self.body, -14))
            segments = 24
            step = (bottom - top) / segments
            for n in range(segments):
                level = low + (n + 1) / segments * (high - low)
                y = bottom - n * step
                color = MINT if value is not None and value >= level else "#24333d"
                rail.create_rectangle(x-12, y-step+1, x+12, y, fill=color, outline="")
            for target in targets:
                if target is not None:
                    y = bottom - (target-low) / (high-low) * (bottom-top)
                    rail.create_line(x-17, y, x+17, y, fill=BLUE, width=2)
            rail.create_text(x, h-12, text=f"{low} to {high}", fill=MUTED, font=(self.body, -13))

    def draw_graph(self):
        graph = self.graph
        graph.delete("all")
        width, height = graph.winfo_width(), graph.winfo_height()
        points = self.measurement.short_term if self.measurement else []
        active = [level for _, level in points if level is not None]
        if not active:
            prompt = "No short-term loudness to display"
            idle = self.measurement is None and not self.busy
            if self.measurement is None:
                prompt = "Scanning audio..." if self.busy else (
                    "Drop an audio or video file here" if self.drops_enabled else "Choose an audio or video file")
            graph.create_rectangle(2, 4, width-2, height-4, outline=GRID, dash=(5, 4))
            graph.create_text(width / 2, height / 2 - (20 if idle else 0),
                              text=prompt, fill=MUTED, font=(self.body, -18))
            if idle:
                graph.create_window(width / 2, height / 2 + 22, window=self.empty_open)
            return

        target = assess(self.measurement, self.profile.get()).target_lufs
        guide_levels = (-36, -24, -12) if target is None else (target - 15, target, target + 9)
        left, right, top, bottom = 45, width - 12, 12, height - 25
        low = min(guide_levels[0] - 5, min(active) - 2)
        high = max(guide_levels[-1] + 1, max(active) + 2)
        stop = points[-1][0]

        def x_at(seconds):
            return left + (right - left) * seconds / stop

        def y_at(level):
            return top + (bottom - top) * (high - level) / (high - low)

        for level in guide_levels:
            y = y_at(level)
            graph.create_text(left - 7, y, text=f"{level:g}", anchor="e", fill=MUTED)
            graph.create_line(left, y, right, y, fill=GRID, dash=(4, 4))
        graph.create_text(left, height - 11, text="0:00", anchor="w", fill=MUTED)
        graph.create_text(right, height - 11, text=f"{round(stop)//60}:{round(stop)%60:02d}",
                          anchor="e", fill=MUTED)

        columns = {}
        for seconds, level in points:
            if level is None:
                continue
            x = round(x_at(seconds))
            y = y_at(level)
            minimum, maximum = columns.get(x, (y, y))
            columns[x] = min(minimum, y), max(maximum, y)
        previous = None
        max_gap = max(1.5, (right - left) * 0.11 / stop + 0.5)
        for x, (minimum, maximum) in columns.items():
            middle = (minimum + maximum) / 2
            graph.create_line(x, minimum, x, max(minimum + 1, maximum), fill=BLUE)
            if previous and x - previous[0] <= max_gap:
                graph.create_line(*previous, x, middle, fill=BLUE)
            previous = x, middle


def main():
    parser = argparse.ArgumentParser(description="Waypoint podcast delivery meter")
    parser.add_argument("file", nargs="?", help="Print measurements as JSON instead of opening the desktop app")
    parser.add_argument("--profile", choices=PROFILES, default="Auto")
    parser.add_argument("--export-wav", metavar="NEW.wav", help="Save a verified gain-only PCM master")
    args = parser.parse_args()
    if args.export_wav and not args.file:
        parser.error("--export-wav requires an input file")
    if not args.file:
        Waypoint().mainloop()
        return
    try:
        result, stamp = scan_file(args.file)
        verdict = assess(result, args.profile)
        metrics = asdict(result)
        metrics.pop("short_term")
        metrics.update(profile=args.profile, target_lufs=verdict.target_lufs,
                       peak_ceiling_dbtp=None if args.profile == ACX else LIMIT,
                       short_term_windows=len(result.short_term), review=verdict.notes,
                       assessment=asdict(verdict))
        if args.export_wav:
            checked = export_wav(args.file, args.export_wav, result, stamp, args.profile)
            metrics["verified_wav"] = dict(path=args.export_wav, integrated=checked.integrated,
                                           true_peak=checked.true_peak, gain_db=verdict.gain_db)
        print(json.dumps(metrics, indent=2, allow_nan=False))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Waypoint · {error}\n")


if __name__ == "__main__":
    main()
