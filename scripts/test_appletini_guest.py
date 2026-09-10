#!/usr/bin/env python3
"""Run real 6502 ROM calls and mapped-memory checks in a fresh Appletini machine.

python3 scripts/test_appletini_guest.py --executable build/GSSquared
Linux needs a display (for CI, use xvfb-run). Uses protocol QUIT on completion.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "clients/python/src"))
from gs2debug import (Client, MEM_MAIN, REG_PC, REG_SP, REG_P, BP_KIND_EXEC,
                      BP_FLAG_ENABLED, BP_FLAG_TEMPORARY, MEDIA_OK, PAUSE,
                      ProtocolError)


def pause_when_ready(c, deadline):
    """Establish main-loop readiness within the existing debug-startup budget."""
    # The listener can answer HELLO while the main thread initializes graphics.
    # Code 6 is generic: retry only this exact bridge timeout, before any guest
    # setup. The server clears bridge_pending_ when the request times out.
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Main thread did not become ready before debug startup timeout")
        try:
            reply = c.request(PAUSE, timeout=remaining)
        except ProtocolError as error:
            if error.code != 6 or error.message != "timeout waiting for main thread":
                raise
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Main thread did not become ready before debug startup timeout") from error
            time.sleep(min(0.1, remaining))
            continue
        if reply:
            raise ProtocolError(0, f"PAUSE reply not empty ({len(reply)} bytes)")
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Main thread did not become ready before debug startup timeout")
        c.wait_stopped(timeout=min(5, remaining))
        return


def write(c, address, value):
    c.write_mem(MEM_MAIN, address, bytes([value]))


def byte(c, address):
    return c.read_mem(MEM_MAIN, address, 1)[0]


def run_code(c, code):
    start = 0x0300
    c.bp_clear_all()
    c.write_mem(MEM_MAIN, start, bytes(code) + b"\xEA")
    c.bp_set(kind=BP_KIND_EXEC, address=start + len(code),
             flags=BP_FLAG_ENABLED | BP_FLAG_TEMPORARY)
    c.set_regs(REG_PC | REG_SP | REG_P, pc=start, sp=0x1FF, p=0x24)
    c.continue_()
    c.wait_stopped(timeout=10)
    regs = c.get_regs()
    assert int.from_bytes(regs[16:18], "little") == start + len(code), "guest did not reach return"
    return regs[13], int.from_bytes(regs[18:20], "little") & 255


def smartport(c, command, params):
    # Standard inline SmartPort dispatch, through the bundled F1.0.8 ROM.
    c.write_mem(MEM_MAIN, 0x0800, bytes(params))
    return run_code(c, [0x8D, 0x06, 0xC0, 0xAD, 0x00, 0xC7,
                        0x20, 0x0D, 0xC7, command, 0x00, 0x08])


def test_smartport(c, directory):
    write(c, 0xC006, 0)  # External slot ROMs on enhanced IIe.
    byte(c, 0xCFFF)
    released = c.read_mem(MEM_MAIN, 0xC800, 32)
    assert c.read_mem(MEM_MAIN, 0xC700, 8)[1::2] == bytes([0x20, 0, 3, 0]), "slot7 signature"
    card = c.read_mem(MEM_MAIN, 0xC800, 32)
    assert card == (ROOT / "assets/roms/cards/pdblock3/appletini_c800.rom").read_bytes()[:32]
    byte(c, 0xCFFF)
    assert c.read_mem(MEM_MAIN, 0xC800, 32) == released and released != card, "CFFF release"

    flags, result = smartport(c, 0, [3, 0, 0, 9, 3])
    assert not flags & 1 and result == 0, "controller GETDIB ROM call"
    dib = c.read_mem(MEM_MAIN, 0x0900, 29)
    assert dib[8:21] == b"\x0cAppletini SP" and dib[27:29] == b"\x01\x00", dib
    assert dib[0] == 1, "only RAM32 is present initially"
    flags, result = smartport(c, 1, [3, 1, 0, 0x20, 2, 0, 0])
    assert flags & 1 and result == 0x28, "missing configured image keeps its unit"
    flags, result = smartport(c, 1, [3, 2, 0, 0x20, 2, 0, 0])
    assert not flags & 1 and result == 0, "RAM32 block read ROM call"
    assert c.read_mem(MEM_MAIN, 0x2004, 6) == b"\xF5RAM32", "ProDOS RAM32 format"

    c.write_mem(MEM_MAIN, 0x0042, bytes([1, 0xF0, 0, 0x20, 2, 0]))
    flags, result = run_code(c, [0x8D, 0x06, 0xC0, 0xAD, 0, 0xC7, 0x20, 0x0A, 0xC7])
    assert not flags & 1 and result == 0, "ProDOS block-entry ROM call"
    assert c.read_mem(MEM_MAIN, 0x2004, 6) == b"\xF5RAM32", "ProDOS block read"

    payload = bytes((i * 17 + 5) & 255 for i in range(512))
    c.write_mem(MEM_MAIN, 0x2000, payload)
    flags, result = smartport(c, 2, [3, 2, 0, 0x20, 0xFE, 0xFF, 0])
    assert not flags & 1 and result == 0, "RAM32 final block write"
    c.reset()
    c.write_mem(MEM_MAIN, 0x2000, bytes(512))
    flags, result = smartport(c, 1, [3, 2, 0, 0x20, 0xFE, 0xFF, 0])
    assert not flags & 1 and c.read_mem(MEM_MAIN, 0x2000, 512) == payload, "reset retains RAM32"
    flags, result = smartport(c, 1, [3, 2, 0, 0x20, 0xFF, 0xFF, 0])
    assert flags & 1 and result == 0x27, "RAM32 out of range"
    flags, result = smartport(c, 1, [3, 8, 0, 0x20, 0, 0, 0])
    assert flags & 1 and result == 0x28, "unmounted eighth unit"

    # Real mounted host image on the eighth registered drive, including a 2MG offset.
    disk = directory / "drive8.2mg"
    header = bytearray(64)
    header[:8] = b"2IMGTEST"
    header[8:12] = (64).to_bytes(2, "little") + (1).to_bytes(2, "little")
    header[12:16] = (1).to_bytes(4, "little")
    header[20:24] = (1024).to_bytes(4, "little")
    header[24:28] = (64).to_bytes(4, "little")
    header[28:32] = (1024 * 512).to_bytes(4, "little")
    disk.write_bytes(header + bytearray(1024 * 512))
    assert c.mount(7, 7, str(disk)) == MEDIA_OK, "eighth host drive registered"
    c.write_mem(MEM_MAIN, 0x2000, payload)
    flags, result = smartport(c, 2, [3, 8, 0, 0x20, 7, 0, 0])
    assert not flags & 1 and result == 0, "eighth host image write"
    assert disk.read_bytes()[64 + 7 * 512:64 + 8 * 512] == payload, "2MG data offset and flush"
    assert disk.read_bytes()[:64] == header, "2MG header preserved"
    assert c.unmount(7, 7) == MEDIA_OK
    locked_image = bytearray(disk.read_bytes())
    locked_image[16:20] = (0x80000000).to_bytes(4, "little")
    disk.write_bytes(locked_image)
    assert c.mount(7, 7, str(disk)) == MEDIA_OK
    flags, result = smartport(c, 0, [3, 8, 0, 9, 3])
    assert not flags & 1 and byte(c, 0x0900) == 0xFC, "2MG locked-media status"
    c.write_mem(MEM_MAIN, 0x2000, bytes(512))
    flags, result = smartport(c, 2, [3, 8, 0, 0x20, 7, 0, 0])
    assert flags & 1 and result == 0x2B, "2MG locked write rejected"
    assert disk.read_bytes() == locked_image, "protected image remains unchanged"
    assert c.unmount(7, 7) == MEDIA_OK

    # Partial FIFO transactions and old READY must not survive Apple reset.
    byte(c, 0xC700)
    write(c, 0xCFF0, 0xFF)
    write(c, 0xCFF1, 0x40)
    assert byte(c, 0xCFF1) == 0x80 and byte(c, 0xCFF0) == 0x21
    write(c, 0xCFF0, 0xAB)
    c.reset()
    byte(c, 0xC700)
    assert byte(c, 0xCFF1) == 0 and byte(c, 0xCFF0) == 0, "reset clears FIFO and READY"
    print("PASS real SmartPort ROM: identification, RAM32, host drive8, 2MG offset, errors, reset")


def test_ramworks(c):
    # Debug reads/writes use the same mapped MMU handlers as CPU data cycles.
    write(c, 0xC003, 0)  # RAMRD on
    write(c, 0xC005, 0)  # RAMWRT on
    for bank, value in [(0, 0x10), (1, 0x21), (127, 0x7F)]:
        write(c, 0xC071, bank)
        write(c, 0x9000, value)
    for bank, expected in [(0, 0x10), (1, 0x21), (127, 0x7F), (128, 0x7F), (255, 0x7F)]:
        write(c, 0xC073, bank)
        assert byte(c, 0x9000) == expected, (bank, "RamWorks bank or invalid high-bit write")
    write(c, 0xC071, 1)
    write(c, 0xC009, 0)  # ALTZP maps zero page and language card through selected aux bank.
    write(c, 0x0080, 0xA1)
    byte(c, 0xC083); byte(c, 0xC083)
    write(c, 0xD000, 0xD1)
    write(c, 0xC071, 127)
    write(c, 0x0080, 0xA7)
    write(c, 0xD000, 0xD7)
    write(c, 0xC071, 1)
    assert byte(c, 0x0080) == 0xA1 and byte(c, 0xD000) == 0xD1, "ALTZP/LC selected bank"
    write(c, 0xC008, 0); write(c, 0xC002, 0); write(c, 0xC004, 0)
    write(c, 0xC001, 0)  # 80STORE
    byte(c, 0xC055); byte(c, 0xC057)  # PAGE2 + HIRES
    write(c, 0x0400, 0x41); write(c, 0x2000, 0x51)
    write(c, 0xC071, 127)
    write(c, 0x0400, 0x47); write(c, 0x2000, 0x57)
    write(c, 0xC071, 1)
    assert byte(c, 0x0400) == 0x41 and byte(c, 0x2000) == 0x51, "80STORE/PAGE2/HGR routing"
    c.reset()
    write(c, 0xC003, 0)
    assert byte(c, 0x9000) == 0x10, "reset selects base auxiliary bank"
    write(c, 0xC071, 127)
    assert byte(c, 0x9000) == 0x7F, "reset preserves expanded bank contents"
    c.reset()
    print("PASS RamWorks: bank0/1/127, invalid selects, ALTZP/language-card, 80STORE/PAGE2, reset")


def test_overlay(c):
    assert c.read_mem(MEM_MAIN, 0xC0F8, 8) == b"LINTXT\x4c\x10", "linear overlay identity"
    write(c, 0xC0F0, 0x1E)
    assert byte(c, 0xC0F1) == 0x7F, "linear overlay capabilities"
    write(c, 0xC0F3, 1)  # Invalid BASE=0 ARM must report error.
    assert byte(c, 0xC0F4) & 0x20, "invalid ARM error"
    c.reset()
    assert byte(c, 0xC0F4) == 0, "overlay reset"
    # Valid ARM snapshots the staged configuration, clears its private shadow,
    # then observes real mapped writes. SHOW/HIDE/OFF commit on display frames.
    write(c, 0xC0F0, 0)
    for value in [0, 0x60, 0x06, 1, 1]:
        write(c, 0xC0F2, value)  # BASE6000, CP437 8x16, 1x1 cell
    write(c, 0xC0F3, 1)
    assert byte(c, 0xC0F4) & 0x80, "ARM exposes BUSY"
    run_code(c, [0xEA] * 32)
    assert byte(c, 0xC0F4) == 2, "ARM completed and capture enabled"
    write(c, 0x6000, ord("A")); write(c, 0x6001, 0x1E)
    write(c, 0xC0F3, 2)
    # ~41K CPU cycles advance two Apple frames, using a bounded counted loop.
    frame_cycles = [0xA2, 0, 0xA0, 0x20, 0xCA, 0xD0, 0xFD, 0x88, 0xD0, 0xFA]
    run_code(c, frame_cycles)
    assert byte(c, 0xC0F4) == 3, "SHOW committed at output frame"
    write(c, 0xC0F3, 3); run_code(c, frame_cycles)
    assert byte(c, 0xC0F4) == 2, "HIDE retains capture"
    write(c, 0xC0F3, 0); run_code(c, frame_cycles)
    assert byte(c, 0xC0F4) == 0, "OFF stops capture"
    for video, dimensions in [(0x80, (1120, 768)), (0xC0, (1280, 800))]:
        write(c, 0xC029, video)
        write(c, 0xC0F0, 0x10)
        canvas = bytes(byte(c, 0xC0F2) for _ in range(4))
        assert (int.from_bytes(canvas[:2], "little"), int.from_bytes(canvas[2:], "little")) == dimensions, "SHR canvas needs C029 bits7+6"
    c.reset()
    print("PASS linear text overlay: identity, ARM/capture/SHOW/HIDE/OFF, canvas selection, reset")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--timeout", default=45, type=float)
    parser.add_argument("--isolated-runtime", action="store_true",
                        help="On Windows, remove toolchain directories from the emulator's DLL search PATH")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gs2-a108-") as directory:
        directory = Path(directory)
        config = directory / "Appletini.gs2"
        config.write_text('''gs2_version = 1
id = "408e9ba8-9a06-4ad4-b132-ed75030436bb"
name = "Appletini firmware smoke"
platform = "apple2e_enhanced"
[appletini]
ram32 = true
ramworks = true
[[cards]]
slot = 7
card = "appletini"
[[storage]]
slot = 7
drive = 1
image = "missing-configured-image.po"
''')
        sock = directory / "debug.sock"
        log = directory / "emulator.log"
        executable = args.executable.resolve()
        env = os.environ.copy()
        env.setdefault("SDL_AUDIODRIVER", "dummy")
        if args.isolated_runtime and os.name == "nt":
            system_root = Path(env.get("SystemRoot", env.get("WINDIR", r"C:\Windows")))
            env["PATH"] = os.pathsep.join((str(system_root / "System32"), str(system_root)))
        with log.open("w") as output:
            process = subprocess.Popen([str(executable), str(config), "--debug", str(sock),
                "--no-quit-confirm"], cwd=executable.parent, env=env, stdout=output, stderr=output)
            c = Client()
            connected = False
            try:
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError("Emulator exited before debug startup")
                    if sock.exists():
                        try:
                            c.connect(str(sock)); c.hello(); connected = True; break
                        except (OSError, ConnectionError):
                            c.close()
                    time.sleep(0.1)
                if not connected: raise TimeoutError("Debug socket did not start")
                pause_when_ready(c, deadline)
                test_smartport(c, directory)
                test_ramworks(c)
                test_overlay(c)
            except BaseException:
                output.flush()
                print(log.read_text(errors="replace")[-10000:], file=sys.stderr)
                raise
            finally:
                if connected:
                    try: c.quit()
                    except (OSError, ConnectionError, ProtocolError): pass
                c.close()
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=5)
            if process.returncode != 0:
                print(log.read_text(errors="replace")[-12000:], file=sys.stderr)
            assert process.returncode == 0, process.returncode
    print("All Appletini guest compatibility smoke checks passed")


if __name__ == "__main__":
    main()
