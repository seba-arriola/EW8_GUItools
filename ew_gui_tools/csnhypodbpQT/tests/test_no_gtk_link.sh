#!/bin/sh
# test_no_gtk_link.sh - AC2: el binario del prototipo NO debe enlazar GTK,
# libadwaita, gobject ni gio.
#
# Nota: libglib-2.0 SI aparece en ldd, pero viene de Qt6Gui
# (QEventDispatcherGlib), no de nuestro codigo ni de GTK. Se comprueba que no
# haya libgtk-4, libadwaita, libgobject ni libgio.
#
# Uso: sh test_no_gtk_link.sh [ruta-al-binario]

set -u

here=$(cd "$(dirname "$0")" && pwd)
bin=${1:-$here/../csnhypodbpQT}

if [ ! -x "$bin" ]; then
    echo "SKIP: no existe $bin (construye con 'make all')"
    exit 0
fi

if ! command -v ldd >/dev/null 2>&1; then
    echo "SKIP: no hay ldd"
    exit 0
fi

bad=$(ldd "$bin" 2>/dev/null | grep -Ei 'libgtk|adwaita|libgobject|libgio' || true)
if [ -n "$bad" ]; then
    echo "FALLA: $bin enlaza GTK/Adwaita/gobject/gio:"
    printf '%s\n' "$bad"
    exit 1
fi

echo "OK: sin libgtk/libadwaita/libgobject/libgio"
exit 0
