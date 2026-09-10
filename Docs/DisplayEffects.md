# Postprocessing

While a machine is running, open **Display → Postprocessing** or press
**Shift+F7**. **F7** toggles full effects on and off. The controls preview changes
as the guest continues running; menus, the debugger and other host controls
remain clear above the processed picture.

Use **Previous** and **Next preset** to browse the six included SuperDuperDisplay
presets and your saved presets. Change the name and choose **Save new** to keep
a variation. **Import** reads SuperDuperDisplay JSON presets; **Export** writes a portable JSON
file. Repeated names create separate files. **Reset** restores neutral settings.
The bezel arrows select a bundled frame and its matching glass image.

Drag a slider or click its numeric field, edit the value, and press Enter.
Click an On/Off row to toggle it. Scroll or use Page Up/Page Down to reach all
controls. The effect levels are 0 (off), 1 (scanlines), and 2 (full CRT).

The full effect set includes scanlines and interlace, phosphor masks, persistence,
blur and glow, film grain, curvature and barrel distortion, corners and vignette,
convergence, RGB/OKLab color adjustments, and bezel/glass reflections. Image and
bezel position and scale are adjustable independently. Frame-pair merging is
automatically suppressed when Appletini has already composed the two fields.
Appletini linear text is composed before the effects at its firmware resolution.

Changes are saved automatically. Desktop settings live under the normal
GSSquared preferences directory in `postprocess/current.json`; saved presets
are in `postprocess/presets`. Browser settings and saved presets use IndexedDB
for the current site. Export a preset to move it between computers or browser
sites. Private browsing and blocked browser storage may prevent persistence;
the settings panel reports storage errors.

Screenshots and image copies include the guest picture, effects, bezel and
glass at the output resolution, excluding the host menus and controls.

The same built-in shaders run through Metal on macOS, Direct3D 12 on Windows,
Vulkan or OpenGL on Linux, and WebGL 2 in the browser. Runtime shader compiler
installation is unnecessary. Unsupported renderers keep the emulator usable
and show the effects availability error in the panel. Importing arbitrary
custom GLSL programs is outside the built-in Postprocessing interface.

See [Appletini compatibility](Appletini_Compatibility.md),
[linear text and VOC comparison](Appletini_LinearText.md), and
[build and validation instructions](Validation.md).
