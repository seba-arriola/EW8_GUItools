#!/bin/bash
# ---------------------------------------------------------------------------
# test_config.sh - Test headless de configuracion de csnrv
#
# Verifica el contrato de modo headless:
#   ./csnrv --print-config <archivo.d>
# que imprime por stdout:
#   InitialZoom=<%.6f>
#   MapImageFile=<ruta>
# y retorna 0 SIN inicializar GTK (no requiere DISPLAY).
#
# Cubre la nueva clave opcional InitialZoom y su validacion/clamp [0.2, 50.0].
# ---------------------------------------------------------------------------
set -u

BIN="${BIN:-./csnrv}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

# write_cfg <archivo> <linea_extra...>
write_cfg() {
    local f="$1"; shift
    {
        echo "MyModuleId      MOD_CSNRV"
        echo "RingName        HYPO_RING"
        echo "HeartBeatInt    30"
        echo "LogFile         1"
        echo "QuakeFile       csnhypodbp_hist.txt"
        echo "MapImageFile    world_map.jpg"
        for extra in "$@"; do echo "$extra"; done
    } > "$f"
}

# run_cfg <archivo>  -> stdout en $OUT, stderr en $ERR, retorno en $RC
run_cfg() {
    OUT="$("$BIN" --print-config "$1" 2>"$TMP/stderr")"
    RC=$?
    ERR="$(cat "$TMP/stderr")"
}

# check_eq <descripcion> <esperado> <actual>
check_eq() {
    local desc="$1" exp="$2" got="$3"
    if [ "$exp" = "$got" ]; then
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc (esperado='$exp' obtenido='$got')"
        FAIL=$((FAIL + 1))
    fi
}

check_contains() {
    local desc="$1" needle="$2" hay="$3"
    case "$hay" in
        *"$needle"*) echo "  PASS: $desc"; PASS=$((PASS + 1)) ;;
        *) echo "  FAIL: $desc (no contiene '$needle'; texto='$hay')"; FAIL=$((FAIL + 1)) ;;
    esac
}

field() { printf '%s\n' "$1" | sed -n "s/^$2=//p"; }

echo "== csnrv test_config =="

# --- T1: sin InitialZoom -> default 8.0 -----------------------------------
echo "T1: InitialZoom ausente -> default 8.0"
write_cfg "$TMP/t1.d"
run_cfg "$TMP/t1.d"
check_eq "retorno 0" "0" "$RC"
check_eq "InitialZoom default" "8.000000" "$(field "$OUT" InitialZoom)"
check_eq "MapImageFile" "world_map.jpg" "$(field "$OUT" MapImageFile)"

# --- T2: InitialZoom 2.0 --------------------------------------------------
echo "T2: InitialZoom 2.0 -> 2.0"
write_cfg "$TMP/t2.d" "InitialZoom     2.0"
run_cfg "$TMP/t2.d"
check_eq "retorno 0" "0" "$RC"
check_eq "InitialZoom" "2.000000" "$(field "$OUT" InitialZoom)"

# --- T3: InitialZoom 999 -> clamp 50.0 + warning --------------------------
echo "T3: InitialZoom 999 -> clamp 50.0 + aviso"
write_cfg "$TMP/t3.d" "InitialZoom     999"
run_cfg "$TMP/t3.d"
check_eq "retorno 0" "0" "$RC"
check_eq "InitialZoom clamp alto" "50.000000" "$(field "$OUT" InitialZoom)"
check_contains "aviso en stderr" "InitialZoom" "$ERR"

# --- T4: InitialZoom 0.01 -> clamp 0.2 + warning --------------------------
echo "T4: InitialZoom 0.01 -> clamp 0.2 + aviso"
write_cfg "$TMP/t4.d" "InitialZoom     0.01"
run_cfg "$TMP/t4.d"
check_eq "retorno 0" "0" "$RC"
check_eq "InitialZoom clamp bajo" "0.200000" "$(field "$OUT" InitialZoom)"
check_contains "aviso en stderr" "InitialZoom" "$ERR"

# --- T5: MapImageFile con Chile -------------------------------------------
echo "T5: MapImageFile world_map_con_chile.jpg"
write_cfg "$TMP/t5.d" "MapImageFile    world_map_con_chile.jpg"
run_cfg "$TMP/t5.d"
check_eq "retorno 0" "0" "$RC"
check_eq "MapImageFile" "world_map_con_chile.jpg" "$(field "$OUT" MapImageFile)"

# --- T6: falta de clave obligatoria -> fallo ------------------------------
echo "T6: falta MapImageFile -> fallo (no regresion)"
{
    echo "MyModuleId      MOD_CSNRV"
    echo "RingName        HYPO_RING"
    echo "HeartBeatInt    30"
    echo "LogFile         1"
    echo "QuakeFile       csnhypodbp_hist.txt"
} > "$TMP/t6.d"
run_cfg "$TMP/t6.d"
if [ "$RC" -ne 0 ]; then
    echo "  PASS: retorno != 0"; PASS=$((PASS + 1))
else
    echo "  FAIL: retorno esperado != 0, obtenido $RC"; FAIL=$((FAIL + 1))
fi

echo "== Resultado: $PASS PASS / $FAIL FAIL =="
[ "$FAIL" -eq 0 ]
