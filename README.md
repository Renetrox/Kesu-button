# kesu-button
<img width="1366" height="768" alt="Captura de pantalla_2026-07-15_16-49-02" src="https://github.com/user-attachments/assets/821208e2-c591-44e6-afff-03079bbe64f3" />


**kesu-button** is a lightweight XFCE panel plugin that provides a classic GnoMenu-style orb/button for Angujanu.

It is designed to revive legacy GnoMenu `Button` themes on modern XFCE desktops. The plugin lives directly inside `xfce4-panel`, reads the selected button theme from the Angujanu/XFCEMenu configuration, and supports the original normal, hover and pressed visual states.

Kesu keeps the main button as a real XFCE panel plugin and now also implements the optional auxiliary transparent window used by classic GnoMenu themes with `Top="1"`.

## Features

* Real `xfce4-panel` plugin.
* Loads legacy GnoMenu `Button` themes.
* Reads the selected button theme from Angujanu/XFCEMenu configuration.
* Parses `<Background Image=... ImageHover=... ImagePressed=...>` from `themedata.xml` instead of depending on fixed filenames.
* Keeps the old `start-here*.png` / SVG names as compatibility fallbacks for incomplete themes.
* Supports normal, hover and pressed button states.
* Keeps the pressed state synchronized with the Angujanu menu process, matching GnoMenu's “menu open” state more closely.
* Supports PNG and SVG button images.
* Scales the main button proportionally to the full panel height, matching the original GnoMenu `Background` behavior.
* Supports legacy `<Label ...>` text definitions.
* Uses `Name`, `MarkupNormal`, `MarkupHover`, `MarkupPressed`, `LabelX` and `LabelY`.
* Draws legacy text such as “Start” or “Iniciar” on top of the button.
* Supports GnoMenu button themes with `Top="1"` and a separate `<Top>` image set.
* Uses the same scale calculated from `Background` for the `<Top>` layer instead of stretching the overlay to the full panel height.
* Synchronizes normal / hover / pressed images between the panel button and the auxiliary Top window.
* For a top panel, vertically flips only the main `Background` when `Top="1"`, following the original GnoMenu behavior; the `<Top>` overlay stays upright.
* Launches Angujanu through `~/.local/bin/xfcemenu`.
* Sends the real Kesu button position, size and XFCE panel edge to compatible Angujanu launchers.
* Supports top, bottom, left and right menu anchoring, with a position fallback for floating panels.

## Current status

This version is based on the stable native `kesu_panel_04` panel-plugin implementation, but the legacy button-theme path has now been brought much closer to original GnoMenu semantics.

Kesu reads the actual `Background` filenames declared by the theme, passes its real panel-button geometry to Angujanu, uses the full panel height for the main orb, and implements the classic `Top="1"` two-window model for horizontal panels.

For `Top="1"` themes, the auxiliary window is placed outside the XFCE panel toward the desktop:

```text
Top panel:    panel → Top overlay below it
Bottom panel: Top overlay above it → panel
```

The overlay uses its own normal / hover / pressed assets and remains interactive, as in original GnoMenu. Kesu periodically follows the real button geometry so moving the plugin across the panel also moves the overlay.

The Top overlay implementation is new and should be considered **experimental until tested across several XFCE compositors/themes**. The normal panel-only path for themes with `Top="0"` remains the compatibility fallback.

## Legacy Button theme model

A classic two-layer theme can look like this:

```xml
<content type="Button">
  <theme Top="1">
    <Background
      Image="start-here.png"
      ImageHover="start-here-glow.png"
      ImagePressed="start-here-depressed.png"/>
    <Top
      Image="start-here-top.png"
      ImageHover="start-here-top-glow.png"
      ImagePressed="start-here-top-depressed.png"/>
  </theme>
</content>
```

`Background` is scaled to the XFCE panel height. The resulting scale factor is then reused for `Top`, which is how the old GnoMenu implementation avoided inflating the protruding overlay to the full panel height.

## Dependencies

Install the required build dependencies:

```bash
sudo apt install build-essential pkg-config libgtk-3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev libxml2-dev
```

## Install

From a Git checkout:

```bash
make clean
make
sudo make install
xfce4-panel -r
```

After restarting the panel, add the plugin from:

```text
Panel → Add New Items → Kesú
```

or in Spanish:

```text
Panel → Agregar nuevos elementos → Kesú
```

## Configuration

`kesu-button` reads the Angujanu/XFCEMenu configuration from:

```text
~/.config/xfcemenu/config.ini
```

The selected button theme is read from:

```ini
[theme]
button_theme = ThemeName
```

The button themes directory is read from:

```ini
[paths]
button_themes_dir = /home/user/.local/share/xfcemenu/themes/Button
```

## Launcher

`kesu-button` launches Angujanu through:

```text
~/.local/bin/xfcemenu
```

This launcher handles opening and closing the menu using the existing XFCEMenu/Angujanu toggle behavior.

When Kesu can read its realized panel widget geometry, it appends internal launcher arguments equivalent to:

```text
--anchor-x X --anchor-y Y --anchor-width W --anchor-height H --panel-position EDGE
```

where `EDGE` is `top`, `bottom`, `left` or `right`. Compatible Angujanu versions use these values to align the menu with Kesu. If geometry cannot be obtained, Kesu falls back to launching `xfcemenu` normally.

For legacy `Top="1"` themes, Kesu reuses that same real geometry to place the transparent auxiliary window next to the actual launcher rather than assuming that the button is at the left edge of the screen.

## Compatibility notes

The original GnoMenu Top layer was a second interactive transparent window rather than a decorative image clipped inside the panel. Kesu follows that model while keeping the main orb inside the native XFCE plugin.

The legacy Top behavior is intentionally limited to horizontal top/bottom panels. Kesu can still anchor Angujanu from left/right panels, but it does not rotate classic GnoMenu button skins by 90 degrees.

Exact historical quirks of legacy `<Label>` font scaling and positioning are still being audited separately; the current label path remains compatible with Kesu's existing themes while the larger Button/Top semantics are restored.
