import re, sys
from collections import defaultdict
ROW = re.compile(r'"([\w.]+)"\s*,\s*(-?\d+)\s*$')
ev=[]
for line in open(sys.argv[1], encoding='utf-8-sig', errors='replace'):
    if not line.lstrip().startswith('Stage'): continue
    m=ROW.search(line.rstrip()); c=[x.strip() for x in line.split(',')]
    if m: ev.append((int(c[16]), int(c[9],16), m.group(1), int(m.group(2))))
ev.sort()
srv={e[1] for e in ev if e[2]=='srv.on_key_event'}
cl=[e for e in ev if e[1] not in srv]
pk=[e for e in cl if e[2]=='tsf.process_key']
print(f'client events: {len(cl)}; tsf.process_key calls: {len(pk)}')
if not pk: sys.exit()
t0=pk[0][0]
v=sorted(e[3] for e in pk)
print(f'tsf.process_key: p50={v[len(v)//2]/1000:.2f} ms  p95={v[int(len(v)*.95)]/1000:.2f} ms  max={v[-1]/1000:.1f} ms')
slow=[(round((t-t0)/1e7,2), round(us/1000)) for t,p,s,us in pk if us>20000]
print('process_key calls > 20 ms  (s since first key, blocked ms):', slow)
print('total host-thread time blocked in those calls: %.0f ms' % (sum(us for _,_,_,us in pk if us>20000)/1000))
print()
print('per-stage breakdown of the slow period (calls > 20 ms):')
by=defaultdict(list)
for t,p,s,us in cl:
    if s.startswith('ipc.') or s in ('tsf.ensure_server','tsf.ipc_key_roundtrip'):
        by[s].append(us)
for s,vv in sorted(by.items()):
    vv=sorted(vv); big=[x for x in vv if x>20000]
    print(f'  {s:22} n={len(vv):4} p50={vv[len(vv)//2]/1000:7.2f} ms  max={vv[-1]/1000:8.1f} ms   calls>20ms: {sorted(round(x/1000) for x in big)}')
