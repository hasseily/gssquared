# SSI-263 comparison harness: GSSquared against the Appletini-One RTL

This harness checks GSSquared's Phasor speech path (the two SSI-263AP
voices, the VIAs they sit behind, the Phasor decode and mix, and the warmth
tone stage) against the Appletini-One firmware's RTL of the same card,
sample for sample and access for access.

## Why the RTL is the reference

The Appletini-One's Phasor (appletini-one `hdl/apple/mockingboard.sv` and
the SSI-263 voices under it) was validated by its author against a real
SSI-263 chip, so it is the reference here: any difference from it is
GSSquared's to explain or remove. The harness runs that RTL unmodified
under Verilator and GSSquared's own sources compiled from this repository,
drives both with the same Apple bus traffic one Apple cycle at a time, and
compares what comes out. Recordings of a real card are supporting evidence
only (`recordings.py`), never the reference.

It covers 177 cases: 146 synthetic register programs (`suite/`, 20 of them
for the filter-frequency register) and 31 traces of real programs
(mb-audit's speech menu and SSI-263 tests, and the Phasor disk's
text-to-speech programs) captured on a2vm with the RTL card linked in.

## Quick start

    cd tools/ssi263-harness
    export APPLETINI_ONE=~/Documents/Repos/appletini-one
    export APPLETINI_SOFTWARE=~/Documents/Repos/appletini-software
    export PHASOR_DISKS=<folder with phasor.hdv and mb-audit-v1.61.po>
    ./setup.sh                               # work/venv with requirements.txt
    ./build.sh                               # work/bin/card_rtl, work/bin/card_gss
    work/venv/bin/python capture.py          # work/traces: the 31 traces (about 10 minutes)
    work/venv/bin/python run_suite.py --strict --tag strict   # the acceptance test (about 30 minutes the first time)

## Requirements

Tested on macOS 27 (Apple silicon) with:

* Apple clang 21 (`clang++ -std=c++20` for the GSSquared side, `cc -std=c11`
  for a2vm). `a2vm/build_a2vm.sh` links with macOS `ld`'s `-U` flags; on
  Linux drop them.
* Verilator 5.050 (`verilator` on `PATH`; `verilator --getenv
  VERILATOR_ROOT` must give its include directory).
* Python 3.10 or later (tested 3.14.6) with numpy 2.5.3
  (`requirements.txt`); `./setup.sh` makes the venv. `build.sh` itself runs
  `python3` for two small generators that need no packages.
* SDL3's headers: GSSquared's headers include `<SDL3/SDL.h>` (nothing of SDL
  is linked). `build.sh` takes `$SDL3_CFLAGS`, else `pkg-config --cflags
  sdl3` (Homebrew's `sdl3`), else `vendored/SDL/include` once that
  submodule is checked out.
* git, zsh, patch.

## Environment

| Variable | Needed by | What |
| --- | --- | --- |
| `GSQ` | build.sh | The GSSquared tree under test. Default: the repository this directory is in (`../..`), so the harness tests the branch it is in. `run_suite.py --strict` refuses any other. |
| `APPLETINI_ONE` | build.sh rtl/tone, run_suite.py --strict, capture.py | The appletini-one checkout, read only: the Verilog (`hdl/`) and the //e ROM (`docs/Apple2e_Enhanced.rom`). The results below are from d4d0499 (the filter-frequency control of cae426f). |
| `APPLETINI_SOFTWARE` | capture.py (a2vm/build_a2vm.sh) | The appletini-software checkout, read only: a2vm is taken from commit `a2vm/BASE` with `git archive`, so its working tree does not matter. |
| `PHASOR_DISKS` | capture.py | The folder holding `phasor.hdv` and `mb-audit-v1.61.po` (on the owner's machine, the Appletini SD card copy under `_software/Phasor`). The disks are read into memory, never written. |
| `SSI_WORK` | everything | Where outputs go. Default `work/` here (gitignored). |
| `SDL3_CFLAGS` | build.sh | See Requirements. |
| `EXP_BASE` | build.sh exp | The commit the attribution builds patch (default d15bdbf9, the code before the fix). |

A missing variable stops the command with a message naming it. Everything
generated goes under the work directory:

    work/venv/           the Python environment (setup.sh)
    work/bin/            card_rtl, card_gss and the other binaries, each with NAME.build and NAME.deps
    work/obj/            Verilator objects, the renamed YM2149 copy, generated sources
    work/gsq-d15bdbf9/   d15bdbf9's src/ (git archive), for the attribution builds
    work/a2vm/           the patched a2vm copy and a2vm_ssi (capture.py)
    work/cap/NAME/       captures: phasor.log, screen.log, state.json, command.txt
    work/traces/         the 31 traces (capture.py)
    work/out/CASE/TAG/   renders (sec.pcm, pri.pcm, card.pcm, events.txt) and compare_TAG.json
    work/out/listen/     WAVs (to_wav.py)

The traces are cut from runs of third-party disks, so they are not in git
(`.gitignore`); `capture/traces.sha256` records what they must be.

## Files

| Path | What |
| --- | --- |
| `build.sh` | builds the renderers and tools into `work/bin`, with build records |
| `setup.sh`, `requirements.txt` | the Python environment |
| `paths.py` | every path and environment variable, for the Python tools |
| `run_suite.py` | runs the cases through both renderers and compares them; `--strict` is the acceptance test |
| `compare.py` | the comparison of one case and its verdict |
| `summarize.py` | aggregates a run's `compare_TAG.json` by case group |
| `gen_suite.py`, `suite/` | the synthetic cases (`gen_suite.py` rewrites `suite/syn_*.txt`; `suite/hello.txt` is hand-written) |
| `capture.py`, `capture/` | regenerates the traces: the expect scripts (`capture/NAME.expect`), the cuts, `capture/traces.sha256` |
| `make_trace.py`, `split_capture.py` | cut traces out of a capture |
| `a2vm/` | the capture machine: `a2vm.patch` (against appletini-software `a2vm/BASE`), the harness's own `ssi_ext.c`/`ssi_ext.h`, `build_a2vm.sh` |
| `common/driver.hpp` | the stimulus driver both renderers share (script and trace formats) |
| `rtl/` | `tb_card.sv` (the bus strobes around `mockingboard.sv`), `ym2149_mute.sv` (the PSG mute wrapper), `rtl_card.hpp` (the RTL card behind the driver), `card_rtl.cpp`, `rtlcard_shim.cpp` (for a2vm) |
| `gss/` | `card_gss.cpp` (GSSquared's card: the mirror of mb2.cpp), `check_mirror.py` and its record `mb2_mirror.ref`, `make_exp.py` (attribution builds), the debug dump builds, `gsscard_shim.cpp` (for a2vm) |
| `tone/` | the tone stage alone: a per-clock model, the RTL block cut out verbatim, the check against GSSquared's `WarmthChannel` |
| `neg_card.py`, `neg_access.py` | fault injection: the gates must catch each fault |
| `to_wav.py`, `recordings.py` | listening, and the comparison with recordings of a real card |

## Commands

All Python runs with the venv's interpreter (`work/venv/bin/python`,
written `py` here).

* Setup: `./setup.sh` (once; `PYTHON=` picks the interpreter).
* Build: `./build.sh` builds both renderers; `./build.sh gss` after
  changing GSSquared, `./build.sh rtl` after changing appletini-one.
  `./build.sh tone` builds the tone-stage tools, `./build.sh dbg` the
  state-dump build of the tree under test, `./build.sh exp TAG FLAG...` and
  `./build.sh dbgexp TAG FLAG...` the attribution builds (below).
* Capture: `py capture.py` builds a2vm (`a2vm/build_a2vm.sh`, after
  `./build.sh rtl`), runs the nine captures six at a time (the longest,
  Speech Adjust, takes about ten minutes), cuts the 31 traces and checks
  each against `capture/traces.sha256`, byte for byte (exit 1 otherwise).
  `--only NAME...` redoes some captures, `--cut-only` only cuts.
* Strict run: `py run_suite.py --strict --tag strict`. Exit 0 only on
  `STRICT: PASS`; see "Acceptance gates".
* Exploratory run: `py run_suite.py -k 'syn_phone_*' -k hello` (a subset),
  `--fast` (a 5x faster, timing-identical but not sample-identical
  reference), `--gss work/bin/card_gss_TAG --tag TAG` (another build),
  `--rerun-ref`, `--jobs N` (default 6). Exit 1 if a case FAILs. PASS
  verdicts appear here; they never count in `--strict`.
  `py summarize.py TAG...` aggregates a run by case group.
* Listening: `py to_wav.py CASE [pri|sec|card] [TAG]` writes the RTL's and
  the tag's render of a case as WAVs in `work/out/listen/`. The RTL renders
  the PSGs muted; `SSI_PSG=1` makes them audible in an exploratory run (the
  reference cache then re-renders; `--strict` refuses the variable).
* Tone stage: `./build.sh tone` then `py tone/tone_suite.py` (needs the
  reference renders of a run).
* Fault injection: `py neg_card.py TAG CASE...` and `py neg_access.py TAG
  CASE...` on cases whose renders under TAG pass (e.g. `strict
  phtalkkbd_04 hello`, `strict mbaspeech_G hello`).
* Recordings: `py recordings.py --rec DIR` with A.wav-G.wav (48 kHz) from
  appletini-one's `Assets/Sounds/"Mockingboard mb-audit samples"`.

## The two renderers

Both are driven by the same stimulus code (`common/driver.hpp`), one Apple
CPU cycle at a time, with the same 48 kHz cadence (GSSquared's integer
accumulator, mb2.cpp), so sample n is produced in the same Apple cycle on
both sides.

| Binary | What runs |
| --- | --- |
| `card_rtl` | The reference: appletini-one's whole Phasor (`hdl/apple/mockingboard.sv`: two VIAs, four PSGs, both SSI-263AP voices, mode GAL, mixer and tone stage) under Verilator, unmodified. The PSGs keep their whole bus protocol but their audio is muted (`rtl/ym2149_mute.sv` wraps a renamed copy of `YM2149.sv` that build.sh writes to work/obj; the checkout is not touched), so `card.pcm` is the speech path alone, as card_gss renders it. `rtl/tb_card.sv` only plays the Apple bus strobes of `apple_bus_wrapper.sv` (addr_en, sss_en, serve_en, data_en), Q3 and `audio_sample_tick` into it. Clocked as the card is (see "Clocking"). |
| `card_gss` | The code under test: GSSquared's `SSI263.cpp`, `W6522.hpp` (the VIAs), `PhasorLogic.hpp` (decode, mix) and `PhasorAudio.hpp` (warmth), compiled from `GSQ`. `gss/card_gss.cpp` mirrors `mb2.cpp`'s `Mockingboard` call for call for everything the speech path touches (write/read decode, `clockDevices` order: VIAs, render secondary then primary, XCK, completion routing, IRQ, and the `generate_frame` mix through the real `PhasorLogic::mixAudioSample` and `PhasorAudio::WarmthFilter`); the AY chips render no audio and the SDL stream is left out. `mb2.cpp` itself needs SDL, the clock, the MMU and the audio system, so it is mirrored rather than compiled; `gss/check_mirror.py` guards the mirror. |

Each writes `sec.pcm`, `pri.pcm` (int16 mono 48 kHz: the raw voice of the
A5 secondary and A6 primary socket), `card.pcm` (int16 stereo: the card
output; tone controls at 0 and warmth +8, GSSquared's fixed setting) and
`events.txt` (D7 of each socket, the CPU IRQ line and each socket's direct
IRQ as the access in that cycle sees them; every write and every read with
cycle, sample, address and value; NOTE, TRACEREAD and END lines; and the
header lines `# impl` and, for the RTL, `# settings`, the renderer settings
it actually used).

Within an Apple cycle the RTL order is [XCK edge ~68, access 73-124, sample
tick on the last clock]; GSSquared's is [access, sample, XCK], the same
order with the XCK numbering shifted by one, so D7/IRQ edges and
closed-loop write cycles compare exactly.

## Clocking

The card's fabric clock is 133.33 MHz and its 48 kHz tick is the carry of a
32-bit accumulator that gains 1546188 a clock (appletini_yarz_top.sv), so
ticks are 2777 or 2778 clocks apart. The Phasor's tone stage
(mockingboard.sv `final_audio_mix`: three one-poles with truncating shifts,
the warmth bands, the knee) runs on every one of those clocks, so its output
depends on that count. `card_rtl` (`rtl/rtl_card.hpp`, `cycleLength`) gives
every sample period exactly the card's count: the Apple cycle that carries
the tick has 130 clocks (a write's data_en, offset 124, stays 5 clocks ahead
of the tick: the voice needs those clocks to take a write ahead of the
sample it lands with, which is GSSquared's [access, sample] order) and the
other 20 or 21 cycles of the period share the rest (126-127 or 132-133
clocks, every bus strobe at its usual offset). The card output is read where
the top level samples it: `audio_l` after edge tick-3. `SSI_CLOCK=fixed`
gives the older 130 clocks every cycle; it changes no socket sample, edge,
write or read in any case.

## The tone stage and the WARMTH residual

`tone/tone_model.hpp` is the tone stage as a per-clock C++ model (every
register of `final_audio_mix`, one `step()` a clock edge);
`tone/extract_tone.py` cuts the block itself out of mockingboard.sv verbatim
(the PSG term of the base set to 0) and `tone_rtl` runs it under Verilator.
`tone/schedule.hpp` is the clocking above; the voice's output reaches the
mixer 151 clocks after the previous tick when the sample had one tract pass
(the SSI-263 backend's pipeline, found by scanning: the one latency at which
the model matches). Since appletini-one cae426f the filter frequency gives a
sample 0, 1 or 2 passes, so that latency is 9, 151 or 293 clocks
(151 + 142 per pass beyond one); `tone_check --passes FILE` takes the passes
per sample, which `card_gss_dbg` writes with `SSI_PASSES=FILE`, and
`tone_suite.py` does that for every case (`--fixed-d` for the old fixed
latency).
`tone_check` runs the model and GSSquared's `PhasorAudio::WarmthChannel` on
the same input; `tone/tone_suite.py` does it for every case (with the RTL's
own speech as input, against the RTL's card.pcm) and for synthetic inputs
(steps to both rails, full-scale noise, sweeps) through the RTL block.

Results with appletini-one 3101934 (2026-10-05, 157 cases, one pass a
sample everywhere):

* The model is the RTL: 0 of 23,273,634 channel-samples differ over the 157
  cases, and 0 on the synthetic inputs, which reach the warmth knee and both
  rails.
* GSSquared's WarmthChannel against the RTL, same speech in: 10,581,657
  samples differ, at most 93 LSB (syn_phone_31 R), worst 10 ms error RMS
  27.7, SNR 41.6-55.2 dB on 95% of the speaking channels. On synthetic
  full-scale input: up to 390 (steps), 438 (noise), 323 (sweep).
* Where it comes from: with the RTL's voice latency removed the residual
  drops to at most 15 LSB on speech. Most of it is the RTL's 154 clocks
  (151 + 3, a twentieth of a sample) during which the filters still
  integrate the previous speech sample, which WarmthChannel's collapsed
  one-step update does not have; the remaining 15 LSB or less is the
  per-clock truncation of the RTL's `>>> 16/14/13` one-poles, which the
  collapsed Q1.31 form does not reproduce (PhasorAudio.hpp says it "can
  differ ... by a few PCM LSBs").

Results with appletini-one d4d0499 (2026-10-05, 177 cases, `tone_suite.py
--no-synth` with the per-sample passes; the synthetic inputs do not depend
on the voice and were not rerun):

* The model is the RTL on 24,131,851 of 24,131,940 channel-samples. The 89
  that differ are syn_ff_reset R, from the Apple RESET on: a warm reset
  zeroes the voice output at the reset's own clock, not a pipeline latency
  after a tick, which the schedule does not model. On every other case the
  per-sample latency 9/151/293 makes the model exact, which also confirms
  GSSquared's pass schedule clock for clock (with the fixed 151 the model
  missed by up to 58 LSB on the three FF cases tried).
* GSSquared's WarmthChannel against the RTL: 10,967,633 samples differ, at
  most 169 LSB (syn_ff_ctl_00 R), worst 10 ms error RMS 52.4 (syn_phone_31
  R, FF=$E6). Two-pass samples put the voice 293 clocks after the tick, so
  the unmodelled latency, and with it this residual, is about twice what it
  was. With the latency removed (tone_check --d 0) it stays at most 16 LSB
  (checked on syn_ff_phones_FF, syn_ff_sweep_coarse, syn_ff_ctl_00).

So GSSquared's warmth stage is not bit-exact with the RTL's. The difference
is small (about -46 dBFS at worst on speech), but by the rule that the RTL
is the reference it is a difference. The card gates below were set from the
3101934 residual (93 and 27.7) and now fail on it: see "Known open items".

## Inputs

* `suite/*.txt`: closed-loop scripts (each implementation follows its own
  responses). `gen_suite.py` writes `suite/syn_*.txt` (145 cases); the
  commands are in the header of `common/driver.hpp`: writes cost 6 cycles
  (LDA #/STA abs), D7 polls 7 cycles a loop, IFR polls 9, IRQ waits add the
  7-cycle entry, `reset N` holds Apple RES.
* `work/traces/*.trace`: open-loop replays of real programs. A cut starts
  with a preamble that rebuilds the card's register state at the cut; both
  renderers replay the same file. `TRACEREAD` lines in `events.txt` flag
  reads that differ from the value the program saw during the capture.

## Captures

`capture.py` runs the programs on a2vm (appletini-software's Apple //e
emulator) with the harness's extensions (`a2vm/ssi_ext.c`, hooked in by
`a2vm/a2vm.patch`): `--blockdev` (a ProDOS block device with a boot ROM in
slot 7), `--boot`, `--phasor-rtl F` (this same RTL card linked in as the
Phasor, so programs run against the card's exact VIA/SSI/IRQ timing; the
captures use F=12, which the card clamps to 20 fabric clocks a cycle with
fixed clocking), `--phasor-log`, `--screen-log` and `--expect` (typing
driven by what is on the text screen, with `@reset` for CTRL-RESET). The
command each capture ran is in `work/cap/NAME/command.txt`.

| Capture | Disk | What | Traces |
| --- | --- | --- | --- |
| mba_speech | mb-audit | the speech menu (C), phrases A-G | mbaspeech_A-G |
| mba_audit | mb-audit | the quick audit (A) with both CTRL+RESET tests | mbaudit_ssiA, mbaudit_ssiB, mbaudit_reset |
| ph_adj | phasor.hdv | Speech Adjust, 'Peter Piper' with the rate and inflection keys | phadj_01-04 (10M-cycle windows) |
| ph_menu | phasor.hdv | /SPEECH/STARTUP, the speech menu | phmenu_01 |
| ph_fayzor | phasor.hdv | Dr. Fay Zor, name APPLE | phfayzor_01-05 |
| ph_talkkbd | phasor.hdv | Talking Keyboard: a phrase, the inflection arrows, ESC | phtalkkbd_01-05 |
| ph_clock | phasor.hdv | Talking Clock | phclock_01-02 |
| ph_mainmenu | phasor.hdv | the main menu STARTUP | phmain_01-02 |
| ph_test | phasor.hdv | TEST: AY effects, then the speech test | phtest_01-02 |

The mb-audit and Speech Adjust traces are fixed cycle ranges of their
captures (`capture.py`, `CUTS`); the other TTS programs are cut per
utterance group by `split_capture.py --gap 400000 --tail 300000`.
The captures are deterministic: on 2026-10-05 `capture.py` reproduced all
31 traces byte for byte from the disks above. Later that day the owner's
phasor.hdv changed (modified 06:52): its TTS Pitch byte, which the programs
write to the filter-frequency register ($C444), is now $F5 where the first
recording had $80. The 21 phasor.hdv traces were re-recorded from it with
appletini-one d4d0499 (`capture/traces.sha256` names the disk's SHA-256).
The RTL change is not the cause: the 10 mb-audit traces came out identical,
17 of the 21 TTS traces differ only in that byte, and 3101934's RTL cuts the
same phadj_01-04 from the new disk as d4d0499's. The TTS traces now run the
tract at $F5 (about 1.46 times the old rate, two passes on 46% of samples);
the mb-audit traces write $E6, $E9 and $00.

## Acceptance gates (`run_suite.py --strict`)

`--strict` exits 0 (`STRICT: PASS`) only if every gate below holds for all
177 cases; otherwise `STRICT: FAIL (...)` and exit 1, or `STRICT: REFUSED
(...)` and exit 2 when it cannot judge (a stale binary it cannot rebuild,
renderer settings in the environment, `--fast`, `GSQ` set to another tree,
a missing or different trace). Its last lines are the totals, the SSI and
card split, the binaries used (path, SHA-256, build record) and the verdict.

**SSI-263 exact.** Every case must be EXACT: both sockets' samples
identical, every D7, IRQ and direct-IRQ edge in the same cycle, every write
and every read identical (count, order, cycle, address, value), and the
NOTE (a poll's timeout), TRACEREAD and END lines the same. There is no
tolerance because the reference is the RTL and the two are meant to be the
same machine: once the speech is identical, any sample difference is a
bug, and in a closed-loop script an A/R or D7 timing difference moves the
polls, so it shows up in the access stream too. (Exploratory runs also
grade PASS: edges within 2 cycles and, per phone, level within 1 dB, onset
within 1 ms, log-mel within 1.5 dB, F0 within 20 cents; those are around
the just-noticeable differences for speech. PASS never counts here.)

**The card check**, per channel of `card.pcm` (the speech through the
Phasor mixer and tone stage; PSGs excluded on both sides):

* routing: each socket's gain into the channel (least squares through a
  linear model of the warmth stage) within 0.03 of the RTL's, which sends
  A5 secondary to the left only and A6 primary to the right only, at unity
  (mockingboard.sv:940-962). Catches a wrong mix, such as the centred 0.707
  mix GSSquared had before the fix.
* silence: no 10 ms window silent in the RTL (RMS <= 30) that has signal in
  the test (RMS > 100), nor the reverse. Catches a voice on the wrong
  channel or a lost one.
* level: whole-case RMS within 0.1 dB; tone shape: per-frame mean-removed
  log-mel within 0.2 dB. Catch a gain or tone-control error.
* lag: the card lines up with the RTL at lag 0 (best SNR over +-4 samples)
  when both sides' socket outputs are identical. Catches added or missing
  card latency.
* sample: every sample within 128 LSB of the RTL's; window: the error RMS
  of every 10 ms window at most 40. These catch a short fault (a click, a
  dropout, a burst of wrapped samples) that whole-case averages cannot.
* noise floor: in every 10 ms window where the RTL channel is silent, the
  error RMS with the window's mean removed at most 6 (-75 dBFS). A steady
  hum or hiss between the words is below every other gate (a 1 kHz tone of
  amplitude 30 passed them all); this one catches amplitude 10 (RMS 7.1).
  The correct build measures at most 3.4.

The sample and window limits are about 1.4 times GSSquared's measured
WARMTH residual (93 per sample, 27.7 per window: "The tone stage"), the
one card difference left when the speech is identical. They apply to the
whole card output, so a case whose speech differs fails them too.

**The suite is whole and current**: no `-k`, every trace of
`capture/traces.sha256` present with that SHA-256 and no other, no ERROR,
and the mirror guard passes.

## The mb2.cpp mirror guard

`gss/check_mirror.py` extracts 26 regions of `mb2.cpp` (the Mockingboard
class's members with their initialisers, such as `phasor_mode =
kModeMockingboard`, `speech_sample_phase` and `warmth_filter`, the nested
types and the function signatures with the bodies removed; the constructor
and destructor, decode, `clockDevices`, mode switch, AY bus cycle,
read/write, `generate_frame`, reset, the slot and $C0nX handlers,
`init_slot_mockingboard`) and `PhasorLogic::advanceAudioSamplePhase`, and
compares them, comments and whitespace ignored, with `gss/mb2_mirror.ref`:
the regions as they were when `gss/card_gss.cpp` was last checked against
them line by line (d15bdbf9; 2ad4f50a changed only comments there). It also
fails if `gss/card_gss.cpp` or `common/driver.hpp` changed since.

When it fails (a change to mb2.cpp's Mockingboard or to the phase
advance), it prints the diff. Read it, bring `gss/card_gss.cpp` (or
`common/driver.hpp`) into line with the change, then record the new state
with `py gss/check_mirror.py --update` and commit `gss/mb2_mirror.ref` with
the change. Update it only after that review: it is what lets the harness
stand in for mb2.cpp. `--show 'Mockingboard members'` prints one region.

## Freshness: the stale-binary and stale-reference guards

`build.sh` writes two records next to every binary: `NAME.deps`, every file
it is built from (the compiler's include closure: the tree's SSI263.cpp,
PhasorLogic.hpp, PhasorAudio.hpp, W6522.hpp and the rest they pull in,
`gss/card_gss.cpp`, `common/driver.hpp`; for `card_rtl` the Verilog,
`rtl/*.sv`, `rtl/rtl_card.hpp`, `rtl/card_rtl.cpp`; `build.sh` itself in
every one), and `NAME.build`, the build.sh arguments, the GSSquared commit
and local change count, and the appletini-one commit.

Stale binaries: `--strict` checks the reference and the binary under test
against their records first. A binary older than any source (or with one
missing, or with a record that does not list build.sh) is rebuilt with its
recorded arguments and checked again; one with no record, or still stale,
is refused. It also refuses a binary under test whose recorded sources lie
outside the repository the harness is in, and a reference not built from
`$APPLETINI_ONE`. So editing SSI263.cpp, a Verilog file or build.sh and
running `--strict` always judges the new code.

Stale references: a reference render is cached in `work/out/CASE/rtl` under
a key (`renderer.key`): the SHA-256 of the `card_rtl` that made it, the
value or absence of every environment variable the renderer reads
(`SSI_FABRIC`, `SSI_CLOCK`, `SSI_AUDIO_CONTROL`, `SSI_PSG`, `SSI_DBG`,
`SSI_DBG_SEL`), and the settings line the render reported (`# settings
fabric=130 clock=card audio_control=0x02040000 psg=muted dbg=off
dbg_sel=0`). A different binary, a different setting, a case file newer
than the render, or a settings line that disagrees with the key re-renders
it; the run prints how many references it rendered and why. `--strict`
renders both sides with `PATH=/usr/bin:/bin LC_ALL=C` only, refuses those
variables in its environment, and makes a case an ERROR when its reference
reports other settings than the defaults, even under a forged key.

## Fault injection

`neg_access.py TAG CASE...` injects one fault at a time into a passing
render's events.txt (a read returning 00, a read moved one cycle, dropped
or added, a write's value, cycle or address changed, a TRACEREAD or NOTE
line added) and requires each to FAIL with a writes/reads/notes reason.
`neg_card.py TAG CASE...` injects faults into a passing card.pcm (a
1-sample full-scale click at the quietest signal window and at the peak, a
10 ms -6 dB dip, 20 ms of int16-wrapped noise, one sample off by exactly
129, and a 1 kHz tone of amplitude 10 and 30 on the whole right channel)
and requires each to FAIL with a card reason. On 2026-10-05, run against
the attribution build that matched the RTL (all `EXP_*` flags and
`EXP_MIX`, the behaviour 2ad4f50a ports), every fault failed: the card
faults on phtalkkbd_04, hello and phfayzor_04, the access faults on
mbaspeech_G and hello, with the baselines EXACT. Before the sample and
window gates existed, the click, dip and noise faults on phtalkkbd_04
passed as EXACT. `INJECT_READ_CYCLE/ADDR/VALUE` (driver.hpp, compile-time,
e.g. `./build.sh exp injread <flags> INJECT_READ_CYCLE=1575528
INJECT_READ_ADDR=0xC48D INJECT_READ_VALUE=0x00`) makes a renderer itself
return a wrong read value.

## Attribution builds and debug dumps

`gss/make_exp.py` writes `work/obj/exp/SSI263_exp.cpp`, a copy of
d15bdbf9's `SSI263.cpp` (the code before the fix, taken from this
repository's history) in which each identified cause can be switched to
the RTL's behaviour (`EXP_*` flags, documented in the script);
`./build.sh exp TAG FLAGS...` builds `work/bin/card_gss_TAG`. `EXP_MIX`
(in gss/card_gss.cpp) replaces d15bdbf9's centred speech mix with the RTL's
routing. These builds are analysis only and never accepted by `--strict`.
`SSI_DBG=file SSI_DBG_SEL=1 work/bin/card_rtl ...` and `card_gss_dbg`
(`./build.sh dbg`) write the per-sample internal state of one voice in the
same format, for finding the first state that differs.

## Known open items

* WARMTH is not bit-exact, and since the filter-frequency control the
  card gates fail on it. GSSquared's `PhasorAudio::WarmthChannel` collapses
  the RTL's per-clock one-poles into one step per sample and has no voice
  latency. With FF=$80 the RTL's voice reaches the mixer 154 clocks after
  the tick and the card output differed by up to 93 LSB (27.7 RMS a window);
  with FF above $80 about half the samples take two tract passes and arrive
  293 clocks after the tick, and the residual grows to 169 LSB (52.4 RMS),
  over the sample (128) and window (40) gates in 35 of 177 cases, every one
  of them with FF above $80 (the $E6 synthetic cases, mb-audit's $E9, the
  TTS traces' $F5, the FF cases at $FF), while every socket sample is
  identical. Either WarmthChannel models the per-sample voice latency (the
  SSI-263 would report its passes, and mb2.cpp's mix and the mirror would
  change with it) or the gates are re-derived from the new residual (1.4
  times it: about 240 and 75). The owner decides; full-scale material is
  not gated either way.
* Coverage of CTLRUN, FCMUTE and Echo+. The fix ports CTLRUN (CTL=1 keeps
  the counters and core running) and FCMUTE (the noise-mix FC latched as 0
  while muted), but with the other causes modelled no case changes when
  either is switched off, so the harness does not check them. Echo+ mode
  is reached only by mb-audit's SSI-263 tests, for a few writes; no speech
  runs in it. A case that drives speech through CTL=1 phases, noise phones
  under mute, and Echo+ would close these.
* The tick-phase assumption. The harness pins the 48 kHz tick to the last
  fabric clock of an Apple cycle, after the access's data_en, with the
  sample cadence of GSSquared's accumulator, and starts both renderers'
  phases together. On the card the tick drifts against the Apple bus; a
  write landing within the backend pipeline of a tick can then start a
  phone one sample earlier or later. An earlier clocking that put the tick
  1-2 clocks after data_en moved one phone start in 4 TTS traces (up to
  8 LSB). The harness checks GSSquared against the RTL under this one
  phase, not across all of them.

## Results (2026-10-05, appletini-one d4d0499: the filter frequency)

appletini-one cae426f made register 4 (aliases 5-7) set the tract rate:
(128+FF)/256 of the 48 kHz rate, a Q8 phase giving each sample 0, 1 or 2
passes through F1..FX, FF=$80 the old response exactly (MB.md, SSI263.cpp
`schedulePasses`). Every reference changed (power-on FF=$00 is half rate;
most synthetic cases write $E6), and the suite gained 20 FF cases
(`gen_suite.py` section 16): the Phasor demo's init with Pitch
0A/80/E8/F5/FA, coarse and fine sweeps across the $80 bypass, writes every
6 cycles, FF written around phone writes, phone starts at every tick phase
at 00/E6/FF (the 0-, 1- and 2-pass abort windows), CTL ring-down and A=0,
Apple RESET with FF kept, an unwritten FF in Mockingboard mode, both
sockets with broadcast and aliases 5-7 in both modes, and all 64 phones at
00 and FF.

On the code before the port (8edd9700, 2ad4f50a's SSI263.cpp) a targeted
run of the FF cases, syn_filfreq_*, syn_phone_08, syn_broadcast and hello
gave 1 EXACT (syn_ff_demo_80, the neutral value) and 26 FAIL. After the
port all 27 had identical sockets and events on the first run.

`run_suite.py --strict` on 502467e5 (the port): 177 cases, SSI-263 (sockets
and events) 177 EXACT; card check 142 pass (8 sample-exact, the silent
cases), 35 FAIL; mirror check OK; STRICT: FAIL, exit 1. Every card failure
is the sample (128) or window (40) gate, worst 169 LSB (syn_ff_ctl_00) and
52.4 RMS (syn_phone_31), all in cases with FF above $80: the WARMTH residual
of "Known open items", not the speech. Every other card gate holds (noise
floor at most 3.6 RMS, level within 0.08 dB, routing, silence, lag).

The results below, and the attribution table, are from appletini-one
3101934, before the filter frequency had an audio effect; the attribution
builds patch d15bdbf9, which has no FF either, so they no longer match the
d4d0499 references.

`run_suite.py --strict` (3101934):

| Tree | Totals | SSI-263 | Card check | STRICT |
| --- | --- | --- | --- | --- |
| 2ad4f50a (feature/ssi263-match-appletini, the fix) | 157 EXACT | 157 EXACT | 157 pass (8 sample-exact) | PASS, exit 0 |
| d15bdbf9 (its parent, before the fix) | 8 EXACT, 0 PASS, 149 FAIL | 8 EXACT, 117 PASS, 32 FAIL | 8 pass, 149 FAIL | FAIL, exit 1 |

On 2ad4f50a every write, read and note is identical in all 157 cases, the
card noise floor is at most 3.4 RMS (gate 6), the worst card sample error
93 and window 27.7 (the WARMTH residual). The 8 sample-exact cards are the
silent cases.

On d15bdbf9, D7, IRQ and direct-IRQ timing was already exact (17,960
edges), but every speaking case failed the card check on routing (A6
primary into both channels at 0.707 instead of right only at unity; the
mirror for A5), level (-3.01 dB) and silence, 32 failed the SSI-263
comparison itself, and the two VIAREAD cases (mbaudit_ssiA/B) also on
reads (5 native T1C-L reads BB for BC each). The attribution builds found
the causes (cases differing / failing when the one flag is off, with all
the others on):

| Cause | Cases differing | FAIL | Worst |
| --- | --- | --- | --- |
| FREERUN (core gated by `active`) | 147 | 23 | log-mel 12.4 dB, level 3.4 dB |
| PITCHLAG | 11 (TTS) | 4 | SNR 2.5 dB, log-mel 3.9 dB, level 2.4 dB |
| START | 141 | 1 (onset) | 2,247 samples, SNR >= 40.7 dB |
| F1TAP0 | 42 | 1 (level 1.5 dB) | max error 570, SNR >= 31 dB |
| ATTACKLATE | 16 | 0 | 693 samples, SNR >= 57 dB |
| VIAREAD (W6522, not SSI) | 2 | 2 | 10 native T1C-L reads 1 lower |
| CTLRUN, FCMUTE | 0 | 0 | no effect once the others are modelled |

2ad4f50a ports all of them into GSSquared (SSI263.cpp; the RTL's routing
in `PhasorLogic::mixAudioSample` and its VIA read order in `readVia`).

To run the harness on another commit, check that commit out in a worktree
and bring this directory in (`git checkout feature/ssi263-match-appletini
-- tools/ssi263-harness`); `--strict` then tests that tree. That is how
the d15bdbf9 row is reproduced.
