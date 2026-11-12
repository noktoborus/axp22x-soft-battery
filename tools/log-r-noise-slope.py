"""Read an axp-log discharge log; work out R from current steps, the noise
of the voltage after averaging, and the slope of the curve.
Usage: python3 -I log-r-noise-slope.py <log> [R in mOhm, when the log has no steps]"""
import json, sys, statistics as st

def enc(path):
    """The logs saved by PowerShell are UTF-16 with a mark; the others UTF-8."""
    with open(path, "rb") as f:
        return "utf-16" if f.read(2) in (b"\xff\xfe", b"\xfe\xff") else "utf-8"

path = sys.argv[1]
ts, vs, is_ = [], [], []
with open(path, encoding=enc(path)) as f:
    for line in f:
        line = line.strip()
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("type") != "sample" or r.get("ac_online") != 0:
            continue
        v, i = r.get("bat_voltage_uv"), r.get("bat_current_ua")
        if v is None or i is None:
            continue
        ts.append(r["unix"]); vs.append(v / 1000.0); is_.append(abs(i) / 1000.0)

n = len(ts)
dt = [ts[k + 1] - ts[k] for k in range(n - 1)]
print(f"samples {n}, dt median {st.median(dt):.2f} s, max {max(dt):.1f} s")
print(f"I mean {st.mean(is_):.0f} mA, V {vs[0]:.0f}..{vs[-1]:.0f} mV")

def q(xs):
    xs = sorted(xs); m = len(xs)
    return xs[m // 4], xs[m // 2], xs[3 * m // 4]

print("\nR from steps of 1 s, mOhm: threshold, count, q25 median q75")
for thr in (100, 200, 300, 500):
    rs = []
    for k in range(n - 1):
        di = is_[k + 1] - is_[k]
        if abs(di) >= thr and dt[k] < 1.5:
            rs.append(-(vs[k + 1] - vs[k]) / di * 1000)
    if rs:
        a, b, c = q(rs)
        print(f"  {thr:4d} mA  {len(rs):5d}  {a:6.0f} {b:6.0f} {c:6.0f}")

print("\nR after a step, held: the current stays within 100 mA of its new")
print("value for k s; mOhm, median (count)")
for thr in (200, 300):
    row = []
    for kk in (1, 5, 10, 30):
        rs = []
        for k in range(n - kk - 1):
            di = is_[k + 1] - is_[k]
            if abs(di) < thr or dt[k] > 1.5:
                continue
            new = is_[k + 1]
            if any(abs(is_[k + 1 + j] - new) > 100 for j in range(kk)):
                continue
            rs.append(-(vs[k + kk] - vs[k]) / (is_[k + kk] - is_[k]) * 1000
                      if abs(is_[k + kk] - is_[k]) >= thr / 2 else None)
        rs = [x for x in rs if x is not None]
        row.append(f"{kk:2d} s: {st.median(rs):5.0f} ({len(rs)})" if rs else f"{kk:2d} s: -")
    print(f"  {thr} mA  " + "   ".join(row))

steps = [-(vs[k + 1] - vs[k]) / (is_[k + 1] - is_[k]) * 1000
         for k in range(n - 1)
         if abs(is_[k + 1] - is_[k]) >= 200 and dt[k] < 1.5]
R = (st.median(steps) if steps else float(sys.argv[2])) / 1000.0
print(f"\nR taken for the rest: {R * 1000:.0f} mOhm")

# V + I*R against a slow trend (the median of 10 min), after a mean of W s
vc = [vs[k] + is_[k] * R for k in range(n)]
def mean_w(xs, w):
    out, s = [], 0.0
    for k, x in enumerate(xs):
        s += x
        if k >= w:
            s -= xs[k - w]
        out.append(s / min(k + 1, w))
    return out
trend = []
half = 300
for k in range(0, n, 60):
    lo, hi = max(0, k - half), min(n, k + half)
    trend.append((k, st.median(vc[lo:hi])))
def trend_at(k):
    j = min(k // 60, len(trend) - 1)
    return trend[j][1]
print("\nthe noise of V + I*R against the trend of 10 min, mV: window, sd, p95 of |e|")
for w in (1, 10, 30, 60, 120):
    m = mean_w(vc, w)
    e = [m[k] - trend_at(k) for k in range(600, n - 600)]
    ae = sorted(abs(x) for x in e)
    print(f"  {w:4d} s  {st.pstdev(e):5.1f}  {ae[int(0.95 * len(ae))]:5.1f}")

# the slope of the curve: mV per 1 % of the charge out, by 10 % bins
q_out = [0.0]
for k in range(1, n):
    q_out.append(q_out[-1] + is_[k] * (ts[k] - ts[k - 1]) / 3600.0)
Q = q_out[-1]
print(f"\ncharge out {Q:.0f} mAh; the slope of V + I*R, mV per 1 %, by level left")
for lvl in range(100, 0, -10):
    a = (100 - lvl) / 100 * Q; b = (100 - lvl + 10) / 100 * Q
    ka = next(k for k in range(n) if q_out[k] >= a)
    kb = next((k for k in range(n) if q_out[k] >= b), n - 1)
    va = st.mean(vc[max(0, ka - 30):ka + 30]); vb = st.mean(vc[max(0, kb - 30):kb + 30])
    print(f"  {lvl:3d}..{lvl - 10:3d} %: {(va - vb) / 10:5.1f}")

print(f"\nthe end: last sample {vs[-1]:.0f} mV at {is_[-1]:.0f} mA; "
      f"mean of the last 60 s {st.mean(vs[-60:]):.0f} mV at {st.mean(is_[-60:]):.0f} mA; "
      f"V + I*R at the end {st.mean(vc[-60:]):.0f} mV")
for vt in (3300, 3200):
    k = next(k for k in range(n) if vs[k] <= vt)
    print(f"  first sample at or under {vt} mV: {q_out[k]:.0f} mAh out, {Q - q_out[k]:.0f} mAh before the end")
