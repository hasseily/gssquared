#!/usr/bin/env python3
"""Guard for the hand-made mirror of mb2.cpp in gss/card_gss.cpp.

mb2.cpp's Mockingboard class needs SDL, NClock, the MMU and the AudioSystem,
so the harness cannot compile it; gss/card_gss.cpp mirrors, call for call,
every part of it the speech path touches (decode, clockDevices order, the
completion routing, IRQ, mode switch, reset, the generate_frame mix) and
compiles the real SSI263.cpp, W6522.hpp, PhasorLogic.hpp and PhasorAudio.hpp.
The driver (common/driver.hpp) mirrors PhasorLogic::advanceAudioSamplePhase
and the phase start in the constructor.

gss/mb2_mirror.ref records those regions of the GSSquared sources as they
were when the mirror was last checked against them line by line, together
with a hash of the mirror files themselves. This test fails when

  * any mirrored region of the worktree's sources differs from the recorded
    one (comments and whitespace ignored): the mirror must be re-checked
    against the change;
  * a region can no longer be found;
  * gss/card_gss.cpp or common/driver.hpp changed since the last check.

usage: check_mirror.py [--gsq DIR] [--update] [--quiet]

--update records the current sources and mirror as checked. Run it only
after reading the diff this script prints and bringing card_gss.cpp (or
driver.hpp) into line with it.
"""
import argparse, difflib, hashlib, json, re, sys
from pathlib import Path

H = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(H))
import paths  # noqa: E402
REF = H / 'gss' / 'mb2_mirror.ref'
# The GSSquared tree checked: the repository this harness is in (or $GSQ;
# run_suite.py --strict checks the repository itself and requires the binary
# under test to be built from it).
DEFAULT_GSQ = paths.GSQ
MIRROR_FILES = ['gss/card_gss.cpp', 'common/driver.hpp']

MB2 = 'src/devices/mockingboard/mb2.cpp'
PL = 'src/devices/mockingboard/PhasorLogic.hpp'
# (name, file, regex for the first line of the region; the region runs to
# the brace that closes the first '{' at or after it[, 'members'])
# A 'members' region is a class with every function body removed: its data
# members with their initialisers (phasor_mode = kModeMockingboard,
# speech_sample_phase, warmth_filter, ...), nested types and the function
# signatures; the bodies are the function regions.
REGIONS = [
    ('Mockingboard members', MB2, r'^class Mockingboard \{', 'members'),
    ('Mockingboard::Mockingboard', MB2, r'^\s*Mockingboard\(NClock \*clock'),
    ('Mockingboard::~Mockingboard', MB2, r'^\s*~Mockingboard\(\)'),
    ('mockingboardMode', MB2, r'^\s*bool mockingboardMode\(\) const'),
    ('phasorNative', MB2, r'^\s*bool phasorNative\(\) const'),
    ('echoPlus', MB2, r'^\s*bool echoPlus\(\) const'),
    ('phasorExtended', MB2, r'^\s*bool phasorExtended\(\) const'),
    ('directSpeechIrq', MB2, r'^\s*bool directSpeechIrq\(\) const'),
    ('updateCardIrq', MB2, r'^\s*void updateCardIrq\(\)'),
    ('routeSpeechCompletion', MB2, r'^\s*void routeSpeechCompletion\('),
    ('clockDevices', MB2, r'^\s*void clockDevices\(\)'),
    ('setAyClockRate', MB2, r'^\s*void setAyClockRate\(\)'),
    ('clearAySelections', MB2, r'^\s*void clearAySelections\(\)'),
    ('modeSwitch', MB2, r'^\s*void modeSwitch\(uint32_t addr\)'),
    ('ayBusCycle', MB2, r'^\s*void ayBusCycle\(uint8_t via\)'),
    ('accessModeSwitch', MB2, r'^\s*void accessModeSwitch\('),
    ('write', MB2, r'^\s*void write\(uint32_t addr, uint8_t data\)'),
    ('read', MB2, r'^\s*uint8_t read\(uint32_t addr, uint8_t floating_bus\)'),
    ('generate_frame', MB2, r'^\s*void generate_frame\(\)'),
    ('reset', MB2, r'^\s*void reset\(bool cold_start\)'),
    ('mb_write_Cx00', MB2, r'^void mb_write_Cx00\('),
    ('mb_read_Cx00', MB2, r'^uint8_t mb_read_Cx00\('),
    ('mb_write_C0nx', MB2, r'^void mb_write_C0nx\('),
    ('mb_read_C0nx', MB2, r'^uint8_t mb_read_C0nx\('),
    ('init_slot_mockingboard', MB2, r'^void init_slot_mockingboard\('),
    ('PhasorLogic::advanceAudioSamplePhase', PL, r'^\s*constexpr bool advanceAudioSamplePhase\('),
]


def strip_comments(text):
    # Good enough for this code: no comment markers inside string literals
    # of the mirrored regions.
    # (a block comment keeps its newlines, so line counts stay right)
    text = re.sub(r'/\*.*?\*/', lambda m: ' ' + '\n' * m.group(0).count('\n'), text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    return text


def extract(path, pattern):
    lines = path.read_text().splitlines(keepends=True)
    rx = re.compile(pattern)
    for i, line in enumerate(lines):
        if not rx.search(line):
            continue
        body = strip_comments(''.join(lines[i:]))
        depth = 0
        started = False
        for j, ch in enumerate(body):
            if ch == '{':
                depth += 1; started = True
            elif ch == '}':
                depth -= 1
                if started and depth == 0:
                    code = body[:j + 1]
                    nl = code.count('\n') + 1
                    raw = ''.join(lines[i:i + nl])
                    return i + 1, raw, ' '.join(code.split())
        return i + 1, None, None
    return None, None, None


FUNC_END = re.compile(r'(\)|\bconst|\boverride|\bnoexcept|\bfinal)\s*$')


def members_only(code):
    """CODE (a class, comments stripped) with each function body replaced by
    '{}': a '{' at class depth whose preceding text ends like a function
    declarator (')', const, override, noexcept, final; a constructor's
    initialiser list ends with ')') opens a body. Braces of initialisers
    ('= {}') and nested types are kept."""
    out = []
    depth = 0          # brace depth within the class text (1 = class body)
    skip = 0           # >0: inside a function body, its depth
    for ch in code:
        if skip:
            if ch == '{':
                skip += 1
            elif ch == '}':
                skip -= 1
                if skip == 0:
                    out.append('}')
            continue
        if ch == '{':
            if depth >= 1 and FUNC_END.search(''.join(out[-200:])):
                out.append('{')
                skip = 1
                continue
            depth += 1
        elif ch == '}':
            depth -= 1
        out.append(ch)
    return ''.join(out)


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()[:16]


def current(gsq):
    out = {}
    for name, rel, pat, *kind in REGIONS:
        line, raw, norm = extract(gsq / rel, pat)
        if kind == ['members'] and raw is not None:
            # the diff shows the reduced class, one statement a line
            red = members_only(strip_comments(raw))
            raw = '\n'.join(l for l in red.splitlines() if l.strip()) + '\n'
            norm = ' '.join(red.split())
        out[name] = {'file': rel, 'line': line, 'raw': raw, 'sha': sha(norm) if norm else None}
    mirror = {f: sha((H / f).read_text()) for f in MIRROR_FILES}
    return out, mirror


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--gsq', default=str(DEFAULT_GSQ))
    ap.add_argument('--update', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--show', help='print the recorded text of one region and exit')
    a = ap.parse_args(argv)
    gsq = Path(a.gsq)
    regions, mirror = current(gsq)
    if a.show:
        print(regions[a.show]['raw'] or '(not found)')
        return 0
    missing = [n for n, r in regions.items() if r['sha'] is None]
    if a.update:
        if missing:
            print('cannot record: regions not found: ' + ', '.join(missing)); return 1
        import subprocess
        rev = subprocess.run(['git', '-C', str(gsq), 'rev-parse', '--short', 'HEAD'],
                             capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(['git', '-C', str(gsq), 'status', '--porcelain', 'src'],
                               capture_output=True, text=True).stdout.strip()
        REF.write_text(json.dumps({'gsq_rev': rev + (' (modified)' if dirty else ''),
                                   'regions': regions, 'mirror': mirror}, indent=1))
        print(f'recorded {len(regions)} regions of {rev} and the mirror files as checked')
        return 0
    if not REF.exists():
        print('mirror check: no gss/mb2_mirror.ref (run with --update after checking the mirror)')
        return 1
    ref = json.loads(REF.read_text())
    bad = 0
    for name, r in regions.items():
        old = ref['regions'].get(name)
        if r['sha'] is None:
            print(f'MISSING  {name}: not found in {r["file"]}'); bad += 1; continue
        if old is None:
            print(f'NEW      {name}: not in the record'); bad += 1; continue
        if old['sha'] != r['sha']:
            bad += 1
            print(f'CHANGED  {name} ({r["file"]}:{r["line"]}, recorded at line {old["line"]} of {ref["gsq_rev"]}):')
            if not a.quiet:
                for d in difflib.unified_diff(old['raw'].splitlines(), r['raw'].splitlines(),
                                              'recorded', 'worktree', lineterm='', n=2):
                    print('    ' + d)
    for f, h in mirror.items():
        if ref['mirror'].get(f) != h:
            bad += 1
            print(f'CHANGED  {f}: the mirror itself changed since it was last checked against the record')
    if bad:
        print(f'mirror check: FAIL ({bad} item(s); see gss/check_mirror.py)')
        return 1
    print(f'mirror check: OK ({len(regions)} regions of mb2.cpp/PhasorLogic.hpp (the Mockingboard members '
          f'and initialisers among them) as recorded at '
          f'{ref["gsq_rev"]}; mirror files unchanged)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
