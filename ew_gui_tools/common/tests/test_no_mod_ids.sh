#!/bin/sh
# Invariante: ningun id de modulo EarthWorm escrito a mano en codigo de
# produccion. La unica fuente es:
#   csnhypodbpqt.d (bin + cfg) -> <cfg>.d (MyModuleId) -> earthworm.d (GetModId)
# Los offsets del ARC (p. ej. `len < 162` o `arc[161]`) NO son mods y no se
# tocan. Los tests usan ids sinteticos y quedan fuera de este chequeo.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
EWG="$ROOT/ew_gui_tools"
fail=0

ck() {  # ck <archivo> <ERE> <descripcion>
    f="$EWG/$1"
    if [ ! -f "$f" ]; then
        echo "FAIL $3 (falta $1)"
        fail=1
        return
    fi
    hits=$(grep -nE "$2" "$f" 2>/dev/null || true)
    if [ -n "$hits" ]; then
        echo "FAIL $3"
        printf '%s\n' "$hits" | sed 's/^/       /'
        fail=1
    else
        echo "PASS $3"
    fi
}

# Tabla de locators: el mod sale de MyModuleId + GetModId.
ck csnhypodbp/csnhypodbp_locator.c '\b(158|162|163|164)\b' \
   "locator: sin ids de modulo escritos a mano"
ck csnhypodbp/csnhypodbp_main.c '\b(158|162|163|164)\b' \
   "GTK: sin ids de modulo en ModLabel"
ck csnhypodbpQT/c/bpq_events.c '\b(158|162|163|164)\b' \
   "Qt: sin ids de modulo en mod_label"
ck csnhypodbpQT/c/bpq_mww.c '\b(158|162|163|164)\b' \
   "Qt: sin ids de modulo al guardar Mww"

# Ingester: el mod lo pone el logo del anillo; no hay defaults.
ck hypodb/hypodb_ingest_main.c '\b(158|162|163|164)\b' \
   "hypodb: sin defaults de mod en el dispatch"
ck common/src/ewgui_db_ids.c '\b(162|163|164)\b' \
   "common: sin mapa mod->algoritmo/status"

# Parseos de ARC: nunca un mod literal (el del anillo o MyModId).
ck hyp2000_ring/hyp2000_ring.c 'parse_solution\([^;]*,\s*(158|162|163|164)\s*,' \
   "hyp2000_ring: parse_solution sin mod literal"
ck csnloc/live_assoc.c 'parse_solution\([^;]*,\s*(158|162|163|164)\s*,' \
   "live_assoc: parse_solution sin mod literal"
ck csnhypodbpQT/ui/bpq_mainwindow.cxx 'parse_solution\([^;]*,\s*(158|162|163|164)\s*,' \
   "Qt: hook BPQ_TEST_ARC sin mod literal"

# El mapa viejo de common ya no existe.
if grep -qE 'ewgui_mod_(algorithm|status)' "$EWG/common/include/ewgui/db_ids.h" \
       "$EWG/common/src/ewgui_db_ids.c"; then
    echo "FAIL common: sigue el mapa ewgui_mod_*"
    fail=1
else
    echo "PASS common: sin ewgui_mod_algorithm/ewgui_mod_status"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: ningun mod id hardcodeado en produccion"
fi
[ "$fail" -eq 0 ]
