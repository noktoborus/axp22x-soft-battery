"""Draw the curves of the logs as an SVG: OCV = V + I x R against the percent
left (each run to its own silence), each run and their mean; under it the
gap of each run to the mean. Text in Russian.

Usage: python3 -I plot-curves.py <R in mOhm> <out.svg> <label=log> ..."""
import json, statistics as st, sys


def enc(path):
    with open(path, "rb") as f:
        return "utf-16" if f.read(2) in (b"\xff\xfe", b"\xfe\xff") else "utf-8"


def curve(path, R):
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
    Q = qo[-1]
    out, k = [], 0
    for pct in range(100, -1, -1):
        while k < len(qo) - 1 and 1 - qo[k] / Q > pct / 100:
            k += 1
        a, b = max(0, k - 30), min(len(qo), k + 30)
        if pct == 0:
            a, b = max(0, len(qo) - 60), len(qo)
        out.append(st.mean(vs[a:b]) + st.mean(is_[a:b]) * R)
    return out, Q, st.mean(is_)


R = float(sys.argv[1]) / 1000
dest = sys.argv[2]
runs = []
for arg in sys.argv[3:]:
    label, path = arg.split("=", 1)
    c, Q, imean = curve(path, R)
    runs.append((label, c, Q, imean))
mean = [st.mean(r[1][i] for r in runs) for i in range(101)]

# Tokens of the reference palette (dataviz, light).
SURF, INK, INK2, MUTED, GRID, BASE = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
SERIES = ["#2a78d6", "#eb6834", "#1baf7a"]

W, H = 1100, 900
L, Rm = 90, 230
P1 = (70, 560)      # top, bottom of the first plot
P2 = (640, 830)
X = lambda pct: L + (100 - pct) / 100 * (W - L - Rm)
v_lo, v_hi = 3200, 4300
Y1 = lambda v: P1[1] - (v - v_lo) / (v_hi - v_lo) * (P1[1] - P1[0])
d_lo, d_hi = -100, 100
Y2 = lambda d: P2[1] - (d - d_lo) / (d_hi - d_lo) * (P2[1] - P2[0])

o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
     f'font-family="DejaVu Sans" font-size="15">',
     f'<rect width="{W}" height="{H}" fill="{SURF}"/>',
     f'<text x="{L}" y="34" font-size="20" font-weight="bold" fill="{INK}">'
     f'Кривая банок по трём прогонам: напряжение покоя против оставшегося заряда</text>',
     f'<text x="{L}" y="56" fill="{INK2}">OCV = V + I × R, R = {R * 1000:.0f} мОм, средние за 60 с; '
     f'0 % — молчание AXP в каждом прогоне</text>']

# grid and axes of plot 1
for v in range(v_lo, v_hi + 1, 100):
    o.append(f'<line x1="{L}" x2="{W - Rm}" y1="{Y1(v):.1f}" y2="{Y1(v):.1f}" stroke="{GRID}" stroke-width="1"/>')
    o.append(f'<text x="{L - 10}" y="{Y1(v) + 5:.1f}" text-anchor="end" fill="{MUTED}">{v}</text>')
for p in range(100, -1, -10):
    o.append(f'<line x1="{X(p):.1f}" x2="{X(p):.1f}" y1="{P1[0]}" y2="{P1[1]}" stroke="{GRID}" stroke-width="1"/>')
    o.append(f'<text x="{X(p):.1f}" y="{P1[1] + 22}" text-anchor="middle" fill="{MUTED}">{p}</text>')
o.append(f'<text x="{(L + W - Rm) / 2:.0f}" y="{P1[1] + 46}" text-anchor="middle" fill="{INK2}">осталось заряда, % (до молчания AXP)</text>')
o.append(f'<text x="24" y="{(P1[0] + P1[1]) / 2:.0f}" fill="{INK2}" transform="rotate(-90 24 {(P1[0] + P1[1]) / 2:.0f})" text-anchor="middle">OCV, мВ</text>')
o.append(f'<line x1="{L}" x2="{W - Rm}" y1="{P1[1]}" y2="{P1[1]}" stroke="{BASE}" stroke-width="1"/>')


def path(vals, Y):
    return " ".join(f'{"M" if i == 0 else "L"}{X(100 - i):.1f},{Y(v):.1f}' for i, v in enumerate(vals))


for (label, c, Q, im), col in zip(runs, SERIES):
    o.append(f'<path d="{path(c, Y1)}" fill="none" stroke="{col}" stroke-width="2" stroke-linejoin="round"/>')
o.append(f'<path d="{path(mean, Y1)}" fill="none" stroke="{INK}" stroke-width="2.5" stroke-dasharray="7 5"/>')

# legend at the right of plot 1: a swatch, the name, the numbers
ly = P1[0] + 20
for (label, c, Q, im), col in zip(runs, SERIES):
    o.append(f'<line x1="{W - Rm + 20}" x2="{W - Rm + 44}" y1="{ly}" y2="{ly}" stroke="{col}" stroke-width="3"/>')
    o.append(f'<text x="{W - Rm + 52}" y="{ly + 5}" fill="{INK}">{label}</text>')
    o.append(f'<text x="{W - Rm + 52}" y="{ly + 24}" fill="{INK2}" font-size="13">{Q:.0f} мА·ч, ср. {im / 1000:.2f} А</text>')
    ly += 54
o.append(f'<line x1="{W - Rm + 20}" x2="{W - Rm + 44}" y1="{ly}" y2="{ly}" stroke="{INK}" stroke-width="2.5" stroke-dasharray="7 5"/>')
o.append(f'<text x="{W - Rm + 52}" y="{ly + 5}" fill="{INK}">среднее</text>')
o.append(f'<text x="{W - Rm + 52}" y="{ly + 24}" fill="{INK2}" font-size="13">встроенная кривая</text>')

# plot 2: the gap to the mean
o.append(f'<text x="{L}" y="{P2[0] - 18}" font-size="17" font-weight="bold" fill="{INK}">Отличие каждого прогона от среднего, мВ</text>')
for d in range(d_lo, d_hi + 1, 50):
    o.append(f'<line x1="{L}" x2="{W - Rm}" y1="{Y2(d):.1f}" y2="{Y2(d):.1f}" stroke="{BASE if d == 0 else GRID}" stroke-width="1"/>')
    o.append(f'<text x="{L - 10}" y="{Y2(d) + 5:.1f}" text-anchor="end" fill="{MUTED}">{d:+d}</text>')
for p in range(100, -1, -10):
    o.append(f'<line x1="{X(p):.1f}" x2="{X(p):.1f}" y1="{P2[0]}" y2="{P2[1]}" stroke="{GRID}" stroke-width="1"/>')
    o.append(f'<text x="{X(p):.1f}" y="{P2[1] + 22}" text-anchor="middle" fill="{MUTED}">{p}</text>')
o.append(f'<text x="{(L + W - Rm) / 2:.0f}" y="{P2[1] + 46}" text-anchor="middle" fill="{INK2}">осталось заряда, %</text>')
for (label, c, Q, im), col in zip(runs, SERIES):
    diff = [c[i] - mean[i] for i in range(101)]
    o.append(f'<path d="{path(diff, Y2)}" fill="none" stroke="{col}" stroke-width="2" stroke-linejoin="round"/>')
    o.append(f'<text x="{W - Rm + 10}" y="{Y2(diff[-1]) + 5:.1f}" fill="{INK}" font-size="13">{label}</text>')
o.append("</svg>")
open(dest, "w").write("\n".join(o))
print("written", dest)
