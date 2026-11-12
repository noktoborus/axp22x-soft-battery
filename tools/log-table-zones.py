"""The error of a table of 21 and 41 points against the curve, by the part
of the discharge: 100-50 %, 50-20 %, 20-0 % left.
Usage: python3 -I log-table-zones.py <log> [<log> ...]"""
import json, sys, statistics as st
def enc(p):
    with open(p,'rb') as f: return 'utf-16' if f.read(2) in (b'\xff\xfe',b'\xfe\xff') else 'utf-8'
for fn in sys.argv[1:]:
    ts=[];vs=[];is_=[]
    with open(fn,encoding=enc(fn)) as f:
        for l in f:
            l=l.strip()
            if '"sample"' not in l[:30]: continue
            r=json.loads(l)
            if r.get('ac_online')!=0 or r.get('bat_voltage_uv') is None: continue
            ts.append(r['unix']); vs.append(r['bat_voltage_uv']/1000); is_.append(abs(r['bat_current_ua'])/1000)
    n=len(ts); qo=[0.0]
    for k in range(1,n): qo.append(qo[-1]+is_[k]*(ts[k]-ts[k-1])/3600)
    Q=qo[-1]; vc=[vs[k]+is_[k]*0.087 for k in range(n)]
    idx=0; pts=[]
    # the curve at 0.5 % steps
    def at(frac):
        k=next((k for k in range(n) if qo[k]>=frac*Q), n-1)
        return st.mean(vc[max(0,k-30):k+30])
    fine=[(p/200, at(p/200)) for p in range(201)]
    print('==', fn.split('/')[-1])
    for npts in (21,41):
        tab=[(i/(npts-1), at(i/(npts-1))) for i in range(npts)]
        def interp(x):
            for (x0,y0),(x1,y1) in zip(tab,tab[1:]):
                if x0<=x<=x1: return y0+(y1-y0)*(x-x0)/(x1-x0)
            return tab[-1][1]
        res={'100-50 %':[], '50-20 %':[], '20-0 %':[]}
        for j,(x,y) in enumerate(fine):
            left=100-x*100
            # error in % of level: the mV error over the local slope (mV per %, over +-2.5 %)
            a=max(0,j-5); b=min(200,j+5)
            slope=abs(fine[b][1]-fine[a][1])/((b-a)*0.5)
            e_mv=abs(interp(x)-y); e_pct=e_mv/max(slope,0.5)
            zone='100-50 %' if left>50 else ('50-20 %' if left>20 else '20-0 %')
            res[zone].append((e_mv,e_pct))
        print('  %d points: '%npts + '; '.join('%s: max %.1f mV, %.1f %%'%(z, max(e for e,_ in v), max(p for _,p in v)) for z,v in res.items()))
