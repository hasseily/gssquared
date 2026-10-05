#!/usr/bin/env python3
"""Regenerate the 31 replay traces from the owner's disks (README.md,
"Captures").

usage: capture.py [--jobs N] [--only CAPTURE ...] [--cut-only] [--no-build]

1. Builds the capture machine (a2vm/build_a2vm.sh: appletini-software's a2vm
   at a2vm/BASE with a2vm.patch and ssi_ext.c, the RTL Phasor linked in;
   ./build.sh rtl must have run).
2. Runs each capture below on it, in WORK/cap/NAME: boots the disk from a
   slot-7 block device, types the keys of capture/NAME.expect when the
   screen shows what it waits for, and logs every card access the program
   makes, with the value the RTL card returned (phasor.log), and the text
   screen (screen.log). The disks are read into memory; nothing is written
   back to them.
3. Cuts the traces into WORK/traces (make_trace.py / split_capture.py, run
   from WORK/traces on ../cap/NAME/phasor.log, so each trace's first line
   is the same wherever WORK is).
4. Checks every trace against capture/traces.sha256, byte for byte. Exit 1
   if one differs or is missing.

--cut-only skips steps 1-2 (the captures in WORK/cap are reused); --only
runs and cuts just the named captures (the check then covers their traces).

Environment: APPLETINI_ONE (the //e ROM, docs/Apple2e_Enhanced.rom),
APPLETINI_SOFTWARE (a2vm), PHASOR_DISKS (phasor.hdv, mb-audit-v1.61.po),
SSI_WORK.
"""
import argparse, concurrent.futures as cf, hashlib, os, shutil, subprocess, sys, time
from pathlib import Path

import paths

H = paths.H
A2VM = paths.WORK / 'a2vm' / 'a2vm_ssi'
MANIFEST = H / 'capture' / 'traces.sha256'

# NAME: (disk, --cycles, what it is). A run ends at the expect script's
# @stop or at --cycles, whichever comes first (README.md, "Captures").
CAPTURES = {
    'mba_speech':  ('mb-audit-v1.61.po', 60_000_000, "mb-audit's speech menu (C), phrases A-G"),
    'mba_audit':   ('mb-audit-v1.61.po', 60_000_000, "mb-audit's quick audit (A) with both CTRL+RESET tests"),
    'ph_menu':     ('phasor.hdv', 25_000_000, '/SPEECH/STARTUP, the speech menu'),
    'ph_fayzor':   ('phasor.hdv', 60_000_000, 'Dr. Fay Zor, name APPLE typed'),
    'ph_talkkbd':  ('phasor.hdv', 60_000_000, 'Talking Keyboard: a phrase, the inflection arrows, ESC'),
    'ph_clock':    ('phasor.hdv', 60_000_000, 'Talking Clock'),
    'ph_adj':      ('phasor.hdv', 90_000_000, "Speech Adjust: 'Peter Piper' with the rate and inflection keys"),
    'ph_mainmenu': ('phasor.hdv', 60_000_000, 'the main menu STARTUP (T)'),
    'ph_test':     ('phasor.hdv', 60_000_000, 'TEST: AY effects, then the speech test'),
}

# The cuts: ('trace', CAPTURE, NAME, FROM, TO, TITLE) for make_trace.py, or
# ('split', CAPTURE, PREFIX, TITLE) for split_capture.py with SPLIT_ARGS.
SPLIT_ARGS = ['--gap', '400000', '--tail', '300000']
MBA = 'mb-audit v1.61 speech menu (C), phrase {} (play-sc01-using-ssi263 v0.8)'
ADJ = "Phasor.hdv Speech Parameter Adjust ('Peter Piper', rate/inflection keys), window {}"
CUTS = [
    ('trace', 'mba_speech', 'mbaspeech_A', 1580177, 4175308, MBA.format('A')),
    ('trace', 'mba_speech', 'mbaspeech_B', 6620487, 7736320, MBA.format('B')),
    ('trace', 'mba_speech', 'mbaspeech_C', 11660739, 13565501, MBA.format('C')),
    ('trace', 'mba_speech', 'mbaspeech_D', 16701021, 18112713, MBA.format('D')),
    ('trace', 'mba_speech', 'mbaspeech_E', 21741281, 24928292, MBA.format('E')),
    ('trace', 'mba_speech', 'mbaspeech_F', 26781620, 29376702, MBA.format('F')),
    ('trace', 'mba_speech', 'mbaspeech_G', 31821930, 35304773, MBA.format('G')),
    ('trace', 'mba_audit', 'mbaudit_ssiA', 3666700, 7323300,
     'mb-audit v1.61 quick audit: TestSSI263, socket A ($C440, tests 00-0B)'),
    ('trace', 'mba_audit', 'mbaudit_ssiB', 7323300, 11968450,
     'mb-audit v1.61 quick audit: TestSSI263, socket B ($C420, tests 80-8B)'),
    ('trace', 'mba_audit', 'mbaudit_reset', 11968450, 13131000,
     'mb-audit v1.61 quick audit: CTRL+RESET tests f0-f3 (SSI263AP power-down)'),
    ('trace', 'ph_adj', 'phadj_01', 3300000, 13300000, ADJ.format(1)),
    ('trace', 'ph_adj', 'phadj_02', 13300000, 23300000, ADJ.format(2)),
    ('trace', 'ph_adj', 'phadj_03', 23300000, 33300000, ADJ.format(3)),
    ('trace', 'ph_adj', 'phadj_04', 33300000, 43300000, ADJ.format(4)),
    ('split', 'ph_menu', 'phmenu', 'Phasor.hdv /SPEECH/STARTUP menu (TTS)'),
    ('split', 'ph_fayzor', 'phfayzor', 'Phasor.hdv Dr. Fay Zor (TTS, typed name APPLE)'),
    ('split', 'ph_talkkbd', 'phtalkkbd', 'Phasor.hdv Talking Keyboard (TTS, typed phrase, inflection arrows, ESC)'),
    ('split', 'ph_clock', 'phclock', 'Phasor.hdv Talking Clock (TTS)'),
    ('split', 'ph_mainmenu', 'phmain', 'Phasor.hdv main menu STARTUP (TTS + AY)'),
    ('split', 'ph_test', 'phtest', 'Phasor.hdv TEST: AY effects, then the TTS speech test'),
]


def sha256(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def run_capture(name, fw, disks):
    disk, cycles, _ = CAPTURES[name]
    d = paths.CAP / name
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    shutil.copy(H / 'capture' / f'{name}.expect', d / 'expect.txt')
    cmd = [str(A2VM), '--rom', str(fw / 'docs' / 'Apple2e_Enhanced.rom'), '--core', 'w65c02s', '--speed', '1',
           '--no-mouse', '--blockdev', str(disks / disk), '--boot', '--phasor-rtl', '12',
           '--phasor-log', 'phasor.log', '--screen-log', 'screen.log', '--expect', 'expect.txt',
           '--cycles', str(cycles), '--state', 'state.json']
    (d / 'command.txt').write_text(' '.join(cmd) + '\n')
    t0 = time.monotonic()
    with open(d / 'run.out', 'w') as out:
        r = subprocess.run(cmd, cwd=d, stdout=out, stderr=subprocess.STDOUT)
    return name, r.returncode, time.monotonic() - t0


def cut(selected):
    paths.TRACES.mkdir(parents=True, exist_ok=True)
    py = sys.executable
    for c in CUTS:
        if c[1] not in selected:
            continue
        log = f'../cap/{c[1]}/phasor.log'
        if not (paths.TRACES / log).exists():
            sys.exit(f'no capture {paths.CAP / c[1] / "phasor.log"}: run capture.py without --cut-only')
        if c[0] == 'trace':
            _, cap, name, frm, to, title = c
            for old in paths.TRACES.glob(f'{name}.trace'):
                old.unlink()
            args = [py, str(H / 'make_trace.py'), log, name, '--from', str(frm), '--to', str(to), '--title', title]
        else:
            _, cap, prefix, title = c
            for old in paths.TRACES.glob(f'{prefix}_*.trace'):
                old.unlink()
            args = [py, str(H / 'split_capture.py'), log, prefix, '--title', title] + SPLIT_ARGS
        subprocess.run(args + ['--dest', '.'], cwd=paths.TRACES, check=True, stdout=subprocess.DEVNULL)


def check(selected):
    """Compare the traces with the manifest; returns the number of problems."""
    want = {}
    for line in MANIFEST.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            h, n = line.split()
            want[n] = h
    prefixes = []
    for c in CUTS:
        if c[1] in selected:
            prefixes.append(c[2] + ('.trace' if c[0] == 'trace' else '_'))
    bad = same = 0
    for n, h in sorted(want.items()):
        if not any(n.startswith(p) for p in prefixes):
            continue
        p = paths.TRACES / n
        if not p.exists():
            print(f'MISSING  {n}'); bad += 1
        elif sha256(p) != h:
            print(f'DIFFERS  {n} (sha256 {sha256(p)[:16]}, recorded {h[:16]})'); bad += 1
        else:
            same += 1
    for p in sorted(paths.TRACES.glob('*.trace')):
        if p.name not in want and any(p.name.startswith(x) for x in prefixes):
            print(f'EXTRA    {p.name} (not in {MANIFEST.name})'); bad += 1
    print(f'traces: {same} identical to {MANIFEST.name}, {bad} problem(s)')
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--only', nargs='+', choices=sorted(CAPTURES))
    ap.add_argument('--cut-only', action='store_true')
    ap.add_argument('--no-build', action='store_true', help='reuse WORK/a2vm/a2vm_ssi')
    a = ap.parse_args()
    selected = a.only or list(CAPTURES)
    if not a.cut_only:
        fw = paths.required('APPLETINI_ONE', ['docs/Apple2e_Enhanced.rom'])
        disks = paths.required('PHASOR_DISKS', ['phasor.hdv', 'mb-audit-v1.61.po'])
        if not (a.no_build and A2VM.exists()):
            paths.required('APPLETINI_SOFTWARE')
            r = subprocess.run([str(H / 'a2vm' / 'build_a2vm.sh')])
            if r.returncode:
                return 2
        print(f'capturing {len(selected)} runs, {a.jobs} at a time (the longest, ph_adj, about 10 minutes)', flush=True)
        failed = []
        with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
            # longest first
            order = sorted(selected, key=lambda n: -CAPTURES[n][1])
            for name, rc, t in ex.map(lambda n: run_capture(n, fw, disks), order):
                log = paths.CAP / name / 'phasor.log'
                ok = rc == 0 and log.exists() and log.read_text()[-200:].rstrip().endswith('END')
                print(f'  {name:12s} {"done" if ok else f"FAILED (exit {rc}, see run.out)"} ({t:.0f} s)', flush=True)
                if not ok:
                    failed.append(name)
        if failed:
            print('captures failed: ' + ', '.join(failed))
            return 1
    cut(selected)
    return 1 if check(selected) else 0


if __name__ == '__main__':
    sys.exit(main())
