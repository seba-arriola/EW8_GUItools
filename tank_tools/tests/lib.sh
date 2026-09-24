#!/bin/bash
# =============================================================================
#  lib.sh - utilidades de la SUITE DE PRUEBAS de tank_tools
#
#  Uso desde un test:
#     source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"
#
#  Convencion copiada de ew_gui_tools/csnrv/test_config.sh:
#     PASS/FAIL + check_* + resumen final; el test sale 0 si FAIL==0.
#
#  Dos familias de tests:
#     OFFLINE  - no arrancan ningun proceso (usan tanksniff/ms2tank/remux_tbuf)
#     LIVE     - arrancan EarthWorm via tank_replay.sh en un EW_PARAMS aislado
#                (ver make_params_copy). Requieren TANK_TESTS_LIVE=1.
#
#  Los helpers PUROS (medidas de tanks, recortes, copia de params, logs, rings)
#  viven en ../tank_lib.sh y los comparten los runners. Aqui solo esta lo
#  especifico de las pruebas: contadores, aserciones y fixtures hermeticos.
# =============================================================================

set -u

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../tank_lib.sh
source "$TESTS_DIR/../tank_lib.sh"

TESTTMP="${TESTTMP:-$TESTS_DIR/tmp}"
mkdir -p "$TESTTMP"
cleanup_tmp() { [ -n "${KEEP_TMP:-}" ] || rm -rf "$TESTTMP"; }
trap cleanup_tmp EXIT

PASS=0
FAIL=0
SKIP=0

# -----------------------------------------------------------------------------
#  Aserciones
# -----------------------------------------------------------------------------
pass()   { PASS=$(( PASS + 1 )); printf '  PASS: %s\n' "$1"; }
failed() { FAIL=$(( FAIL + 1 )); printf '  FAIL: %s\n' "$1"; }
skip()   { SKIP=$(( SKIP + 1 )); printf '  SKIP: %s\n' "$1"; }

check_eq() {  # desc esperado actual
    if [ "$2" = "$3" ]; then pass "$1 (= $3)"; else failed "$1 (esperado '$2', obtenido '$3')"; fi
}

check_ne() {  # desc no_esperado actual
    if [ "$2" != "$3" ]; then pass "$1 (!= $3)"; else failed "$1 (no deberia ser '$2')"; fi
}

check_exit() { check_eq "$1" "$2" "$3"; }

check_contains() {  # desc fichero regex
    if [ ! -f "$2" ]; then failed "$1 (no existe $2)"; return; fi
    if grep -qE -- "$3" "$2"; then pass "$1"; else failed "$1 (patron '$3' ausente en $2)"; fi
}

check_not_contains() {  # desc fichero regex
    if [ ! -f "$2" ]; then failed "$1 (no existe $2)"; return; fi
    if grep -qE -- "$3" "$2"; then failed "$1 (patron '$3' PRESENTE en $2)"; else pass "$1"; fi
}

check_gt() {  # desc actual limite   (actual > limite)
    if [ "$2" -gt "$3" ]; then pass "$1 ($2 > $3)"; else failed "$1 ($2 no es > $3)"; fi
}

check_lt() {  # desc actual limite
    if [ "$2" -lt "$3" ]; then pass "$1 ($2 < $3)"; else failed "$1 ($2 no es < $3)"; fi
}

check_ge() {  # desc actual limite   (actual >= limite)
    if [ "$2" -ge "$3" ]; then pass "$1 ($2 >= $3)"; else failed "$1 ($2 no es >= $3)"; fi
}

summary() {
    echo "== Resultado: $PASS PASS / $FAIL FAIL / $SKIP SKIP =="
    [ "$FAIL" -eq 0 ]
}

# -----------------------------------------------------------------------------
#  Fixtures hermeticos
# -----------------------------------------------------------------------------
# Tank "demultiplexado": bloques por estacion (msgs de A, luego de B, ...),
# que es exactamente lo que remux_tbuf sabe reordenar.
#   make_demux_fixture <master> <out> <STA...>
make_demux_fixture() {
    local master="$1" out="$2"; shift 2
    local vals min span d=0
    vals="$(tank_span "$master")" || return 1
    min="$(printf '%s' "$vals" | awk '{ print $1 }')"
    span="$(printf '%s' "$vals" | awk '{ print $3 }')"
    mkdir -p "$TESTTMP/demux"
    : > "$out"
    local sta part
    for sta in "$@"; do
        part="$TESTTMP/demux/$sta.tank"
        tank_cut "$master" "$part" 0 "$(( span + 1 ))" -S "$sta" || return 1
        cat "$part" >> "$out"
        d=$(( d + 1 ))
    done
    [ "$d" -gt 0 ] && [ -s "$out" ]
}

# Arbol EW_HOME falso para ejercitar mk_tank.sh de forma hermética.
#   make_fake_ew <dir> <fichero_demux> [--no-remux]
# Deja: <dir>/earthworm_8.0/bin/{ms2tank(stub),tanksniff,tankcut,remux_tbuf}
make_fake_ew() {
    local dir="$1" demux="$2" noremux="${3:-}"
    local bin="$dir/earthworm_8.0/bin"
    rm -rf "$dir"; mkdir -p "$bin"
    cat > "$bin/ms2tank" <<EOF
#!/bin/bash
# stub: ignora argumentos y emite el tank demultiplexado preconstruido
cat "$demux"
EOF
    chmod +x "$bin/ms2tank"
    ln -sf "$(ew_bin)/tanksniff"  "$bin/tanksniff"
    ln -sf "$(ew_bin)/tankcut"    "$bin/tankcut"
    if [ "$noremux" != "--no-remux" ]; then
        ln -sf "$(ew_bin)/remux_tbuf" "$bin/remux_tbuf"
    fi
    printf '%s\n' "$dir"
}

# -----------------------------------------------------------------------------
#  Tests LIVE
# -----------------------------------------------------------------------------
live_enabled() { [ "${TANK_TESTS_LIVE:-0}" = "1" ]; }

require_live() {  # <nombre del test>
    if live_enabled; then return 0; fi
    skip "$1 (LIVE deshabilitado: exporta TANK_TESTS_LIVE=1)"
    return 1
}

# Reproduce un tank con EW_PARAMS y EW_LOG aislados. Devuelve la ruta del log.
#   live_play <tank> <params_dir> <log_dir> <args extra de tank_replay.sh...>
live_play() {
    local tank="$1" pdir="$2" ldir="$3"; shift 3
    mkdir -p "$ldir"
    local log="$ldir/replay_$(basename "$tank").log"
    # LIVE_TIMEOUT: red de seguridad para que ninguna prueba se cuelgue
    /usr/bin/timeout "${LIVE_TIMEOUT:-900}" env \
        TANK_REPLAY_PARAMS="$pdir" TANK_REPLAY_LOG="$ldir" EW_PARAMS="$pdir" EW_LOG="$ldir" \
        "$TANK_REPLAY" play "$tank" "$@" > "$log" 2>&1
    printf '%s\n' "$log"
}

live_stop() {
    TANK_REPLAY_PARAMS="${1:-}" "$TANK_REPLAY" stop >/dev/null 2>&1 || true
}
