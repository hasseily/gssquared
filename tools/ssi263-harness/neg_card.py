#!/usr/bin/env python3
"""Negative tests of the card check: inject short faults into a passing
render's card.pcm and require compare.compare to FAIL each with a card
reason, and the unmodified render to keep its verdict.

usage: neg_card.py TAG CASE...      (e.g. neg_card.py strict_allmix phtalkkbd_04 hello)

TAG is a run_suite.py --tag whose renders in out/CASE/TAG pass. For each
case, on its louder channel:
  click      1 sample set to full scale of the opposite sign, at the
             quietest non-silent point and at the loudest point
  dip        10 ms at -6 dB (x/2) at the loudest 10 ms window
  wrapnoise  20 ms of int16-wrapped noise (x + U(-20000, 20000) mod 2^16)
             at the loudest window
  small      1 sample off by CARD_MAX_ERR + 1 (just over the gate)
and on the right channel, the whole case:
  hum        a 1 kHz tone of amplitude 10 (-70 dBFS, RMS 7.1) and of
             amplitude 30 (-61 dBFS), audible between the words: the
             noise-floor gate (compare.CARD_FLOOR_RMS) must catch both
The faulted copies live in WORK/neg_card/ and are deleted afterwards.
Exit 1 if a fault passes or the baseline is not EXACT/PASS.
"""
import shutil, sys
from pathlib import Path
import numpy as np

H = Path(__file__).resolve().parent
sys.path.insert(0, str(H))
import compare  # noqa: E402
import paths  # noqa: E402


def main():
    tag, names = sys.argv[1], sys.argv[2:]
    scratch = paths.WORK / 'neg_card'
    rng = np.random.default_rng(1)
    bad = 0
    try:
        for name in names:
            ref = paths.OUT / name / 'rtl'
            test = paths.OUT / name / tag
            base = compare.compare(str(ref), str(test))
            ok = base['verdict'] in ('EXACT', 'PASS')
            print(f"{name}: baseline {base['verdict']} (card {'ok' if base['card_ok'] else 'FAIL'}, "
                  f"max err L {base['card']['max_err_l']} R {base['card']['max_err_r']}, "
                  f"worst window error RMS L {base['card']['win_err_rms_l']:.1f} R {base['card']['win_err_rms_r']:.1f})")
            if not ok:
                bad += 1
                for r in base['reasons'][:5]:
                    print('    - ' + r)
            card = np.fromfile(test / 'card.pcm', '<i2').astype(np.int64).reshape(-1, 2)
            n = len(card)
            ch = int(np.argmax([np.sqrt(np.mean(card[:, c].astype(float) ** 2)) for c in (0, 1)]))
            x = card[:, ch].astype(np.float64)
            W = compare.CARD_WIN
            nw = n // W
            wr = np.sqrt(np.mean(x[:nw * W].reshape(nw, W) ** 2, axis=1))
            loud_w = int(np.argmax(wr))
            loud = loud_w * W
            peak = int(np.argmax(np.abs(x)))
            # quietest window with signal (rms > CARD_SIGNAL_RMS), its first sample
            sig = np.nonzero(wr > compare.CARD_SIGNAL_RMS)[0]
            quiet = int(sig[np.argmin(wr[sig])]) * W + W // 2 if len(sig) else n // 2

            def full_scale(v):
                return -32768 if v >= 0 else 32767
            faults = []
            for at, label in ((quiet, 'quiet'), (peak, 'peak')):
                c = card.copy(); c[at, ch] = full_scale(c[at, ch])
                faults.append((f'click 1 sample full scale @{at} ({label})', c))
            c = card.copy(); a, b = loud, min(n, loud + W)
            c[a:b, ch] = c[a:b, ch] // 2
            faults.append((f'dip 10 ms -6 dB @{a}', c))
            c = card.copy(); a, b = loud, min(n, loud + 2 * W)
            noisy = c[a:b, ch] + rng.integers(-20000, 20001, b - a)
            c[a:b, ch] = ((noisy + 32768) % 65536) - 32768
            faults.append((f'wrapped noise 20 ms @{a}', c))
            # off by the gate + 1 from the reference itself (the residual
            # already there is replaced, not added to)
            rcard = np.fromfile(ref / 'card.pcm', '<i2').astype(np.int64).reshape(-1, 2)
            c = card.copy(); at = quiet
            r0 = rcard[at, ch]
            c[at, ch] = r0 + (compare.CARD_MAX_ERR + 1) * (1 if r0 < 0 else -1)
            faults.append((f'small: 1 sample off by {compare.CARD_MAX_ERR + 1} @{at} ({"quiet"})', c))
            t = np.arange(n)
            for amp in (10, 30):
                c = card.copy()
                c[:, 1] = np.clip(c[:, 1] + np.round(amp * np.sin(2 * np.pi * 1000 * t / compare.SR)).astype(np.int64),
                                  -32768, 32767)
                faults.append((f'hum R: 1 kHz tone amplitude {amp}, whole case', c))
            for label, c in faults:
                d = scratch / name
                if d.exists():
                    shutil.rmtree(d)
                d.mkdir(parents=True)
                for f in ('sec.pcm', 'pri.pcm', 'events.txt'):
                    shutil.copy(test / f, d / f)
                c.astype('<i2').tofile(d / 'card.pcm')
                res = compare.compare(str(ref), str(d))
                card_reasons = [r for r in res['reasons'] if r.startswith('card')]
                caught = res['verdict'] == 'FAIL' and card_reasons
                if not caught:
                    bad += 1
                fch = 1 if label.startswith('hum R') else ch
                err = int(np.max(np.abs(c[:, fch] - card[:, fch])))  # injected change
                print(f"  {'CH ' + 'LR'[fch]} {label:46s} error {err:6d}: {res['verdict']}"
                      f"{'' if caught else '  <-- NOT CAUGHT'}")
                for r in card_reasons[:3]:
                    print('      - ' + r)
    finally:
        if scratch.exists():
            shutil.rmtree(scratch)
    print('NEGATIVE TESTS: ' + ('PASS (every fault caught, baselines pass)' if not bad else f'FAIL ({bad})'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
