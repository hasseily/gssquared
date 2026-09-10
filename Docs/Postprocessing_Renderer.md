# Postprocessing renderer

The built-in effects are ported from SuperDuperDisplay 0.9.1 (`b49a491`). The shared
GLSL sources are under `assets/shaders/postprocess`; the original CRT and bezel
sources are retained beside them for review. The CRT source retains its GPL-2.0-or-later
attribution. The SuperDuperDisplay MIT notice for the bezel code/assets is installed with the other
licenses.

## Rendering

`gs2_postprocess` owns the effect resources. On macOS, Windows and Linux it creates
an SDL GPU device (Metal, D3D12 or Vulkan), then an offscreen SDL renderer on the same
device. App-owned, mipmapped scene textures are imported through SDL's public texture
properties. Existing display devices and the host UI keep using SDL rendering.

Each frame contains these steps:

1. Draw the decoded Apple picture and optional Appletini text to the scene. The
   scene retains at least 1280 by 800 pixels so firmware text is preserved before
   postprocessing. Sampling dimensions, decoded dimensions and logical scanline
   count are separate metadata.
2. Draw host menus, controls and messages to a transparent UI target.
3. Generate the scene mip chain, run the CRT shader, and retain its result in one
   of two history textures. Phosphor history is independent of host UI and bezels.
4. Composite the CRT, bezel reflections, optional glass, and host UI, then present.

There is no per-frame native CPU readback. Screenshot capture performs an additional
GPU composition and readback only on request; it includes the CRT, bezel and glass
at output resolution and excludes host UI. `capture_crt()` exposes the earlier CRT
pass for regression tests.

The browser uses WebGL2/GLSL ES 3.00 with matching passes. Linux can use the OpenGL
3.3 fallback, including Mesa software implementations, if the native GPU path is
unavailable. These paths use SDL's public external-GL-texture properties and
`SDL_FlushRenderer()` to coordinate renderer state. A last-resort plain SDL renderer
keeps the emulator usable and reports that effects are unavailable.

History is cleared after settings, sampling geometry, field composition, output
size or renderer changes. Already-composed Appletini fields bypass the explicit
pair-merge control, preventing a second average. Host UI is never retained in
phosphor history. Absolute IIgs mouse coordinates use the same image zoom,
translation, curvature and barrel distortion as rendering.

Missing or unreadable bezel/glass assets use a transparent fallback and report
the error without retrying file loads each frame. Explicitly reloading the
preset retries the same paths after files are repaired; renderer recreation
also reloads assets.

## Dependency and shader builds

The application uses unmodified SDL 3.4.16 from the pinned release archive outside
`vendored/`; no SDL source patches are required. For an offline/local build,
`GS2_SDL_SOURCE_DIR` can point to an existing unmodified release checkout. Equivalent
`GS2_SDL_IMAGE_SOURCE_DIR`, `GS2_SDL_TTF_SOURCE_DIR` and `GS2_SDL_NET_SOURCE_DIR` options
allow reuse of initialized companion sources.

Regenerate all packaged shader formats with:

```sh
python3 scripts/shaders/compile_postprocess.py \
  --glslang /path/to/glslangValidator \
  --spirv-cross /path/to/spirv-cross \
  --dxc /path/to/dxc
```

The reference tools are glslang 16.5.0, SPIRV-Cross
`be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a`, and DXC v1.9.2607. GLSL is compiled to
SPIR-V, then translated into MSL, HLSL, desktop GLSL and GLSL ES. DXC produces DXIL
from HLSL. `--without-dxil` is a Mac development convenience; it does not produce
complete Windows release artifacts. Changes to shared GLSL must regenerate all
formats before release. The reference files are documentation, not shader inputs.

## Regression checks

`postprocesstest --require-gpu` checks real GPU rendering: quadrant orientation and
colors, complete imported SuperDuperDisplay presets, retained visible CRT content, final bezel/glass
composition, mipmaps, phosphor history/reset, the composed-field merge guard, mouse
geometry, and renderer recreation. `GS2_TEST_ARTIFACT_DIR` stores preset BMP fixtures;
`GS2_TEST_RESOURCE_PATH` overrides the resource directory. Plain CTest skips this
test with code 77 when no desktop GPU renderer is available; release checks must
use `--require-gpu` so an unavailable backend cannot pass unnoticed.

`--benchmark` additionally reports wall-frame timing for 180 1080p frames with CRT,
mipmapped blur and ghosting. It includes submission/presentation and should not be
interpreted as an isolated shader timing.

Separate preset, render-resource, capture and Appletini tests exercise configuration,
texture recreation, large output-resolution captures, text commands, fields and the
card protocol. Platform CI must execute the appropriate native/browser runtime checks;
compiling a shader variant alone does not establish runtime parity.
