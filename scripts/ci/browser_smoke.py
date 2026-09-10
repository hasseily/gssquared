#!/usr/bin/env python3
"""Run the packaged Emscripten app in a browser with real WebGL2 shader loading.

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
    parser.add_argument("--browser", choices=("chromium", "firefox", "webkit"), default="chromium")
    parser.add_argument("--browser-executable", type=Path,
                        help="Optional existing executable for the selected browser engine")
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
            launch_args = [
                "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader",
            ] if args.browser == "chromium" else []
            browser = getattr(playwright, args.browser).launch(
                executable_path=str(args.browser_executable) if args.browser_executable else None,
                args=launch_args)
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
                canvas_box = page.locator("#canvas").bounding_box()
                assert canvas_box is not None
                panel_width = min(600, int(canvas_box["width"]) - 24)
                panel_height = min(760, int(canvas_box["height"]) - 24)
                panel_x = canvas_box["x"] + canvas_box["width"] - panel_width - 12
                panel_y = canvas_box["y"] + (canvas_box["height"] - panel_height) / 2

                def click_action(index: int) -> None:
                    button_width = (panel_width - 36) / 3 - 6
                    page.mouse.click(panel_x + 16 + (index % 3) * (button_width + 6) + button_width / 2,
                                     panel_y + 100 + (index // 3) * 34)

                def current_settings() -> dict:
                    return page.evaluate("JSON.parse(FS.readFile('/postprocess/current.json', {encoding:'utf8'}))")

                # Exercise the real canvas controls, including the SDL-to-web
                # file picker and browser download adapter.
                click_action(1)  # Next preset
                page.wait_for_timeout(600)
                assert current_settings()["preset_name"] != "CI full effects", "Next preset button did not load a preset"
                # The first row is the effects level. Clicking its right side
                # places the caret after the existing value.
                def edit_level(value: str) -> None:
                    page.mouse.click(panel_x + panel_width - 52, panel_y + 225)
                    for _ in range(4):
                        page.keyboard.press("Backspace")
                    page.keyboard.type(value)
                    page.keyboard.press("Enter")
                    page.wait_for_timeout(600)

                edit_level("1")
                assert current_settings()["p_i_postprocessingLevel"] == 1, "Numeric effects editor did not apply the typed value"
                edit_level("2")
                assert current_settings()["p_i_postprocessingLevel"] == 2
                saved_count = page.evaluate("FS.readdir('/postprocess/presets').filter(name => name.endsWith('.json')).length")
                click_action(2)  # Save new
                page.wait_for_timeout(600)
                assert page.evaluate("FS.readdir('/postprocess/presets').filter(name => name.endsWith('.json')).length") == saved_count + 1
                page.locator("#canvas").screenshot(path=str(args.output / "controls-before-scroll.png"))
                page.mouse.move(panel_x + 200, panel_y + 350)
                page.mouse.wheel(0, 400)
                page.wait_for_timeout(300)
                page.locator("#canvas").screenshot(path=str(args.output / "controls-scrolled.png"))
                before_scroll = Image.open(args.output / "controls-before-scroll.png").convert("RGB")
                after_scroll = Image.open(args.output / "controls-scrolled.png").convert("RGB")
                controls_area = (int(panel_x - canvas_box["x"] + 20), int(panel_y - canvas_box["y"] + 195),
                                 int(panel_x - canvas_box["x"] + panel_width - 40),
                                 int(panel_y - canvas_box["y"] + panel_height - 90))
                assert ImageChops.difference(before_scroll.crop(controls_area), after_scroll.crop(controls_area)).getbbox(), "Control list did not scroll"
                with page.expect_file_chooser():
                    click_action(3)  # Import, then cancel
                page.locator("input[type=file]").dispatch_event("cancel")
                page.wait_for_timeout(300)
                with page.expect_file_chooser() as picker:
                    click_action(3)
                imported = {"preset_name": "CI imported preset", "p_i_postprocessingLevel": 2,
                            "p_f_brightness": 1.2, "p_i_maskType": 2}
                picker.value.set_files({"name": "browser-preset.json", "mimeType": "application/json",
                                        "buffer": json.dumps(imported).encode()})
                page.wait_for_timeout(600)
                assert current_settings()["preset_name"] == imported["preset_name"], "Browser preset import did not apply"
                with page.expect_download() as download:
                    click_action(4)  # Export
                export_path = args.output / "exported-preset.json"
                download.value.save_as(export_path)
                assert json.loads(export_path.read_text())["preset_name"] == imported["preset_name"], "Browser export differs from the active preset"
                page.wait_for_function("!Module.gs2PostprocessSyncBusy && !Module.gs2PostprocessSyncDirty")
                page.wait_for_timeout(600)
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
                left = int(panel_x - canvas_box["x"])
                top = int(panel_y - canvas_box["y"])
                panel_area = (left + 24, top + 24, left + panel_width - 24,
                              top + min(580, panel_height - 100))
                assert min(ImageStat.Stat(panel.crop(panel_area)).mean) > 160, "Postprocessing settings panel did not open"
                panel_delta = ImageStat.Stat(ImageChops.difference(panel.crop(panel_area), restored.crop(panel_area)))
                assert max(panel_delta.mean) < 1, "Settings panel textures were not restored intact"
                assert not page_errors, page_errors
                assert not any("Postprocessing unavailable" in line or "Postprocessing presentation failed" in line for line in logs), logs
                print(json.dumps({"browser": args.browser, "backend": version, "preset_persistence": "retained after reload", "context_recovery": "guest state and settings panel retained", "screenshots": str(args.output)}, indent=2))
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
