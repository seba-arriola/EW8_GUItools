#!/bin/bash
# Invariante de commonqt: sus binarios de test no enlazan GTK ni cairo.
#
# El modo de fallo que evita: incluir un header de common/ que arrastre
# <gtk/gtk.h> y acabar con un modulo Qt enlazado a GTK (el `ldd` lo delata).
# `libglib-2.0` si se permite: Qt6Core la usa en algunas distribuciones y no es
# GTK (misma excepcion que tests/test_no_gtk_link.sh de csnhypodbpQT).
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="$ROOT/ew_gui_tools/commonqt/build"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

BINS=$(find "$BUILD" -maxdepth 1 -type f -name 'test_*' -perm -u+x 2>/dev/null | sort)
if [ -z "$BINS" ]; then
    bad "no hay binarios de test en $BUILD (¿make check?)"
    exit 1
fi

FORBIDDEN='libgtk-4|libgtk-3|libgdk-|libadwaita|libcairo'
for b in $BINS; do
    name=$(basename "$b")
    deps=$(ldd "$b" 2>/dev/null | awk '{print $1}')
    hit=$(printf '%s\n' "$deps" | grep -E "$FORBIDDEN" || true)
    if [ -n "$hit" ]; then
        bad "$name enlaza GTK/cairo: $(printf '%s ' $hit)"
    else
        ok "$name sin GTK/cairo"
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "OK: ningún binario de commonqt enlaza GTK ni cairo"
fi
exit "$fail"
