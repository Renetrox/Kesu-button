#!/usr/bin/env python3
import os
import sys
import argparse
import subprocess
import cairo

import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk, GdkPixbuf, GLib

from kesu_button_loader import (
    load_button_theme,
    get_orb_image,
    get_label_markup,
    describe_button_theme,
)


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
THEMES_DIR = os.path.join(BASE_DIR, "themes")
BUTTON_THEMES_DIR = os.path.join(THEMES_DIR, "Button")


class KesuOrb(Gtk.Window):
    def __init__(
        self,
        button_theme,
        panel_position="bottom",
        scale=1.0,
        size=0,
        anchor="bottom-left",
        offset_x=0,
        offset_y=0,
        command="",
        quiet=False,
        draggable=True,
    ):
        super().__init__(title="Kesú")

        self.button_theme = button_theme
        self.panel_position = panel_position
        self.requested_scale = max(0.2, float(scale or 1.0))
        self.size = int(size or 0)
        self.anchor = (anchor or "bottom-left").strip().lower()
        self.offset_x = int(offset_x or 0)
        self.offset_y = int(offset_y or 0)
        self.command = command.strip()
        self.quiet = bool(quiet)
        self.draggable = bool(draggable)

        if self.anchor not in ("bottom-left", "pointer"):
            self.anchor = "bottom-left"

        self.scale = self.resolve_scale()

        self.pixbufs = {
            "normal": self.load_state_pixbuf("normal"),
            "hover": self.load_state_pixbuf("hover"),
            "pressed": self.load_state_pixbuf("pressed"),
        }

        if not self.pixbufs["normal"]:
            raise RuntimeError("No se pudo cargar la imagen normal del orb.")

        self.set_decorated(False)
        self.set_resizable(False)
        self.set_keep_above(True)
        self.set_skip_taskbar_hint(True)
        self.set_skip_pager_hint(True)
        self.set_app_paintable(True)
        self.set_accept_focus(False)

        self.setup_transparency()
        self.connect("draw", self.on_draw)

        self.fixed = Gtk.Fixed()
        self.fixed.set_name("kesu-fixed")

        self.image = Gtk.Image.new_from_pixbuf(self.pixbufs["normal"])
        self.fixed.put(self.image, 0, 0)

        self.label = None
        self.setup_label()

        self.event_box = Gtk.EventBox()
        self.event_box.set_name("kesu-eventbox")
        self.event_box.set_visible_window(False)
        self.event_box.add(self.fixed)
        self.add(self.event_box)

        self.event_box.add_events(
            Gdk.EventMask.ENTER_NOTIFY_MASK |
            Gdk.EventMask.LEAVE_NOTIFY_MASK |
            Gdk.EventMask.BUTTON_PRESS_MASK |
            Gdk.EventMask.BUTTON_RELEASE_MASK
        )

        self.event_box.connect("enter-notify-event", self.on_enter)
        self.event_box.connect("leave-notify-event", self.on_leave)
        self.event_box.connect("button-press-event", self.on_press)
        self.event_box.connect("button-release-event", self.on_release)
        self.connect("key-press-event", self.on_key_press)

        self.resize_to_orb()
        self.show_all()

        # Esperamos un ciclo para que GTK conozca tamaño/ventana antes de mover.
        GLib.idle_add(self.position_window)

    def resolve_scale(self):
        if self.size <= 0:
            return self.requested_scale

        normal_path = get_orb_image(self.button_theme, "normal", self.panel_position)
        if not normal_path or not os.path.isfile(normal_path):
            return self.requested_scale

        try:
            pixbuf = GdkPixbuf.Pixbuf.new_from_file(normal_path)
            height = pixbuf.get_height()
            if height > 0:
                return max(0.05, float(self.size) / float(height))
        except Exception:
            pass

        return self.requested_scale

    def setup_transparency(self):
        screen = self.get_screen()

        try:
            visual = screen.get_rgba_visual()
            if visual is not None:
                self.set_visual(visual)
            elif not self.quiet:
                print("Kesú: no hay visual RGBA disponible.")
        except Exception as error:
            if not self.quiet:
                print(f"Kesú: no se pudo activar visual RGBA: {error}")

        css = b"""
        window,
        GtkWindow,
        #kesu-fixed,
        #kesu-eventbox,
        eventbox,
        fixed,
        image,
        label {
            background-color: transparent;
            background-image: none;
            border: none;
            box-shadow: none;
        }
        """

        provider = Gtk.CssProvider()
        provider.load_from_data(css)

        Gtk.StyleContext.add_provider_for_screen(
            screen,
            provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

    def on_draw(self, widget, cr):
        cr.set_operator(cairo.OPERATOR_CLEAR)
        cr.paint()
        cr.set_operator(cairo.OPERATOR_OVER)
        return False

    def setup_label(self):
        label_data = getattr(self.button_theme, "label", None)
        markup = get_label_markup(self.button_theme, "normal")

        if not label_data or not markup:
            return

        self.label = Gtk.Label()
        self.label.set_use_markup(True)
        self.label.set_markup(markup)
        self.label.set_xalign(0)
        self.label.set_yalign(0)
        self.label.set_name("kesu-label")

        x = int(label_data.x * self.scale)
        y = int(label_data.y * self.scale)

        self.fixed.put(self.label, x, y)

    def update_label_state(self, state):
        if not self.label:
            return

        markup = get_label_markup(self.button_theme, state)
        if markup:
            self.label.set_markup(markup)

    def load_state_pixbuf(self, state):
        path = get_orb_image(self.button_theme, state, self.panel_position)

        if not path or not os.path.isfile(path):
            return None

        pixbuf = GdkPixbuf.Pixbuf.new_from_file(path)

        if self.scale != 1.0:
            width = max(1, int(pixbuf.get_width() * self.scale))
            height = max(1, int(pixbuf.get_height() * self.scale))
            pixbuf = pixbuf.scale_simple(width, height, GdkPixbuf.InterpType.BILINEAR)

        return pixbuf

    def resize_to_orb(self):
        pixbuf = self.pixbufs["normal"]
        width = pixbuf.get_width()
        height = pixbuf.get_height()

        self.set_size_request(width, height)
        self.fixed.set_size_request(width, height)
        self.event_box.set_size_request(width, height)
        self.resize(width, height)

    def set_state(self, state):
        pixbuf = self.pixbufs.get(state) or self.pixbufs.get("normal")
        if pixbuf:
            self.image.set_from_pixbuf(pixbuf)
        self.update_label_state(state)

    def get_monitor_geometry(self):
        display = Gdk.Display.get_default()
        monitor = display.get_primary_monitor() if display else None

        if monitor is None and display and display.get_n_monitors() > 0:
            monitor = display.get_monitor(0)

        if monitor:
            return monitor.get_geometry()

        screen = self.get_screen()
        return Gdk.Rectangle(x=0, y=0, width=screen.get_width(), height=screen.get_height())

    def get_pointer_position(self):
        """
        Devuelve coordenadas globales del puntero.
        En X11/GTK3 suele funcionar con seat/pointer.
        """
        display = Gdk.Display.get_default()
        if not display:
            return None

        try:
            seat = display.get_default_seat()
            pointer = seat.get_pointer()
            _screen, x, y = pointer.get_position()
            return int(x), int(y)
        except Exception:
            pass

        try:
            device_manager = display.get_device_manager()
            pointer = device_manager.get_client_pointer()
            _screen, x, y = pointer.get_position()
            return int(x), int(y)
        except Exception:
            return None

    def position_window(self):
        pixbuf = self.pixbufs["normal"]
        width = pixbuf.get_width()
        height = pixbuf.get_height()

        if self.anchor == "pointer":
            pos = self.get_pointer_position()

            if pos:
                px, py = pos
                x = int(px - (width / 2) + self.offset_x)
                y = int(py - (height / 2) + self.offset_y)
                self.move(x, y)

                if not self.quiet:
                    print(f"Kesú: anchor pointer -> {x},{y}")

                return False

            if not self.quiet:
                print("Kesú: no se pudo leer puntero; usando bottom-left.")

        try:
            geom = self.get_monitor_geometry()
            x = geom.x + self.offset_x
            y = geom.y + geom.height - height + self.offset_y
            self.move(x, y)

            if not self.quiet:
                print(f"Kesú: anchor bottom-left -> {x},{y}")

        except Exception:
            self.move(self.offset_x, self.offset_y)

        return False

    def on_enter(self, widget, event):
        self.set_state("hover")
        return False

    def on_leave(self, widget, event):
        self.set_state("normal")
        return False

    def on_press(self, widget, event):
        # Botón izquierdo: estado pressed normal del orb.
        if event.button == 1:
            self.set_state("pressed")
            return False

        # Botón derecho: mover Kesú manualmente.
        # Esto no interfiere con el clic izquierdo que luego abrirá Angujanu.
        if event.button == 3 and self.draggable:
            try:
                self.begin_move_drag(
                    event.button,
                    int(event.x_root),
                    int(event.y_root),
                    event.time
                )
                return True
            except Exception as error:
                if not self.quiet:
                    print(f"Kesú: no se pudo iniciar movimiento manual: {error}")

        return False

    def on_release(self, widget, event):
        if event.button == 1:
            self.set_state("hover")

            if self.command:
                subprocess.Popen(self.command, shell=True)
            elif not self.quiet:
                print("Kesú: clic detectado. Todavía no hay comando configurado.")

        return False

    def on_key_press(self, widget, event):
        key = Gdk.keyval_name(event.keyval)

        if key in ("Escape", "q", "Q"):
            Gtk.main_quit()
            return True

        if key in ("p", "P"):
            try:
                x, y = self.get_position()
                print(f"Kesú: posición actual absoluta -> x={x}, y={y}")
            except Exception:
                pass
            return True

        return False


def resolve_button_theme_path(button_theme_name="", theme_dir=""):
    if theme_dir:
        return os.path.abspath(os.path.expanduser(theme_dir))

    if not button_theme_name:
        button_theme_name = "UbuntuOrb"

    return os.path.join(BUTTON_THEMES_DIR, button_theme_name)


def main():
    parser = argparse.ArgumentParser(description="Kesú: orb Button independiente para Angujanu.")
    parser.add_argument("--button-theme", default="UbuntuOrb", help="Nombre de carpeta dentro de themes/Button/")
    parser.add_argument("--theme-dir", default="", help="Ruta directa a un tema Button")
    parser.add_argument("--panel-position", default="bottom", choices=("bottom", "top"), help="Usar imágenes normales o Top")
    parser.add_argument("--scale", default="1.0", help="Escala visual del orb, por ejemplo 1.0 o 0.75")
    parser.add_argument("--size", default="0", help="Alto deseado del orb en píxeles. Si se usa, reemplaza --scale.")
    parser.add_argument("--anchor", default="bottom-left", choices=("bottom-left", "pointer"), help="Dónde posicionar Kesú.")
    parser.add_argument("--offset-x", default="0", help="Ajuste horizontal manual.")
    parser.add_argument("--offset-y", default="0", help="Ajuste vertical manual.")
    parser.add_argument("--command", default="", help="Comando opcional a ejecutar al hacer clic")
    parser.add_argument("--quiet", action="store_true", help="No imprimir mensajes de clic/posición.")
    parser.add_argument("--no-drag", action="store_true", help="Desactiva mover Kesú con clic derecho.")
    parser.add_argument("--print-info", action="store_true", help="Imprime datos del tema y sale")

    args = parser.parse_args()

    theme_path = resolve_button_theme_path(args.button_theme, args.theme_dir)
    button_theme = load_button_theme(theme_path)

    if args.print_info:
        for key, value in describe_button_theme(button_theme).items():
            print(f"{key}: {value}")
        return 0

    KesuOrb(
        button_theme=button_theme,
        panel_position=args.panel_position,
        scale=float(args.scale),
        size=int(args.size or 0),
        anchor=args.anchor,
        offset_x=int(args.offset_x or 0),
        offset_y=int(args.offset_y or 0),
        command=args.command,
        quiet=args.quiet,
        draggable=not args.no_drag,
    )

    Gtk.main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
