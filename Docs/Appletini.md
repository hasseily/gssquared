# Appletini ONE

Select Appletini in slot 7 or start `assets/gs2/IIe_Appletini.gs2`.
The separate BazFast 3 card retains its own ROM and register protocol.
GSSquared supplies the CPU, memory and other slot devices; Appletini does not
instantiate its internally virtualized peripheral cards.

The virtual card follows Appletini ONE F1.0.8
[`c5044416`](https://github.com/hasseily/appletini-one/tree/c5044416fb1bf4543157f64cbb4114e4480094c0).
It implements the original C700/C800 ROMs, CFF0–CFF2 DATA/CTRL/POP FIFO,
ProDOS and SmartPort block requests, and eight host drives. Storage drive IDs
remain fixed when earlier units are absent. STATUS counts present devices.
DMA capability bits stay clear; the physical FPGA/ARM transport is not emulated.

Optional `[appletini] ram32 = true` creates a volatile 65,535-block ProDOS RAM32
volume in the first unconfigured free unit. Mounted paths, including paths that
fail to load, reserve their unit. Apple reset preserves RAM32 contents; replacing
or disabling the volume discards them. The system editor saves this option.

The bundled ROMs come from the firmware's `hdl/apple` SmartPort assembly/images,
under its GPL-3.0 license (the same license as GSSquared):

- C700 SHA256: `d8df8553530dd984a0452d708d8971f450c323fe83a240876ec7ef9b03b707de`
- C800 SHA256: `3410c0649b12d15f4d82e4d66e9fd063b179e9822c4f2a1358410097c45d9f54`

`appletinismartporttest` checks FIFO ownership, eight units/holes, 24-bit blocks,
write protection, malformed requests, and RAM32 formatting/reset persistence.
These are firmware-contract checks; they do not establish physical-card timing
or hardware DMA equivalence.

On the standard and enhanced IIe, Appletini also supplies 128 selectable 64 KB
auxiliary banks (8 MB total). C071/C073 select banks 0–127; values with bit 7 set
are ignored. ALTZP/language-card and 80STORE/PAGE2 follow the selected bank.
Video continues to read bank 0. Reset selects bank 0 without clearing contents.
`[appletini] ramworks = false` disables this extension; an independently selected
memory expansion takes precedence. Other machine platforms keep their own
memory implementations.

The optional accelerator defaults off. Set `[appletini] accelerator = true` and
`speed = "33"` to start at 33.333333 MHz; other saved values are `"1"`, `"2.8"`,
`"7"`, `"14"`, and `"unlimited"`. The system editor and native speed menus expose
33 MHz. Legacy `machine.speed` settings migrate when Appletini is selected.

C074 decodes its low two bits: 1/2 select native speed, 3 stays slow until reset,
and 0 restores the prior speed (including the calibrated unlimited multiplier).
`ignore_c074 = true` suppresses these overrides. NTSC/PAL scanner and audio time
follow a rational base-clock cadence at 33 MHz; physical TransWarp per-region
slowdown scheduling is outside this guest-visible control interface.

Appletini video on IIe-family machines implements Video-7 MIX/MONO, C021/C022/
C029/C034 controls, SHR/SHR4/3200/PAL256 decoding and legacy A2Li page modes.
Mode 1 weaves fields, mode 2 merges them once, and the FF loader hold preserves
the previous complete picture while guest software replaces page memory. Mixed
text tails are taken from their respective pages. Logical scanline and composed-
field metadata accompany the image, without requiring a new renderer.

`appletinivideotest` checks palettes/modes, Video-7 bit spans, page composition,
loader hold/reset and mixed tails. The generic scanline safety regression guards
resynchronization after these RAM-based modes discard the cycle stream. Physical
120 Hz output scheduling and external-video hardware are not emulated.

The native linear-text interface belongs to Appletini slot 7 at C0F0–C0FF;
it does not install a VOC or Second Sight card. C0F8–C0FD identify `LINTXT`,
with C0FE/C0FF version bytes 4C/10. INDEX/DATA/DATA_INC select configuration;
CMD (C0F3) provides OFF/ARM/SHOW/HIDE, and STATUS (C0F4) exposes BUSY, STALE,
CONFIG_ERROR, FRAME_PENDING, ARMED and VISIBLE.

After staging the buffer/geometry, ARM and poll BUSY, then write every cell to
the selected main/base-aux buffer before SHOW. Each cell is character+attribute;
prior RAM contents are not captured. The supported range begins at 0200 and
ends below C000. Higher RamWorks banks and unshadowed IIgs fast RAM are excluded.
The canvas is 1120×768 for legacy video or 1280×800 for SHR; glyph pixels are
composed before emulator controls through the existing SDL renderer.

VT100/DEC and CP437 glyphs, attributes, underline, cursor, blink and transparent
backgrounds follow the F1.0.8 ABI. Guest software owns ANSI parsing and scrolling.
VOC supplies a distinct IIgs bitmap/field interface, not this character-buffer
ABI. `appletinitexttest` checks register/state transitions, glyphs/clipping and
resolved MMU capture. Fixed Terminus 4.49.1 font bitmaps retain their SIL Open Font
License in `assets/licenses/Terminus-OFL.txt`, shipped with application resources.
