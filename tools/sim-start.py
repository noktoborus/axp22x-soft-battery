"""The start level by the voltage alone (a boot in the middle of a
discharge), with R and with no R. The mean model of two logs; over the
third log, at each 60 s, the level by the voltage (the means of 30 s, the
table moved by (I - I_t) * R) against the true level of the run.
Printed: the mean and the largest error, and the largest error by the
current at that moment.

Usage: python3 -I sim-start.py <log to play> <log> <log>"""
import statistics as st, sys

ns = {}
src = open(__file__.replace("sim-start.py", "sim-level.py")).read().split("\nQ, tab = model(")[0]
exec(compile(src, "sim-level", "exec"), ns)
run = ns["load"](sys.argv[1])
ms = [ns["model"](p) for p in sys.argv[2:4]]
Q = st.mean(m[0] for m in ms)
tab = [(ms[0][1][j][0], st.mean(m[1][j][1] for m in ms), st.mean(m[1][j][2] for m in ms))
       for j in range(len(ms[0][1]))]
ts, vs, is_, qo = run
Qr = qo[-1]
for R in (0.0, 0.087):
    errs = []
    for k in range(60, len(ts), 60):
        a = k - 30
        v, i = st.mean(vs[a:k + 1]), st.mean(is_[a:k + 1])
        lv = ns["left_by_voltage"](tab, R, v, i) * Q / Qr
        truth = 1 - qo[k] / Qr
        if truth < 0.05:
            continue
        errs.append(((lv - truth) * 100, i))
    e = [x for x, _ in errs]
    lo = [x for x, i in errs if i < 1500]; hi = [x for x, i in errs if i >= 2200]
    print(f"  R {R * 1000:3.0f}: mean {st.mean(e):+5.1f} %, largest {max(e, key=abs):+5.1f} %;"
          f" at I < 1.5 A {st.mean(lo) if lo else float('nan'):+5.1f} %, at I >= 2.2 A {st.mean(hi) if hi else float('nan'):+5.1f} %")
