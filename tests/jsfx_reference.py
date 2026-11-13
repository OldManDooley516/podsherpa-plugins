"""Reference transcription of the JSFX audio code (Summit 0.3.1, Dropoff 0.3.1 natural phase),
kept as close to the JSFX line order as Python allows. Compares against the C++ output written
by `ps_tests --dump <dir>`.   Usage: python3 jsfx_reference.py <dir>"""
import sys, math, struct, array

def load(path):
    a = array.array('f'); data = open(path, 'rb').read(); a.frombytes(data); a.byteswap() if sys.byteorder != 'little' else None
    return list(a[0::2]), list(a[1::2])

def summit(L, R, sr, thr, ceil_, spd, tp_on, mode):
    ln10 = math.log(10)
    delay = max(1, math.floor(sr * 0.003 + 0.5)); tpd = 8; window = delay + 1; lookback = delay + tpd
    abuf = lookback + 1; capacity = lookback + 2
    left = [0.0] * abuf; right = [0.0] * abuf; gain_buf = [1.0] * window
    qv = [0.0] * capacity; qt = [0] * capacity
    def i0(x):
        s = 1.0; t = 1.0
        for k in range(1, 31): t *= x / 2 / k; s += t * t
        return s
    beta = 7; i0b = i0(beta); coef = [0.0] * 112
    for r in range(1, 8):
        sm = 0.0
        for k in range(16):
            jj = 8 * k + r - 64
            coef[(r - 1) * 16 + k] = (1 if jj == 0 else math.sin(math.pi * jj / 8) / (math.pi * jj / 8)) * i0(beta * math.sqrt(1 - (jj / 64) ** 2)) / i0b
            sm += coef[(r - 1) * 16 + k]
        for k in range(16): coef[(r - 1) * 16 + k] /= sm
    tpl = [0.0] * 36; tpr = [0.0] * 36
    def tp_est(b, x, on):
        p = int((b[0] + 15) % 16); b[0] = p; b[1 + p] = x; b[17 + p] = x
        if not on: return 0.0
        m = 0.0
        for r in range(7):
            s = 0.0
            for k in range(16): s += b[1 + p + k] * coef[r * 16 + k]
            m = max(m, abs(s))
        return m
    def sat(v):
        u = 0.7 * v
        return (math.copysign(1, u) / 0.7) if abs(u) >= 3 else (u * (27 + u * u) / (27 + 9 * u * u)) / 0.7
    gpos = apos = 0; clock = 0; head = tail = 0; gain_sum = float(window); gf = gs = 1.0
    thr_db = thr; ceil_db = ceil_; speed = 10 - spd; param_c = math.exp(-1 / (0.02 * sr))
    oL = []; oR = []
    for n in range(len(L)):
        spl0 = L[n]; spl1 = R[n]
        thr_db = param_c * thr_db + (1 - param_c) * thr
        ceil_db = param_c * ceil_db + (1 - param_c) * ceil_
        speed = param_c * speed + (1 - param_c) * (10 - spd)
        ceiling = math.exp(ln10 * ceil_db / 20); inv_thr = math.exp(-ln10 * thr_db / 20)
        x_l = spl0 * inv_thr; x_r = spl1 * inv_thr
        if mode == 2: x_l = sat(x_l); x_r = sat(x_r)
        peak = max(abs(x_l), abs(x_r))
        tp_x = max(tp_est(tpl, x_l, tp_on), tp_est(tpr, x_r, tp_on))
        if tp_on: peak = max(peak, tp_x)
        while head != tail and qt[head] < clock - lookback: head = (head + 1) % capacity
        while head != tail and qv[(tail + capacity - 1) % capacity] <= peak: tail = (tail + capacity - 1) % capacity
        qv[tail] = peak; qt[tail] = clock; tail = (tail + 1) % capacity
        safe_gain = min(1, 1 / max(0.000000000001, qv[head]))
        rel_ms = 40 * math.exp(3.1135 * speed / 10)
        if mode == 1: fk = 0.2; sk = 1; mixf = 0.8
        else: fk = 0.4; sk = 2; mixf = 0.6
        cf = math.exp(-1 / (0.001 * rel_ms * fk * sr)); cs = math.exp(-1 / (0.001 * rel_ms * sk * sr))
        gf = min(safe_gain, 1 - cf * (1 - gf)); gs = min(safe_gain, 1 - cs * (1 - gs))
        release_gain = mixf * gf + (1 - mixf) * gs
        gain_sum += release_gain - gain_buf[gpos]; gain_buf[gpos] = release_gain; gpos = (gpos + 1) % window
        applied_gain = min(1, max(0, gain_sum / window))
        read_pos = (apos + 1) % abuf
        out_l = left[read_pos]; out_r = right[read_pos]
        left[apos] = x_l; right[apos] = x_r; apos = read_pos
        oL.append(out_l * applied_gain * ceiling); oR.append(out_r * applied_gain * ceiling)
        clock += 1
    return oL, oR

def dropoff(L, R, sr, f, slope, block):
    stn = [1, 1, 2, 2, 3, 4]
    stq = [[0], [.70711], [0, 1], [.54120, 1.30656], [.51764, .70711, 1.93185], [.50980, .60134, .89998, 2.56292]]
    zs = [0.0] * 16; sm = f   # the JSFX starts the smoother at 0 Hz; the port starts it on the target
    oL = []; oR = []
    for start in range(0, len(L), block):
        nb = min(block, len(L) - start)
        al = 1 - math.exp(-nb / (sr * .015)); sm += (f - sm) * al
        cfs = []
        for j in range(stn[slope]):
            q = stq[slope][j]; fc = min(sm, sr * .45)
            if q == 0:
                k = math.tan(math.pi * fc / sr); ia = 1 / (1 + k); cfs.append((1 * ia, -1 * ia, 0.0, (k - 1) * ia, 0.0))
            else:
                w0 = 2 * math.pi * fc / sr; c = math.cos(w0); s = math.sin(w0); a = s / (2 * q); ia = 1 / (1 + a)
                cfs.append(((1 + c) / 2 * ia, -(1 + c) * ia, (1 + c) / 2 * ia, -2 * c * ia, (1 - a) * ia))
        for n in range(start, start + nb):
            yl = L[n]; yr = R[n]
            for i, pc in enumerate(cfs):
                z = i * 4
                t = pc[0] * yl + zs[z]; zs[z] = pc[1] * yl - pc[3] * t + zs[z + 1]; zs[z + 1] = pc[2] * yl - pc[4] * t; yl = t
                t = pc[0] * yr + zs[z + 2]; zs[z + 2] = pc[1] * yr - pc[3] * t + zs[z + 3]; zs[z + 3] = pc[2] * yr - pc[4] * t; yr = t
            oL.append(yl); oR.append(yr)
    return oL, oR

def compare(name, ref, got):
    worst = max(max(abs(a - b) for a, b in zip(ref[c], got[c])) for c in range(2))
    peak = max(max(abs(a) for a in ref[c]) for c in range(2))
    ok = worst < 2e-6
    print(('  ok    ' if ok else '  FAIL  ') + f'{name}: largest difference {worst:.2e} (signal peak {peak:.3f})')
    return ok

if __name__ == '__main__':
    d = sys.argv[1]; sr = 48000.0; L, R = load(d + '/in.f32'); good = True
    for mode in range(3):
        for tp in range(2):
            good &= compare(f'Summit mode {mode}, true peak {tp}', summit(L, R, sr, -9.0, -1.0, 6.0, tp == 1, mode), load(f'{d}/summit_m{mode}_tp{tp}.f32'))
    for slope in range(6):
        good &= compare(f'Dropoff slope {slope}', dropoff(L, R, sr, 90.0, slope, 256), load(f'{d}/dropoff_s{slope}.f32'))
    print('MATCHES JSFX REFERENCE' if good else 'MISMATCH'); sys.exit(0 if good else 1)
