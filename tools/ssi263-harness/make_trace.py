#!/usr/bin/env python3
"""Cut a replay trace (WORK/traces/NAME.trace) out of an a2vm --phasor-log
capture.

usage: make_trace.py PHASOR_LOG NAME --from CYCLE --to CYCLE [--lead 2000]
                     [--title TEXT] [--dest DIR]

The trace's first line names PHASOR_LOG as given (capture.py runs this from
WORK/traces with ../cap/NAME/phasor.log, so the line is the same wherever
WORK is).

The cut keeps every card access in [FROM, TO) with its cycle, rebased so the
cut starts at cycle LEAD of the replay. Before it, a preamble rebuilds the
card's register state at FROM from the capture's earlier writes: the mode
switch, the VIA registers that hold configuration (DDRA/DDRB, ORB/ORA, ACR,
PCR, IER, timer latches are left to the program), then each SSI socket:
CTL=1, its registers 0-2 and 4, then its last CTL value (lowering CTL starts
the latched phone, as it would have been running). Both renderers replay the
same file, so the comparison is fair whatever the preamble misses; the
capture's own read values are kept in the trace and checked by the replay
(TRACEREAD lines in events.txt flag reads that differ from the capture).
"""
import argparse
from pathlib import Path

import paths


def ssi_targets(addr, mode):
    """Sockets an SSI write reaches (mockingboard.sv:100-103): 'P' (A6), 'S' (A5)."""
    if mode not in (0, 5):
        return []
    off = addr & 0xFF
    t = []
    if off & 0x40:
        t.append('P')
    if off & 0x20:
        t.append('S')
    return t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('name')
    ap.add_argument('--from', dest='frm', type=int, default=0)
    ap.add_argument('--to', type=int, default=None)
    ap.add_argument('--lead', type=int, default=2000)
    ap.add_argument('--title', default='')
    ap.add_argument('--dest', default=str(paths.TRACES))
    a = ap.parse_args()

    mode = 0
    via = {}            # (via index, reg) -> last value
    ssi = {'P': {}, 'S': {}}
    body = []
    for line in open(a.log):
        if line.startswith('#'):
            continue
        p = line.split()
        if len(p) < 2:
            continue
        c = int(p[0])
        if p[1] == 'END':
            break
        if a.to is not None and c >= a.to:
            break
        if c < a.frm:
            if p[1] == 'RES':
                mode = 0
                for s in ssi.values():
                    s[3] = 0x80
                via.clear()
                continue
            addr = int(p[2], 16)
            val = int(p[3], 16)
            if (addr & 0xFFF0) == 0xC0C0:
                mode = 0 if addr & 8 else mode
                mode |= addr & 7
                continue
            if p[1] != 'W':
                continue
            off = addr & 0xFF
            reg = off & 0x0F
            vias = []
            if mode == 0:
                vias = [1 if off & 0x80 else 0]
            elif mode == 5:
                vias = ([0] if off & 0x10 else []) + ([1] if off & 0x80 else [])
            for v in vias:
                if reg in (0, 1, 2, 3, 0x0B, 0x0C, 0x0E):
                    via[(v, reg)] = val if reg != 0x0E else (via.get((v, reg), 0) | (val & 0x7F)
                                                              if val & 0x80 else
                                                              via.get((v, reg), 0) & ~val & 0x7F)
            for s in ssi_targets(addr, mode):
                ssi[s][addr & 7 if (addr & 7) < 4 else 4] = val
            continue
        body.append((c - a.frm + a.lead, ' '.join(p[1:])))

    out = [f'# {a.title or a.name}: cut of {a.log} [{a.frm}, {a.to}) rebased to {a.lead}']
    t = 10
    def w(addr, val):
        nonlocal t
        out.append(f'{t} W {addr:04X} {val:02X}')
        t += 6
    # Mode: Phasor native needs $C0C5 (the card starts in Mockingboard mode).
    # VIA configuration (written in Mockingboard mode: VIA-A at $C400, B at $C480).
    for (v, reg), val in sorted(via.items()):
        if reg == 0x0E:
            w(0xC400 + 0x80 * v + 0x0E, 0x7F)
            if val:
                w(0xC400 + 0x80 * v + 0x0E, 0x80 | val)
        else:
            w(0xC400 + 0x80 * v + reg, val)
    if mode:
        out.append(f'{t} R C0C{mode:X} 00')
        t += 6
    # SSI sockets: at $C440 (P) / $C420 (S); in MB mode these also alias
    # VIA-A registers 0-7, so restore the VIAs again afterwards.
    for s, base in (('P', 0xC440), ('S', 0xC420)):
        regs = ssi[s]
        if not regs:
            continue
        w(base + 3, 0x80)
        for r in (1, 2, 4, 0):
            if r in regs:
                w(base + r, regs[r])
        if 3 in regs:
            w(base + 3, regs[3])
    if mode == 0:
        for (v, reg), val in sorted(via.items()):
            if v == 0 and reg in (0, 1, 2, 3):
                w(0xC400 + reg, val)
    if t >= a.lead:
        raise SystemExit('preamble longer than --lead')
    for c, rest in body:
        out.append(f'{c} {rest}')
    end = (a.to - a.frm + a.lead) if a.to is not None else (body[-1][0] + 48000 if body else a.lead)
    out.append(f'{end} END')
    dst = Path(a.dest) / f'{a.name}.trace'
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text('\n'.join(out) + '\n')
    print(f'{dst}: {len(body)} accesses, {end} cycles ({end / 1020484:.2f} s)')


if __name__ == '__main__':
    main()
