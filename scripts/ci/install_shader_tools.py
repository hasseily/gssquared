#!/usr/bin/env python3
"""Install the pinned shader build tools outside the source tree.

Linux: builds glslang/SPIRV-Cross and installs DXC. Windows --dxc-only:
installs the native DXC release for independent HLSL/DXIL validation.
Downloads are official release archives checked against GitHub's SHA256 digest.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

GLSLANG = "a8d28bd082bff18ffbe80996e922b012f915cf07"  # 16.5.0
SPIRV_CROSS = "be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a"
DXC_RELEASE = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2607/"
DXC_ASSETS = {
    "Linux": ("linux_dxc_2026_07_29.x86_x64.tar.gz", "55665c87824051ed4774ff3280a79ccbbb7d39243b9736ca5e98222134112d54"),
    "Windows": ("dxc_2026_07_29.zip", "a1dfb116ba3eeae6a1582291b53a8e7bf65ad760676bd3194685c8f7367cd241"),
}


def run(*args: str, cwd: Path | None = None) -> None:
    subprocess.run(args, cwd=cwd, check=True)


def find_dxc(root: Path, system: str) -> Path:
    # Official releases have used both a flat archive and a versioned outer
    # directory. Preserve that layout (and adjacent shared libraries) while
    # selecting the intended host architecture explicitly.
    suffix = ("bin", "x64", "dxc.exe") if system == "Windows" else ("bin", "dxc")
    candidates = [path for path in root.rglob(suffix[-1])
                  if path.is_file() and path.parts[-len(suffix):] == suffix]
    if len(candidates) != 1:
        raise SystemExit(f"Expected one {system} DXC executable in {root}; found {len(candidates)}")
    return candidates[0]


def build_tool(prefix: Path, name: str, revision: str, options: list[str]) -> None:
    source = prefix / "src" / name
    source.mkdir(parents=True, exist_ok=True)
    if not (source / ".git").exists():
        run("git", "init", str(source))
        run("git", "-C", str(source), "remote", "add", "origin", f"https://github.com/KhronosGroup/{name}.git")
    run("git", "-C", str(source), "fetch", "--depth=1", "origin", revision)
    run("git", "-C", str(source), "checkout", "--detach", revision)
    build = prefix / "build" / name
    run("cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={prefix}", *options)
    run("cmake", "--build", str(build), "--parallel", "2")
    run("cmake", "--install", str(build))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--dxc-only", action="store_true")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    system = platform.system()
    if system not in DXC_ASSETS:
        raise SystemExit("Pinned DXC binaries are available for Linux x86_64 and Windows x64.")
    if not args.dxc_only:
        build_tool(prefix, "glslang", GLSLANG, ["-DENABLE_OPT=OFF", "-DBUILD_TESTING=OFF", "-DENABLE_GLSLANG_BINARIES=ON"])
        build_tool(prefix, "SPIRV-Cross", SPIRV_CROSS, ["-DSPIRV_CROSS_CLI=ON", "-DSPIRV_CROSS_ENABLE_TESTS=OFF"])
    filename, expected = DXC_ASSETS[system]
    archive = prefix / filename
    with urllib.request.urlopen(DXC_RELEASE + filename, timeout=120) as response, archive.open("wb") as output:
        shutil.copyfileobj(response, output)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise SystemExit("DXC archive SHA256 does not match the pinned release digest")
    dxc_root = prefix / "dxc"
    dxc_root.mkdir(exist_ok=True)
    if system == "Windows":
        with zipfile.ZipFile(archive) as bundle:
            bundle.extractall(dxc_root)
    else:
        with tarfile.open(archive) as bundle:
            bundle.extractall(dxc_root, filter="data")
    dxc = find_dxc(dxc_root, system)
    run(str(dxc), "--version")
    if os.environ.get("GITHUB_PATH"):
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
            output.write(str(prefix / "bin") + "\n" + str(dxc.parent) + "\n")
    if os.environ.get("GITHUB_ENV"):
        with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as output:
            output.write("GS2_DXC_PATH=" + str(dxc) + "\n")
    print(f"Installed shader tools in {prefix}")


if __name__ == "__main__":
    main()
