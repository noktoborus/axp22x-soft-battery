"""Play the level with an end by a voltage under load, over a log, with a
model made of another log.

The end: the point x_e where the table, moved to the current of now,
reaches V_thr: V_t(x) - (I - I_t(x)) * R = V_thr. Past the end of the
table it is the end of the table. "Left" is then x_e - x_now, x_now the
place of the voltage of now on the table. The level is C' of D23 (the
weight of the voltage 0 above 30 %, 1 at 10 %, f in 0.5..2, never rises),
with this "left" in place of the distance to the end of the table.

Printed for each V_thr: the level shown at the silence, how long before
the silence 0 % came (in % of the run), the largest gap to the true level.

Usage: python3 -I sim-end.py <log of the model> <log to play> V_thr [V_thr ...]"""
import statistics as st, sys

src = open(__file__.replace("sim-end.py", "sim-level.py")).read().split("\nQ, tab = model(")[0]
ns = {}
exec(compile(src, "sim-level", "exec"), ns)
model, load = ns["model"], ns["load"]
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


def play(Q, tab, run, vthr, hi=30, lo=10, fmin=0.5, fmax=2.0, win=30):
    ts, vs, is_, qo = run
    Qr = qo[-1]
    L, gap, zero_at = 1.0, 0.0, None
    for k in range(1, len(ts)):
        dq = (qo[k] - qo[k - 1]) / Q
        a = max(0, k - win)
        v = st.mean(vs[a:k + 1]); i = st.mean(is_[a:k + 1])
        x_now = place(tab, v, i, v)          # where the voltage of now is
        x_end = place(tab, v, i, vthr) if vthr else 1.0
        left = max(0.0, x_end - x_now)
        w = 0.0 if L * 100 >= hi else (1.0 if L * 100 <= lo else (hi - L * 100) / (hi - lo))
        f = min(fmax, max(fmin, L / left)) if left > 0 else fmax
        L = max(0.0, L - dq * (1 + w * (f - 1)))
        if v <= vthr if vthr else False:
            L = 0.0
        truth = 1 - qo[k] / Qr
        gap = max(gap, abs(L - truth))
        if L == 0.0 and zero_at is None:
            zero_at = truth
    return L * 100, (zero_at or 0) * 100, gap * 100


Q, tab = model(sys.argv[1])
run = load(sys.argv[2])
for t in [0.0] + [float(x) for x in sys.argv[3:]]:
    end, early, gap = play(Q, tab, run, t)
    name = "end of the table" if not t else f"V_thr {t:.0f} mV"
    print(f"  {name:17s} at the silence {end:4.1f} %, 0 % came {early:4.1f} % early, gap {gap:4.1f} %")
