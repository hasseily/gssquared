# Postprocessing reference validation

`postprocessreferencetest` compiles the original SuperDuperDisplay 0.9.1 CRT and bezel GLSL with macOS OpenGL 4.1, then compares their GPU output with the production Metal renderer. Neither reference shader is translated or edited by the test. Only the host-side framebuffer orientation is normalized so row zero means the top of both readbacks.

The reference files are copied from SuperDuperDisplay commit `b49a491671cb78d0a1fba1d1ba2c950f1a6c2455`:

| File | SHA-256 |
| --- | --- |
| `assets/shaders/postprocess/source_crt_reference.glsl` | `491c6e55967d88d33034eb0d5d4a3db0393963416de8000abe42a6ed8059afef` |
| `assets/shaders/postprocess/source_bezel_reference.glsl` | `7532e291345c339cce644cedb0f0fed90d83512f37d522dae0f1933226aa326c` |

The test reproduces the original application's sampler, alpha blending and uniform behavior, including transparent texture borders, nearest magnification, trilinear minification, nearest history sampling, and shader output dimensions before zoom. Bezel and glass images use linear sampling. Reflections use the original unprocessed image and mirrored repeat.

Asset loading preserves the original straight-alpha PNG bytes, including RGB under zero alpha. The original application uses `stbi_load`; all test and production asset loading goes through SDL_image's common memory-stream decoder. File-based native ImageIO decoding can premultiply and unpremultiply pixels, erasing hidden RGB and rounding translucent channels. That changed reflection edges and glass colors across platforms, so the golden suite also asserts exact transparent/translucent fixture pixels before rendering.

## Running it

On macOS, configure and build normally, then run:

```sh
cmake --build build --target postprocessreferencetest
ctest --test-dir build -R '^postprocess_reference$' --output-on-failure
```

To require both GPU backends and save results in a chosen directory:

```sh
./build/postprocessreferencetest ./build/resources /tmp/postprocessing-reference --require-gpu
```

The reference context requests an accelerated OpenGL visual and logs its vendor, renderer and version. Without `--require-gpu`, unavailable OpenGL/Metal hardware returns CTest's skip code 77; a software-only original OpenGL implementation does not qualify as the hardware reference. Compilation, shader errors and failed comparisons always fail. The test always writes `metrics.csv` and saves paired `*-reference.png` / `*-metal.png` images for failed comparisons. Add `--all-images` to save every comparison. Files are generated in the selected output directory, not checked into the repository.

Add `--benchmark` for presentation-inclusive 1080p and 4K measurements. Add `--boundary-probes` for the diagnostic zoom cases described below; those diagnostics are recorded but do not participate in the strict pass/fail total.

## Fixtures and tolerances

There are 35 CRT cases and nine final-composition cases, each tested over four deterministic bright/dark/moving frames: **176 image comparisons**. The normal input/output size is 1280×800, with an identical RGBA gradient, checker pattern and moving colored circle supplied to both renderers. The alternate scanline fixture uses a 1280×400 source displayed at 1280×800.

Cases cover both scanline styles, both masks and slot overlay, zoom, curvature, hard and soft corners, black level, all three color-space transforms, convergence, linear and perceptual color changes, vignette, phosphor blur/glow, grain, interlace, temporal ghosting, frame merging, bezel alpha, reflections, reflection outlines, glass, high glass thickness, and all six bundled presets. Preset zoom/translation and bezel placement are preserved. Because source and output dimensions are equal, original automatic scaling and fill-window select the same initial quad.

RGB errors are measured on the 0–255 byte scale. The test also computes mean luminance SSIM over non-overlapping 8×8 windows, using luminance weights 0.2126/0.7152/0.0722 and standard constants C1=6.5025 and C2=58.5225.

One byte of render-target rounding is allowed per RGB channel across backends. Comparisons with no larger error pass directly; other non-grain comparisons must meet **every** threshold:

- Mean residual RGB error ≤0.15 after subtracting the one-byte rounding allowance from each absolute difference (with a minimum of zero).
- RGB root mean square error ≤3.
- At most 0.5% of RGB channels differ by more than three byte values.
- Luminance SSIM ≥0.995.

Grain uses a floating-point sine hash, whose phase can change with driver arithmetic. Its thresholds are mean absolute error ≤3, RMS error ≤5, SSIM ≥0.97, and absolute signed mean RGB error ≤0.2. The last check prevents missing grain or a wrong average grain strength from passing merely because individual noise pixels differ.

The sparse-error allowance covers hard discard boundaries and nearest-sample ties: a one-ULP coordinate difference can select another side of a high-contrast edge. It does not excuse broad image changes. The suite caught missing smooth-corner blending, incorrect mask density after zoom, incorrect border filtering, undefined derivatives inside a varying branch, premature glass-alpha clamping, and platform-dependent PNG alpha conversion; those defects were corrected before the results below.

## Defined comparison domain

The original application did not initialize its first ghost-history image. Both renderers are warmed with the same unghosted frame before temporal comparisons. Fresh/reset history behavior is tested separately by `postprocesstest`.

The original alternate-scanline shader discards alternate rows before implicit-derivative texture sampling. At exactly 1:1 scale, that can remove one lane of each derivative pair and produce driver-dependent coarse-mip samples. The strict fixture uses 2× vertical scaling so the original shader has defined neighboring samples. The production renderer also has explicit history/reset and field-composition tests.

At exact 0.75 zoom, the original slot-mask `fract` thresholds coincide with many pixel centers. OpenGL's transformed-quad interpolation and Metal's reconstructed coordinates can differ by one ULP and choose opposite bands. The strict geometry fixture uses 0.70931 zoom; `--boundary-probes` retains the 0.75 case to expose this discontinuity rather than hiding it behind a broad tolerance. It remains a known pixel-phase difference at exact threshold ties, not a mask-density difference.

The original UV discard also makes implicit texture derivatives driver-dependent at a fractional quad edge when a two-pixel derivative pair straddles that edge. At 0.713 zoom, SwiftShader differs from Apple hardware primarily on the first/last drawn rows; excluding only the one-pixel inside boundary reduces SwiftShader RGB RMS from 4.110 to 0.933. The strict 0.70931 fixture instead aligns raster bounds to complete 2×2 derivative groups and avoids repeated exact mask-phase ties. It still compares every pixel, with unchanged tolerances. The 0.713 case remains available as `boundary_derivative_zoom`.

The port deliberately uses sign-preserving cube roots to keep perceptual color calculations defined when black-level adjustment makes a channel negative. The original shader's fractional `pow` is undefined for negative inputs; numerical reference comparisons do not require preserving its NaNs. Transparent glass regions are similarly kept defined when both input alphas are zero.

The live reference suite compares original OpenGL with Metal on the same Mac. A compact original-output corpus also lets each production backend replay the same reference images without needing original OpenGL on that platform, as described below.

## Recorded results

Validated on an **Apple M3 Pro, macOS 26.5.1**, using stock SDL 3.4.16:

- 176 strict comparisons passed.
- Worst mean absolute RGB error: **0.077404/255** (convergence).
- Worst RGB RMS error: **2.60364/255** (sparse hard-corner boundary pixels).
- Lowest luminance SSIM: **0.998982** (convergence).
- Worst fraction of RGB channels differing by more than three: **0.278711%** (grain).
- The four presets containing bezel/glass assets, after final composition: mean absolute error ≤**0.0024/255**, SSIM ≥**0.999964**.
- Neutral rendering, alternate scanlines at 2×, ordinary masks, linear history, simple bezel alpha, reflection outlines, ordinary glass, and high-thickness glass matched byte-for-byte in the recorded run. Soft corners, synthetic reflections and perceptual history differed by at most one byte.

The benchmark uses the Apple CRT preset with phosphor blur 0.5, ghosting 50%, and perceptual color enabled. It warms ten frames and measures 120 frames without screenshot readback in the timed loop. A readback before timing verifies the actual CRT target dimensions, and a separate acquired swapchain texture verifies the presentation dimensions.

| CRT target and acquired swapchain | Wall time per frame |
| --- | --- |
| 1920×1080 | 8.388 ms |
| 3840×2160 | 8.353 ms |

These times include scene submission, effects, composition and presentation. They are consistent with a 120 Hz presentation limit and are **not isolated shader execution times**. They show this fixture staying within a 60 Hz frame budget on the tested Mac, not a performance guarantee for other devices.

## Portable backend comparisons

`postprocessgoldentest` replays 16 representative cases through the production renderer and compares 21 lossless PNG checkpoints with output captured from the **original** shaders. It renders every intermediate frame, including identical history warmup; static cases retain their last frame, while temporal ghosting and frame merging retain multiple checkpoints. Cases include all six built-in presets, smooth corners, zoomed masks, phosphor blur, linear/perceptual history, field merging, reflections, glass and high glass alpha. The input generator and metric implementation are shared with the live reference test in `apps/postprocessfixtures/Fixture.hpp`.

The corpus is about 8.6 MB under `apps/postprocessgoldentest/goldens`. It is test data and is not installed as application resources. The manifest records the original commit and shader hashes, every setting, explicit asset locations, timestamps and source-frame phases. Renderer output must never be used to update these expectations.

Native build and strict execution:

```sh
cmake --build build --target postprocessgoldentest
./build/postprocessgoldentest --require-gpu
```

`--require-gpu` requires a native SDL GPU device, so an unavailable Metal, Vulkan or Direct3D backend cannot silently pass by falling back to OpenGL. Linux OpenGL fallback is tested separately:

```sh
SDL_GPU_DRIVER=unavailable ./build/postprocessgoldentest --require-opengl
```

The CI workflow explicitly selects Vulkan and Direct3D 12 for their native jobs, and runs Metal on macOS. `GS2_TEST_RESOURCE_PATH` and `GS2_TEST_GOLDEN_PATH` override data directories when needed. `GS2_TEST_ARTIFACT_DIR` writes `golden-metrics.csv` and failure PNGs.

Emscripten builds the same target as `postprocessgoldentest.html`, preloading only postprocessing resources and golden data. It advances one replay frame per main-loop callback and reports `window.gs2GoldenTest` with `status`, `comparisons`, `failures` and `backend`. Browser automation must wait for `status: "passed"`, `comparisons: 21`, and `failures: 0`; a page load alone is not success.

Regenerate expectations only on a Mac with the original reference shaders available:

```sh
./build/postprocessreferencetest ./build/resources /tmp/postprocessing-reference \
  --require-gpu --export-goldens ./apps/postprocessgoldentest/goldens
```

Review the changed reference images and manifest before committing. The exporter writes expectations directly from original OpenGL readbacks, never from the production renderer. Both reference and portable suites must then pass. Changes to original shader provenance or fixture definitions require a corresponding manifest/version review.

The portable suite passed all 21 checkpoints locally on Metal and in every browser configuration below after correcting asset decoding and using the final 0.70931 fixture. The same tolerances apply to every backend; no pixels are excluded.

| Browser configuration | Reported renderer | Passed | Highest RGB RMS | Lowest SSIM |
| --- | --- | --- | --- | --- |
| Chromium software | ANGLE Vulkan, SwiftShader LLVM 10 | 21/21 | 0.956764 | 0.998212 |
| Chromium hardware | ANGLE Metal, Apple M3 Pro | 21/21 | 0.943990 | 0.999564 |
| Firefox | Apple M1, or similar (privacy-masked identity) | 21/21 | 0.943990 | 0.999564 |
| WebKit | Apple GPU | 21/21 | 0.943990 | 0.999564 |

The browser runner saves renderer identity in `result.json` alongside per-case metrics and failure images under `build/smoke/<engine>/goldens`. Native Vulkan, OpenGL and Direct3D results are established by their own required CI executions, not inferred from these Metal/WebGL results.
