#!/bin/zsh
# Create the harness's Python environment: WORK/venv (WORK is $SSI_WORK,
# default work/ here) with requirements.txt. Python 3.10 or later; set
# PYTHON to choose the interpreter.
set -e
H=${0:A:h}
WORK=${SSI_WORK:-$H/work}
mkdir -p $WORK
PY=${PYTHON:-python3}
$PY -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else "Python 3.10 or later is needed")'
[[ -x $WORK/venv/bin/python ]] || $PY -m venv $WORK/venv
$WORK/venv/bin/python -m pip install --quiet --upgrade pip
$WORK/venv/bin/python -m pip install --quiet -r $H/requirements.txt
echo "ready: $WORK/venv/bin/python ($($WORK/venv/bin/python --version))"
