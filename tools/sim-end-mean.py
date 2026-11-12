"""Leave one out: the mean model of two logs (D29: each value and each
pair (V, I) of the table averaged) played over the third log, with the end
of the table and with ends by a voltage V_thr (see sim-end.py). Also the
jump: the level shown just before it fell to 0 % at once.

Usage: python3 -I sim-end-mean.py <log to play> <log> <log> V_thr [V_thr ...]"""
import statistics as st, sys

base = __file__.replace("sim-end-mean.py", "")
ns = {}
src = open(base + "sim-level.py").read().split("\nQ, tab = model(")[0]
exec(compile(src, "sim-level", "exec"), ns)
R = 0.087


def place(tab, v, i, target):
    """x where V_t(x) - (I - I_t(x)) * R = target; 0..1 on the table."""
    def g(j):
        _, vt, it = tab[j]
        return vt - (i - it) * R - target
    if g(0) <= 0:
        return 0.0
    for j in range(len(tab) - 1):
        if g(j + 1) <= 0:
            x0, x1 = tab[j][0], tab[j + 1][0]
            g0, g1 = g(j), g(j + 1)
            return x0 + (x1 - x0) * g0 / (g0 - g1)
    return 1.0


ns["place"] = place

run = ns["load"](sys.argv[1])
ms = [ns["model"](p) for p in sys.argv[2:4]]
Q = st.mean(m[0] for m in ms)
tab = [(ms[0][1][j][0], st.mean(m[1][j][1] for m in ms), st.mean(m[1][j][2] for m in ms))
       for j in range(len(ms[0][1]))]
print(f"  mean model {Q:.0f} mAh (of {ms[0][0]:.0f}, {ms[1][0]:.0f}); run {run[3][-1]:.0f} mAh")


def play_jump(vthr):
    ts, vs, is_, qo = run
    Qr = qo[-1]
    L, jump, zero_at, gap = 1.0, 0.0, None, 0.0
    for k in range(1, len(ts)):
        dq = (qo[k] - qo[k - 1]) / Q
        a = max(0, k - 30)
        v = st.mean(vs[a:k + 1]); i = st.mean(is_[a:k + 1])
        x_now = ns["place"](tab, v, i, v)
        x_end = ns["place"](tab, v, i, vthr) if vthr else 1.0
        left = max(0.0, x_end - x_now)
        w = 0.0 if L * 100 >= 30 else (1.0 if L * 100 <= 10 else (30 - L * 100) / 20)
        f = min(2.0, max(0.5, L / left)) if left > 0 else 2.0
        before = L
        L = max(0.0, L - dq * (1 + w * (f - 1)))
        if vthr and v <= vthr:
            L = 0.0
        if L == 0.0 and before > 0:
            jump = max(jump, before)
            zero_at = 1 - qo[k] / Qr
        gap = max(gap, abs(L - (1 - qo[k] / Qr)))
    return L * 100, (zero_at or 0) * 100, jump * 100, gap * 100


for t in [0.0] + [float(x) for x in sys.argv[4:]]:
    end, early, jump, gap = play_jump(t)
    name = "end of the table" if not t else f"V_thr {t:.0f} mV"
    print(f"  {name:17s} at the silence {end:4.1f} %, 0 % came {early:4.1f} % early, "
          f"jump to 0 from {jump:4.1f} %, gap {gap:4.1f} %")
