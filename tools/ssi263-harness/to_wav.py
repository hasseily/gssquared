#!/usr/bin/env python3
"""Write 16-bit mono 48 kHz WAVs of a case's socket for listening:
WORK/out/listen/CASE_SOCKET_{rtl,TAG}.wav.  usage: to_wav.py CASE [pri|sec] [TAG]
(`card` as the socket writes the stereo card output instead.)"""
import sys, wave
from paths import OUT
case = sys.argv[1]; ch = sys.argv[2] if len(sys.argv) > 2 else 'pri'
tags = ['rtl', sys.argv[3] if len(sys.argv) > 3 else 'gss']
(OUT / 'listen').mkdir(parents=True, exist_ok=True)
nch = 2 if ch == 'card' else 1
for t in tags:
    data = (OUT / case / t / f'{ch}.pcm').read_bytes()
    dst = OUT / 'listen' / f'{case}_{ch}_{t}.wav'
    with wave.open(str(dst), 'wb') as w:
        w.setnchannels(nch); w.setsampwidth(2); w.setframerate(48000); w.writeframes(data)
    print(dst, len(data) // (2 * nch) / 48000, 's')
