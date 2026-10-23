#!/bin/bash
# Invariante de enlace: el binario del port Qt NO enlaza GTK, Adwaita ni cairo.
#
# Es el modo de fallo que ya se pago en csnhypodbpQT: incluir un header de GTK
# (o enlazar libewgui_gtk4.a) deja un "modulo Qt" arrastrando GTK. `ldd` lo
# delata. `libglib-2.0` si se permite: la usa Qt6Core en algunas distribuciones.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
bin="$here/../ew_controllerQT"
[ -x "$bin" ] || bin="$root/ew_8.0/bin/ew_controllerQT"

fail=0
ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

if [ ! -x "$bin" ]; then
    bad "no existe $bin (make -C ew_gui_tools/ew_controllerQT)"
    exit 1
fi

FORBIDDEN='libgtk-4|libgtk-3|libgdk-|libadwaita|libcairo|libgobject'
deps=$(ldd "$bin" 2>/dev/null | awk '{print $1}')
hit=$(printf '%s\n' "$deps" | grep -E "$FORBIDDEN" || true)
if [ -n "$hit" ]; then
    bad "$(basename "$bin") enlaza GTK/Adwaita/cairo: $(printf '%s ' $hit)"
else
    ok "$(basename "$bin") sin GTK/Adwaita/cairo"
fi

if ! printf '%s\n' "$deps" | grep -q 'libQt6Widgets'; then
    bad "no enlaza libQt6Widgets (¿binario equivocado?)"
else
    ok "enlaza libQt6Widgets"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: el binario Qt no arrastra GTK"
fi
exit "$fail"
