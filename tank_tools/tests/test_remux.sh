#!/bin/bash
# =============================================================================
#  test_remux.sh - pruebas del camino --remux de mk_tank.sh  (OFFLINE)
#
#  R1  fixture demultiplexado (bloques por estacion)
#  R2  el detector de pauta lo marca como NO reproducible
#  R3  remux_tbuf lo arregla
#  R4  no se pierde ni un mensaje ni un SCNL
#  R5  remux_tbuf es idempotente
#  R6  mk_tank.sh --remux ejercitado de verdad (arbol EW_HOME falso, hermetico)
#  R7  mk_tank.sh --remux cuando falta el binario remux_tbuf (no debe abortar)
#  R8  [lento, TANK_TESTS_SLOW=1] mk_tank.sh sobre el mseed real
#
#  No arranca ningun servicio.
# =============================================================================
set -u
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

STAS="AC02 AF01 CO02 CO03 LL03"
DEMUX="$TESTTMP/demux.tank"
FIXED="$TESTTMP/fixed.tank"

# --- tank base ---------------------------------------------------------------
BASE=""
if [ -f "$TANKS_REAL/test120.tank" ]; then
    BASE="$TANKS_REAL/test120.tank"
elif [ -f "$TANKS_REAL/master.tank" ]; then
    BASE="$TESTTMP/base.tank"
    if tank_cut "$TANKS_REAL/master.tank" "$BASE" 300 120; then :; else BASE=""; fi
fi
if [ -z "$BASE" ]; then
    echo "== test_remux: SKIP (no hay tanks en $TANKS_REAL; corre mk_tank.sh antes) =="
    exit 0
fi
echo "-- tank base: $BASE ($(tank_msgcount "$BASE") mensajes, span $(tank_span "$BASE" | awk '{print $3}')s)"

# -----------------------------------------------------------------------------
echo "=== R1: fixture demultiplexado ==="
if make_demux_fixture "$BASE" "$DEMUX" $STAS; then
    pass "make_demux_fixture genero el fixture ($(tank_msgcount "$DEMUX") mensajes)"
    got="$(tank_scnls "$DEMUX" | awk -F. '{print $1}' | sort -u | tr '\n' ' ')"
    want="$(printf '%s\n' $STAS | sort -u | tr '\n' ' ')"
    check_eq "el fixture contiene solo las estaciones pedidas" "$want" "$got"
else
    failed "make_demux_fixture no genero el fixture"
fi

# -----------------------------------------------------------------------------
echo "=== R2: el detector marca el desorden ==="
check_order "$DEMUX"; check_exit "check_order(demux) = 2" 2 "$?"
check_gt "hay inversiones de endtime" "$(tank_order_violations "$DEMUX")" 0

# -----------------------------------------------------------------------------
echo "=== R3: remux_tbuf lo arregla ==="
"$(ew_bin)/remux_tbuf" "$DEMUX" "$FIXED" >/dev/null 2>&1
check_exit "remux_tbuf exit 0" 0 "$?"
check_order "$FIXED"; check_exit "check_order(fixed) = 0" 0 "$?"

# -----------------------------------------------------------------------------
echo "=== R4: no se pierde nada ==="
check_eq "mismo numero de mensajes" "$(tank_msgcount "$DEMUX")" "$(tank_msgcount "$FIXED")"
check_eq "mismos SCNL" \
    "$(tank_scnls "$DEMUX" | md5sum | cut -d' ' -f1)" \
    "$(tank_scnls "$FIXED" | md5sum | cut -d' ' -f1)"

# -----------------------------------------------------------------------------
echo "=== R5: idempotencia ==="
"$(ew_bin)/remux_tbuf" "$FIXED" "$TESTTMP/fixed2.tank" >/dev/null 2>&1
check_eq "mismo numero de mensajes tras el 2o remux" \
    "$(tank_msgcount "$FIXED")" "$(tank_msgcount "$TESTTMP/fixed2.tank")"
check_order "$TESTTMP/fixed2.tank"; check_exit "check_order(fixed2) = 0" 0 "$?"

# -----------------------------------------------------------------------------
echo "=== R6: mk_tank.sh --remux (arbol EW_HOME falso) ==="
DUMMY="$TESTTMP/dummy.mseed"; printf 'x' > "$DUMMY"
FAKE="$(make_fake_ew "$TESTTMP/fake_ew" "$DEMUX")"
EW_HOME="$FAKE" EW_VERSION=earthworm_8.0 "$MK_TANK" "$DUMMY" "$TESTTMP/out6" \
    >"$TESTTMP/r6a.log" 2>&1
check_exit "sin --remux -> 2" 2 "$?"
check_contains "informa de pauta no reproducible" "$TESTTMP/r6a.log" "no es reproducible con pauta correcta"
EW_HOME="$FAKE" EW_VERSION=earthworm_8.0 "$MK_TANK" "$DUMMY" "$TESTTMP/out6" --remux \
    >"$TESTTMP/r6b.log" 2>&1
check_exit "con --remux -> 0" 0 "$?"
check_order "$TESTTMP/out6/master.tank"; check_exit "master resultante reproducible" 0 "$?"

# -----------------------------------------------------------------------------
echo "=== R7: --remux sin el binario remux_tbuf ==="
FAKE2="$(make_fake_ew "$TESTTMP/fake_ew2" "$DEMUX" --no-remux)"
EW_HOME="$FAKE2" EW_VERSION=earthworm_8.0 "$MK_TANK" "$DUMMY" "$TESTTMP/out7" --remux \
    >"$TESTTMP/r7.log" 2>&1
check_exit "no aborta: sale 2 por el detector" 2 "$?"
check_contains "avisa de que falta el binario" "$TESTTMP/r7.log" "no existe .*remux_tbuf"

# -----------------------------------------------------------------------------
echo "=== R8: mseed real (lento) ==="
if [ "${TANK_TESTS_SLOW:-0}" != "1" ]; then
    skip "R8 (exporta TANK_TESTS_SLOW=1 para ejecutarlo: ~45 s y ~1,3 GB)"
elif [ ! -f "$MSEED_REAL" ]; then
    skip "R8 (no existe $MSEED_REAL)"
else
    R8="$TESTTMP/r8"
    "$MK_TANK" "$MSEED_REAL" "$R8" --nsamp 1008 >"$TESTTMP/r8a.log" 2>&1
    check_exit "mseed real sin --remux -> 2" 2 "$?"
    m1="$(tank_msgcount "$R8/master.tank")"
    echo "     ($m1 mensajes convertidos; ahora con --remux ...)"
    "$MK_TANK" "$MSEED_REAL" "$R8" --nsamp 1008 --remux >"$TESTTMP/r8b.log" 2>&1
    check_exit "mseed real con --remux -> 0" 0 "$?"
    check_eq "mismo numero de mensajes tras remux" "$m1" "$(tank_msgcount "$R8/master.tank")"
    check_order "$R8/master.tank"; check_exit "master real reproducible" 0 "$?"
    rm -f "$R8/master.tank.remux"
fi

summary
