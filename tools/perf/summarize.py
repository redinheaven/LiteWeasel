"""Summarize LiteWeasel-Perf ETW traces.

Usage:
  logman create trace lw -p "{5974258d-005e-4490-adef-9e265b26abf5}" 0xffffffffffffffff 0xff -o lw.etl -ets
  ... type some text ...
  logman stop lw -ets
  tracerpt lw.etl -o lw.csv -of CSV -y
  python tools/perf/summarize.py lw.csv

Prints count / p50 / p95 / max (microseconds) for every stage.
Note: pass the level and keyword explicitly to logman, otherwise the
verbose-level events may be filtered out.
"""
import re
import statistics
import sys
from collections import defaultdict

# tracerpt CSV row: "Stage, ..., 45, 60, "srv.on_key_event", 1084"
ROW_RE = re.compile(r'"([\w.]+)"\s*,\s*(-?\d+)\s*$')


def main(path):
    stages = defaultdict(list)
    with open(path, encoding='utf-8-sig', errors='replace') as f:
        for line in f:
            if not line.lstrip().startswith('Stage'):
                continue
            m = ROW_RE.search(line.rstrip())
            if m:
                stages[m.group(1)].append(int(m.group(2)))
    if not stages:
        print('no LiteWeasel-Perf events found')
        return 1
    print(f'{"stage":28} {"count":>6} {"p50":>8} {"p95":>8} {"max":>8}  (us)')
    for name in sorted(stages):
        v = sorted(stages[name])
        p95 = v[min(len(v) - 1, int(len(v) * 0.95))]
        print(f'{name:28} {len(v):6} {int(statistics.median(v)):8} {p95:8} {v[-1]:8}')
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
