"""Per-process breakdown of LiteWeasel-Perf events: first key vs. later keys.

Usage: python tools/perf/by_pid.py lw.csv   (lw.csv from tracerpt -of CSV)
"""
import re, sys
from collections import defaultdict
ROW = re.compile(r'"([\w.]+)"\s*,\s*(-?\d+)\s*$')
ev = defaultdict(list)
for line in open(sys.argv[1], encoding='utf-8-sig', errors='replace'):
    if not line.lstrip().startswith('Stage'):
        continue
    m = ROW.search(line.rstrip()); c = [x.strip() for x in line.split(',')]
    if m:
        ev[int(c[9], 16)].append((int(c[16]), m.group(1), int(m.group(2))))
for pid, rows in sorted(ev.items(), key=lambda kv: min(r[0] for r in kv[1])):
    rows.sort()
    stages = defaultdict(list)
    for t, s, us in rows: stages[s].append(us)
    if 'tsf.ipc_key_roundtrip' not in stages:
        print(f'pid {pid}: server only ({len(rows)} events)'); continue
    print(f'pid {pid}: {len(stages["tsf.ipc_key_roundtrip"])} keys')
    for s in ('tsf.ipc_key_roundtrip','tsf.get_response','tsf.do_edit_session','ui.do_paint','ui.present'):
        v = stages.get(s, [])
        if v: print(f'   {s:24} first={v[0]:7}  next-median={sorted(v[1:])[len(v[1:])//2] if len(v)>1 else "-":>7}  n={len(v)}')
