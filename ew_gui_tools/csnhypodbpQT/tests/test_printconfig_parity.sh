#!/bin/sh
# test_printconfig_parity.sh - AC6: `--print-config` del prototipo debe coincidir
# (clave + valor, orden irrelevante) con el del modulo GTK para el MISMO .d.
#
# Se ejecuta con cwd = run_working_v8/params para que las rutas relativas del .d
# resuelvan como en produccion. No necesita DISPLAY.
#
# Uso: sh test_printconfig_parity.sh [archivo.d]

set -u

here=$(cd "$(dirname "$0")" && pwd)
params=$(cd "$here/../../../run_working_v8/params" 2>/dev/null && pwd)
bin=$here/../csnhypodbpQT
gtk=${GTK_BIN:-$here/../../../ew_8.0/bin/csnhypodbp}
d=${1:-csnhypodbp.d}

if [ ! -x "$bin" ]; then echo "SKIP: no existe $bin (construye con 'make all')"; exit 0; fi
if [ ! -x "$gtk" ]; then echo "SKIP: no existe $gtk (modulo GTK no instalado)"; exit 0; fi
if [ ! -d "$params" ]; then echo "SKIP: no existe $params"; exit 0; fi

a=/tmp/bpq_pc_gtk.$$
b=/tmp/bpq_pc_qt.$$
trap 'rm -f "$a" "$b"' EXIT INT TERM

cd "$params" || exit 2
"$gtk" --print-config "$d" 2>/dev/null | sort > "$a" || {
    echo "SKIP: el binario GTK no pudo leer $d"; exit 0; }
"$bin" --print-config "$d" > /dev/null 2>&1 || {
    echo "FALLA: csnhypodbpQT --print-config $d termino con error"; exit 1; }
"$bin" --print-config "$d" | sort > "$b"

if ! diff -u "$a" "$b"; then
    echo "FALLA: --print-config difiere del modulo GTK"
    exit 1
fi

echo "OK: --print-config identico ($(wc -l < "$b") lineas)"
exit 0
