#!/usr/bin/env bash
# Golden de comportamiento (T0.2): corre los tests unitarios actuales de los
# módulos GUI y guarda salida + hash. Sirve para demostrar que el refactor no
# cambia el comportamiento.
#
# Uso:
#   source ./ew8_unix.sh
#   ew_gui_tools/common/tests/baseline.sh [dir_de_salida]
#
# Default de salida: ew_gui_tools/common/build/baseline
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${1:-$HERE/../build/baseline}"

cd "$ROOT"
mkdir -p "$OUT"

if [ -f ./ew8_unix.sh ]; then
    # shellcheck disable=SC1091
    source ./ew8_unix.sh
fi

run() {
    local name="$1"; shift
    echo "== $name =="
    if "$@" > "$OUT/$name.out" 2>&1; then
        echo "PASS $name"
    else
        echo "FAIL $name (exit $?)"
    fi
    sha256sum "$OUT/$name.out" | awk '{print $1}' > "$OUT/$name.sha"
}

# csntvp: filtro IIR standalone (sin GTK/EarthWorm)
run csntvp_filter bash -c \
    'make -C ew_gui_tools/csntvp test_filter >/dev/null && ew_gui_tools/csntvp/test_filter'

# csnhypodbp: filtro IIR + gaps + demean (DSP puro)
run csnhypodbp_filter bash -c \
    'make -C ew_gui_tools/csnhypodbp test_filter >/dev/null && ew_gui_tools/csnhypodbp/test_filter'

# csnrv: parseo de magnitudes headless (enlaza rw_mag.o + libew_mt.a)
run csnrv_test_mag bash -c \
    'make -C ew_gui_tools/csnrv csnrv >/dev/null && ew_gui_tools/csnrv/csnrv --test-mag'

echo
echo "Golden en: $OUT"
echo "Para comparar tras un cambio: re-correr y diff de los .out / .sha"
