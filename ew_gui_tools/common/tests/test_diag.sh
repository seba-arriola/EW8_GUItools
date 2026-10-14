#!/bin/bash
# test_diag - contrato de `ew_gui_tools/common/tools/diag.sh`.
#
# Por que: `diag.sh` es la UNICA puerta por la que un agente (plan) ejecuta algo
# en el proyecto. Su valor está en ser una lista CERRADA: si un subcomando de
# control (startstop/stopmodule/restart/ew_monitor.sh/make/git/sudo/rm/kill) se
# colara, el agente podría tumbar el stack en marcha creyendo que "diagnostica".
#
# No arranca EarthWorm ni ningún módulo real: los módulos se simulan con `true`
# y `sleep`, con DIAG_PARAMS/DIAG_LOG/DIAG_TMP en un temporal.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIAG="$ROOT/ew_gui_tools/common/tools/diag.sh"
fail=0

[ -f "$DIAG" ] || { echo "FAIL: no existe $DIAG"; exit 1; }

ok() { echo "ok  : $1"; }
ko() { echo "FAIL: $1"; fail=1; }

TMP="$(mktemp -d)" || exit 1
BG_PIDS=""
cleanup() {
    [ -n "$BG_PIDS" ] && kill $BG_PIDS 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

mkdir -p "$TMP/d" "$TMP/params" "$TMP/log"
: > "$TMP/params/true.d"
: > "$TMP/params/sleep.d"
printf 'linea uno\nlinea dos\nerror de mentira\n' > "$TMP/log/foo_20260101T000000Z.log"

# diag <args…> ; deja la salida en $TMP/out y el rc en $rc
rc=0
diag() {
    ( cd "$ROOT" && DIAG_PARAMS="$TMP/params" DIAG_LOG="$TMP/log" DIAG_TMP="$TMP/d" \
        sh "$DIAG" "$@" ) >"$TMP/out" 2>&1
    rc=$?
}

expect_rc() {  # expect_rc <descripcion> <esperado> <args…>
    local desc="$1" want="$2"; shift 2
    diag "$@"
    if [ "$rc" = "$want" ]; then
        ok "$desc (rc=$rc)"
    else
        ko "$desc: rc=$rc, se esperaba $want — $(head -n 2 "$TMP/out" | tr '\n' ' ')"
    fi
}

# --- uso ---------------------------------------------------------------------
expect_rc "sin argumentos es error de uso"        2
diag help
if [ "$rc" = 0 ]; then ok "help sale con 0"; else ko "help: rc=$rc"; fi
for sub in run rings process env config log; do
    if grep -q -- "$sub" "$TMP/out"; then ok "help lista '$sub'"; else ko "help no lista '$sub'"; fi
done

# --- lista cerrada: lo que NO es diagnóstico sale con 3 ----------------------
for bad in make startstop stopmodule restart ew_monitor.sh git kill pkill rm sudo \
           chmod mkdir cp mv tee not_a_subcommand; do
    expect_rc "'$bad' es subcomando prohibido" 3 "$bad"
done

# --- independencia del cwd ---------------------------------------------------
( cd /tmp && DIAG_PARAMS="$TMP/params" DIAG_LOG="$TMP/log" DIAG_TMP="$TMP/d" \
    sh "$DIAG" help ) >"$TMP/out" 2>&1
if [ "$?" = 0 ]; then ok "help funciona desde /tmp"; else ko "help desde /tmp falló"; fi
expect_rc "env funciona con el entorno sin cargar" 0 env

# --- nivel A -----------------------------------------------------------------
expect_rc "config de un módulo sin validador se rechaza" 3 config csntvp
expect_rc "config sin argumento es error de uso"          2 config
expect_rc "rings con nombre inválido se rechaza"          3 rings 'PICK/RING'
expect_rc "rings con nombre en minúsculas se rechaza"     3 rings pickring

diag logs
if [ "$rc" = 0 ] && grep -q 'foo_20260101' "$TMP/out"; then
    ok "logs lista el inventario"
else
    ko "logs: rc=$rc y no listó el log de prueba"
fi

diag log foo 2
if [ "$rc" = 0 ] && grep -q 'error de mentira' "$TMP/out"; then
    ok "log <modulo> [n] muestra las últimas líneas"
else
    ko "log foo: rc=$rc sin la última línea del log"
fi

diag log inexistente
if [ "$rc" = 1 ]; then ok "log de un módulo sin log sale con 1"; else ko "log inexistente: rc=$rc"; fi

diag errors
if [ "$rc" = 0 ] && grep -q 'error de mentira' "$TMP/out"; then
    ok "errors filtra las líneas de error"
else
    ko "errors: rc=$rc sin la línea de error del log de prueba"
fi

expect_rc "process no falla sin EarthWorm" 0 process

# --- nivel A: barrido de TODOS los subcomandos -------------------------------
# Un bashismo (`${@:3}`, `"${a[@]}"`) no falla al *parsear* sino al *expandir*,
# asi que `sh -n` no lo ve: hay que ejecutarlos. `$sub` va sin comillas a
# proposito para poder pasar dos palabras ("config csnhypodbp").
for sub in "env" "install" "config csnhypodbp" "profiles" "overlays csnrv" \
           "logs" "errors" "process"; do
    diag $sub
    if grep -qiE 'bad substitution|not found|unexpected|syntax error' "$TMP/out"; then
        ko "subcomando '$sub' revienta al ejecutarse: $(head -n 2 "$TMP/out" | tr '\n' ' ')"
    else
        ok "subcomando '$sub' se ejecuta sin errores de shell"
    fi
done

# --- nivel B: guardas --------------------------------------------------------
expect_rc "run de un módulo no declarado se rechaza"     3 run nosuchmod 2
expect_rc "run con segundos fuera de rango se rechaza"   3 run true 999
expect_rc "run con segundos no numéricos se rechaza"     3 run true abc
expect_rc "run con opción desconocida se rechaza"        3 run true 2 --daemon
expect_rc "stop de un módulo que no lanzamos se rechaza" 3 stop nosuchmod
expect_rc "attached sin salida previa falla"             1 attached nosuchmod

# --- nivel B: precondición "ya está corriendo" -------------------------------
sleep 30 &
BG_PIDS="$!"
sleep 0.3
expect_rc "run rechaza un módulo que ya corre (exit 4)" 4 run sleep 2

# --- nivel A: los parsers del .d con el FORMATO REAL -------------------------
# Formato real:  Process\t  "bin bin.d"   y   Ring   WAVE_RING   51200
# (un parser ingenuo se queda con la comilla de `Process` -> pgrep nunca casa y
#  el módulo se ve "apagado" para siempre; y con el TAMANO del anillo).
printf 'Ring    FAKE_RING   128\nProcess\t  "sleep sleep.d"\n' > "$TMP/params/startstop_unix.d"

diag process
if [ "$rc" = 0 ] && grep -q 'sleep' "$TMP/out"; then
    ok "process lee el formato real de Process y encuentra el pid"
else
    ko "process no reconoce las líneas Process del .d: $(head -n 2 "$TMP/out" | tr '\n' ' ')"
fi

diag rings
if grep -q -- '--- FAKE_RING ---' "$TMP/out" && ! grep -q -- '--- 128 ---' "$TMP/out"; then
    ok "rings toma el NOMBRE del anillo y no su tamaño"
else
    ko "rings parsea mal las líneas Ring: $(head -n 3 "$TMP/out" | tr '\n' ' ')"
fi

# --- nivel B: arranque real y acotado ---------------------------------------
diag run true 2
if [ "$rc" = 0 ] && grep -q 'EXIT: 0' "$TMP/out" && grep -q 'CMD:' "$TMP/out"; then
    ok "run de un módulo declarado arranca y reporta CMD/EXIT"
else
    ko "run true: rc=$rc — $(head -n 3 "$TMP/out" | tr '\n' ' ')"
fi

# --- no toca la configuración del proyecto -----------------------------------
# Se compara NOMBRE + TAMAÑO (no mtime): el entorno mantiene ficheros de estado
# volatiles que se re-estampan solos (p.ej. run_working_v8/params/slink126.state,
# que algo del cliente SeedLink reescribe cada pocos segundos). Lo que se
# verifica es que diag.sh no cree, borre ni reescriba configuracion.
PDIR="$ROOT/run_working_v8/params"
if [ -d "$PDIR" ]; then
    snap() {
        ls -1 "$PDIR" 2>/dev/null | while IFS= read -r f; do
            case "$f" in *.state) continue ;; esac
            stat -c '%n %s' "$PDIR/$f" 2>/dev/null
        done
    }
    snap > "$TMP/params.before"
    diag process
    diag logs
    diag errors
    diag env
    snap > "$TMP/params.after"
    if diff -q "$TMP/params.before" "$TMP/params.after" >/dev/null; then
        ok "los subcomandos de lectura no crean ni cambian archivos de run_working_v8/params"
    else
        ko "run_working_v8/params cambió tras subcomandos de solo lectura:"
        diff "$TMP/params.before" "$TMP/params.after" | sed -n '1,10p'
    fi
fi

if [ "$fail" -gt 0 ]; then echo; echo "$fail FALLOS"; exit 1; fi
echo; echo "OK test_diag"
