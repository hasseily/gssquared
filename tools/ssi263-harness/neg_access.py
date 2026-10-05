#!/usr/bin/env python3
"""Negative tests of the access and note comparison: inject single faults
into a passing render's events.txt and require compare.compare to FAIL
each with a writes/reads/notes reason, and the unmodified render to stay
EXACT.

usage: neg_access.py TAG CASE...      (e.g. neg_access.py strict_allmix mbaspeech_G hello)

Faults (each alone, on a copy in WORK/neg_access/, deleted afterwards):
  read value    the middle read of the busiest read address returns 00
                (on mbaspeech_G: the VIA IFR read $C48D at cycle 1575528,
                C0 in the reference, the interrupt left unserviced)
  read moved    one read one cycle later
  read dropped  one read removed (counts differ)
  read added    one read duplicated at the next cycle
  write value   one write's value XOR 01
  write moved   one write one cycle later
  write addr    one write to the next register
  traceread     a TRACEREAD line added (open-loop traces: the read differs
                from the capture) / a NOTE line added (closed loop)
Exit 1 if a fault passes or a baseline is not EXACT.
"""
import shutil, sys
from collections import Counter
from pathlib import Path

H = Path(__file__).resolve().parent
sys.path.insert(0, str(H))
import compare  # noqa: E402
import paths  # noqa: E402


def main():
    tag, names = sys.argv[1], sys.argv[2:]
    scratch = paths.WORK / 'neg_access'
    bad = 0
    try:
        for name in names:
            ref = paths.OUT / name / 'rtl'
            test = paths.OUT / name / tag
            base = compare.compare(str(ref), str(test))
            print(f"{name}: baseline {base['verdict']} ({base['reads']['n_ref']} reads, "
                  f"{base['writes']['n_ref']} writes, {base['notes']['n_ref']} notes in ref)")
            if base['verdict'] != 'EXACT':
                bad += 1
                for r in base['reasons'][:5]:
                    print('    - ' + r)
            lines = (test / 'events.txt').read_text().splitlines()
            idx = {'R': [], 'W': []}
            for i, l in enumerate(lines):
                p = l.split()
                if not l.startswith('#') and len(p) >= 5 and p[2] in idx:
                    idx[p[2]].append(i)
            # the read to change: mbaspeech_G's IFR read at 1575528 when
            # present, else the middle read of the busiest address
            target = [i for i in idx['R'] if lines[i].startswith('1575528 ') and ' R C48D ' in lines[i]]
            if not target:
                busiest = Counter(lines[i].split()[3] for i in idx['R']).most_common(1)[0][0]
                cand = [i for i in idx['R'] if lines[i].split()[3] == busiest and lines[i].split()[4] != '00']
                target = [cand[len(cand) // 2]]
            ri = target[0]
            wi = idx['W'][len(idx['W']) // 2]

            def field(l, k, v):
                p = l.split(); p[k] = v; return ' '.join(p)

            def shift(l):
                p = l.split(); p[0] = str(int(p[0]) + 1); return ' '.join(p)
            faults = []
            L = list(lines); L[ri] = field(L[ri], 4, '00')
            faults.append((f'read value: [{lines[ri]}] -> 00', L))
            L = list(lines); L[ri] = shift(L[ri])
            faults.append((f'read moved +1 cycle: [{lines[ri]}]', L))
            L = list(lines); del L[ri]
            faults.append((f'read dropped: [{lines[ri]}]', L))
            L = list(lines); L.insert(ri + 1, shift(lines[ri]))
            faults.append((f'read added after [{lines[ri]}]', L))
            pw = lines[wi].split()
            L = list(lines); L[wi] = field(L[wi], 4, '%02X' % (int(pw[4], 16) ^ 1))
            faults.append((f'write value ^01: [{lines[wi]}]', L))
            L = list(lines); L[wi] = shift(L[wi])
            faults.append((f'write moved +1 cycle: [{lines[wi]}]', L))
            L = list(lines); L[wi] = field(L[wi], 3, '%04X' % (int(pw[3], 16) + 1))
            faults.append((f'write address +1: [{lines[wi]}]', L))
            p = lines[ri].split()
            L = list(lines); L.insert(ri + 1, f'{p[0]} {p[1]} TRACEREAD {p[3]} trace={p[4]} here=00')
            faults.append(('TRACEREAD line added', L))
            L = list(lines); L.insert(ri + 1, f'{p[0]} {p[1]} NOTE waitd7 TIMEOUT')
            faults.append(('NOTE line added', L))
            for label, L in faults:
                d = scratch / name
                if d.exists():
                    shutil.rmtree(d)
                d.mkdir(parents=True)
                for f in ('sec.pcm', 'pri.pcm', 'card.pcm'):
                    shutil.copy(test / f, d / f)
                (d / 'events.txt').write_text('\n'.join(L) + '\n')
                res = compare.compare(str(ref), str(d))
                acc = [r for r in res['reasons'] if r.startswith(('writes:', 'reads:', 'notes'))]
                caught = res['verdict'] == 'FAIL' and acc
                if not caught:
                    bad += 1
                print(f"  {label:60s}: {res['verdict']}{'' if caught else '  <-- NOT CAUGHT'}")
                for r in acc[:2]:
                    print('      - ' + r)
    finally:
        if scratch.exists():
            shutil.rmtree(scratch)
    print('NEGATIVE TESTS: ' + ('PASS (every fault caught, baselines EXACT)' if not bad else f'FAIL ({bad})'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
