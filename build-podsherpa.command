#!/bin/bash
# Build and install the PodSherpa plugins on this Mac.
#
# Double-click this file in Finder. It will ask for your password once, near the
# end, to copy the finished plugins into your Library folder.
#
# First run takes about ten minutes. Later runs take a minute or two, because
# only what changed gets rebuilt.

set -u
cd "$(dirname "$0")"
echo
echo "================================================"
echo "  PodSherpa — building the plugins on this Mac"
echo "================================================"
echo

fail () { echo; echo "STOPPED: $1"; echo; echo "Press any key to close."; read -r -n 1; exit 1; }

# ---- 1. Xcode command line tools -------------------------------------------
if ! xcode-select -p >/dev/null 2>&1; then
    echo "[1/5] Apple's developer tools are not installed yet."
    echo "      A window will open. Click Install, wait for it to finish,"
    echo "      then run this script again."
    xcode-select --install
    echo
    echo "Press any key to close."; read -r -n 1; exit 0
fi
echo "[1/5] Developer tools: found."

# ---- 2. cmake ---------------------------------------------------------------
if ! command -v cmake >/dev/null 2>&1; then
    if command -v brew >/dev/null 2>&1; then
        echo "[2/5] Installing cmake with Homebrew (a few minutes)..."
        brew install cmake || fail "Homebrew could not install cmake."
    else
        fail "cmake is not installed, and neither is Homebrew.
       Install Homebrew first by pasting this into Terminal:

         /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"

       Then run this script again."
    fi
else
    echo "[2/5] cmake: found."
fi

# ---- 3. Get or update the source -------------------------------------------
REPO="https://github.com/OldManDooley516/podsherpa-plugins"
SRC="$HOME/podsherpa-plugins"
if [ -d "$SRC/.git" ]; then
    echo "[3/5] Updating the source in $SRC ..."
    git -C "$SRC" pull --ff-only || fail "Could not update the source. If you have edited files there, move them aside and try again."
else
    echo "[3/5] Downloading the source to $SRC ..."
    git clone "$REPO" "$SRC" || fail "Could not download the source. Check your internet connection."
fi
cd "$SRC" || fail "Could not enter $SRC"

# ---- 4. Build ---------------------------------------------------------------
echo "[4/5] Building. This is the slow part — around ten minutes the first time."
echo
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /tmp/podsherpa-configure.log 2>&1 \
    || fail "Setup failed. The details are in /tmp/podsherpa-configure.log"
cmake --build build -j"$(sysctl -n hw.ncpu)" > /tmp/podsherpa-build.log 2>&1 \
    || fail "The build failed. The details are in /tmp/podsherpa-build.log"
echo "      Built."

# ---- 5. Install --------------------------------------------------------------
VST3=$(ls -d build/*_artefacts/Release/VST3/*.vst3 2>/dev/null | wc -l | tr -d ' ')
AU=$(ls -d build/*_artefacts/Release/AU/*.component 2>/dev/null | wc -l | tr -d ' ')
[ "$VST3" = "0" ] && fail "The build finished but produced no plugins. See /tmp/podsherpa-build.log"

echo "[5/5] Installing $VST3 VST3 and $AU AU plugins."
echo "      macOS will ask for your password now."
sudo mkdir -p /Library/Audio/Plug-Ins/VST3 /Library/Audio/Plug-Ins/Components || fail "Could not create the plugin folders."
sudo cp -R build/*_artefacts/Release/VST3/*.vst3 /Library/Audio/Plug-Ins/VST3/ || fail "Could not copy the VST3 plugins."
if [ "$AU" != "0" ]; then
    sudo cp -R build/*_artefacts/Release/AU/*.component /Library/Audio/Plug-Ins/Components/ || fail "Could not copy the AU plugins."
fi

echo
echo "================================================"
echo "  Done. $VST3 plugins installed."
echo
echo "  In REAPER:  Preferences > Plug-ins > VST"
echo "              > Re-scan > Clear cache and re-scan"
echo
echo "  Built here, so there is no quarantine step and"
echo "  nothing to unblock in Security settings."
echo "================================================"
echo
echo "Press any key to close."
read -r -n 1
