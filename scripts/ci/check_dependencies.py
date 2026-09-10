#!/usr/bin/env python3
"""Fail if building changed a pinned third-party revision or tracked source."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
subprocess.run(["git", "diff", "--exit-code", "--", "vendored"], cwd=root, check=True)
status = subprocess.check_output(["git", "submodule", "status", "--recursive"], cwd=root, text=True)
if any(line and line[0] != " " for line in status.splitlines()):
    raise SystemExit("Submodules are missing or differ from the committed revisions:\n" + status)
subprocess.run(["git", "submodule", "foreach", "--recursive", "git diff --exit-code"], cwd=root, check=True)
print("All vendored revisions and tracked sources are unchanged")
