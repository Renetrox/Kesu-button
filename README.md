# kesu-button
<img width="1366" height="768" alt="Captura de pantalla_2026-07-15_16-49-02" src="https://github.com/user-attachments/assets/821208e2-c591-44e6-afff-03079bbe64f3" />


**kesu-button** is a lightweight XFCE panel plugin that provides a classic GnoMenu-style orb/button for Angujanu.

It is designed to revive legacy GnoMenu `Button` themes on modern XFCE desktops. The plugin lives directly inside `xfce4-panel`, reads the selected button theme from the Angujanu/XFCEMenu configuration, and supports normal, hover and pressed button states.

`kesu-button` is not a floating overlay hack: it is a real XFCE panel plugin. Optional GnoMenu-style top-overlay support may be added later for themes that used a separate `Top` layer.

## Features

* Real `xfce4-panel` plugin.
* Loads legacy GnoMenu `Button` themes.
* Reads the selected button theme from Angujanu/XFCEMenu configuration.
* Supports normal, hover and pressed button states.
* Supports PNG and SVG button images.
* Scales the main button proportionally to the full panel height, matching the original GnoMenu `Background` behavior more closely.
* Basic `themedata.xml` support.
* Supports legacy `<Label ...>` text definitions.
* Uses `Name`, `MarkupNormal`, `MarkupHover`, `MarkupPressed`, `LabelX` and `LabelY`.
* Draws legacy text such as “Start” or “Iniciar” on top of the button.
* Launches Angujanu through `~/.local/bin/xfcemenu`.
* Sends the real Kesu button position, size and XFCE panel edge to compatible Angujanu launchers.
* Supports top, bottom, left and right panel anchoring, with a position fallback for floating panels.

## Current status

This version is based on `kesu_panel_04` and keeps the stable native panel-plugin implementation.

Kesu now passes its real panel-button geometry to Angujanu so the menu can open relative to the launcher instead of relying on pointer position or a fixed panel-height guess. The main `Background` image also uses the full panel height instead of being reduced by internal vertical padding.

Themes using a separate `Top` layer are not fully supported yet. Future versions may add an optional GnoMenu-style overlay mode for those themes.

## Dependencies

Install the required build dependencies:

```bash
sudo apt install build-essential pkg-config libgtk-3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev libxml2-dev
```

## Install

```bash
unzip -o ~/Descargas/kesu_panel_04.zip
make clean
make
sudo make install
xfce4-panel -r
```

When installing from a Git checkout, run the same build steps from the repository directory so `libkesu.so` is rebuilt from the current `kesu-panel-plugin.c`.

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

## Notes

Some legacy GnoMenu Button themes include a `Top` layer. In the original GnoMenu, this layer was often drawn using a separate transparent overlay window above the panel.

For stability, the current version focuses on rendering the main button image and legacy label text inside the XFCE panel plugin. Full `Top` overlay behavior may be implemented later as an optional experimental mode.
