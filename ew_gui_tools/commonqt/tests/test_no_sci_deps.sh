#!/bin/bash
# Invariante de commonqt: la lib compartida no arrastra NADA del dominio
# sismologico ni de GTK. Puede usarla cualquier modulo GUI sin enlazar
# EarthWorm, libpq, cairo ni glib.
#
# Verifica:
#   (a) la lib existe;
#   (b) sus simbolos indefinidos no incluyen EW/kom/libpq/cairo/GTK (ni bpq_/ecq_,
#       que serian una dependencia circular con un modulo);
#   (c) ningun header ni fuente de commonqt incluye gtk/cairo/earthworm/libpq.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIR="$ROOT/ew_gui_tools/commonqt"
LIB="$DIR/build/libewguiqt.a"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# --- (a) la lib --------------------------------------------------------------
if [ ! -f "$LIB" ]; then
    bad "no existe $LIB (¿make -C ew_gui_tools/commonqt?)"
    exit 1
fi
ok "libewguiqt.a presente"

# --- (b) simbolos indefinidos ------------------------------------------------
FORBIDDEN_SYM='tport_|GetKey|GetLocalInst|GetModId|logit_|PQ[A-Za-z]*conn|PQexec|res_mag|wc_|inline_|cairo_|gtk_|gdk_|adw_|g_object_|g_malloc|g_free|bpq_|ecq_'
SYMS=$(nm -u "$LIB" 2>/dev/null | grep -oE '[A-Za-z_][A-Za-z0-9_]*$' || true)
HITS=$(printf '%s\n' "$SYMS" | grep -E "$FORBIDDEN_SYM" | sort -u | head -n 10)
if [ -n "$HITS" ]; then
    bad "la lib referencia simbolos que no debe:"
    printf '       %s\n' $HITS
else
    ok "sin simbolos de EW/libpq/cairo/GTK en la lib"
fi

# --- (c) includes prohibidos -------------------------------------------------
BAD_INC='gtk/gtk.h|gdk/gdk.h|adwaita|glib.h|glib/|gio/|cairo|earthworm|transport\.h|kom\.h|libpq|wstore|csn[a-z]*\.h|csnhypodbp'
HITS=$(grep -rEn "#include[[:space:]]*[<\"]([^>\"]*)" "$DIR/src" "$DIR/include" 2>/dev/null |
       grep -E "$BAD_INC" || true)
if [ -n "$HITS" ]; then
    bad "includes prohibidos en commonqt:"
    printf '%s\n' "$HITS" | head -n 10
else
    ok "sin includes de GTK/cairo/EW/libpq en src/ ni include/"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: libewguiqt.a solo depende de Qt6 (y del comparador C puro)"
fi
exit "$fail"
