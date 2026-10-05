#!/usr/bin/env python3
"""Compare a GSSquared render with the RTL reference render of one case.

usage: compare.py REF_DIR TEST_DIR [--json OUT] [--quiet]

Both directories hold sec.pcm, pri.pcm (int16 mono 48 kHz), card.pcm
(int16 stereo) and events.txt from the renderers (common/driver.hpp).

Metrics
  events   D7 (each socket), the CPU IRQ line and each socket's direct IRQ:
           rise/fall cycles matched in order. Accesses: every write and
           every read (cycle, address, value; the driver logs each one) in
           order, and the NOTE / TRACEREAD / END lines.
  audio    per socket: sample-exact (first differing sample, count, max
           error), SNR, best SNR within +-4 samples of lag; per phone (a
           segment starts at each reg-0 write of that socket in the reference):
           RMS level difference, onset (5 ms RMS envelope's first upward crossing of 30% of the reference segment's peak, when the reference starts below it) difference, frame
           log-mel distance (25 ms frames, 10 ms hop, 40 bands, 60-6000 Hz,
           in dB) both frame-aligned and after DTW (+-15 frame band), and F0
           (autocorrelation, voiced frames of both).

Verdict (thresholds; see README.md):
  EXACT  every sample of both sockets and every event identical
  PASS   events within EVENT_TOL cycles, every write, read and note
         identical (count, order, cycle, address, value), and for every
         phone with signal: |level diff| <= LEVEL_DB, |onset diff| <=
         ONSET_MS, aligned log-mel <= MEL_DB, |F0 diff| <= F0_CENTS
  FAIL   otherwise (the reasons are listed): any write moved or mismatched,
         any read whose cycle, address or value differs, a different
         number of writes or reads, a NOTE/TRACEREAD/END line that differs;
         or the card check fails

Card check (card.pcm, the card's stereo output; the PSGs are muted on the
RTL side by rtl/ym2149_mute.sv and silent in gss/card_gss.cpp, so it is the
speech path through the Phasor mixer and tone stage alone). Per channel:
  routing  the gain of each socket (A5 secondary, A6 primary) into the
           channel, estimated on each side from its own sec.pcm/pri.pcm
           through the warmth tone stage (least squares); the RTL routes A5
           to the left only and A6 to the right only, at unity
           (mockingboard.sv:940-962). Each determinable gain within
           CARD_ROUTE_TOL of the reference's.
  silence  no CARD_WIN window where the reference channel is silent
           (RMS <= CARD_SILENT_RMS) and the test has signal (RMS >
           CARD_SIGNAL_RMS), nor the other way round.
  lag      the test channel lines up with the reference at lag 0 (best SNR
           over +-CARD_MAX_LAG samples): no added or missing card latency.
           Checked only when both sides' sec.pcm and pri.pcm are identical
           (otherwise the speech's own timing differences dominate it).
  level    whole-case RMS within CARD_LEVEL_DB.
  shape    frame log-mel with each frame's mean level removed (the tone
           stage's spectral shape) within CARD_SHAPE_DB.
  sample   every sample's error |test - ref| <= CARD_MAX_ERR.
  window   the error RMS of every CARD_WIN window <= CARD_WIN_ERR_RMS.
  floor    in every CARD_WIN window where the reference is silent (RMS <=
           CARD_SILENT_RMS), the error RMS with the window's mean removed
           <= CARD_FLOOR_RMS (6, -75 dBFS; the correct build measures at
           most CARD_FLOOR_MEASURED): a steady hum, buzz or hiss between
           the words, too small for the gates above, fails here.
  The last two catch what whole-case averages cannot: a click, a dropout,
  a short burst of noise. Their limits sit just above the one difference
  the card stage keeps when the speech is identical: GSSquared's collapsed
  warmth form (PhasorAudio::WarmthChannel) against the RTL's per-clock
  tone stage, measured with the RTL's own speech as input over every case
  (tone/tone_check.cpp, README "The tone stage and the WARMTH residual"): at most CARD_TONE_MAX_ERR per
  sample and CARD_TONE_WIN_RMS per window. bin/card_rtl clocks the tone
  stage as the card does (2777/2778 fabric clocks a sample), so that
  residual is GSSquared's own, not the harness's.
"""
import argparse, json, math, os, sys
from collections import defaultdict
import numpy as np

EVENT_TOL = 2        # Apple cycles
LEVEL_DB = 1.0
ONSET_MS = 1.0
MEL_DB = 1.5
F0_CENTS = 20.0
SR = 48000
# Card check (see the docstring).
CARD_LEVEL_DB = 0.1
CARD_SHAPE_DB = 0.2
CARD_ROUTE_TOL = 0.03    # gain units (0.26 dB at unity); a wrong route is 0.29 or more
CARD_SILENT_RMS = 30.0   # int16 RMS, about -61 dBFS
CARD_SIGNAL_RMS = 100.0  # int16 RMS, about -50 dBFS (hysteresis against SILENT)
CARD_MAX_LAG = 4         # samples searched for the card's alignment
CARD_WIN = 480           # 10 ms windows
# Per sample and per window (see the docstring). The measured residual of
# GSSquared's collapsed warmth form against the RTL tone stage, all 157
# cases, the RTL's own speech as input (tone/tone_check.cpp):
CARD_TONE_MAX_ERR = 93        # syn_phone_31 R (tone/tone_suite.py, 2026-10-05)
CARD_TONE_WIN_RMS = 27.7      # syn_phone_31 R
# The gates: about 1.4x the residual. A difference GSSquared's card stage
# adds on top of its tone-stage residual (a click, a dropout, a burst, a
# gain step) has to stay under these to pass.
CARD_MAX_ERR = 128            # int16 LSB (-48 dBFS)
CARD_WIN_ERR_RMS = 40.0       # int16 RMS over CARD_WIN (-58 dBFS)
# Noise floor in silent passages: in every CARD_WIN window where the
# reference channel is silent (RMS <= CARD_SILENT_RMS), the error with the
# window's own mean removed. The correct build (bin/card_gss_allmix, the
# same tone-stage residual) measures at most CARD_FLOOR_MEASURED over all
# 157 cases; a hum, buzz or hiss under the words adds to it.
CARD_FLOOR_MEASURED = 3.4     # README "Results" (2ad4f50a, mbaspeech_F R)
CARD_FLOOR_RMS = 6.0          # int16 RMS (-75 dBFS); a 1 kHz tone of amplitude 10 is 7.1


def load_pcm(path, ch=1):
    if not os.path.exists(path):
        return np.zeros(0, dtype=np.int64)
    a = np.fromfile(path, dtype='<i2').astype(np.int64)
    return a.reshape(-1, ch) if ch > 1 else a


def load_events(path):
    ev = defaultdict(list)    # signal -> [(cycle, sample, value)]
    writes, reads, misc = [], [], []
    with open(path) as f:
        for line in f:
            if line.startswith('#'):
                continue
            p = line.split()
            if len(p) < 3:
                continue
            c, s, op = int(p[0]), int(p[1]), p[2]
            if op in ('IRQ', 'D7S', 'D7P', 'DIRQS', 'DIRQP'):
                ev[op].append((c, s, int(p[3])))
            elif op == 'W':
                writes.append((c, s, int(p[3], 16), int(p[4], 16)))
            elif op == 'R':
                reads.append((c, s, int(p[3], 16), int(p[4], 16)))
            else:
                misc.append(line.strip())
    return ev, writes, reads, misc


def phone_starts(writes, socket):
    """Sample indices of reg-0 writes that reach a socket (A6 primary, A5
    secondary), any mode (the writes the decode sends to the SSI)."""
    bit = 0x40 if socket == 'pri' else 0x20
    out = []
    for c, s, a, v in writes:
        if (a & 0xFF00) == 0xC400 and (a & bit) and (a & 7) == 0:
            out.append((s, v))
    return out


_MEL = {}


def mel_fb(n_fft=1200, n_mels=40, fmin=60.0, fmax=6000.0):
    key = (n_fft, n_mels)
    if key in _MEL:
        return _MEL[key]
    def hz2mel(h): return 2595 * np.log10(1 + h / 700.0)
    def mel2hz(m): return 700 * (10 ** (m / 2595.0) - 1)
    mels = np.linspace(hz2mel(fmin), hz2mel(fmax), n_mels + 2)
    hz = mel2hz(mels)
    bins = np.floor((n_fft + 1) * hz / SR).astype(int)
    fb = np.zeros((n_mels, n_fft // 2 + 1))
    for i in range(n_mels):
        l, c, r = bins[i], bins[i + 1], bins[i + 2]
        for k in range(l, c):
            fb[i, k] = (k - l) / max(c - l, 1)
        for k in range(c, r):
            fb[i, k] = (r - k) / max(r - c, 1)
    _MEL[key] = fb
    return fb


def frames(x, win=1200, hop=480):
    if len(x) < win:
        x = np.concatenate([x, np.zeros(win - len(x))])
    n = 1 + (len(x) - win) // hop
    idx = np.arange(win)[None, :] + hop * np.arange(n)[:, None]
    return x[idx]


def logmel(x):
    f = frames(x.astype(np.float64) / 32768.0) * np.hanning(1200)
    spec = np.abs(np.fft.rfft(f, axis=1)) ** 2
    m = spec @ mel_fb().T
    # 60 dB below the loudest band of the excerpt is the floor: differences
    # in bands far below audibility (or exact digital silence) do not count.
    floor = max(float(m.max()) * 1e-6, 1e-12)
    return 10 * np.log10(np.maximum(m, floor)), np.sqrt(np.mean(f ** 2, axis=1) + 1e-20)


def dtw_dist(a, b, band=15):
    n, m = len(a), len(b)
    if n == 0 or m == 0:
        return float('nan')
    INF = 1e18
    D = np.full((n + 1, m + 1), INF)
    D[0, 0] = 0
    for i in range(1, n + 1):
        lo = max(1, i - band); hi = min(m, i + band)
        if lo > hi:
            continue
        cost = np.mean(np.abs(b[lo - 1:hi] - a[i - 1]), axis=1)
        for j in range(lo, hi + 1):
            D[i, j] = cost[j - lo] + min(D[i - 1, j], D[i, j - 1], D[i - 1, j - 1])
    return D[n, m] / (n + m)


def f0_track(x, win=1920, hop=480):
    f = frames(x.astype(np.float64), win, hop)
    out = []
    lo, hi = SR // 300, SR // 50     # the SSI-263 glottal range
    for fr in f:
        fr = fr - fr.mean()
        e = np.dot(fr, fr)
        if e < win * 200.0 ** 2:
            out.append(0.0); continue
        ac = np.correlate(fr, fr, 'full')[win - 1:]
        seg = ac[lo:hi]
        k = int(np.argmax(seg)) + lo
        if ac[k] / ac[0] < 0.5:
            out.append(0.0); continue
        out.append(SR / k)
    return np.array(out)


def snr_db(r, d):
    pr = float(np.sum(r.astype(np.float64) ** 2))
    pd = float(np.sum(d.astype(np.float64) ** 2))
    if pd == 0:
        return float('inf')
    if pr == 0:
        return float('-inf')
    return 10 * math.log10(pr / pd)


def audio_metrics(r, g, starts):
    n = min(len(r), len(g))
    res = {'len_ref': int(len(r)), 'len_test': int(len(g))}
    r = r[:n]; g = g[:n]
    d = r - g
    nz = np.nonzero(d)[0]
    res['exact'] = bool(len(nz) == 0 and len(r) == len(g))
    res['ndiff'] = int(len(nz))
    res['first_diff'] = int(nz[0]) if len(nz) else None
    res['max_err'] = int(np.max(np.abs(d))) if n else 0
    res['peak_ref'] = int(np.max(np.abs(r))) if n else 0
    res['peak_test'] = int(np.max(np.abs(g))) if n else 0
    res['snr_db'] = snr_db(r, d)
    best = (res['snr_db'], 0)
    for lag in range(-4, 5):
        if lag == 0:
            continue
        if lag > 0:
            s = snr_db(r[lag:], r[lag:] - g[:n - lag])
        else:
            s = snr_db(r[:n + lag], r[:n + lag] - g[-lag:])
        if s > best[0]:
            best = (s, lag)
    res['best_lag'] = best[1]
    res['best_lag_snr_db'] = best[0]
    # Per-phone segments.
    segs = []
    bounds = [s for s, v in starts if s < n] + [n]
    if not starts or starts[0][0] > 0:
        bounds = [0] + bounds
        labels = ['pre'] + ['%02X' % v for s, v in starts if s < n]
    else:
        labels = ['%02X' % v for s, v in starts if s < n]
    for i in range(len(bounds) - 1):
        a, b = bounds[i], bounds[i + 1]
        if b - a < 240:
            continue
        rs, gs = r[a:b], g[a:b]
        seg = {'start': a, 'end': b, 'phone': labels[i] if i < len(labels) else '?'}
        rr = math.sqrt(float(np.mean(rs.astype(np.float64) ** 2)))
        gr = math.sqrt(float(np.mean(gs.astype(np.float64) ** 2)))
        seg['rms_ref'] = rr
        seg['rms_test'] = gr
        # Level, onset and spectral metrics need at least 20 ms.
        seg['signal'] = rr > 100.0 and b - a >= 960
        seg['level_db'] = 20 * math.log10((gr + 1e-9) / (rr + 1e-9)) if seg['signal'] else 0.0
        def env(x):
            e = np.sqrt(np.convolve(x.astype(np.float64) ** 2, np.ones(240) / 240.0, 'same'))
            return e
        er_, eg_ = env(rs), env(gs)
        thr = max(64.0, 0.3 * float(np.max(er_)))
        def onset(e):
            # the first upward crossing (a tail of the previous sound that
            # starts the segment above the threshold is not an onset)
            up = np.nonzero((e[1:] > thr) & (e[:-1] <= thr))[0]
            return int(up[0]) + 1 if len(up) else None
        # Only a real onset: the reference starts below the threshold.
        if float(np.max(er_[:240])) < thr:
            o1, o2 = onset(er_), onset(eg_)
        else:
            o1 = o2 = None
        seg['onset_ref'] = o1
        seg['onset_test'] = o2
        seg['onset_ms'] = (None if o1 is None or o2 is None else (o2 - o1) * 1000.0 / SR)
        if seg['signal'] and b - a >= 1200:
            mr, er = logmel(rs)
            mg, eg = logmel(gs)
            mask = er > 1e-3
            seg['mel_db'] = float(np.mean(np.abs(mr[mask] - mg[mask]))) if mask.any() else 0.0
            seg['mel_dtw_db'] = float(dtw_dist(mr[mask], mg[mask])) if mask.any() else 0.0
            fr, fg = f0_track(rs), f0_track(gs)
            both = (fr > 0) & (fg > 0)
            seg['f0_ref'] = float(np.median(fr[both])) if both.any() else None
            both = (fr > 0) & (fg > 0)
            seg['f0_cents'] = (float(np.median(1200 * np.log2(fg[both] / fr[both])))
                               if both.sum() >= 5 else None)
        segs.append(seg)
    res['segments'] = segs
    return res


def match_events(er, eg):
    out = {}
    for sig in sorted(set(er) | set(eg)):
        a, b = er.get(sig, []), eg.get(sig, [])
        deltas = []
        for i in range(min(len(a), len(b))):
            if a[i][2] != b[i][2]:
                deltas.append(None)
            else:
                deltas.append(b[i][0] - a[i][0])
        out[sig] = {'n_ref': len(a), 'n_test': len(b), 'deltas': deltas,
                    'first_ref': a[:3], 'first_test': b[:3]}
    return out


# The warmth stage in a linear form (one step a sample, the poles' decay over
# the mean period of 2^32/1546188 fabric clocks: 1 - (1 - 2^-s)^2777.78 in
# Q1.31 for s = 16, 14, 13; no truncation, voice latency or knee): the model
# through which the socket gains into each card channel are estimated. Both
# sides are measured with the same model, so it only has to be close, not
# exact.
_WARM_K = (89120856 / 2.0 ** 31, 334906882 / 2.0 ** 31, 617599807 / 2.0 ** 31)


def _one_pole(x, k, block=64):
    """y[n] = y[n-1] + k (x[n] - y[n-1]), y[-1] = 0, in blocks of numpy ops."""
    a = 1.0 - k
    y = np.empty(len(x))
    pw = a ** np.arange(1, block + 1)            # a^1 .. a^B
    state = 0.0
    for i in range(0, len(x), block):
        xb = x[i:i + block]
        m = len(xb)
        p = pw[:m]
        # y[j] = a^(j+1) state + k sum_{t<=j} a^(j-t) x[t]
        acc = np.cumsum(xb / p) * p
        yb = p * state + k * acc
        y[i:i + m] = yb
        state = yb[-1]
    return y


def warm_model(x):
    x = x.astype(np.float64)
    low = _one_pole(x, _WARM_K[0])
    warm = _one_pole(x, _WARM_K[1])
    mid = _one_pole(x, _WARM_K[2])
    return x + (warm - low) - 0.25 * (x - mid)


def socket_gains(card_ch, sec, pri):
    """Least-squares gains of the two sockets (through warm_model) into one
    card channel. Returns {'sec': g|None, 'pri': g|None, 'sum': g|None}: a
    socket with no signal has no evidence (None); when the two sockets carry
    the same signal (broadcast) only their summed gain is determinable."""
    n = min(len(card_ch), len(sec), len(pri))
    y = card_ch[:n].astype(np.float64)
    cols = {}
    for k, x in (('sec', sec[:n]), ('pri', pri[:n])):
        if n and float(np.sqrt(np.mean(x.astype(np.float64) ** 2))) > CARD_SILENT_RMS:
            cols[k] = warm_model(x)
    out = {'sec': None, 'pri': None, 'sum': None}
    if not cols:
        return out
    if len(cols) == 2:
        a, b = cols['sec'], cols['pri']
        rho = float(np.dot(a, b) / math.sqrt(np.dot(a, a) * np.dot(b, b)))
        if abs(rho) > 0.99:
            # gain of the one signal both carry: y = g * (a + b) / 2
            s = (a + b) * 0.5
            out['sum'] = float(np.dot(s, y) / np.dot(s, s))
            return out
    X = np.stack([cols[k] for k in cols], axis=1)
    g, *_ = np.linalg.lstsq(X, y, rcond=None)
    for k, v in zip(cols, g):
        out[k] = float(v)
    return out


def card_metrics(ref, test):
    cr = load_pcm(os.path.join(ref, 'card.pcm'), 2)
    cg = load_pcm(os.path.join(test, 'card.pcm'), 2)
    n = min(len(cr), len(cg))
    card = {'len_ref': int(len(cr)), 'len_test': int(len(cg)), 'reasons': []}
    if not n:
        card['reasons'].append('card: no card output (ref %d, test %d samples)' % (len(cr), len(cg)))
        card['ok'] = False
        card['exact'] = False
        return card
    card['exact'] = bool(len(cr) == len(cg) and np.array_equal(cr, cg))
    socks = {}
    for side, d in (('ref', ref), ('test', test)):
        socks[side] = (load_pcm(os.path.join(d, 'sec.pcm')), load_pcm(os.path.join(d, 'pri.pcm')))
    # The lag check is about the card's own latency: it is only meaningful
    # when both sides feed the mixer the same socket samples.
    card['sockets_exact'] = all(len(r) == len(t) and np.array_equal(r, t)
                                for r, t in zip(socks['ref'], socks['test']))
    nw = n // CARD_WIN
    for i, ch in ((0, 'l'), (1, 'r')):
        a, b = cr[:n, i], cg[:n, i]
        ra = math.sqrt(float(np.mean(a.astype(np.float64) ** 2)))
        rb = math.sqrt(float(np.mean(b.astype(np.float64) ** 2)))
        card['rms_ref_' + ch] = ra
        card['rms_test_' + ch] = rb
        card['level_db_' + ch] = (20 * math.log10(rb / ra) if ra > CARD_SILENT_RMS and rb > CARD_SILENT_RMS
                                  else None)
        d = a - b
        card['ndiff_' + ch] = int(np.count_nonzero(d))
        card['max_err_' + ch] = int(np.max(np.abs(d)))
        card['max_err_at_' + ch] = int(np.argmax(np.abs(d)))
        over = np.nonzero(np.abs(d) > CARD_MAX_ERR)[0]
        card['over_err_' + ch] = int(len(over))
        card['first_over_err_' + ch] = int(over[0]) if len(over) else None
        # Error RMS per 10 ms window (the last, partial window included).
        nwe = -(-n // CARD_WIN)
        de = np.zeros(nwe * CARD_WIN)
        de[:n] = d
        sq = (de.reshape(nwe, CARD_WIN) ** 2).sum(axis=1)
        cnt = np.full(nwe, CARD_WIN, dtype=np.float64)
        cnt[-1] = n - (nwe - 1) * CARD_WIN
        we = np.sqrt(sq / cnt)
        card['win_err_rms_' + ch] = float(we.max())
        card['win_err_at_' + ch] = int(np.argmax(we)) * CARD_WIN
        card['over_win_' + ch] = int(np.count_nonzero(we > CARD_WIN_ERR_RMS))
        card['snr_db_' + ch] = snr_db(a, d)
        best = (card['snr_db_' + ch], 0)
        if ra > CARD_SILENT_RMS and card['ndiff_' + ch]:
            for lag in range(-CARD_MAX_LAG, CARD_MAX_LAG + 1):
                if lag > 0:
                    v = snr_db(a[lag:], a[lag:] - b[:n - lag])
                elif lag < 0:
                    v = snr_db(a[:n + lag], a[:n + lag] - b[-lag:])
                else:
                    continue
                if v > best[0]:
                    best = (v, lag)
        card['best_lag_' + ch] = best[1]
        card['best_lag_snr_db_' + ch] = best[0]
        if ra > CARD_SILENT_RMS and rb > CARD_SILENT_RMS:
            ma, ea = logmel(a)
            mb, eb = logmel(b)
            mask = (ea > 1e-3) & (eb > 1e-3)
            # spectral shape only: remove each frame's mean level
            if mask.any():
                da = ma[mask] - ma[mask].mean(axis=1, keepdims=True)
                db = mb[mask] - mb[mask].mean(axis=1, keepdims=True)
                card['shape_db_' + ch] = float(np.mean(np.abs(da - db)))
        # Silence, window by window.
        if nw:
            wa = np.sqrt(np.mean(a[:nw * CARD_WIN].astype(np.float64).reshape(nw, CARD_WIN) ** 2, axis=1))
            wb = np.sqrt(np.mean(b[:nw * CARD_WIN].astype(np.float64).reshape(nw, CARD_WIN) ** 2, axis=1))
            extra = np.nonzero((wa <= CARD_SILENT_RMS) & (wb > CARD_SIGNAL_RMS))[0]
            missing = np.nonzero((wa > CARD_SIGNAL_RMS) & (wb <= CARD_SILENT_RMS))[0]
            card['silent_windows_' + ch] = int(np.count_nonzero(wa <= CARD_SILENT_RMS))
            card['extra_windows_' + ch] = int(len(extra))
            card['missing_windows_' + ch] = int(len(missing))
            card['first_extra_' + ch] = int(extra[0]) * CARD_WIN if len(extra) else None
            card['first_missing_' + ch] = int(missing[0]) * CARD_WIN if len(missing) else None
            card['max_extra_rms_' + ch] = float(wb[extra].max()) if len(extra) else 0.0
            # Noise floor where the reference is silent (the last, partial
            # window too when it has at least 1 ms).
            bounds = [(k * CARD_WIN, (k + 1) * CARD_WIN) for k in range(nw)]
            tail_rms = None
            if n - nw * CARD_WIN >= 48:
                t0 = nw * CARD_WIN
                bounds.append((t0, n))
                tail_rms = math.sqrt(float(np.mean(a[t0:n].astype(np.float64) ** 2)))
            ref_rms = list(wa) + ([tail_rms] if tail_rms is not None else [])
            floor_max, floor_at, over = 0.0, None, []
            for (w0, w1), rr in zip(bounds, ref_rms):
                if rr > CARD_SILENT_RMS:
                    continue
                e = d[w0:w1].astype(np.float64)
                e = e - e.mean()
                v = math.sqrt(float(np.mean(e ** 2)))
                if v > floor_max:
                    floor_max, floor_at = v, w0
                if v > CARD_FLOOR_RMS:
                    over.append(w0)
            card['silent_err_rms_' + ch] = floor_max
            card['silent_err_at_' + ch] = floor_at
            card['over_floor_' + ch] = len(over)
            card['floor_windows_' + ch] = sum(1 for rr in ref_rms if rr <= CARD_SILENT_RMS)
            card['first_over_floor_' + ch] = over[0] if over else None
        # Routing: each side's own sockets into its own card channel.
        card['gains_ref_' + ch] = socket_gains(cr[:, i], *socks['ref'])
        card['gains_test_' + ch] = socket_gains(cg[:, i], *socks['test'])
    card_verdict(card)
    return card


def card_verdict(card):
    reasons = card['reasons']
    if card['len_ref'] != card['len_test']:
        reasons.append(f"card: {card['len_ref']} samples in ref, {card['len_test']} in test")
    names = {'sec': 'A5 secondary', 'pri': 'A6 primary'}
    for ch in ('l', 'r'):
        C = ch.upper()
        gr, gt = card.get('gains_ref_' + ch, {}), card.get('gains_test_' + ch, {})
        for k in ('sec', 'pri', 'sum'):
            if gr.get(k) is not None and gt.get(k) is not None and abs(gt[k] - gr[k]) > CARD_ROUTE_TOL:
                what = names.get(k, 'A5+A6 broadcast (one socket\'s signal)')
                reasons.append(f"card {C} routing: {what} -> {C} gain {gt[k]:.3f} (ref {gr[k]:.3f})")
        if card.get('extra_windows_' + ch):
            reasons.append(f"card {C}: test has signal where ref is silent in {card['extra_windows_' + ch]} "
                           f"of {card['silent_windows_' + ch]} silent 10 ms windows (first @{card['first_extra_' + ch]}, "
                           f"rms up to {card['max_extra_rms_' + ch]:.0f})")
        if card.get('over_floor_' + ch):
            reasons.append(f"card {C}: noise floor: {card['over_floor_' + ch]} of {card['floor_windows_' + ch]} "
                           f"windows where ref is silent have error RMS (mean removed) over {CARD_FLOOR_RMS:g} "
                           f"(worst {card['silent_err_rms_' + ch]:.1f} @{card['silent_err_at_' + ch]}, "
                           f"first @{card['first_over_floor_' + ch]})")
        if card.get('missing_windows_' + ch):
            reasons.append(f"card {C}: test silent where ref has signal in {card['missing_windows_' + ch]} "
                           f"10 ms windows (first @{card['first_missing_' + ch]})")
        if card.get('sockets_exact') and card.get('best_lag_' + ch):
            reasons.append(f"card {C}: test lines up with ref {card['best_lag_' + ch]:+d} samples off "
                           f"(SNR {card['best_lag_snr_db_' + ch]:.1f} dB there, {card['snr_db_' + ch]:.1f} dB at 0)")
        lv = card.get('level_db_' + ch)
        if lv is not None and abs(lv) > CARD_LEVEL_DB:
            reasons.append(f"card {C}: level {lv:+.2f} dB")
        sh = card.get('shape_db_' + ch)
        if sh is not None and sh > CARD_SHAPE_DB:
            reasons.append(f"card {C}: tone shape {sh:.2f} dB")
        if card.get('over_err_' + ch):
            reasons.append(f"card {C}: {card['over_err_' + ch]} samples off by more than {CARD_MAX_ERR} "
                           f"(first @{card['first_over_err_' + ch]}, worst {card['max_err_' + ch]} "
                           f"@{card['max_err_at_' + ch]})")
        if card.get('over_win_' + ch):
            reasons.append(f"card {C}: {card['over_win_' + ch]} 10 ms windows with error RMS over "
                           f"{CARD_WIN_ERR_RMS:g} (worst {card['win_err_rms_' + ch]:.1f} "
                           f"@{card['win_err_at_' + ch]})")
    card['ok'] = not reasons


def compare(ref, test):
    er, wr, rr, mr = load_events(os.path.join(ref, 'events.txt'))
    eg, wg, rg, mg = load_events(os.path.join(test, 'events.txt'))
    res = {'ref': ref, 'test': test}
    res['events'] = match_events(er, eg)
    # Every write and every read, in order (closed loop: their cycles follow
    # each side's own A/R timing, so a timing difference shows here too).
    res['writes'] = access_diff(wr, wg)
    res['reads'] = access_diff(rr, rg)
    # NOTE (e.g. a poll's TIMEOUT), TRACEREAD (a read that differs from the
    # captured trace), END (cycles, samples, accesses) and ABORT lines.
    res['notes'] = notes_diff(mr, mg)
    res['audio'] = {}
    for ch in ('sec', 'pri'):
        r = load_pcm(os.path.join(ref, ch + '.pcm'))
        g = load_pcm(os.path.join(test, ch + '.pcm'))
        res['audio'][ch] = audio_metrics(r, g, phone_starts(wr, ch))
    res['card'] = card_metrics(ref, test)
    verdict(res)
    return res


def access_diff(ref, test):
    """Accesses (cycle, sample, address, value) of the two logs position by
    position: 'moved' (same address and value, other cycle), 'mismatch'
    (other address or value), and the count difference."""
    out = {'n_ref': len(ref), 'n_test': len(test), 'n_moved': 0, 'n_mismatch': 0,
           'first': None, 'diffs': []}
    for i in range(min(len(ref), len(test))):
        a, b = ref[i], test[i]
        if a == b:
            continue
        kind = 'moved' if a[2:] == b[2:] else 'mismatch'
        out['n_' + kind] += 1
        d = (kind, i, '%d %04X %02X' % (a[0], a[2], a[3]), '%d %04X %02X' % (b[0], b[2], b[3]))
        if out['first'] is None:
            out['first'] = d
        if len(out['diffs']) < 20:
            out['diffs'].append(d)
    if len(ref) != len(test):
        extra = ref[len(test):] or test[len(ref):]
        if out['first'] is None:
            e = extra[0]
            out['first'] = ('extra in ' + ('ref' if len(ref) > len(test) else 'test'),
                            min(len(ref), len(test)), '%d %04X %02X' % (e[0], e[2], e[3]), '')
    out['identical'] = (out['n_moved'] == 0 and out['n_mismatch'] == 0 and len(ref) == len(test))
    return out


def notes_diff(ref, test):
    """The non-access, non-edge lines (NOTE, TRACEREAD, END, ABORT) must be
    the same lines in the same order."""
    out = {'n_ref': len(ref), 'n_test': len(test), 'identical': ref == test,
           'traceread_ref': sum('TRACEREAD' in l for l in ref),
           'traceread_test': sum('TRACEREAD' in l for l in test), 'first': None}
    if ref != test:
        for i in range(max(len(ref), len(test))):
            a = ref[i] if i < len(ref) else '(none)'
            b = test[i] if i < len(test) else '(none)'
            if a != b:
                out['first'] = (i, a, b)
                break
    out['ref'] = ref[:20]
    out['test'] = test[:20]
    return out


def verdict(res):
    reasons = []
    exact = True
    for sig, e in res['events'].items():
        if e['n_ref'] != e['n_test']:
            reasons.append(f"{sig}: {e['n_ref']} edges in ref, {e['n_test']} in test")
            exact = False
        bad = [d for d in e['deltas'] if d is None or abs(d) > EVENT_TOL]
        if any(d != 0 for d in e['deltas']):
            exact = False
        if bad:
            worst = max((abs(d) for d in e['deltas'] if d is not None), default=0)
            reasons.append(f"{sig}: {len(bad)} edges off by >{EVENT_TOL} cycles (worst {worst})")
    # Accesses and notes: any difference fails (no tolerance).
    for what in ('writes', 'reads'):
        x = res[what]
        if not x['identical']:
            exact = False
            k, i, a, b = x['first']
            reasons.append(f"{what}: {x['n_ref']} in ref, {x['n_test']} in test, {x['n_moved']} moved, "
                           f"{x['n_mismatch']} mismatched; first at #{i} ({k}): ref [{a}] test [{b}]")
    nt = res['notes']
    if not nt['identical']:
        exact = False
        i, a, b = nt['first']
        reasons.append(f"notes (NOTE/TRACEREAD/END): {nt['n_ref']} in ref, {nt['n_test']} in test "
                       f"(TRACEREAD {nt['traceread_ref']} vs {nt['traceread_test']}); first at #{i}: "
                       f"ref [{a}] test [{b}]")
    for ch, a in res['audio'].items():
        if not a['exact']:
            exact = False
        for s in a['segments']:
            if not s['signal']:
                if s['rms_ref'] <= 100.0 and s['rms_test'] > 100.0 and s['end'] - s['start'] >= 960:
                    reasons.append(f"{ch} {s['phone']}@{s['start']}: test has signal (rms {s['rms_test']:.0f}) where ref is silent")
                continue
            if abs(s['level_db']) > LEVEL_DB:
                reasons.append(f"{ch} {s['phone']}@{s['start']}: level {s['level_db']:+.2f} dB")
            if s['onset_ms'] is not None and abs(s['onset_ms']) > ONSET_MS:
                reasons.append(f"{ch} {s['phone']}@{s['start']}: onset {s['onset_ms']:+.2f} ms")
            if s.get('mel_db') is not None and s['mel_db'] > MEL_DB:
                reasons.append(f"{ch} {s['phone']}@{s['start']}: log-mel {s['mel_db']:.2f} dB")
            if s.get('f0_cents') is not None and abs(s['f0_cents']) > F0_CENTS:
                reasons.append(f"{ch} {s['phone']}@{s['start']}: F0 {s['f0_cents']:+.1f} cents")
    # The SSI-263 verdict proper (sockets and events), then the card check.
    res['ssi_verdict'] = 'EXACT' if exact else ('PASS' if not reasons else 'FAIL')
    card = res.get('card', {})
    reasons += card.get('reasons', [])
    res['card_ok'] = bool(card.get('ok', False))
    res['verdict'] = 'FAIL' if reasons else ('EXACT' if exact else 'PASS')
    res['reasons'] = reasons


def summary_line(name, res):
    a = res['audio']
    parts = [f"{name:28s} {res['verdict']:5s}"]
    for ch in ('pri', 'sec'):
        x = a[ch]
        if x['peak_ref'] == 0 and x['peak_test'] == 0:
            continue
        mel = [s['mel_db'] for s in x['segments'] if s.get('mel_db') is not None]
        parts.append(f"{ch}: diff {x['ndiff']}/{x['len_ref']} max {x['max_err']} "
                     f"snr {x['snr_db']:.1f}dB lag{x['best_lag']:+d}:{x['best_lag_snr_db']:.1f}dB "
                     f"mel<= {max(mel) if mel else 0:.2f}dB")
    c = res.get('card', {})
    if c:
        def lv(ch):
            if c.get('rms_ref_' + ch, 0) <= CARD_SILENT_RMS and c.get('rms_test_' + ch, 0) <= CARD_SILENT_RMS:
                return 'silent'
            d = c.get('level_db_' + ch)
            if d is not None:
                snr = c.get('snr_db_' + ch)
                return '%+.2fdB/%sdB' % (d, 'inf' if snr == float('inf') else '%.0f' % snr)
            return 'ref-silent' if c.get('rms_ref_' + ch, 0) <= CARD_SILENT_RMS else 'test-silent'
        parts.append('card %s L %s R %s' % ('EXACT' if c.get('exact') else ('ok' if c.get('ok') else 'FAIL'),
                                            lv('l'), lv('r')))
    ev = res['events']
    dd = [d for e in ev.values() for d in e['deltas'] if d is not None]
    parts.append(f"events max|d| {max((abs(d) for d in dd), default=0)}")
    return '  '.join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref'); ap.add_argument('test')
    ap.add_argument('--json'); ap.add_argument('--name', default='')
    ap.add_argument('--reasons', type=int, default=8)
    a = ap.parse_args()
    res = compare(a.ref, a.test)
    print(summary_line(a.name or os.path.basename(os.path.dirname(a.ref.rstrip('/'))), res))
    for r in res['reasons'][:a.reasons]:
        print('    - ' + r)
    if len(res['reasons']) > a.reasons:
        print(f"    - ... {len(res['reasons']) - a.reasons} more")
    if a.json:
        with open(a.json, 'w') as f:
            json.dump(res, f, indent=1, default=str)
    return 0 if res['verdict'] in ('EXACT', 'PASS') else 1


if __name__ == '__main__':
    sys.exit(main())
