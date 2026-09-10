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
