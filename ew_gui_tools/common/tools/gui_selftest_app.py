#!/usr/bin/env python3
"""App GTK4 minima para el auto-test del arnes (gui_run.sh --selftest).

No forma parte de ningun modulo: sirve para comprobar que Xvfb + D-Bus + AT-SPI
+ xdotool funcionan ANTES de usarlos contra csnhypodbp/csnrv/csntvp. La etiqueta
cambia de texto al pulsar el boton, de modo que la sonda puede verificar que el
clic llego de verdad.
"""
import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk


class SelfTestApp(Gtk.Application):
    def __init__(self):
        super().__init__(application_id="org.ewgui.selftest")
        self.n = 0

    def do_activate(self):
        win = Gtk.ApplicationWindow(application=self, title="ewgui-selftest")
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.label = Gtk.Label(label="selftest-ready")
        btn = Gtk.Button(label="selftest-target")
        btn.connect("clicked", self.on_clicked)
        box.append(self.label)
        box.append(btn)
        win.set_child(box)
        win.present()

    def on_clicked(self, _btn):
        self.n += 1
        self.label.set_label("selftest-clicked-%d" % self.n)


SelfTestApp().run([])
