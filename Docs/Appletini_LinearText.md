# Appletini linear text overlay

The Appletini virtual card implements the **Linear RAM Text Overlay 1.0**
interface from Appletini ONE firmware **F1.0.8** (`c5044416`). It shares
slot 7 with SmartPort: SmartPort uses its slot and expansion ROM, while
the text registers use `$C0F0–$C0FF`. No extra card or slot is installed.
VOC and Second Sight remain separate GSSquared cards with their own APIs.

This is a text display interface. Guest software parses VT100/ANSI escape
sequences, scrolls its buffer and maps bold/reverse video into cell colors.
The card supplies the VT100 character map, DEC Special Graphics, underline,
CP437, 8×14/8×16 fonts, 16 VGA colors, blink and a cursor.

## How this differs from VOC and Second Sight

| | Appletini linear text | GSSquared VOC | Second Sight |
|---|---|---|---|
| Guest interface | Native Appletini slot-7 INDEX/DATA/CMD/STATUS registers, `LINTXT` identity | Independent IIgs Video Overlay Card slot registers (normally C0B0–C0BF) | Independent VGA card registers, ROM and memory |
| Source | A captured linear character/attribute buffer in Apple main or base aux RAM | IIgs E0/E1 SHR bitmap fields and their palettes | VGA framebuffer/text state; optional host rendering of Apple text |
| Current output | Font grid over the existing Apple picture, with transparent backgrounds, cursor, blink and clipping | 640×400 interlaced SHR composition and field-status reporting | Its VGA/text rendering paths |
| Terminal behavior | Guest software handles ANSI/VT100 escape sequences and scrolling; hardware supplies glyphs/attributes | No terminal parser, character grid or font/attribute ABI | Separate VGA text semantics |
| Slot ownership | Part of the Appletini already in slot 7 | Explicitly configured separate card | Explicitly configured separate card |

VOC does not implement the Appletini VT100 interface. Reusing its register or
memory model would break both guest APIs. Its existing 640×400 renderer is useful
as a bitmap-composition reference. The shared implementation is the host video
path: normal Apple/VOC/Second Sight output is composed first, Appletini text is
then added, and the same postprocessing and renderer-resource recovery applies.
The card-specific registers, memory routing and glyph rendering remain separate.

The upstream VOC implementation currently stores several overlay/color controls
without rendering those effects, and its VBL-interrupt clear is a stub. Adding
Appletini text does not claim to complete physical VOC external-video/genlock
functionality. Neither VOC nor Second Sight is automatically installed by
Appletini.

## Programming

Each cell is two bytes: character followed by attribute, with no row padding.
The buffer may start at any address from `$0200` and must end below `$C000`.
It belongs to either main RAM or the base auxiliary bank. The card observes
actual RAM write routing, including RAMWRT, 80STORE and PAGE2. Nonzero
RamWorks banks and unshadowed IIgs fast RAM are not captured.

1. Read `$C0FE == $4C`, `$C0FF == $10` and `$C0F8–$C0FD == "LINTXT"` to detect.
2. Set INDEX at `$C0F0`, then use DATA (`$C0F1`) or DATA_INC (`$C0F2`).
3. Stage base, bank/font options, columns/rows, origin and scale.
4. Write ARM (`1`) to CMD (`$C0F3`), wait for BUSY to clear and check CONFIG_ERROR.
5. **Write every desired cell after ARM finishes.** The card clears a private
   shadow buffer. It cannot read bytes that were already in RAM before ARM.
6. Write SHOW (`2`) and wait for FRAME_PENDING to clear at an output frame.
7. Write HIDE (`3`) to hide while continuing capture, or OFF (`0`) to hide and
   stop capture. Wait for FRAME_PENDING before exiting guest software.

STATUS at `$C0F4`: bit 7 BUSY, 6 STALE, 5 CONFIG_ERROR, 4 FRAME_PENDING,
1 ARMED, 0 VISIBLE. An invalid ARM retains the previous armed/active buffer.
ARM is ignored while a frame handoff is pending. All commands are ignored
while BUSY is set. A new valid ARM clears STALE and CONFIG_ERROR.

The emulator completes an ARM after 32 emulated CPU cycles. This models the
firmware's asynchronous buffer clearing; software must poll rather than
depend on the duration. Capture is synchronous and lossless in the emulator.
The core still implements STALE/disarm/frame-edge hide for an explicit
capture-loss event.

| Index | Value |
|---|---|
| `$00–01` | Buffer base, little endian |
| `$02` | CONFIG: bit 0 aux, 1 CP437, 2 font16, 3 attribute blink, 4 transparent |
| `$03–04` | Columns (1–255), rows (1–127) |
| `$05–08` | X/Y origin, little endian |
| `$09` | Scale: high nibble vertical, low nibble horizontal (each 1–15) |
| `$0A–0C` | Live cursor X, Y, control |
| `$0D–0E` | Shadow fill character/attribute |
| `$10–13` | Current canvas width/height, little endian |
| `$14–1D` | Active base/config/geometry/origin/scale |
| `$1E` | Capabilities (`$7F`) |

The canvas is 1120×768 in legacy video and 1280×800 in SHR, excluding
border/bezel. Origin and scale are canvas pixels, independent of window size.
The overlay clips at the canvas edge and does not resize its grid on a mode
change. The host compositor combines the normal Apple picture and text before
display postprocessing, then draws the emulator controls.

VT100 mode uses low seven character bits for ASCII/DEC and bit 7 for
underline. Codes `$00–1F` map to DEC Special Graphics `$5F–7E`; `$7F` is blank.
CP437 uses all eight bits for glyphs. Attributes use foreground in bits 3–0;
background in bits 6–4, extended by bit 7 when blink mode is off. Blink and
the blinking cursor share a 500 ms on/500 ms off phase. Cursor control bits
0/1 enable and blink; bits 3–2 select block, underline or left bar.

Fixed bitmaps come from the firmware's Terminus Font 4.49.1 tables. The
copyright and SIL Open Font License are in `assets/licenses/Terminus-OFL.txt`.

## Validation

`ctest --test-dir build -R appletinitexttest --output-on-failure` checks the
register/state contract, old versus captured RAM contents, staged handoffs,
invalid ranges, reset/loss behavior, font/color/transparency/cursor pixels,
scaling/clipping and blink timing. It also exercises actual MMU writes,
including IIgs direct E1/SHR-linear/shadow memory and 80STORE bank selection.
