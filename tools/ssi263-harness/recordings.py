#!/usr/bin/env python3
"""Supporting evidence only (never the reference): appletini-one's
Assets/Sounds/"Mockingboard mb-audit samples" A-G against the RTL and
GSSquared renders of the same mb-audit speech phrases (traces mbaspeech_X).

For each phrase: the speech span (first to last 10 ms frame within 30 dB of
the loudest), and the DTW log-mel distance (per-frame mean-removed, so the
analog level and the recording chain's gain do not count) of the RTL render
and of the GSSquared render to the recording.

usage: recordings.py --rec DIR   (DIR holds A.wav .. G.wav, 48 kHz mono:
       appletini-one's Assets/Sounds/"Mockingboard mb-audit samples",
       converted; not part of the harness)
"""
import argparse, os, sys, wave
import numpy as np
from pathlib import Path

H = Path(__file__).resolve().parent
sys.path.insert(0, str(H))
import compare  # noqa: E402
import paths  # noqa: E402


def load_wav(p):
    with wave.open(str(p)) as w:
        assert w.getframerate() == 48000 and w.getsampwidth() == 2
        a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.int64)
        if w.getnchannels() == 2:
            a = a.reshape(-1, 2).mean(axis=1).astype(np.int64)
    return a


def span(x):
    f = compare.frames(x.astype(np.float64), 480, 480)
    e = 10 * np.log10(np.mean(f ** 2, axis=1) + 1e-9)
    thr = max(e.max() - 30, np.percentile(e, 10) + 8)   # above the noise floor
    k = np.nonzero(e > thr)[0]
    return (k[-1] - k[0] + 1) * 0.01 if len(k) else 0.0, k[0] if len(k) else 0


def shape(x):
    m, e = compare.logmel(x)
    edb = 20 * np.log10(e + 1e-12)
    keep = edb > max(edb.max() - 30, np.percentile(edb, 10) + 8)
    m = m[keep]
    return m - m.mean(axis=1, keepdims=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rec', required=True)
    a = ap.parse_args()
    print(f"{'phrase':6s} {'rec span':>9s} {'rtl span':>9s} {'gss span':>9s}   {'DTW rtl-rec':>11s} {'DTW gss-rec':>11s} {'DTW gss-rtl':>11s}")
    for x in 'ABCDEFG':
        rec = load_wav(Path(a.rec) / f'{x}.wav')
        base = paths.OUT / f'mbaspeech_{x}'
        if not (base / 'rtl' / 'pri.pcm').exists():
            continue
        rtl = compare.load_pcm(base / 'rtl' / 'pri.pcm')
        gss = compare.load_pcm(base / 'gss' / 'pri.pcm')
        sr, _ = span(rec); st, _ = span(rtl); sg, _ = span(gss)
        mr, mt, mg = shape(rec), shape(rtl), shape(gss)
        band = abs(len(mr) - len(mt)) + max(len(mr), len(mt)) // 4 + 10
        d1 = compare.dtw_dist(mt, mr, band)
        d2 = compare.dtw_dist(mg, mr, band)
        d3 = compare.dtw_dist(mg, mt, band)
        print(f"{x:6s} {sr:8.2f}s {st:8.2f}s {sg:8.2f}s   {d1:9.2f}dB {d2:9.2f}dB {d3:9.2f}dB")


if __name__ == '__main__':
    main()
