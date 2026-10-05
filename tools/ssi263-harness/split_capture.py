#!/usr/bin/env python3
"""Split an a2vm --phasor-log capture into speech traces (one per utterance
group): a new group starts where no SSI register write happened for GAP
cycles. Each group becomes WORK/traces/PREFIX_NN.trace via make_trace.py, from
LEAD cycles before its first SSI write to TAIL cycles after its last card
access (clipped to the next group).

usage: split_capture.py PHASOR_LOG PREFIX [--gap 1500000] [--lead 150000]
                        [--tail 400000] [--title TEXT] [--min-writes 4]
                        [--dest DIR]
"""
import argparse, subprocess, sys
from pathlib import Path

H = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log'); ap.add_argument('prefix')
    ap.add_argument('--gap', type=int, default=1_500_000)
    ap.add_argument('--lead', type=int, default=150_000)
    ap.add_argument('--tail', type=int, default=400_000)
    ap.add_argument('--min-writes', type=int, default=4)
    ap.add_argument('--title', default='')
    ap.add_argument('--dest', default=None)
    a = ap.parse_args()
    acc, ssi = [], []
    mode = 0
    for line in open(a.log):
        if line.startswith('#'):
            continue
        p = line.split()
        if len(p) < 3 or p[1] not in ('R', 'W'):
            continue
        c, addr = int(p[0]), int(p[2], 16)
        acc.append(c)
        if (addr & 0xFFF0) == 0xC0C0:
            mode = (0 if addr & 8 else mode) | (addr & 7)
        elif p[1] == 'W' and (addr & 0xFF00) == 0xC400 and (addr & 0x60) and mode in (0, 5):
            ssi.append(c)
    groups = []
    for c in ssi:
        if groups and c - groups[-1][1] <= a.gap:
            groups[-1][1] = c; groups[-1][2] += 1
        else:
            groups.append([c, c, 1])
    groups = [g for g in groups if g[2] >= a.min_writes]
    for i, (s, e, n) in enumerate(groups):
        nxt = groups[i + 1][0] - a.lead if i + 1 < len(groups) else None
        last = max(x for x in acc if x <= (nxt if nxt else 1 << 62) and x >= s)
        to = last + a.tail
        if nxt is not None:
            to = min(to, nxt)
        name = f'{a.prefix}_{i + 1:02d}'
        subprocess.run([sys.executable, str(H / 'make_trace.py'), a.log, name,
                        '--from', str(max(0, s - a.lead)), '--to', str(to),
                        '--title', f'{a.title} (utterance group {i + 1}, {n} SSI writes)']
                       + (['--dest', a.dest] if a.dest else []),
                       check=True)


if __name__ == '__main__':
    main()
