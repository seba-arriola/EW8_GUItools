#!/bin/bash
# Regenera los fixtures de referencia con el generador ORIGINAL del CSN
# (fasew/wphase/libdir/{i_master,cmtsolution}.py, Python 3 compatible).
#
# Los fixtures son DATOS (no código): permiten verificar que csnwphase escribe
# i_master y CMTSOLUTION byte a byte igual que el orquestador del CSN.
#
# Uso: make -C ew_gui_tools/csnwphase fixtures
#      (o WPHASE_CSN_LIBDIR=<ruta> tools/gen_fixtures.sh)
set -e

LIBDIR=${WPHASE_CSN_LIBDIR:-/home/seba/Dev/wphase/CSNop/fasew/wphase/libdir}
OUT="$(cd "$(dirname "$0")/.." && pwd)/tests/data"

if [ ! -f "$LIBDIR/i_master.py" ] || [ ! -f "$LIBDIR/cmtsolution.py" ]; then
    echo "gen_fixtures: no encuentro i_master.py/cmtsolution.py en $LIBDIR" >&2
    echo "              exporta WPHASE_CSN_LIBDIR=<ruta a fasew/wphase/libdir>" >&2
    exit 1
fi

mkdir -p "$OUT"

python3 - "$LIBDIR" "$OUT" <<'PY'
import sys

libdir, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, libdir)

import cmtsolution
import i_master

# Mismos argumentos que usa test_prep.c (evento de referencia: Illapel 2015).
open(out + "/i_master_illapel.txt", "w").write(
    i_master.generate(8.3, "Chile", "-"))
open(out + "/cmt_illapel.txt", "w").write(
    cmtsolution.generate(2015, 9, 16, 22, 54, 32.0, -31.573, -71.674, 25.0,
                         8.3, "Chile"))
PY

echo "fixtures regenerados en $OUT"
