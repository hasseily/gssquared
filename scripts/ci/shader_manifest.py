#!/usr/bin/env python3
"""Bind all offline shader outputs to their source files and exact CI revision."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

STAGES = ("fullscreen.vert", "fullscreen_target.vert", "crt.frag", "composite.frag")
FORMATS = ("spv", "metal", "hlsl", "gles", "glsl", "dxil")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.root.resolve()
    shaders = root / "assets/shaders/postprocess"
    files = [name for stage in STAGES for name in (stage, *(f"{stage}.{fmt}" for fmt in FORMATS))]
    hashes = {}
    for name in files:
        content = (shaders / name).read_bytes()
        if not content:
            raise SystemExit(f"Empty shader: {name}")
        if name.endswith(".spv") and content[:4] != b"\x03\x02\x23\x07":
            raise SystemExit(f"Invalid SPIR-V header: {name}")
        if name.endswith(".dxil") and content[:4] != b"DXBC":
            raise SystemExit(f"Invalid DXIL container: {name}")
        hashes[name] = hashlib.sha256(content).hexdigest()
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    manifest = {"revision": revision, "sha256": hashes}
    path = shaders / "ci-shader-manifest.json"
    if args.write:
        path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    else:
        expected = json.loads(path.read_text())
        if manifest != expected:
            raise SystemExit("Shader artifact revision/content differs from this checkout")
    print(f"Verified {len(files)} shader sources/outputs for {revision}")


if __name__ == "__main__":
    main()
