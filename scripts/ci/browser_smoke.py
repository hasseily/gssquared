#!/usr/bin/env python3
"""Run the packaged Emscripten app in Chromium/WebGL2 with real shader loading.

Requires `pip install playwright==1.59.0 pillow==11.3.0` and
`python -m playwright install chromium`. Saves screenshots and the console log.
"""
from __future__ import annotations

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading

from PIL import Image, ImageChops, ImageStat
from playwright.sync_api import sync_playwright


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-web"))
    parser.add_argument("--output", type=Path, default=Path("build-web/smoke"))
    parser.add_argument("--browser-executable", type=Path,
                        help="Optional existing Chromium/Chrome executable for local testing")
    args = parser.parse_args()
    if not (args.build / "GSSquared.html").is_file():
        raise SystemExit("Packaged GSSquared.html missing")
    args.output.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(args.build.resolve())))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    logs: list[str] = []
    page_errors: list[str] = []
    # Install a preset immediately before main(), after the shell finishes
    # restoring IDBFS. This exercises actual preset loading and full effects.
    initialize = r"""
        let value;
        Object.defineProperty(window, 'Module', {
            configurable: true,
            get() { return value; },
            set(m) {
                value = m;
                if (!m || m.gs2CiConfigured) return;
                m.gs2CiConfigured = true;
                m.arguments = ['/resources/gs2/IIe_Appletini.gs2'];
                const prior = m.onRuntimeInitialized;
                m.onRuntimeInitialized = function () {
                    // Only seed the first boot. A subsequent reload must read
                    // the settings the real application saved through IDBFS.
                    if (!sessionStorage.getItem('gs2CiPresetInstalled')) {
                        FS.mkdirTree('/postprocess');
                        FS.writeFile('/postprocess/current.json', JSON.stringify({
                            preset_name: 'CI full effects', p_i_postprocessingLevel: 2,
                            p_f_scanlineWeight: 0.65, p_i_maskType: 2,
                            p_f_barrelDistortion: 0.08, p_f_vignetteWeight: 0.1,
                            p_f_ghostingPercent: 15, p_f_phosphorBlur: 0.1
                        }));
                        sessionStorage.setItem('gs2CiPresetInstalled', '1');
                    }
                    if (prior) prior.apply(this, arguments);
                };
            }
        });
    """
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(
                executable_path=str(args.browser_executable) if args.browser_executable else None, args=[
                "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader",
            ])
            page = browser.new_page(viewport={"width": 1288, "height": 928})
            page.on("console", lambda msg: logs.append(f"{msg.type}: {msg.text}"))
            page.on("pageerror", lambda error: page_errors.append(str(error)))
            page.add_init_script(initialize)
            try:
                page.goto(f"http://127.0.0.1:{server.server_port}/GSSquared.html", wait_until="domcontentloaded")
                page.wait_for_function("window.runtimeReady === true", timeout=120_000)
                page.locator("#overlay").click()
                page.wait_for_timeout(3000)
                assert page.evaluate("crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined'"), "No pthread isolation"
                version = page.evaluate("document.getElementById('canvas').getContext('webgl2').getParameter(0x1F02)")
                assert "WebGL 2" in version, version
                assert not page_errors, page_errors
                assert any("Postprocessing: WebGL2" in line for line in logs), "SuperDuperDisplay WebGL2 backend did not initialize"
                assert any("Appletini RamWorks: 8MB auxiliary expansion enabled" in line for line in logs), "Appletini guest configuration did not start"
                failures = [line for line in logs if any(term in line for term in
                    ("Postprocessing shader:", "Postprocessing shader link:", "Postprocessing unavailable", "Postprocessing presentation failed"))]
                assert not failures, failures
                page.locator("#canvas").screenshot(path=str(args.output / "effects-on.png"))
                page.keyboard.press("F7")
                page.wait_for_timeout(1000)
                page.locator("#canvas").screenshot(path=str(args.output / "effects-toggled.png"))
                rendered = Image.open(args.output / "effects-on.png").convert("RGB")
                assert max(hi - lo for lo, hi in rendered.getextrema()) > 32, "Canvas is blank"
                toggled = Image.open(args.output / "effects-toggled.png").convert("RGB")
                assert ImageChops.difference(rendered, toggled).getbbox(), "F7 did not change the rendered image"
                assert page.evaluate("Module.gs2PostprocessPersistent === true"), "IDBFS settings storage unavailable"
                page.wait_for_function("!Module.gs2PostprocessSyncBusy && !Module.gs2PostprocessSyncDirty")
                assert page.evaluate("JSON.parse(FS.readFile('/postprocess/current.json', {encoding:'utf8'})).p_i_postprocessingLevel") == 0
                page.reload(wait_until="domcontentloaded")
                page.wait_for_function("window.runtimeReady === true", timeout=120_000)
                page.locator("#overlay").click()
                page.wait_for_timeout(1000)
                assert page.evaluate("JSON.parse(FS.readFile('/postprocess/current.json', {encoding:'utf8'})).p_i_postprocessingLevel") == 0, "Saved effects setting was not restored from IDBFS"
                # Resizing exercises FBO/texture reallocation and persistent settings.
                previous_size = page.evaluate("[Module.canvas.width, Module.canvas.height]")
                page.set_viewport_size({"width": 960, "height": 720})
                page.wait_for_timeout(1000)
                assert page.evaluate("[Module.canvas.width, Module.canvas.height]") != previous_size, "Canvas backing buffer did not resize"
                page.locator("#canvas").screenshot(path=str(args.output / "resized.png"))
                assert not page_errors, page_errors
                # Recover the real browser context with effects and the live
                # settings panel visible. The application checks guest RAM/PC
                # before and after resource rebuilding at the frame boundary.
                page.keyboard.press("F7")
                page.keyboard.press("Shift+F7")
                page.wait_for_timeout(500)
                page.locator("#canvas").screenshot(path=str(args.output / "settings-open.png"))
                previous_recoveries = page.evaluate("Module.ccall('gs2_webgl_recovery_count', 'number', [], [])")
                page.evaluate("""() => {
                    const gl = document.getElementById('canvas').getContext('webgl2');
                    window.gs2ContextLoss = gl.getExtension('WEBGL_lose_context');
                    if (!window.gs2ContextLoss) throw Error('WEBGL_lose_context unavailable');
                    window.gs2ContextLoss.loseContext();
                }""")
                page.wait_for_function("document.getElementById('canvas').getContext('webgl2').isContextLost()")
                page.wait_for_timeout(250)
                page.evaluate("window.gs2ContextLoss.restoreContext()")
                page.wait_for_function(
                    "count => Module.ccall('gs2_webgl_recovery_count', 'number', [], []) > count",
                    arg=previous_recoveries, timeout=30_000)
                assert page.evaluate("Module.ccall('gs2_webgl_preserved_state', 'number', [], [])") == 1, "Context recovery changed guest RAM/PC"
                page.wait_for_timeout(1000)
                page.locator("#canvas").screenshot(path=str(args.output / "context-restored.png"))
                restored = Image.open(args.output / "context-restored.png").convert("RGB")
                assert max(hi - lo for lo, hi in restored.getextrema()) > 32, "Restored canvas is blank"
                panel = Image.open(args.output / "settings-open.png").convert("RGB")
                # The opaque panel interior is static even while the guest's
                # display and phosphor history continue to animate behind it.
                panel_area = (40, 40, panel.width - 40, min(580, panel.height - 100))
                panel_delta = ImageStat.Stat(ImageChops.difference(panel.crop(panel_area), restored.crop(panel_area)))
                assert max(panel_delta.mean) < 1, "Settings panel textures were not restored intact"
                assert not page_errors, page_errors
                assert not any("Postprocessing unavailable" in line or "Postprocessing presentation failed" in line for line in logs), logs
                print(json.dumps({"backend": version, "preset_persistence": "retained after reload", "context_recovery": "guest state and settings panel retained", "screenshots": str(args.output)}, indent=2))
            finally:
                page.screenshot(path=str(args.output / "final-page.png"))
                browser.close()
    finally:
        (args.output / "console.log").write_text("\n".join(logs + page_errors) + "\n")
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    main()
