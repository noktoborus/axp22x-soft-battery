"""R from small steps; the error of a table of N points against the curve.
Usage: python3 -I log-r-table.py <log>"""
import json, sys, statistics as st

def enc(path):
    """The logs saved by PowerShell are UTF-16 with a mark; the others UTF-8."""
    with open(path, "rb") as f:
        return "utf-16" if f.read(2) in (b"\xff\xfe", b"\xfe\xff") else "utf-8"
ts, vs, is_ = [], [], []
with open(sys.argv[1], encoding=enc(sys.argv[1])) as f:
    for line in f:
        line = line.strip()
        if not line.startswith("{"): continue
        r = json.loads(line)
        if r.get("type") != "sample" or r.get("ac_online") != 0: continue
        if r.get("bat_voltage_uv") is None: continue
        ts.append(r["unix"]); vs.append(r["bat_voltage_uv"]/1000); is_.append(abs(r["bat_current_ua"])/1000)
n = len(ts)
print("R by threshold of the step (1 s): thr, count, q25 med q75 mOhm")
for thr in (20, 30, 50, 100):
    rs = sorted(-(vs[k+1]-vs[k])/(is_[k+1]-is_[k])*1000 for k in range(n-1) if abs(is_[k+1]-is_[k]) >= thr)
    if rs: print(f"  {thr:3d} {len(rs):6d}  {rs[len(rs)//4]:5.0f} {rs[len(rs)//2]:5.0f} {rs[3*len(rs)//4]:5.0f}")
qo = [0.0]
for k in range(1, n): qo.append(qo[-1] + is_[k]*(ts[k]-ts[k-1])/3600)
Q = qo[-1]
R = 0.087
vc = [vs[k] + is_[k]*R for k in range(n)]
# the curve at 0.5 % steps: mean of V+IR over 60 s around each point
def at(frac):
    k = next((k for k in range(n) if qo[k] >= frac*Q), n-1)
    return st.mean(vc[max(0,k-30):k+30])
fine = [(p/200, at(p/200)) for p in range(0, 201)]
for npts in (11, 21, 41):
    tab = [(i/(npts-1), at(i/(npts-1))) for i in range(npts)]
    def interp(x):
        for (x0,y0),(x1,y1) in zip(tab, tab[1:]):
            if x0 <= x <= x1: return y0 + (y1-y0)*(x-x0)/(x1-x0)
        return tab[-1][1]
    worst = []
    for x, y in fine:
        e = interp(x) - y
        # slope there, mV per %
        k = min(int(x*200), 199)
        sl = abs(fine[k+1][1]-fine[k][1]) / 0.5 if k < 200 else 1
        worst.append((abs(e), x, abs(e)/max(sl, 0.5)))
    me = max(worst)
    mp = max(worst, key=lambda w: w[2])
    print(f"table of {npts}: max error {me[0]:.1f} mV at {100-me[1]*100:.0f} % left; "
          f"max in % {mp[2]:.1f} % at {100-mp[1]*100:.0f} % left")
