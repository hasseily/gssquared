# Appletini and display validation

`.github/workflows/validate.yml` builds the exact pushed commit (or pull request
head, rather than a synthetic merge commit) on macOS ARM64, macOS Intel,
Windows MinGW64, Linux, and Emscripten. Each native build runs all registered
CTest cases and `scripts/test_appletini_guest.py` against the built emulator.
ARM64 macOS, Windows and Linux additionally require the actual GPU regression
and comparisons with original-shader golden images to pass. The hosted Intel
Mac has no supported GPU, so that job validates the CPU build, guest behavior
and package; it reports this limitation explicitly. Its downloaded Intel
graphics tests and app can be run under Rosetta on an Apple Silicon Mac to
exercise the x86 executable with Metal. That is a translated CPU test on an
Apple GPU, not a test on Intel graphics hardware. The guest smoke test
boots a fresh Appletini machine, calls the bundled SmartPort ROM with 6502
code, and checks RAM32, mounted unit 8, 2MG offsets, reset, RamWorks mapping,
and LINTXT detection. It uses the debugger's QUIT command on completion.
Build jobs fetch the repository history so the commit-count package version
matches local builds; third-party submodules remain shallow pinned checkouts.

The shader producer runs first on Linux. `scripts/ci/install_shader_tools.py`
builds exact glslang and SPIRV-Cross revisions outside the source tree and
installs an official DXC archive verified by SHA256. It then runs
`scripts/shaders/compile_postprocess.py` to compile all four stages to
SPIR-V, Metal, HLSL, DXIL, desktop GLSL, and GLSL ES. The artifact includes a
manifest binding every source and output to the exact Git commit. Native and
web jobs verify that manifest before packaging; the Windows job also compiles
and validates HLSL with native Windows DXC. Runtime packages need no shader
compiler installation.

The browser test serves the real package with COOP/COEP headers, starts an
Appletini system under Chromium, Firefox, and Playwright WebKit using WebGL2,
loads a full effects preset, toggles
effects, reloads saved settings from IDBFS, checks backing-buffer resizing,
and exercises `WEBGL_lose_context`. Recovery must preserve RAM and PC, restore
the settings panel and effects, and produce a nonblank frame. The static
panel pixels are compared before and after recovery. Screenshots and browser
logs are retained as workflow artifacts.
The same test uses the canvas controls to select a preset, type a numeric
value, scroll, save a new preset, cancel and reopen the import picker, reject
malformed JSON without interrupting emulation or changing settings, import a
valid JSON file, and verify the exported browser download.
Playwright WebKit covers that browser engine; it is not a Safari product test.
Linux CI runs Firefox with `--headed` under Xvfb so Mesa can provide its GL
context. Canvas geometry is checked across six samples before viewport crops
are captured, independently of Playwright's animated-element stability wait.
The backing dimensions must match CSS size times device pixel ratio; headed
Retina Firefox is also tested at DPR 2 (1288×928 CSS, 2576×1856 backing).
The web build retains the debugger model without creating its native window,
which would otherwise resize the shared emulator canvas.
`renderresourcetest` independently replaces the renderer three times and
compares the exact frame, asset atlas, and font-rendering output.

Every build checks that the committed vendored revisions and tracked source
files remain unchanged. SDL 3.4.16 is fetched into the build directory using
the pinned archive hash; it does not replace or patch `vendored/SDL`.

The [Postprocessing reference validation](PostprocessingReference.md) describes
the independent original-shader comparisons, numerical tolerances, known
floating-point boundary differences, and measured 1080p/4K performance.

## Local commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGS2_PROGRAM_FILES=ON -DGS2_BUILD_NATIVE=ON
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure --no-tests=error
GS2_TEST_RESOURCE_PATH="$PWD/build/resources" build/postprocesstest --require-gpu
python3 scripts/test_appletini_guest.py --executable build/GSSquared
```

Use `GSSquared.exe` on Windows from the documented MSYS2 MinGW64 environment.
Linux GUI tests need a display; `xvfb-run -a` with Mesa's Vulkan/OpenGL software
drivers provides one on headless machines. The ordinary CTest GPU case can
skip when there is no GPU/display; CI runs the explicit `--require-gpu` check
to prevent a skip from counting as graphics validation.
Linux also forces the OpenGL fallback and runs the same pixel regressions with
`SDL_GPU_DRIVER=unavailable build/postprocesstest --require-opengl` under Xvfb.

For a prebuilt Emscripten package:

```sh
python3 -m pip install playwright==1.59.0 pillow==11.3.0
python3 -m playwright install chromium firefox webkit
python3 scripts/ci/browser_smoke.py --build build-web --browser chromium --output build-web/smoke/chromium
# Build postprocessgoldentest too when running original-shader image comparisons.
python3 scripts/ci/browser_smoke.py --build build-web --golden-only --browser chromium --output build-web/smoke/chromium/goldens
```

Native jobs also create and smoke-test the installed macOS app (with a DMG),
the extracted Windows ZIP, and the Linux AppImage. The web ZIP includes a
local Python server with the required COOP/COEP headers. Linux packaging uses
a fixed linuxdeploy release checked by SHA256. These packages are validation
outputs; the macOS app has an ad hoc signature and is not notarized.
The Windows package includes runtimes located beside the selected MinGW
compiler. Its smoke test removes toolchain directories from the emulator's
PATH so an incomplete ZIP cannot borrow DLLs from the build environment.
