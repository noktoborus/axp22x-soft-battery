"""Play the level C' of D23 over a log, with a model made of another log.

The model: Q_full, a table of 21 points (mean V and I at each 5 % of the
charge out), R, as the module would make them at `done`. The level of
the run is then played sample by sample: the count, its speed scaled by
f = (the level shown) / (left by the voltage), f held in 0.5..2, the
weight of the voltage from 0 at `hi` % to 1 at `lo` %; the level never
rises. Printed: the level shown at the silence, the largest gap to the
true level (the charge left of the run itself), for each pair of weights.

Usage: python3 -I sim-level.py <log of the model> <log to play> [R mOhm]"""
import json, sys, statistics as st


def enc(path):
    with open(path, "rb") as f:
        return "utf-16" if f.read(2) in (b"\xff\xfe", b"\xfe\xff") else "utf-8"


def load(path):
    ts, vs, is_ = [], [], []
    with open(path, encoding=enc(path)) as f:
        for line in f:
            line = line.strip()
            if '"sample"' not in line[:30]:
                continue
            r = json.loads(line)
            if r.get("ac_online") != 0 or r.get("bat_voltage_uv") is None:
                continue
            ts.append(r["unix"]); vs.append(r["bat_voltage_uv"] / 1000)
            is_.append(abs(r["bat_current_ua"]) / 1000)
    qo = [0.0]
    for k in range(1, len(ts)):
        qo.append(qo[-1] + is_[k] * (ts[k] - ts[k - 1]) / 3600)
    return ts, vs, is_, qo


def model(path, npts=21):
    ts, vs, is_, qo = load(path)
    Q = qo[-1]
    tab = []
    for i in range(npts):
        x = i / (npts - 1)
        k = next((k for k in range(len(qo)) if qo[k] >= x * Q), len(qo) - 1)
        a, b = max(0, k - 30), min(len(qo), k + 30)
        tab.append((x, st.mean(vs[a:b]), st.mean(is_[a:b])))
    return Q, tab


def left_by_voltage(tab, R, v, i):
    """1 - x where V_t(x) - (I - I_t(x)) * R = V, x the part out."""
    def g(j):
        _, vt, it = tab[j]
        return vt - (i - it) * R - v
    if g(0) <= 0:
        return 1.0
    for j in range(len(tab) - 1):
        if g(j + 1) <= 0:
            x0, x1 = tab[j][0], tab[j + 1][0]
            g0, g1 = g(j), g(j + 1)
            return 1 - (x0 + (x1 - x0) * g0 / (g0 - g1))
    return 0.0


def play(Q, tab, R, run, hi, lo, fmin=0.5, fmax=2.0, win=30):
    ts, vs, is_, qo = run
    Qr = qo[-1]
    L = 1.0
    gap = 0.0
    for k in range(1, len(ts)):
        dq = (qo[k] - qo[k - 1]) / Q
        a = max(0, k - win)
        v = st.mean(vs[a:k + 1]); i = st.mean(is_[a:k + 1])
        lv = left_by_voltage(tab, R, v, i)
        w = 0.0 if L * 100 >= hi else (1.0 if L * 100 <= lo else (hi - L * 100) / (hi - lo))
        f = min(fmax, max(fmin, L / lv)) if lv > 0 else fmax
        L = max(0.0, L - dq * (1 + w * (f - 1)))
        truth = 1 - qo[k] / Qr
        gap = max(gap, abs(L - truth))
    return L * 100, gap * 100


Q, tab = model(sys.argv[1])
run = load(sys.argv[2])
R = (float(sys.argv[3]) if len(sys.argv) > 3 else 87) / 1000
print(f"model {Q:.0f} mAh, run {run[3][-1]:.0f} mAh, R {R * 1000:.0f} mOhm")
for hi, lo in ((101, 100), (30, 10), (50, 20), (60, 20)):
    name = "count only" if hi > 100 else f"weight {hi}->{lo} %"
    end, gap = play(Q, tab, R, run, hi, lo)
    print(f"  {name:18s} level at the silence {end:5.1f} %, largest gap {gap:4.1f} %")

# What the voltage alone says, against the count, at points of the true
# level of the run (its own charge left).
ts, vs, is_, qo = run
Qr = qo[-1]
print("  true level: by voltage / by count")
for true_left in (50, 30, 20, 10, 5, 2, 0):
    k = next((k for k in range(len(qo)) if 1 - qo[k] / Qr <= true_left / 100), len(qo) - 1)
    a = max(0, k - 30)
    lv = left_by_voltage(tab, R, st.mean(vs[a:k + 1]), st.mean(is_[a:k + 1]))
    print(f"    {true_left:3d} %: {lv * 100:5.1f} % / {(1 - qo[k] / Q) * 100:5.1f} %")
