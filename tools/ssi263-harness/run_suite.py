#!/usr/bin/env python3
"""Run the SSI-263 comparison suite: every case through the RTL reference
(bin/card_rtl) and GSSquared (bin/card_gss), then compare.py on each.

usage: run_suite.py [-k PATTERN ...] [--jobs N] [--fast] [--gss BIN]
                    [--out DIR] [--rerun-ref] [--strict]

Paths (paths.py): binaries in WORK/bin, renders in WORK/out, WORK is
$SSI_WORK or work/ here; the reference's appletini-one is $APPLETINI_ONE.

Cases: suite/*.txt (closed-loop scripts) and WORK/traces/*.trace (captured
register traffic from a2vm, replayed open loop; capture.py makes them). The
reference render of a case is cached in OUT/<case>/rtl and reused only while it is complete,
newer than the case file, and its key (OUT/<case>/rtl/renderer.key) matches
this run: the SHA-256 of bin/card_rtl and the value (or absence) of every
environment variable the renderer reads (RENDER_VARS: SSI_FABRIC,
SSI_CLOCK, SSI_AUDIO_CONTROL, SSI_PSG, SSI_DBG, SSI_DBG_SEL), and the
settings line the render itself reported (events.txt "# settings": fabric
clocks, card or fixed clocking, tone controls, PSG mute, debug) matches the
key. Anything else re-renders it; the run lists which references it
rendered and why. --rerun-ref forces a new render. Prints one line per
case and the totals; exit status 1 if any case FAILs (the SSI-263 checks,
the access and note streams, or the card check, see compare.py).

--strict is the acceptance test: exit status 0 only if every case is EXACT
(both sockets sample for sample; every D7/IRQ/direct-IRQ edge; every write
and every read, count, order, cycle, address and value; every NOTE,
TRACEREAD and END line), every case's card check passes, no case is an
ERROR, the whole suite ran (no -k, and every trace listed in
capture/traces.sha256 present with that content), and gss/check_mirror.py
finds gss/card_gss.cpp's mirror of mb2.cpp current. PASS does not count.

--strict renders with a fixed environment (STRICT_ENV: PATH and LC_ALL
only), so every renderer setting is its default, and requires every
reference to report STRICT_SETTINGS; it refuses to start (exit 2) with
--fast or with any RENDER_VARS variable set.

--strict never judges a stale binary. Before anything runs, the RTL
renderer (bin/card_rtl) and the binary under test (--gss) are each checked
against their build records (build.sh writes bin/NAME.deps, every file the
binary is built from: the GSSquared worktree sources in its include
closure, the harness C++, the Verilog, build.sh itself; and
bin/NAME.build, the build.sh arguments): a binary older than any of its
sources, with a source missing, or whose record does not list build.sh, is
rebuilt with its recorded arguments and checked again; one without a
record, or still stale, is refused (exit 2). It also refuses a binary under
test whose recorded sources lie in another GSSquared tree than the one
gss/check_mirror.py checks (paths.DEFAULT_GSQ: the repository this harness
is in), and a reference built from another appletini-one than
$APPLETINI_ONE. The binaries used are printed
(path, SHA-256, build record) at the start and in the verdict.
"""
import argparse, concurrent.futures as cf, fnmatch, hashlib, json, os, shutil, subprocess, sys, time
from pathlib import Path

H = Path(__file__).resolve().parent
sys.path.insert(0, str(H))
sys.path.insert(0, str(H / 'gss'))
import compare  # noqa: E402
import check_mirror  # noqa: E402
import paths  # noqa: E402

BIN = paths.BIN
# The trace manifest: every trace capture.py makes, with its SHA-256.
TRACE_MANIFEST = H / 'capture' / 'traces.sha256'
# Every environment variable a renderer reads (rtl/rtl_card.hpp; card_gss
# reads none, the dbg builds SSI_DBG/SSI_DBG_SEL). Part of the reference
# cache key; --strict refuses to start when any is set.
RENDER_VARS = ('SSI_FABRIC', 'SSI_CLOCK', 'SSI_AUDIO_CONTROL', 'SSI_PSG', 'SSI_DBG', 'SSI_DBG_SEL')
# --strict runs both renderers with exactly this environment.
STRICT_ENV = {'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'}
# ... and requires every reference to report exactly these settings (the
# card's clocking, warmth +8 with the AY-3-8913 table, PSGs muted, no
# debug output), events.txt's "# settings" line (RtlCard::settings).
STRICT_SETTINGS = 'fabric=130 clock=card audio_control=0x02040000 psg=muted dbg=off dbg_sel=0'
# Build-record paths that belong to neither the harness nor a worktree.
SYSTEM_PREFIXES = ('/Applications/', '/Library/', '/opt/homebrew/', '/usr/', '/System/')


def cases(patterns):
    out = []
    for p in sorted(paths.SUITE.glob('*.txt')):
        out.append(('script', p))
    for p in sorted(paths.TRACES.glob('*.trace')):
        out.append(('trace', p))
    if patterns:
        out = [c for c in out if any(fnmatch.fnmatch(c[1].stem, pat) for pat in patterns)]
    return out


def trace_problems():
    """--strict: the traces must be exactly the ones capture.py makes (the
    manifest's names and SHA-256s); returns the problems (empty: fine)."""
    want = {}
    for line in TRACE_MANIFEST.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            h, name = line.split()
            want[name] = h
    have = {p.name for p in paths.TRACES.glob('*.trace')}
    out = []
    missing = sorted(set(want) - have)
    if missing:
        out.append(f'{len(missing)} of {len(want)} traces missing from {paths.TRACES} ({", ".join(missing[:3])}'
                   f'{", ..." if len(missing) > 3 else ""}): run capture.py')
    extra = sorted(have - set(want))
    if extra:
        out.append(f'traces not in {TRACE_MANIFEST.name}: {", ".join(extra[:3])}')
    for name in sorted(set(want) & have):
        if sha256(paths.TRACES / name) != want[name]:
            out.append(f'{name} differs from the capture recorded in {TRACE_MANIFEST.name}')
    return out


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def build_record(binary):
    """(args, comment lines, deps) from build.sh's records, or None."""
    b = Path(binary)
    rec, deps = b.with_name(b.name + '.build'), b.with_name(b.name + '.deps')
    if not rec.exists() or not deps.exists():
        return None
    lines = rec.read_text().splitlines()
    return lines[0].split(), [l for l in lines[1:] if l.startswith('#')], \
        [Path(l) for l in deps.read_text().splitlines() if l.strip()]


def staleness(binary):
    """Why BINARY is not current with its sources (empty list: it is)."""
    b = Path(binary)
    if not b.exists():
        return ['the binary does not exist']
    rec = build_record(b)
    if rec is None:
        return [f'no build record ({b.name}.build / {b.name}.deps): build it with build.sh']
    _, _, deps = rec
    if not deps:
        return ['empty dependency record']
    t = b.stat().st_mtime
    out = []
    if (H / 'build.sh') not in deps:
        out.append('build.sh is not in the dependency record (a record from before it was)')
    for d in deps:
        if not d.exists():
            out.append(f'source missing: {d}')
        elif d.stat().st_mtime > t:
            out.append(f'source newer than the binary: {d}')
    return out


def foreign_sources(binary, root, must):
    """--strict: the build record's sources outside the harness and the
    system must all lie in ROOT (the worktree the mirror check reads, or the
    appletini-one checkout), and MUST (a file under ROOT) must be one of
    them. Returns the problems (empty: fine)."""
    rec = build_record(binary)
    if rec is None:
        return ['no build record']
    deps = rec[2]
    root = Path(root).resolve()
    out = []
    for d in deps:
        if d == H or H in d.parents or str(d).startswith(SYSTEM_PREFIXES):
            continue
        if root not in d.parents:
            out.append(f'{d} is not under {root}')
    if (root / must) not in deps:
        out.append(f'{root / must} is not among its sources')
    return out


def build_env():
    """build.sh's environment for a --strict rebuild: no GSQ override, so it
    builds from the tree the checks use (APPLETINI_ONE is kept: it is the
    checkout the reference check compares with)."""
    return {k: v for k, v in os.environ.items() if k != 'GSQ' and k not in RENDER_VARS}


def ensure_current(binary, label):
    """--strict: rebuild a stale binary from its record; refuse if that is
    impossible or does not help. Returns (ok, description)."""
    b = Path(binary).resolve()
    why = staleness(b)
    if why:
        rec = build_record(b)
        args = rec[0] if rec else ({'card_gss': ['gss'], 'card_rtl': ['rtl']}.get(b.name)
                                   if b.parent == BIN.resolve() else None)
        if not args:
            return False, f'{label} {b} is stale and has no build record to rebuild it: ' + '; '.join(why[:3])
        print(f'{label} {b.name} is stale ({why[0]}{"; ..." if len(why) > 1 else ""}): ./build.sh {" ".join(args)}',
              flush=True)
        r = subprocess.run([str(H / 'build.sh')] + args, capture_output=True, text=True, env=build_env())
        if r.returncode:
            return False, f'rebuilding {b.name} failed:\n{r.stdout}{r.stderr}'
        why = staleness(b)
        if why:
            return False, f'{label} {b} is still stale after the rebuild: ' + '; '.join(why[:3])
    rec = build_record(b)
    st = b.stat()
    desc = (f'{label}: {b} sha256 {sha256(b)[:16]} built {time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(st.st_mtime))}'
            f' (./build.sh {" ".join(rec[0])}; {len(rec[2])} sources, all older)')
    for c in rec[1]:
        if not c.startswith('# built'):
            desc += f'\n    {c}'
    return True, desc


def render(binary, kind, src, outdir, env, max_cycles):
    if outdir.exists():
        shutil.rmtree(outdir)
    outdir.mkdir(parents=True)
    t0 = time.monotonic()
    r = subprocess.run([str(binary), kind, str(src), str(outdir), str(max_cycles)],
                       env=env, capture_output=True, text=True, timeout=7200)
    (outdir / 'stdout.txt').write_text(r.stdout + r.stderr)
    return r.returncode, time.monotonic() - t0


def render_env(args):
    """The renderers' environment: STRICT_ENV under --strict, else the
    caller's (with SSI_FABRIC=26 for --fast)."""
    if args.strict:
        return dict(STRICT_ENV)
    env = dict(os.environ)
    if args.fast:
        env['SSI_FABRIC'] = '26'
    return env


def ref_key(args, env):
    """What a cached reference must have been made with: the renderer binary
    and every setting it reads from the environment."""
    return {'renderer_sha256': args.rtl_sha, 'env': {k: env.get(k) for k in RENDER_VARS}}


def settings_line(events):
    try:
        with open(events) as f:
            for line in f:
                if not line.startswith('#'):
                    break
                if line.startswith('# settings '):
                    return line[len('# settings '):].strip()
    except OSError:
        pass
    return None


def run_case(kind, src, args):
    name = src.stem
    base = Path(args.out) / name
    env = render_env(args)
    rtl_dir = base / ('rtl_fast' if args.fast else 'rtl')
    gss_dir = base / args.tag
    rtl_bin = BIN / 'card_rtl'
    stamp = rtl_dir / 'events.txt'
    keyfile = rtl_dir / 'renderer.key'
    want = ref_key(args, env)
    t_ref = 0.0
    why = None
    def complete(p):
        try:
            with open(p, 'rb') as f:
                f.seek(max(0, p.stat().st_size - 200))
                return b' END' in f.read()
        except OSError:
            return False
    def key_mismatch():
        """Why the cached reference cannot be reused (None: it can)."""
        try:
            have = json.loads(keyfile.read_text())
        except (OSError, ValueError):
            return 'no cache key (renderer.key)'
        if have.get('renderer_sha256') != want['renderer_sha256']:
            return 'another bin/card_rtl'
        diff = [f"{k} {have.get('env', {}).get(k) or 'unset'} -> {want['env'][k] or 'unset'}"
                for k in RENDER_VARS if have.get('env', {}).get(k) != want['env'][k]]
        if diff:
            return 'renderer settings differ: ' + ', '.join(diff)
        if have.get('settings') != settings_line(stamp):
            return 'the settings it reports differ from its key'
        return None
    if args.rerun_ref:
        why = '--rerun-ref'
    elif not stamp.exists() or not complete(stamp):
        why = 'missing or incomplete'
    elif stamp.stat().st_mtime < src.stat().st_mtime:
        why = 'older than the case file'
    else:
        why = key_mismatch()
    if why:
        rc, t_ref = render(rtl_bin, kind, src, rtl_dir, env, args.max_cycles)
        if rc:
            return name, None, f'rtl rc={rc}', t_ref, 0, why
        keyfile.write_text(json.dumps(dict(want, settings=settings_line(stamp)), indent=1) + '\n')
    if args.strict and settings_line(stamp) != STRICT_SETTINGS:
        return name, None, f'reference settings [{settings_line(stamp)}], strict needs [{STRICT_SETTINGS}]', t_ref, 0, why
    rc, t_gss = render(Path(args.gss), kind, src, gss_dir, env, args.max_cycles)
    if rc:
        return name, None, f'gss rc={rc}', t_ref, t_gss, why
    res = compare.compare(str(rtl_dir), str(gss_dir))
    res['reference_rendered'] = why
    with open(base / f'compare_{args.tag}.json', 'w') as f:
        json.dump(res, f, indent=1, default=str)
    return name, res, None, t_ref, t_gss, why


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-k', action='append', default=[])
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--fast', action='store_true')
    ap.add_argument('--gss', default=str(BIN / 'card_gss'))
    ap.add_argument('--tag', default='gss')
    ap.add_argument('--out', default=str(paths.OUT))
    ap.add_argument('--rerun-ref', action='store_true')
    ap.add_argument('--max-cycles', type=int, default=400_000_000)
    ap.add_argument('--reasons', type=int, default=3)
    ap.add_argument('--strict', action='store_true',
                    help='exit 1 unless every case is EXACT, every card check passes and the mb2.cpp mirror is current')
    args = ap.parse_args()
    todo = cases(args.k)
    if not todo:
        print('no cases'); return 2
    used = []
    if args.strict:
        refuse = []
        if args.fast:
            refuse.append('--fast renders a different reference (SSI_FABRIC=26)')
        setv = [k for k in RENDER_VARS if k in os.environ]
        if setv:
            refuse.append('renderer settings in the environment (' + ', '.join(setv) +
                          '): --strict renders with a fixed environment; unset them')
        if os.environ.get('GSQ') and Path(os.environ['GSQ']).resolve() != paths.DEFAULT_GSQ:
            refuse.append(f'GSQ={os.environ["GSQ"]}: --strict tests the tree it is in ({paths.DEFAULT_GSQ}); unset GSQ')
        refuse += trace_problems()
        if refuse:
            print('STRICT: REFUSED (' + '; '.join(refuse) + ')')
            return 2
        fw = paths.required('APPLETINI_ONE', ['hdl/apple/mockingboard.sv'])
        for b, label in ((BIN / 'card_rtl', 'reference'), (Path(args.gss), 'under test')):
            ok, desc = ensure_current(b, label)
            if not ok:
                print('STRICT: REFUSED (' + desc + ')')
                return 2
            print(desc, flush=True)
            used.append(desc)
        # The binary under test must be built from the worktree the mirror
        # check reads, the reference from the appletini-one checkout.
        for b, root, must, label in (
                (Path(args.gss), paths.DEFAULT_GSQ, 'src/devices/mockingboard/SSI263.cpp', 'under test'),
                (BIN / 'card_rtl', fw, 'hdl/apple/mockingboard.sv', 'reference')):
            bad = foreign_sources(b, root, must)
            if bad:
                print(f'STRICT: REFUSED ({label} {b} is not built from {root}: ' + '; '.join(bad[:3])
                      + (f'; ... {len(bad) - 3} more' if len(bad) > 3 else '') + ')')
                return 2
        print(f'renderer environment: {STRICT_ENV}; reference settings required: {STRICT_SETTINGS}', flush=True)
    if not (BIN / 'card_rtl').exists():
        print(f'no {BIN / "card_rtl"}: run ./build.sh first')
        return 2
    args.rtl_sha = sha256(BIN / 'card_rtl')
    t0 = time.monotonic()
    results = {}
    rerendered = {}
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(run_case, k, s, args): s for k, s in todo}
        for fut in cf.as_completed(futs):
            name, res, err, tr, tg, why = fut.result()
            results[name] = (res, err)
            if why:
                rerendered[name] = why
    counts = {'EXACT': 0, 'PASS': 0, 'FAIL': 0, 'ERROR': 0}
    ssi = {'EXACT': 0, 'PASS': 0, 'FAIL': 0}
    card = {'ok': 0, 'exact': 0, 'FAIL': 0}
    for k, s in todo:
        res, err = results[s.stem]
        if err:
            print(f'{s.stem:28s} ERROR {err}'); counts['ERROR'] += 1; continue
        counts[res['verdict']] += 1
        ssi[res['ssi_verdict']] += 1
        if res['card_ok']:
            card['ok'] += 1
        else:
            card['FAIL'] += 1
        card['exact'] += bool(res['card'].get('exact'))
        print(compare.summary_line(s.stem, res))
        for r in res['reasons'][:args.reasons]:
            print('    - ' + r)
        if len(res['reasons']) > args.reasons:
            print(f"    - ... {len(res['reasons']) - args.reasons} more")
    if rerendered:
        whys = {}
        for n, w in sorted(rerendered.items()):
            whys.setdefault(w, []).append(n)
        print(f'\nreferences rendered this run: {len(rerendered)} of {len(todo)}')
        for w, ns in whys.items():
            print(f'  {len(ns):3d} {w}: ' + ' '.join(ns[:6]) + (' ...' if len(ns) > 6 else ''))
    else:
        print(f'\nreferences: all {len(todo)} reused from the cache (same renderer, same settings)')
    print(f"\n{len(todo)} cases: " + ', '.join(f'{v} {k}' for k, v in counts.items())
          + f"  ({time.monotonic() - t0:.0f} s)")
    print(f"  SSI-263 (sockets and events): {ssi['EXACT']} EXACT, {ssi['PASS']} PASS, {ssi['FAIL']} FAIL;"
          f" card check: {card['ok']} pass ({card['exact']} sample-exact), {card['FAIL']} FAIL")
    if args.strict:
        mirror_rc = check_mirror.main(['--quiet', '--gsq', str(paths.DEFAULT_GSQ)])
        problems = []
        n_not_exact = len(todo) - counts['EXACT']
        if n_not_exact:
            problems.append(f'{n_not_exact} cases not EXACT')
        if card['FAIL']:
            problems.append(f"{card['FAIL']} card check failures")
        if counts['ERROR']:
            problems.append(f"{counts['ERROR']} errors")
        if args.k:
            problems.append('a subset (-k) was run')
        if mirror_rc:
            problems.append('the mb2.cpp mirror is not current')
        print('binaries used:')
        for u in used:
            print('  ' + u.replace('\n', '\n  '))
        if problems:
            print('STRICT: FAIL (' + '; '.join(problems) + ')')
            return 1
        print(f'STRICT: PASS ({len(todo)} cases EXACT, card checks pass, mirror current)')
        return 0
    return 0 if counts['FAIL'] == 0 and counts['ERROR'] == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
