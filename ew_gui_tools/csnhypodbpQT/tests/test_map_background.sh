#!/bin/sh
# test_map_background.sh - Fondo del panel de planta (MapImageFile), sin DISPLAY.
#
# Regresion de una trampa medida de Qt6: QImageReader limita la decodificacion a
# 128 MB (Qt < 6.5) y el mapa 2:1 de 10800x5400 ocupa 233 MB en ARGB32, asi que
# sin `QImageReader::setAllocationLimit` el fondo se RECHAZABA en silencio
# ("Rejecting image as it exceeds the current allocation limit of 128 megabytes")
# y el panel caia al lienzo virtual sin dejar rastro.
#
# Comprueba el contrato observable en dumpTree():
#   - clave activa           -> bg=<ancho>x<alto> con la imagen real decodificada
#   - clave comentada        -> bg=0x0 (lienzo virtual), sin mensaje de error
#   - ruta inexistente       -> bg=0x0 y el motivo REAL en stderr
#
# El `.d` se copia a un temporal y solo se reescribe la linea MapImageFile: el
# resto de claves (overlays, perfiles, rings) se conservan. cwd=params para que
# la ruta relativa de la imagen resuelva igual que en produccion.
#
# No toca anillos, BD ni estado real: el modo fixture los desactiva y
# BPQ_STATE_DIR apunta a un temporal.
#
# Uso: sh tests/test_map_background.sh

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../csnhypodbpQT
[ -x "$bin" ] || bin=$root/ew_8.0/bin/csnhypodbpQT
base=$params/csnhypodbpqt.d

if [ ! -x "$bin" ]; then
    echo "SKIP: no existe csnhypodbpQT (construye con 'make all')"
    exit 0
fi
if [ ! -f "$base" ]; then
    echo "SKIP: falta $base"
    exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/bpq_map.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT INT TERM

# Imagen declarada en el `.d` (si esta todo comentado, la versionada por defecto).
map=$(awk '$1 == "MapImageFile" { print $2; exit }' "$base")
[ -n "$map" ] || map=world_map_con_chile.jpg
if [ ! -f "$params/$map" ] && [ ! -f "$map" ]; then
    echo "SKIP: falta la imagen del mapa ($map)"
    exit 0
fi

# Variantes del `.d`: la clave activa, comentada y con una ruta que no existe.
sed "s|^#*MapImageFile.*|MapImageFile $map|" "$base" > "$tmp/on.d"      || exit 2
sed "s|^MapImageFile.*|#&|" "$base"             > "$tmp/off.d"     || exit 2
sed "s|^#*MapImageFile.*|MapImageFile no_existe_bpq.jpg|" "$base" \
                                                > "$tmp/missing.d" || exit 2

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

out=
err=
rc=1
run_dump() {  # <archivo.d>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_WAVE_WAIT_MS=300 \
        BPQ_STATE_DIR="$tmp" "$bin" "$1" 2>"$tmp/err.txt" )
    rc=$?
    err=$(cat "$tmp/err.txt")
}

ck() {  # <descripcion> <regex ERE>
    if printf '%s\n' "$out" | grep -qE "$2"; then
        ok "$1"
    else
        bad "$1"
        printf '%s\n' "$out" | grep -E '^panels ' | sed 's/^/       /'
    fi
}

ck_err() {  # <descripcion> <regex ERE>
    if printf '%s\n' "$err" | grep -qE "$2"; then
        ok "$1"
    else
        bad "$1"
        printf '%s\n' "$err" | sed 's/^/       stderr: /'
    fi
}

# --- 1. clave activa: el fondo se decodifica de verdad ----------------------
run_dump "$tmp/on.d"
[ "$rc" -eq 0 ] && ok "clave activa: exit 0" || bad "clave activa: exit $rc"
ck "clave activa: bg con dimensiones reales" '^panels .* bg=[1-9][0-9]*x[1-9][0-9]*$'
if printf '%s\n' "$err" | grep -q 'no se pudo cargar el mapa'; then
    bad "clave activa: no debe reportar fallo de carga"
    printf '%s\n' "$err" | sed 's/^/       stderr: /'
else
    ok "clave activa: sin fallo en stderr"
fi

# --- 2. sin clave: lienzo virtual, y en silencio ----------------------------
run_dump "$tmp/off.d"
[ "$rc" -eq 0 ] && ok "sin clave: exit 0" || bad "sin clave: exit $rc"
ck "sin clave: bg=0x0 (lienzo virtual)" '^panels .* bg=0x0$'
ck "sin clave: el resto del panel sigue igual" '^panels pts=[0-9]+ pref=[0-9]+ sel=[0-9]+ '
if printf '%s\n' "$err" | grep -q 'no se pudo cargar el mapa'; then
    bad "sin clave: no debe intentar cargar nada"
else
    ok "sin clave: sin mensaje de error"
fi

# --- 3. ruta inexistente: fallo visible y sin crash ------------------------
run_dump "$tmp/missing.d"
[ "$rc" -eq 0 ] && ok "ruta mala: exit 0 (no muere)" || bad "ruta mala: exit $rc"
ck "ruta mala: bg=0x0" '^panels .* bg=0x0$'
ck_err "ruta mala: stderr nombra la ruta" 'no se pudo cargar el mapa <no_existe_bpq\.jpg>'
ck_err "ruta mala: stderr trae el motivo"  'no se pudo cargar el mapa <[^>]*>: .+'

echo "----"
echo "== mapa de fondo checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
