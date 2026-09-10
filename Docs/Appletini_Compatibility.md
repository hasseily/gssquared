# Appletini ONE compatibility

The virtual card targets **Appletini ONE F1.0.8**, tag/commit
`c5044416fb1bf4543157f64cbb4114e4480094c0`. It uses GSSquared's CPU,
Apple memory mapping, display decoder and independently configured slots.
Appletini occupies slot 7. It does not instantiate Appletini's internally
virtualized Disk II, Mockingboard/Phasor, mouse, serial, printer, Applicard or
other cards. Those remain ordinary GSSquared devices.

Start with `assets/gs2/IIe_Appletini.gs2` or add an Appletini to slot 7 in the
system editor. The Appletini panel saves accelerator enable/speed, ignore-C074,
8 MB RamWorks and RAM32 choices. Fresh defaults match firmware: accelerator
off, C074 honored, RamWorks on where the IIe memory configuration allows it,
RAM32 off. An independently configured auxiliary-memory card takes precedence.

```toml
[appletini]
accelerator = false
speed = "33"
ignore_c074 = false
ramworks = true
ram32 = false

[[cards]]
slot = 7
card = "appletini"
```

Speed values are `"1"`, `"2.8"`, `"7"`, `"14"`, `"33"` and `"unlimited"`.
The saved speed takes effect when acceleration is enabled. Legacy Appletini
`machine.speed` values migrate, and explicit Appletini settings take precedence.

## Firmware contract

| Interface | GSSquared behavior | F1.0.8 evidence |
| --- | --- | --- |
| Slot and expansion ROM | Original 256-byte C700 and 2048-byte C800 ROMs, slot selection and CFFF release. Actual ROM entry points execute on the GSS CPU. | `hdl/apple/smartport_a2retronet_style.asm` and C700/C800 `.mem` images |
| SmartPort identification | `Appletini SP` / `Appletini HD`; DIB version bytes remain **1,0**, as in firmware 1.0.8. Controller/unit STATUS and GETDIB, correct block lengths and write protection. | `ps_sources/frontend/smartport_service.c:1195–1267` |
| Data protocol | DATA/CTRL/POP FIFO at CFF0–CFF2; ProDOS and SmartPort read/write; 24-bit block addresses; malformed/unsupported command errors. Physical DMA capability bits stay clear. | `hdl/apple/smartport_card.sv`, `smartport_service.c` command handlers |
| Eight units | Eight registered host drives, preserved configured unit IDs, image data offsets, flushed writes, read-only errors and bounds. Debug protocol and system editor both expose drive 8. | `SP_MAX_DEVICES`, `smartport_present_count()` and `build_sp_status()` |
| RAM32 | Optional volatile SmartPort unit, 65,535 blocks of 512 bytes, ProDOS `RAM32` directory/bitmap, first unconfigured free unit. It does not displace mounted or failed configured images. | `smartport_service.c:2473–2591` |
| Apple reset | Clears partial command, response and READY, restores accelerator control, resets extension video state and aborts A2Li loader hold. RAM32 and expanded RAM contents survive. | `hdl/apple/smartport_card.sv`, `vtw_core_top.sv`, renderer reset handling |
| C074 | Low two bits decoded. States 1/2 slow to native speed; state 3 holds slow until reset. State 0 restores the prior speed, including calibrated unlimited speed. Disabled/ignore-control settings suppress writes. | `hdl/apple/vtw_core_top.sv`, `card_control_regs.h` |
| RamWorks | Existing GSS auxiliary-memory subsystem; bank 0–127, C071/C073 aliases, invalid high-bit selections ignored, ALTZP/language-card and 80STORE/PAGE2 routing. Base bank selected at reset without clearing RAM. Explicit auxiliary expansion retains control. | `hdl/apple/vtw_shadow.sv`, `vtw_core_top.sv` |
| Legacy A2Li | Lores and hires mode 1 weave, mode 2 single merge, mono/color and doubled variants, each page's mixed-text tail, FF loader hold. Lores interlace uses four-line nibble bands; hires alternates lines. | `ps_sources/frontend/apple_cycle_renderer.c` legacy weave/flip renderers |
| Video extensions | Existing Video-7 MIX/MONO and C021/C022/C029/C034 handling; SHR/SHR4/3200/PAL256 modes use the GSS Appletini decoder. Already composed fields are marked to prevent a second postprocessor merge. | `apple_cycle_renderer.c`, SHR helpers and palette selectors |
| Linear text overlay | Native C0F0–C0FF `LINTXT` interface, captured main/aux writes, ARM/SHOW, attributes/fonts and composition. It is part of Appletini slot 7. | See [Appletini linear text](Appletini_LinearText.md). |

Controller STATUS reports the **number of present units**, not the highest unit
ID. Firmware preserves holes: if only unit 8 is mounted, count is 1, units 1–7
are absent, and unit 8 remains addressable. GSSquared deliberately matches this.
Software that enumerates only IDs 1 through count should use contiguous mounts.
Firmware's present-mask helper is host service statistics, not an Apple-visible
SmartPort status extension. RAM32 survives Apple reset; disabling it or mounting
a host image over its unit drops its contents. If relocated to another free
unit, it is freshly formatted, matching firmware.

The checked-in ROM SHA-256 hashes are:

- C700: `d8df8553530dd984a0452d708d8971f450c323fe83a240876ec7ef9b03b707de`
- C800: `3410c0649b12d15f4d82e4d66e9fd063b179e9822c4f2a1358410097c45d9f54`

The source baseline is [Appletini ONE F1.0.8](https://github.com/hasseily/appletini-one/tree/c5044416fb1bf4543157f64cbb4114e4480094c0).

## Hardware boundary

Physical USB/UART/JTAG, firmware flashing, AXI/DMA transports, FPGA/ARM
scheduling and the physical boot/menu takeover are not virtualized. GSSquared's
host system editor configures this card. The unused command-family 0x40 returns
BADCTL, also matching firmware; it is not an omitted working guest configuration
API.

Per-region TransWarp slowdown is **not exposed**: it is a firmware host setting
in AXI register 0x6B (`card_control_regs.h:296–315`), with slot/floating-I/O/paddle
masks and a native-cycle window, default off. It has no Apple-addressable register
interface. GSSquared does not reproduce this physical accelerator scheduling or
automatically alter the speed of independently configured slot 4 peripherals.
Apple-visible C074 control is implemented separately. GSSquared's own clock and
peripheral models remain responsible for their timing.

Firmware's optional 120 Hz physical output scheduling is not a guest register
extension. GSSquared presents its decoded/composed frame at the host renderer's
cadence. It does not treat an already woven image as two incoming raw fields.
Host renderer/context recreation preserves the last complete Appletini CPU
pixels, guest RAM and card state, including an in-progress A2Li loader hold.

## Verification

`ctest -R 'appletini|systemconfig' --output-on-failure` covers protocol/FIFO,
eight units and holes, 24-bit blocks, RAM32 format and persistence, C074 states,
configuration round trips, legacy composition, video modes and linear text.

`python3 scripts/test_appletini_guest.py --executable build/GSSquared` starts a
fresh enhanced IIe and executes the bundled 6502 SmartPort/ProDOS ROM. It checks
C800 selection/release, identification, RAM32 read/write/reset, mounted drive 8
with a 2MG header offset, error returns, mapped RamWorks behavior and overlay
identity/reset. It shuts down through debug-protocol QUIT. Linux CI needs a
display such as Xvfb; Windows uses the `.exe` path.
