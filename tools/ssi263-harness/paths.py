"""Where the harness finds its inputs and puts its outputs (README.md,
"Environment"). Every Python tool of the harness imports this; build.sh,
setup.sh and a2vm/build_a2vm.sh compute the same paths in zsh.

  H       this directory (the harness sources, tracked in git)
  GSQ     the GSSquared tree under test: the repository this directory is
          in (tools/ssi263-harness -> ../..), or $GSQ. run_suite.py --strict
          accepts only the default, so it tests the branch it is in.
  WORK    build outputs, renders, captures and traces: $SSI_WORK, default
          H/work (gitignored)
  APPLETINI_ONE       the appletini-one checkout (read only): the RTL and
                      the Apple //e ROM
  APPLETINI_SOFTWARE  the appletini-software checkout (read only): a2vm
  PHASOR_DISKS        the folder with phasor.hdv and mb-audit-v1.61.po
The last three come from the environment only; required() stops with a
clear message when one is needed and missing.
"""
import os, sys
from pathlib import Path

H = Path(__file__).resolve().parent
DEFAULT_GSQ = H.parent.parent
GSQ = Path(os.environ['GSQ']).resolve() if os.environ.get('GSQ') else DEFAULT_GSQ
WORK = Path(os.environ['SSI_WORK']).resolve() if os.environ.get('SSI_WORK') else H / 'work'
BIN = WORK / 'bin'
OBJ = WORK / 'obj'
OUT = WORK / 'out'
TRACES = WORK / 'traces'
CAP = WORK / 'cap'
SUITE = H / 'suite'

ENV_HELP = {
    'APPLETINI_ONE': 'the appletini-one checkout (hdl/apple/mockingboard.sv, docs/Apple2e_Enhanced.rom)',
    'APPLETINI_SOFTWARE': 'the appletini-software checkout (demos/doom_gs/tools/a2vm)',
    'PHASOR_DISKS': 'the folder holding phasor.hdv and mb-audit-v1.61.po',
}


def required(name, must_exist=()):
    """The directory named by environment variable NAME; exits with a clear
    message when it is unset, missing, or lacks one of MUST_EXIST."""
    v = os.environ.get(name)
    if not v:
        sys.exit(f'{name} is not set: export {name}=<{ENV_HELP.get(name, "path")}> (see README.md, "Environment")')
    p = Path(v).expanduser().resolve()
    if not p.is_dir():
        sys.exit(f'{name}={v} is not a directory ({ENV_HELP.get(name, "")})')
    for rel in must_exist:
        if not (p / rel).exists():
            sys.exit(f'{name}={v} has no {rel} ({ENV_HELP.get(name, "")})')
    return p


def optional(name):
    """The directory named by NAME, or None when it is unset."""
    v = os.environ.get(name)
    return Path(v).expanduser().resolve() if v else None
