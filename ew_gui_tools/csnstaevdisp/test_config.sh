#!/bin/bash
# ---------------------------------------------------------------------------
# test_config.sh - Test headless de configuracion/overlays de csnstaevdisp
#
# Verifica el modo headless:
#   ./csnstaevdisp --check-overlays <archivo.d>
# que imprime "overlay_layers=<N> overlay_polylines=<M>" y retorna 0 sin GTK.
# ---------------------------------------------------------------------------
set -u

BIN="${BIN:-./csnstaevdisp}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

write_cfg() {
    local f="$1"; shift
    {
        echo "MyModuleId      MOD_STAEVDISP"
        echo "RingName        HYPO_RING"
        echo "HeartBeatInt    30"
        echo "LogFile         1"
        echo "StaFile         estaciones_107.txt"
        echo "DbEnvFile       db.env"
        echo "MapImageFile    world_map.jpg"
        echo "StationSize     15"
        for extra in "$@"; do echo "$extra"; done
    } > "$f"
}

check_contains() {
    local desc="$1" needle="$2" hay="$3"
    case "$hay" in
        *"$needle"*) echo "  PASS: $desc"; PASS=$((PASS + 1)) ;;
        *) echo "  FAIL: $desc (no contiene '$needle'; texto='$hay')"; FAIL=$((FAIL + 1)) ;;
    esac
}

echo "== csnstaevdisp test_config =="

# --- T1: sin OverlayFile -> 0 capas ---------------------------------------
echo "T1: sin OverlayFile -> 0 capas"
write_cfg "$TMP/t1.d"
OUT="$("$BIN" --check-overlays "$TMP/t1.d" 2>"$TMP/err")"
RC=$?
if [ "$RC" -eq 0 ]; then echo "  PASS: retorno 0"; PASS=$((PASS + 1));
else echo "  FAIL: retorno $RC"; FAIL=$((FAIL + 1)); fi
check_contains "0 capas" "overlay_layers=0" "$OUT"

# --- T2: con OverlayFile --------------------------------------------------
echo "T2: OverlayFile -> 1 capa / 2 polilineas"
{
    echo "#layer name=a color=FF0000"
    echo "0 0"; echo "1 1"; echo ">"; echo "2 2"
} > "$TMP/ov.lines"
write_cfg "$TMP/t2.d" "OverlayFile     $TMP/ov.lines"
OUT="$("$BIN" --check-overlays "$TMP/t2.d" 2>"$TMP/err")"
RC=$?
if [ "$RC" -eq 0 ]; then echo "  PASS: retorno 0"; PASS=$((PASS + 1));
else echo "  FAIL: retorno $RC"; FAIL=$((FAIL + 1)); fi
check_contains "1 capa" "overlay_layers=1" "$OUT"
check_contains "2 polilineas" "overlay_polylines=2" "$OUT"

# --- T3: falta StationSize -> fallo (no regresion) ------------------------
echo "T3: falta StationSize -> fallo"
{
    echo "MyModuleId      MOD_STAEVDISP"
    echo "RingName        HYPO_RING"
    echo "HeartBeatInt    30"
    echo "LogFile         1"
    echo "StaFile         estaciones_107.txt"
    echo "QuakeFile       csnhypodbp_hist.txt"
    echo "MapImageFile    world_map.jpg"
} > "$TMP/t3.d"
OUT="$("$BIN" --check-overlays "$TMP/t3.d" 2>/dev/null)"
RC=$?
if [ "$RC" -ne 0 ]; then echo "  PASS: retorno != 0"; PASS=$((PASS + 1));
else echo "  FAIL: retorno esperado != 0, obtenido $RC"; FAIL=$((FAIL + 1)); fi

# --- T4: OverlayHide apaga una capa (y el reporte lo dice) ----------------
echo "T4: OverlayHide"
{
    echo "#layer name=visib color=FF0000 width=2"
    echo "0 0"; echo "1 1"
    echo "#layer name=oculta color=0000FF width=2"
    echo "2 2"; echo "3 3"
} > "$TMP/ov2.lines"
write_cfg "$TMP/t4.d" "OverlayFile     $TMP/ov2.lines" "OverlayHide     oculta"
OUT="$("$BIN" --check-overlays "$TMP/t4.d" 2>"$TMP/err")"
RC=$?
if [ "$RC" -eq 0 ]; then echo "  PASS: retorno 0"; PASS=$((PASS + 1));
else echo "  FAIL: retorno $RC"; FAIL=$((FAIL + 1)); fi
check_contains "2 capas" "overlay_layers=2" "$OUT"
check_contains "la otra sigue visible" \
    "layer visib visible=1 color=FF0000 width=2.0 polylines=1 vertices=2" "$OUT"
check_contains "la pedida queda apagada" \
    "layer oculta visible=0 color=0000FF width=2.0 polylines=1 vertices=2" "$OUT"

echo "== Resultado: $PASS PASS / $FAIL FAIL =="
[ "$FAIL" -eq 0 ]
