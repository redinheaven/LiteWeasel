import re, sys
ROW = re.compile(r'"([\w.]+)"\s*,\s*(-?\d+)\s*$')
ev=[]
for line in open(sys.argv[1], encoding='utf-8-sig', errors='replace'):
    if not line.lstrip().startswith('Stage'): continue
    m=ROW.search(line.rstrip()); c=[x.strip() for x in line.split(',')]
    if m: ev.append((int(c[16]), int(c[9],16), m.group(1), int(m.group(2))))
ev.sort()
srv={e[1] for e in ev if e[2]=='srv.on_key_event'}
cl=[e for e in ev if e[1] not in srv]
t0=min(e[0] for e in ev)
print('breaker events and blocked process_key calls (seconds since capture start):')
for t,p,s,us in cl:
    if s.startswith('ipc.breaker'): print(f'  {(t-t0)/1e7:7.2f}s  {s:26} value={us/1000:.0f} ms')
    elif s=='tsf.process_key' and us>20000: print(f'  {(t-t0)/1e7:7.2f}s  process_key blocked {us/1000:.0f} ms')
# when did keys flow normally again after the last breaker close?
closes=[t for t,p,s,us in cl if s=='ipc.breaker.close']
keys=[(t,us) for t,p,s,us in cl if s=='ipc.key']
if closes:
    tc=closes[-1]; after=[k for k in keys if k[0]>tc]
    print(f'last breaker close at {(tc-t0)/1e7:.2f}s; ipc.key calls after it: {len(after)}')
