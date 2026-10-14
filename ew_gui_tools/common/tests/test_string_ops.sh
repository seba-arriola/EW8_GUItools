#!/usr/bin/env bash
# test_string_ops.sh - invariantes de manejo de cadenas en la libreria compartida.
#
# Motivo: `strncat(dst, src, sizeof(dst) - 1)` reserva el tamano TOTAL en vez del
# hueco libre; con el buffer ya lleno escribe fuera de limite. Paso en
# ewgui_ctrl.c al construir el cfgfile de respaldo ("<name>.d"): el desborde caia
# en el campo siguiente del struct (el nombre del modulo siguiente). El margen
# correcto es sizeof-1-strlen(dst), y lo mas simple es no usar strncat para eso.
set -u
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SRC="$ROOT/src"
fails=0

echo "== strncat con margen calculado a partir de sizeof =="
if grep -rnE 'strncat *\(.*sizeof' "$SRC" ; then
    echo "FALLO: strncat con sizeof (hay que usar el hueco libre, o snprintf)"
    fails=$((fails + 1))
else
    echo "ok  : ningun strncat calcula el margen con sizeof"
fi

echo "== el respaldo del cfgfile usa el helper acotado =="
if grep -q 'ewgui_ctrl_cfgfile_por_nombre' "$SRC/ewgui_ctrl.c" ; then
    echo "ok  : ewgui_ctrl.c construye el cfgfile con ewgui_ctrl_cfgfile_por_nombre()"
else
    echo "FALLO: ewgui_ctrl.c no usa ewgui_ctrl_cfgfile_por_nombre()"
    fails=$((fails + 1))
fi

if [ "$fails" -ne 0 ]; then
    echo "FALLO: $fails invariante(s) de cadenas"
    exit 1
fi
echo "OK: invariantes de cadenas"
