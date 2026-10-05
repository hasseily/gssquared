#!/usr/bin/env python3
"""Tone-stage-only check over every case and over synthetic inputs.

usage: tone/tone_suite.py [--jobs N] [--clock card|fixed] [--d D] [--no-synth]

1. Every reference render out/*/rtl (bin/card_rtl): bin/tone_check runs the
   per-clock model of the RTL tone stage (tone/tone_model.hpp) and
   GSSquared's PhasorAudio::WarmthChannel on the RTL's own speech (sec.pcm
   -> left, pri.pcm -> right) and compares both with the RTL's card.pcm.
   The model must match every sample of every case; WarmthChannel's
   difference is GSSquared's tone-stage residual.
2. Synthetic inputs (steps up to full scale, so the warmth knee and the
   output saturation are reached; full-scale noise; sine sweeps) through
   the RTL block itself (bin/tone_rtl: mockingboard.sv's final_audio_mix
   under Verilator), the model and WarmthChannel.
Prints a line per case and the totals; exit 1 if the model ever differs
from the RTL.
"""
import argparse, concurrent.futures as cf, re, subprocess, sys
from pathlib import Path
import numpy as np

H = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(H))
import paths  # noqa: E402
TMP = paths.WORK / 'tone_tmp'
LINE = re.compile(r'^([LR]) model-vs-rtl (\d+)/(\d+) diff max (\d+) \| gss-vs-rtl (\d+) diff max (\d+) @(\d+) '
                  r'win-rms ([\d.]+) @(\d+) snr (\S+) dB')


def run_case(d, args):
    r = subprocess.run([str(paths.BIN / 'tone_check'), str(d), '--clock', args.clock, '--d', str(args.d)],
                       capture_output=True, text=True)
    out = {}
    for l in r.stdout.splitlines():
        m = LINE.match(l)
        if m:
            out[m.group(1)] = dict(model_diff=int(m.group(2)), n=int(m.group(3)), model_max=int(m.group(4)),
                                   gss_diff=int(m.group(5)), gss_max=int(m.group(6)), gss_at=int(m.group(7)),
                                   win=float(m.group(8)), win_at=int(m.group(9)), snr=float(m.group(10)))
    if r.stdout.strip() == 'empty':
        out = {'L': None, 'R': None}
    return d.parent.name, out, r.stdout + r.stderr


def synth():
    """Each signal starts with 10 ms of silence: the bench's first sample
    period is one 130-clock Apple cycle (rtl_card.hpp), not a whole
    2777-clock period, so a signal present from sample 0 would measure that
    start-up, not the tone stage (the real cases all start silent)."""
    sr = 48000
    rng = np.random.default_rng(7)
    parts = []
    # steps: 50 ms holds through a ladder of levels up to both rails
    lv = [0, 8000, -8000, 16000, -16000, 20000, -20000, 24000, -24000, 28000, -28000,
          32767, -32768, 32767, 0, -32768, 0, 12000, 30000, -30000, 0]
    st = np.repeat(np.array(lv, dtype=np.int64), sr // 20)
    parts.append(('steps', np.stack([st, -st], axis=1)))
    nz = rng.integers(-32768, 32768, sr // 2)
    parts.append(('noise', np.stack([nz, rng.integers(-4000, 4001, sr // 2)], axis=1)))
    t = np.arange(sr) / sr
    f = 50 * (20000 / 50) ** t                      # log sweep 50 Hz - 20 kHz in 1 s
    ph = 2 * np.pi * np.cumsum(f) / sr
    parts.append(('sweep', np.stack([np.round(32767 * np.sin(ph)), np.round(3277 * np.sin(ph))], axis=1).astype(np.int64)))
    z = np.zeros((sr // 100, 2), dtype=np.int64)
    return [(label, np.concatenate([z, x])) for label, x in parts]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--clock', default='card')
    ap.add_argument('--d', type=int, default=151)
    ap.add_argument('--no-synth', action='store_true')
    ap.add_argument('--synth-only', action='store_true')
    ap.add_argument('-k', default='*')
    args = ap.parse_args()
    dirs = [] if args.synth_only else sorted(p for p in paths.OUT.glob(args.k + '/rtl') if (p / 'card.pcm').exists())
    bad = 0
    tot = dict(n=0, model_diff=0, gss_diff=0, gss_max=0, win=0.0, snr=float('inf'))
    worst = {}
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for name, out, raw in ex.map(lambda d: run_case(d, args), dirs):
            if set(out) != {'L', 'R'}:
                print(f'{name:28s} ERROR {raw.strip()[:200]}'); bad += 1; continue
            if out['L'] is None:
                print(f'{name:28s} empty'); continue
            parts = []
            for ch in 'LR':
                o = out[ch]
                tot['n'] += o['n']; tot['model_diff'] += o['model_diff']; tot['gss_diff'] += o['gss_diff']
                if o['model_diff']:
                    bad += 1
                for k in ('gss_max', 'win'):
                    if o[k] > tot[k]:
                        tot[k] = o[k]; worst[k] = f"{name} {ch} @{o['gss_at' if k == 'gss_max' else 'win_at']}"
                if o['gss_diff'] and o['snr'] < tot['snr']:
                    tot['snr'] = o['snr']; worst['snr'] = f'{name} {ch}'
                parts.append(f"{ch}: model {o['model_diff']} diff | gss {o['gss_diff']}/{o['n']} max {o['gss_max']} "
                             f"win {o['win']:.1f} snr {o['snr']:.1f}")
            print(f'{name:28s} ' + '  '.join(parts))
    print(f"\n{len(dirs)} cases, {tot['n']} channel-samples: model vs RTL {tot['model_diff']} samples differ; "
          f"WarmthChannel vs RTL {tot['gss_diff']} differ, max {tot['gss_max']} ({worst.get('gss_max')}), "
          f"worst 10 ms error RMS {tot['win']:.1f} ({worst.get('win')}), lowest SNR {tot['snr']:.1f} dB ({worst.get('snr')})")
    if not args.no_synth:
        TMP.mkdir(exist_ok=True)
        try:
            print('\nsynthetic inputs (L full scale, R about -20 dBFS):')
            for label, x in synth():
                inp = TMP / f'{label}_in.pcm'
                x.astype('<i2').tofile(inp)
                rtl, mod, gss = TMP / f'{label}_rtl.pcm', TMP / f'{label}_model.pcm', TMP / f'{label}_gss.pcm'
                subprocess.run([str(paths.BIN / 'tone_rtl'), str(inp), str(rtl), '--clock', args.clock,
                                '--d', str(args.d)], check=True)
                subprocess.run([str(paths.BIN / 'tone_check'), '--in', str(inp), '--clock', args.clock, '--d',
                                str(args.d), '--model-out', str(mod), '--gss-out', str(gss)], check=True,
                               capture_output=True)
                R = np.fromfile(rtl, '<i2').astype(np.int64).reshape(-1, 2)
                M = np.fromfile(mod, '<i2').astype(np.int64).reshape(-1, 2)
                G = np.fromfile(gss, '<i2').astype(np.int64).reshape(-1, 2)
                for c in (0, 1):
                    md = int(np.count_nonzero(R[:, c] - M[:, c]))
                    e = G[:, c] - R[:, c]
                    if md:
                        bad += 1
                    pr = float(np.sum(R[:, c].astype(float) ** 2)); pe = float(np.sum(e.astype(float) ** 2))
                    snr = 10 * np.log10(pr / pe) if pe else float('inf')
                    print(f"  {label:6s} {'LR'[c]}: RTL block vs model {md} of {len(R)} differ; WarmthChannel vs RTL "
                          f"max {int(np.max(np.abs(e)))} @{int(np.argmax(np.abs(e)))}, {int(np.count_nonzero(e))} differ, "
                          f"SNR {snr:.1f} dB; RTL peak {int(np.max(np.abs(R[:, c])))}")
        finally:
            for p in TMP.glob('*'):
                p.unlink()
            TMP.rmdir()
    print('TONE: ' + ('model = RTL everywhere' if not bad else f'{bad} FAILURES'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
