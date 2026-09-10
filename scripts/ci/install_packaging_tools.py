#!/usr/bin/env python3
"""Install the fixed official linuxdeploy AppImage used by the Linux CI package."""
import argparse
import hashlib
from pathlib import Path
import urllib.request

URL = "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
SHA256 = "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
with urllib.request.urlopen(URL, timeout=120) as response:
    content = response.read()
if hashlib.sha256(content).hexdigest() != SHA256:
    raise SystemExit("linuxdeploy release digest does not match the pinned SHA256")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(content)
args.output.chmod(0o755)
print(f"Verified linuxdeploy 1-alpha-20251107-1: {args.output}")
