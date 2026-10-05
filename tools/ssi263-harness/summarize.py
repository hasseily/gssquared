#!/usr/bin/env python3
"""Aggregate WORK/out/*/compare_TAG.json over the cases of a run.

usage: summarize.py [TAG ...] [-k PATTERN]

Per tag: verdict counts; cases with any sample difference; total differing
samples; worst sample error; lowest SNR; worst segment log-mel, level and
onset; event edges off; cases whose write, read or note (NOTE, TRACEREAD,
END) stream differs (acc); card check failures, worst card level and
tone-shape difference, lowest card SNR, worst card sample error (cErr),
10 ms window error RMS (cWin) and error RMS, mean removed, in a window where
the reference is silent (cFlr); per case group (syn_phone, syn
other, mbaspeech, mbaudit, Phasor programs).
"""
import argparse, fnmatch, json, math
from collections import defaultdict
from pathlib import Path

import paths


def group(name):
    if name.startswith('syn_phone_'):
        return 'syn phonemes'
    if name.startswith('syn_') or name == 'hello':
        return 'syn other'
    if name.startswith('mbaspeech'):
        return 'mb-audit A-G'
    if name.startswith('mbaudit'):
        return 'mb-audit tests'
    if name.startswith('ph'):
        return 'Phasor.hdv'
    return 'other'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tags', nargs='*', default=['gss'])
    ap.add_argument('-k', action='append', default=[])
    a = ap.parse_args()
    for tag in a.tags:
        rows = defaultdict(lambda: defaultdict(float))
        for p in sorted(paths.OUT.glob(f'*/compare_{tag}.json')):
            name = p.parent.name
            if a.k and not any(fnmatch.fnmatch(name, k) for k in a.k):
                continue
            r = json.loads(p.read_text())
            for g in (group(name), 'ALL'):
                s = rows[g]
                s['cases'] += 1
                s[r['verdict']] += 1
                diff = 0; tot = 0
                for ch, x in r['audio'].items():
                    diff += x['ndiff']; tot += x['len_ref'] if x['peak_ref'] else 0
                    s['max_err'] = max(s['max_err'], x['max_err'])
                    if x['ndiff'] and x['snr_db'] not in (None,):
                        snr = float(x['snr_db'])
                        s['min_snr'] = snr if 'min_snr' not in s else min(s['min_snr'], snr)
                    for seg in x['segments']:
                        if seg.get('mel_db') is not None:
                            s['max_mel'] = max(s['max_mel'], seg['mel_db'])
                        if seg['signal']:
                            s['max_level'] = max(s['max_level'], abs(seg['level_db']))
                        if seg.get('onset_ms') is not None:
                            s['max_onset'] = max(s['max_onset'], abs(seg['onset_ms']))
                        if seg.get('f0_cents') is not None:
                            s['max_f0'] = max(s['max_f0'], abs(seg['f0_cents']))
                s['diff_samples'] += diff
                s['total_samples'] += tot
                if diff:
                    s['cases_with_diff'] += 1
                for e in r['events'].values():
                    s['edges'] += e['n_ref']
                    s['edges_off'] += sum(1 for d in e['deltas'] if d) + abs(e['n_ref'] - e['n_test'])
                # cases whose write, read or note stream differs at all
                if 'identical' in r['reads']:
                    s['acc_diff'] += 0 if (r['reads']['identical'] and r['writes']['identical']
                                           and r['notes']['identical']) else 1
                else:   # a JSON from before every access was compared
                    s['acc_diff'] += 1 if r['reads'].get('n_diff') else 0
                c = r.get('card')
                if c is not None and 'card_ok' in r:
                    s['card_fail'] += 0 if r['card_ok'] else 1
                    for ch in ('l', 'r'):
                        if c.get('level_db_' + ch) is not None:
                            s['card_level'] = max(s['card_level'], abs(c['level_db_' + ch]))
                        if c.get('shape_db_' + ch) is not None:
                            s['card_shape'] = max(s['card_shape'], c['shape_db_' + ch])
                        s['card_err'] = max(s.get('card_err', 0), c.get('max_err_' + ch, 0))
                        s['card_win'] = max(s.get('card_win', 0.0), c.get('win_err_rms_' + ch, 0.0))
                        s['card_floor'] = max(s.get('card_floor', 0.0), c.get('silent_err_rms_' + ch, 0.0))
                        snr = c.get('snr_db_' + ch)
                        if snr is not None and c.get('ndiff_' + ch):
                            s['card_snr'] = snr if 'card_snr' not in s else min(s['card_snr'], snr)
        print(f'== {tag}')
        print(f"{'group':16s} {'cases':>5s} {'EXACT':>5s} {'PASS':>5s} {'FAIL':>5s} {'w/diff':>6s} "
              f"{'diff samples':>14s} {'max err':>7s} {'min SNR':>7s} {'mel':>5s} {'level':>5s} "
              f"{'onset':>6s} {'F0':>5s} {'edges':>6s} {'off':>4s} {'acc':>5s} "
              f"{'cardF':>5s} {'cLev':>5s} {'cShp':>5s} {'cSNR':>6s} {'cErr':>6s} {'cWin':>6s} {'cFlr':>5s}")
        for g in sorted(rows, key=lambda g: (g == 'ALL', g)):
            s = rows[g]
            pct = 100.0 * s['diff_samples'] / s['total_samples'] if s['total_samples'] else 0
            print(f"{g:16s} {int(s['cases']):5d} {int(s['EXACT']):5d} {int(s['PASS']):5d} {int(s['FAIL']):5d} "
                  f"{int(s['cases_with_diff']):6d} {int(s['diff_samples']):8d} {pct:4.1f}% {int(s['max_err']):7d} "
                  f"{s.get('min_snr', float('inf')):6.1f}dB {s['max_mel']:4.2f} {s['max_level']:5.2f} "
                  f"{s['max_onset']:5.2f}ms {s['max_f0']:5.1f} {int(s['edges']):6d} {int(s['edges_off']):4d} "
                  f"{int(s['acc_diff']):5d} {int(s['card_fail']):5d} {s['card_level']:5.2f} "
                  f"{s['card_shape']:5.2f} {s.get('card_snr', float('inf')):5.1f}dB {int(s.get('card_err', 0)):6d} "
                  f"{s.get('card_win', 0.0):6.1f} {s.get('card_floor', 0.0):5.1f}")


if __name__ == '__main__':
    main()
