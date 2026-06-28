#!/usr/bin/env python3
import os
import glob
import html
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass


IMAGE_EXTENSIONS = (".png", ".svg", ".xpm", ".jpg", ".jpeg", ".webp")


@dataclass
class ButtonLabel:
    name: str = ""
    markup_normal: str = ""
    markup_hover: str = ""
    markup_pressed: str = ""
    x: int = 0
    y: int = 0


@dataclass
class ButtonTheme:
    name: str = ""
    author: str = ""
    copyright: str = ""
    theme_dir: str = ""

    normal: str = ""
    hover: str = ""
    pressed: str = ""

    top_normal: str = ""
    top_hover: str = ""
    top_pressed: str = ""

    has_top: bool = False
    label: ButtonLabel = None

    def __post_init__(self):
        if self.label is None:
            self.label = ButtonLabel()


def _attr(node, name, default=""):
    if node is None:
        return default
    return (node.attrib.get(name, default) or "").strip()


def _int_attr(node, name, default=0):
    try:
        return int(float(_attr(node, name, default)))
    except Exception:
        return int(default)


def _abs(theme_dir, filename):
    filename = (filename or "").strip()
    if not filename:
        return ""
    if os.path.isabs(filename):
        return filename
    return os.path.join(theme_dir, filename)


def _existing(path):
    return path if path and os.path.isfile(path) else ""


def _first_existing(theme_dir, names):
    for name in names:
        path = _abs(theme_dir, name)
        if os.path.isfile(path):
            return path
    return ""


def _first_glob(theme_dir, patterns):
    for pattern in patterns:
        matches = sorted(glob.glob(os.path.join(theme_dir, pattern)), key=lambda p: p.lower())
        for path in matches:
            if os.path.isfile(path) and path.lower().endswith(IMAGE_EXTENSIONS):
                return path
    return ""


def _fallback_images(theme_dir):
    """
    Muchos temas Button de GnoMenu declaran imágenes en XML, pero otros solo
    traen archivos con nombres convencionales. Este fallback cubre ambos mundos.
    """
    normal = _first_existing(theme_dir, [
        "start-here.png",
        "start-here.svg",
        "logo.svg",
        "logo.png",
        "normal.png",
        "normal.svg",
        "button.png",
        "button.svg",
        "orb.png",
        "orb.svg",
    ])

    if not normal:
        normal = _first_glob(theme_dir, [
            "start-here.*",
            "logo.*",
            "normal.*",
            "button.*",
            "orb.*",
            "*.png",
            "*.svg",
        ])

    hover = _first_existing(theme_dir, [
        "start-here-glow.png",
        "start-here-glow.svg",
        "glow.svg",
        "glow.png",
        "hover.png",
        "hover.svg",
        "button-hover.png",
        "button-hover.svg",
        "orb-hover.png",
        "orb-hover.svg",
    ])

    if not hover:
        hover = _first_glob(theme_dir, [
            "*glow.*",
            "*hover.*",
        ])

    pressed = _first_existing(theme_dir, [
        "start-here-depressed.png",
        "start-here-depressed.svg",
        "pressed.svg",
        "pressed.png",
        "depressed.png",
        "depressed.svg",
        "button-pressed.png",
        "button-pressed.svg",
        "orb-pressed.png",
        "orb-pressed.svg",
    ])

    if not pressed:
        pressed = _first_glob(theme_dir, [
            "*depressed.*",
            "*pressed.*",
        ])

    top_normal = _first_existing(theme_dir, [
        "start-here-top.png",
        "start-here-top.svg",
        "top.png",
        "top.svg",
    ])

    top_hover = _first_existing(theme_dir, [
        "start-here-top-glow.png",
        "start-here-top-glow.svg",
        "top-glow.png",
        "top-glow.svg",
        "top-hover.png",
        "top-hover.svg",
    ])

    top_pressed = _first_existing(theme_dir, [
        "start-here-top-depressed.png",
        "start-here-top-depressed.svg",
        "top-depressed.png",
        "top-depressed.svg",
        "top-pressed.png",
        "top-pressed.svg",
    ])

    return normal, hover, pressed, top_normal, top_hover, top_pressed


def _parse_label(theme_node):
    label_node = theme_node.find("Label")
    label = ButtonLabel()

    if label_node is None:
        return label

    label.name = _attr(label_node, "Name", "")
    label.markup_normal = html.unescape(_attr(label_node, "MarkupNormal", ""))
    label.markup_hover = html.unescape(_attr(label_node, "MarkupHover", "")) or label.markup_normal
    label.markup_pressed = html.unescape(_attr(label_node, "MarkupPressed", "")) or label.markup_normal
    label.x = _int_attr(label_node, "LabelX", 0)
    label.y = _int_attr(label_node, "LabelY", 0)

    # Fallback por si el tema trae Name pero no markup.
    if label.name and not label.markup_normal:
        label.markup_normal = label.name
        label.markup_hover = label.name
        label.markup_pressed = label.name

    return label


def load_button_theme(theme_dir):
    """
    Lee un tema legacy GnoMenu de tipo Button.

    Soporta:
    - Background Image / ImageHover / ImagePressed
    - Top Image / ImageHover / ImagePressed
    - Label con MarkupNormal y coordenadas
    - fallback por nombres de archivo cuando el XML no declara imágenes
    """
    theme_dir = os.path.abspath(os.path.expanduser(theme_dir))
    themedata = os.path.join(theme_dir, "themedata.xml")

    if not os.path.isfile(themedata):
        raise FileNotFoundError(f"No existe themedata.xml en: {theme_dir}")

    root = ET.parse(themedata).getroot()

    content_type = (root.attrib.get("type", "") or "").strip().lower()
    if content_type and content_type != "button":
        raise ValueError(f"El tema no es de tipo Button: {content_type}")

    result = ButtonTheme(theme_dir=theme_dir)

    content_data = root.find(".//ContentData")
    if content_data is not None:
        result.name = _attr(content_data, "Name", os.path.basename(theme_dir))
        result.author = _attr(content_data, "Author", "")
        result.copyright = _attr(content_data, "Copyright", "")

    if not result.name:
        result.name = os.path.basename(theme_dir)

    theme_node = root.find(".//theme")
    if theme_node is None:
        raise ValueError("No se encontró nodo <theme> en themedata.xml")

    background = theme_node.find("Background")
    top = theme_node.find("Top")

    result.normal = _existing(_abs(theme_dir, _attr(background, "Image", "")))
    result.hover = _existing(_abs(theme_dir, _attr(background, "ImageHover", "")))
    result.pressed = _existing(_abs(theme_dir, _attr(background, "ImagePressed", "")))

    result.top_normal = _existing(_abs(theme_dir, _attr(top, "Image", "")))
    result.top_hover = _existing(_abs(theme_dir, _attr(top, "ImageHover", "")))
    result.top_pressed = _existing(_abs(theme_dir, _attr(top, "ImagePressed", "")))

    fallback = _fallback_images(theme_dir)

    if not result.normal:
        result.normal = fallback[0]
    if not result.hover:
        result.hover = fallback[1] or result.normal
    if not result.pressed:
        result.pressed = fallback[2] or result.normal

    if not result.top_normal:
        result.top_normal = fallback[3]
    if not result.top_hover:
        result.top_hover = fallback[4] or result.top_normal
    if not result.top_pressed:
        result.top_pressed = fallback[5] or result.top_normal

    result.has_top = bool(
        _attr(theme_node, "Top", "") in ("1", "true", "True", "yes", "Yes")
        or result.top_normal
        or result.top_hover
        or result.top_pressed
    )

    result.label = _parse_label(theme_node)

    return result


def get_orb_image(button_theme, state="normal", panel_position="bottom"):
    state = (state or "normal").strip().lower()
    panel_position = (panel_position or "bottom").strip().lower()

    use_top = panel_position == "top" and button_theme.has_top and button_theme.top_normal

    if use_top:
        if state == "hover":
            return button_theme.top_hover or button_theme.top_normal
        if state == "pressed":
            return button_theme.top_pressed or button_theme.top_normal
        return button_theme.top_normal

    if state == "hover":
        return button_theme.hover or button_theme.normal
    if state == "pressed":
        return button_theme.pressed or button_theme.normal
    return button_theme.normal


def get_label_markup(button_theme, state="normal"):
    label = button_theme.label
    if not label:
        return ""

    state = (state or "normal").strip().lower()

    if state == "hover":
        return label.markup_hover or label.markup_normal or label.name
    if state == "pressed":
        return label.markup_pressed or label.markup_normal or label.name
    return label.markup_normal or label.name


def describe_button_theme(button_theme):
    label = button_theme.label or ButtonLabel()

    return {
        "name": button_theme.name,
        "author": button_theme.author,
        "copyright": button_theme.copyright,
        "theme_dir": button_theme.theme_dir,
        "normal": button_theme.normal,
        "hover": button_theme.hover,
        "pressed": button_theme.pressed,
        "has_top": button_theme.has_top,
        "top_normal": button_theme.top_normal,
        "top_hover": button_theme.top_hover,
        "top_pressed": button_theme.top_pressed,
        "label_name": label.name,
        "label_x": label.x,
        "label_y": label.y,
        "label_markup_normal": label.markup_normal,
        "label_markup_hover": label.markup_hover,
        "label_markup_pressed": label.markup_pressed,
    }
