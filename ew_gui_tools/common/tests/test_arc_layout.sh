#!/usr/bin/env bash
# Invariantes del layout del TYPE_HYP2000ARC (ewgui/arc_layout.h).
#
# El campo del modelo de velocidades NO puede caer en 122..135: HYPOINVERSE lee
# la linea 1 como su resumen (hypoinverse/hyphs.for, FORMAT 1212 =
# `T123,2(A1,F3.2,F3.1)`) y en 122..135 espera las magnitudes BMTYPI/BMAGI/TEMPI.
# Un nombre como "iasp91" ahi hace abortar LOC con "Bad value during floating
# point read", y hyp2000_ring le pasa el ARC de entrada VERBATIM a LOC.
#
# Se ejecuta desde `make -C ew_gui_tools/common check`.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"   # ew_gui_tools/
fail=0
hdr="$root/common/include/ewgui/arc_layout.h"

must_have() { # archivo, patrón, mensaje
    if ! grep -qE "$2" "$1"; then
        echo "FAIL: $3 ($1 no contiene '$2')"
        fail=1
    fi
}

if [ ! -f "$hdr" ]; then
    echo "FAIL: falta $hdr"
    exit 1
fi

def() { # nombre de macro -> valor
    sed -n "s/^#define[[:space:]]\+$1[[:space:]]\+\([0-9]\+\).*/\1/p" "$hdr"
}

off=$(def EWGUI_ARC_VMODEL_OFF)
len=$(def EWGUI_ARC_VMODEL_LEN)
leg0=$(def EWGUI_ARC_VMODEL_LEGACY_OFF)
leg1=$(def EWGUI_ARC_VMODEL_LEGACY_END)

if [ -z "$off" ] || [ -z "$len" ] || [ -z "$leg0" ] || [ -z "$leg1" ]; then
    echo "FAIL: faltan constantes EWGUI_ARC_VMODEL_* en $hdr"
    exit 1
fi

# 1) El bloque "legado" es exactamente el de magnitudes de HYPOINVERSE (122..135).
if [ "$leg0" -ne 122 ] || [ "$leg1" -ne 136 ]; then
    echo "FAIL: EWGUI_ARC_VMODEL_LEGACY_* debe ser 122..136 (excl.) y es $leg0..$leg1"
    fail=1
fi

# 2) El campo real NO solapa el bloque de magnitudes.
if [ "$off" -lt "$leg1" ] && [ "$((off + len))" -gt "$leg0" ]; then
    echo "FAIL: EWGUI_ARC_VMODEL_OFF=$off..$((off + len - 1)) solapa 122..135 (magnitudes de HYPOINVERSE)"
    fail=1
fi

# 3) Cabe en la linea 1 (197 bytes) sin pisar los campos del proyecto
#    136..145 (qid), 161, 174 (marca manual) y 178..181 (version).
if [ "$((off + len))" -gt 197 ]; then
    echo "FAIL: EWGUI_ARC_VMODEL_OFF=$off + $len no cabe en la linea 1 (197)"
    fail=1
fi
if [ "$off" -le 181 ]; then
    echo "FAIL: EWGUI_ARC_VMODEL_OFF=$off solapa 136..181 (qid/version/marca manual)"
    fail=1
fi

# 4) Escritores/lectores usan la constante (no un offset a mano).
must_have "$root/csnloc/hypo_out.c"          'EWGUI_ARC_VMODEL_OFF' "csnloc debe escribir el modelo con la constante"
must_have "$root/hyp2000_ring/hyp2000_ring.c" 'EWGUI_ARC_VMODEL_OFF' "hyp2000_ring debe reescribirlo con la constante"
must_have "$root/nlloc_ring/nlloc_ring.c"     'EWGUI_ARC_VMODEL_OFF' "nlloc_ring debe reescribirlo con la constante"
must_have "$root/hypodb/hypodb_ingest.c"      'EWGUI_ARC_VMODEL_OFF' "hypodb debe leerlo con la constante"

# 5) hyp2000_ring limpia el bloque legado antes de escribir arcIn (red de
#    seguridad para ARCs viejos o ya en el anillo).
must_have "$root/hyp2000_ring/hyp2000_ring.c" 'EWGUI_ARC_VMODEL_LEGACY_OFF' "hyp2000_ring debe limpiar el bloque legado"
must_have "$root/hyp2000_ring/hyp2000_ring.c" 'EWGUI_ARC_VMODEL_LEGACY_END' "hyp2000_ring debe limpiar el bloque legado"

# 6) Ningun offset 123 a mano en los sitios que arman/leen la linea 1.
for f in hypodb/test_ingest_db.c csnloc/hypo_out.c; do
    if grep -qE '(\+|line\[)[[:space:]]*123([^0-9]|$)' "$root/$f"; then
        echo "FAIL: $f usa el offset 123 a mano (debe ser EWGUI_ARC_VMODEL_OFF)"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "test_arc_layout: FALLOS"
    exit 1
fi
echo "ALL ARC LAYOUT TESTS PASSED (vmodel off=$off len=$len)"
