"""Make the default curve: the voltage at rest (OCV = V + I x R) against the
percent left, from logs of axp-log that run from the full state to the
silence of the AXP.

For each log: the charge out is counted; the percent left is
100 x (1 - out / Q), Q the charge out at the silence; at each step of 5 %
the means of V and I over 60 s around it give OCV. The curve is the mean
of the logs at each step. Printed: the curve of each log, the spread, the
mean; the error of the level by the voltage alone on each log, with the
mean curve and with the curve of the two other logs; the curve as
`ocv-capacity-table-0` of the device tree and as an array of C.

Usage: python3 -I make-curve.py <R in mOhm> <log> <log> ..."""
import json, statistics as st, sys


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


R = float(sys.argv[1]) / 1000
STEPS = list(range(100, -1, -5))          # percent left
runs = [(p.rsplit("/", 1)[-1], load(p)) for p in sys.argv[2:]]


def curve(run):
    ts, vs, is_, qo = run
    Q = qo[-1]
    out = []
    for pct in STEPS:
        k = next((k for k in range(len(qo)) if 1 - qo[k] / Q <= pct / 100), len(qo) - 1)
        a, b = max(0, k - 30), min(len(qo), k + 30)
        if pct == 0:
            a, b = max(0, len(qo) - 60), len(qo)
        out.append(st.mean(vs[a:b]) + st.mean(is_[a:b]) * R)
    return out


curves = {name: curve(run) for name, run in runs}
mean = [st.mean(c[i] for c in curves.values()) for i in range(len(STEPS))]

print(f"R {R * 1000:.0f} mOhm; OCV in mV by the percent left")
print("  %   " + "  ".join(f"{n[:10]:>10s}" for n in curves) + "      mean  spread")
for i, pct in enumerate(STEPS):
    vals = [c[i] for c in curves.values()]
    print(f"{pct:4d}  " + "  ".join(f"{v:10.0f}" for v in vals)
          + f"  {mean[i]:8.0f}  {max(vals) - min(vals):6.0f}")


def level(tab, ocv):
    """The percent by OCV on a table (OCV falls as the percent falls)."""
    if ocv >= tab[0]:
        return 100.0
    for i in range(len(tab) - 1):
        if tab[i + 1] <= ocv <= tab[i]:
            return STEPS[i] + (STEPS[i + 1] - STEPS[i]) * (tab[i] - ocv) / (tab[i] - tab[i + 1])
    return 0.0


print("\nthe level by the voltage alone (means of 30 s), against the true level")
print("  log          table           mean err  largest  at true 50/20/10/5 %")
for name, run in runs:
    ts, vs, is_, qo = run
    Q = qo[-1]
    others = [c for n, c in curves.items() if n != name]
    loo = [st.mean(c[i] for c in others) for i in range(len(STEPS))]
    for tname, tab in (("mean of all", mean), ("two others", loo)):
        errs, at = [], {}
        for k in range(30, len(ts), 30):
            v = st.mean(vs[k - 30:k + 1]); i = st.mean(is_[k - 30:k + 1])
            truth = 100 * (1 - qo[k] / Q)
            e = level(tab, v + i * R) - truth
            errs.append(e)
            for t in (50, 20, 10, 5):
                if t not in at and truth <= t:
                    at[t] = e
        print(f"  {name[:12]:12s} {tname:12s} {st.mean(errs):+6.1f} {max(errs, key=abs):+8.1f}   "
              + " ".join(f"{at.get(t, float('nan')):+5.1f}" for t in (50, 20, 10, 5)))

print("\nocv-capacity-celsius = <25>;")
print("ocv-capacity-table-0 = " + ",\n\t".join(
    f"<{round(m) * 1000} {p}>" for m, p in zip(mean, STEPS)) + ";")
print("\n/* OCV, mV, at 100, 95, ... 0 % left */")
print("static const u16 default_ocv_mv[21] = {\n\t" + ", ".join(f"{round(m)}" for m in mean) + "\n};")
