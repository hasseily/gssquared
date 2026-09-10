# Displays

GS2 provides a variety of display modes, emulations, and controls.

These are matters of personal preference, so you get to pick the one you like best!

Typically any mode / control can be used with any computer.


## Display Engines

GS2 supports three different Apple II Display Engines:

* Composite / NTSC
* IIgs RGB
* Monochrome

You can use any rendering mode (for the most part) with any computer type.

In the Control Panel, there are buttons to change the display engine - NTSC, RGB, and Monochrome. And, buttons to change the Monochrome color (green, amber, white).

Display engine can be selected by menu, using the OSD Buttons, or by pressing **F2** to cycle through the display engines.

NTSC mode has additional controls for Hue (Color) and Saturation, just like real composite monitors.

Hue is adjusted with the keypad Plus and Minus Keys, while holding SHIFT.
Saturation is adjusted with the keypad Plus and Minus Keys, while holding OPTION / WINDOWS.

Reducing Saturation to 0.5 results in colors that are a little more subdued, similar to some other emulators and certain CRT monitors.
Reducing Saturation to 0 results in a grayscale (not monochrome) display.

## Display Configuration

In the OSD, there are buttons to change the display engine - NTSC, RGB, and Monochrome. And, buttons to change the Monochrome color (green, amber, white).

* F2 cycles through the display engines.  
* F5 toggles between pixel-blur and rectangular. pixel-blur provides a little more "analog" upscaling of Apple II dots to modern displays. Rectangular performs an exact square upscaling/downscaling.
* F3 toggles between Full-Screen and Windowed modes.

These are matters of personal preference, so you get to pick the one you like best.

## Postprocessing

Use **Display → Postprocessing...** or **Shift+F7** to edit CRT scanlines, masks, phosphor persistence, color, curvature, bezel reflections and glass. Choose a bundled preset, save your own, or import/export compatible SuperDuperDisplay JSON presets. The panel supports sliders and precise typed values.

**F7** or **Display → CRT Shader** toggles the effects; `-g` enables them at boot. Current settings are restored on startup. Browser presets use local browser storage; export a preset to retain a separate copy.

The same effects run on macOS (Metal), Windows (D3D12), Linux (Vulkan or OpenGL), and the browser (WebGL2). Host controls remain outside the effects. Screenshots include the processed guest picture, bezel and glass.

## Second Sight Text

If the current machine has a **Second Sight** card, **Display → Second Sight Text** renders Apple II fullscreen 40- or 80-column text through the card’s VGA path, using an Apple-ified font from the Second Sight ROM. Graphics modes and Second Sight’s own VGA modes are unchanged.

The menu item is grayed out when no Second Sight card is present. The setting is remembered in app settings.

## Pixel Modes

GS2 has two Pixel Modes:

* Pixel blur ("Analog")
* Rectangular

Pixel-blur provides a little more "analog" upscaling of Apple II dots to modern displays, that gives it more of that old CRT feel.

Rectangular performs an exact square upscaling/downscaling, which is common in other emulators, and may be more to some people's liking.

Pixel Mode can be selected by menu, using the OSD buttons, or by pressing **F5** to toggle between pixel-blur and rectangular.

## Windowed - Full Screen

GS2 Supports windowed (default) and full-screen modes.

Use full-screen mode to make your classic Apple II games the full size of your modern monitor.

**F3** toggles between Full-Screen and Windowed modes.

