#!/bin/bash
# test_ew_monitor_module - el subcomando de modulos de `ew_monitor.sh`:
#
#   * la lista de modulos sale de la configuracion de startstop (nada literal:
#     una lista hardcodeada se queda vieja y encima puede matar de mas),
#   * un nombre AMBIGUO (dos modulos con el mismo binario y distinto .d) no se
#     resuelve "al primero": se avisa y no se envia nada,
#   * `restart`/`stop` van POR PID a los binarios de fabrica y se VERIFICAN (los
#     de fabrica salen con 0 aunque no hagan nada),
#   * `EW_PARAMS`/`EW_LOG` del llamante sobreviven al `source ew8_unix.sh`.
#
# No arranca ni manda nada real: `pgrep`, `kill`, `sleep`, `status`, `restart` y
# `stopmodule` son de mentira y viven en un PATH propio.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
MON="$ROOT/ew_monitor.sh"
fail=0
ok() { echo "ok  : $1"; }
ko() { echo "FAIL: $1"; fail=1; }

[ -f "$MON" ] || { echo "FAIL: no existe $MON"; exit 1; }

TMP="$(mktemp -d)" || exit 1
# `startstop` de mentira: un proceso REAL del propio test, nunca un pid
# inventado (un `kill` que se colase no debe poder tocar nada ajeno).
sleep 300 & FAKE_SPID=$!
trap 'kill '"$FAKE_SPID"' 2>/dev/null; rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/log" "$TMP/params"
echo "$FAKE_SPID" > "$TMP/fake-startstop-pid"

# `kill` es un builtin de bash: sin desactivarlo, el stub del PATH no se usa
# (ni queda registro). BASH_ENV lo desactiva SOLO en los shells hijos.
printf 'enable -n kill\n' > "$TMP/no-builtin-kill.sh"

# ---------------------------------------------------------------------------
# 1) invariantes de fuente
# ---------------------------------------------------------------------------
need() {  # need <descripcion> <texto literal>
    if grep -qF -- "$2" "$MON"; then ok "$1"; else ko "$1 (falta en ew_monitor.sh: $2)"; fi
}
nneedre() { # nneedre <descripcion> <patron ERE>  (no debe aparecer)
    if grep -qE -- "$2" "$MON"; then ko "$1 (no debería estar: $2)"; else ok "$1"; fi
}

need    "los modulos salen del .d de startstop"  'ewmon_cfg_modules'
need    "los huerfanos se buscan por nombre EXACTO" 'pgrep -x'
need    "las acciones por modulo van por PID"    'ewmon_bin restart'
need    "sin argumentos start/stop/restart siguen siendo el stack" 'if [ "$#" -eq 0 ]; then start;'
nneedre "sin pgrep -f (mataba por linea de comandos)" 'pgrep[[:space:]]+-f'
nneedre "sin NINGUN nombre de modulo literal" 'slink2ew|pick_FP|pickS|csnloc|csnmags|hypodb_ingest|hyp2000_ring|nlloc_ring|wstore_ingest|csntvp|csnhypodbp|csnrv|csnstaevdisp|ew_controller|wave_serverV'

# ---------------------------------------------------------------------------
# 2) entorno falso
# ---------------------------------------------------------------------------
# `ew8_unix.sh` de mentira: existe y ADEMAS pisa EW_PARAMS/EW_LOG, que es lo que
# hace el de verdad (ew8_unix.sh:12-15). Si el script no los restaurase, todas
# las pruebas de abajo leerian un .d inexistente.
cat > "$TMP/unix.sh" <<'EOF'
export EW_PARAMS=/nope
export EW_LOG=/nope
EOF

# Configuracion de startstop de mentira (mismos casos: un solo Process sin .d,
# uno comentado, dos homonimos y un binario cuyo nombre no es el del .d).
cat > "$TMP/params/startstop_unix.d" <<'EOF'
 Ring WAVE_RING 1000
 Process    "slink2ew"
 Class/Priority    OTHER 0
 Process    "csnloc csnloc.d"
 # Process    "otro otro.d"
 Process    "pickS pickS.d"
 Process    "csnmags csnmags.d"
 Process    "csnmags csnmags_raw.d"
 Process    "hypodb_ingest hypodb.d"
 Process    "csnrv csnrv.d"
EOF

# `status` de mentira: el MISMO formato que EncodeStatus, con lo que va ANTES de
# la tabla (cabecera, anillos y resumen, que tienen columnas numericas) y con la
# fila del padre incluida: las dos cosas deben quedar fuera de `list`.
cat > "$TMP/bin/status" <<'EOF'
#!/bin/bash
TMP="$(cd "$(dirname "$0")/.." && pwd)"
printf '%s\n' "$*" >> "$TMP/status.log"
pidof() { cat "$TMP/pid-$1" 2>/dev/null || echo "$2"; }
st() { if [ -f "$TMP/pid-$1" ]; then echo Alive; else echo "$2"; fi; }
printf '\r'
cat <<'TAB'
                    EARTHWORM-64 SYSTEM STATUS

        Hostname-OS:            testhost - Linux 6.0
        Start time (UTC):       Wed Oct 14 03:17:53 2026
        Current time (UTC):     Wed Oct 14 07:12:45 2026
        Disk space avail:       642872236 kb
        Ring  1 name/key/size:  WAVE_RING / 1000 / 51200 kb
        Ring  2 name/key/size:  SCN_RING / 1040 / 500 kb
        Startstop's Log Dir:    /tmp/log
        Startstop's Params Dir: /tmp/params
        Startstop Version:      v8.0b1 2024-12-09 (64 bit)

         Process  Process               Class/    CPU
          Name      Id      Status     Priority   Used    Argument
         -------  -------   ------     --------   ----    --------
TAB
row() { printf '%13s %7s    %-9s  %s  %-9s %s\n' "$1" "$2" "$3" "??/ 0" "00:00:01" "$4"; }
row startstop 4242 Alive -
row csnloc "$(pidof csnloc 9002)" Alive csnloc.d
[ -f "$TMP/gone-pickS" ] || row pickS "$(pidof pickS 9005)" Alive pickS.d
row csnmags 9003 Alive csnmags.d
row csnmags "$(pidof csnmags_raw 9004)" "$(st csnmags_raw Zombie)" csnmags_raw.d
row hypodb_ingest "$(pidof hypodb_ingest 9006)" Alive hypodb.d
row csnrv 9010 Alive csnrv.d
EOF

# `pgrep` de mentira: responde al sondeo de startstop (4242) y a los hijos
# (`-P`), y REGISTRA cada invocacion (para auditar que se busca por `-x`).
cat > "$TMP/bin/pgrep" <<'EOF'
#!/bin/sh
TMP="$(cd "$(dirname "$0")/.." && pwd)"
printf '%s\n' "$*" >> "$TMP/pgrep.log"
case " $* " in
    *" -x startstop "*) [ -n "${EWMON_TEST_NOSTARTSTOP:-}" ] || cat "$TMP/fake-startstop-pid" ;;
esac
case " $* " in
    *" -P "*) echo 9001 ;;
esac
exit 0
EOF

# `restart` de mentira: registra `-c <cfg> <pid>` y hace "volver" al modulo con
# pid NUEVO (asi la verificacion puede tener exito).
cat > "$TMP/bin/restart" <<'EOF'
#!/bin/sh
TMP="$(cd "$(dirname "$0")/.." && pwd)"
printf '%s\n' "$*" >> "$TMP/restart.log"
case "$3" in
    9002) echo 9102 > "$TMP/pid-csnloc" ;;
    9004) echo 9104 > "$TMP/pid-csnmags_raw" ;;
    9006) echo 9106 > "$TMP/pid-hypodb_ingest" ;;
esac
exit 0
EOF

# `stopmodule` de mentira: registra `<cfg> <pid>` y hace DESAPARECER el modulo.
cat > "$TMP/bin/stopmodule" <<'EOF'
#!/bin/sh
TMP="$(cd "$(dirname "$0")/.." && pwd)"
printf '%s\n' "$*" >> "$TMP/stopmodule.log"
[ "$2" = 9005 ] && : > "$TMP/gone-pickS"
exit 0
EOF

# `kill`/`sleep` de mentira: la parada del stack NO debe matar nada de verdad.
cat > "$TMP/bin/kill" <<'EOF'
#!/bin/sh
TMP="$(cd "$(dirname "$0")/.." && pwd)"
printf '%s\n' "$*" >> "$TMP/kill.log"
exit 0
EOF
printf '#!/bin/sh\nexit 0\n' > "$TMP/bin/sleep"

# Binarios de los modulos que SI estan instalados (la guarda de la accion
# comprueba el PATH). `csnrv` NO se crea a proposito: es el caso "NoExec".
for b in csnloc pickS csnmags hypodb_ingest; do
    printf '#!/bin/sh\nexit 0\n' > "$TMP/bin/$b"
done
chmod +x "$TMP/bin/status" "$TMP/bin/pgrep" "$TMP/bin/restart" \
         "$TMP/bin/stopmodule" "$TMP/bin/kill" "$TMP/bin/sleep" \
         "$TMP/bin/csnloc" "$TMP/bin/pickS" "$TMP/bin/csnmags" "$TMP/bin/hypodb_ingest"

mon() {   # mon <args ew_monitor.sh>  -> con el PATH y el entorno de mentira
    # PATH CERRADO (sólo los stubs y el sistema): si se colase el bin de
    # EarthWorm -p. ej. porque el que invoca hizo `source ew8_unix.sh`- las
    # pruebas "el binario no existe" dejarian de probar nada.
    PATH="$TMP/bin:/usr/bin:/bin" \
    BASH_ENV="$TMP/no-builtin-kill.sh" \
    EW_UNIX="$TMP/unix.sh" \
    EW_LOG_DIR="$TMP/log" \
    EW_PARAMS="$TMP/params" \
    EWMON_VERIFY_TIMEOUT=1 \
        bash "$MON" "$@"
}
reset() { rm -f "$TMP/restart.log" "$TMP/stopmodule.log" "$TMP/status.log" \
                 "$TMP/pid-csnloc" "$TMP/pid-csnmags_raw" "$TMP/pid-hypodb_ingest" "$TMP/gone-pickS"; }

# ---------------------------------------------------------------------------
# 3) `list`: el .d entero, con pid/estado, sin el propio startstop
# ---------------------------------------------------------------------------
reset
out="$(mon list 2>&1)"; rc=$?
[ "$rc" -eq 0 ] && ok "list: rc 0" || ko "list: rc=$rc [$out]"
grep -qE 'MODULO +BINARIO +CFG +PID +ESTADO' <<< "$out" && ok "list: cabecera" || ko "list: sin cabecera [$out]"
for pat in 'csnloc +csnloc +csnloc\.d +9002 +Alive' \
           'pickS +pickS +pickS\.d +9005 +Alive' \
           'csnmags +csnmags +csnmags\.d +9003 +Alive' \
           'csnmags_raw +csnmags +csnmags_raw\.d +9004 +Zombie' \
           'hypodb +hypodb_ingest +hypodb\.d +9006 +Alive' \
           'csnrv +csnrv +csnrv\.d +9010 +Alive' \
           'slink2ew +slink2ew +- +- +ausente'; do
    if grep -qE -- "^$pat" <<< "$out"; then ok "list: $pat"; else ko "list: falta $pat [$out]"; fi
done
grep -qE '^startstop ' <<< "$out" && ko "list: lista al propio startstop" || ok "list: no lista startstop"
n_rows="$(grep -cE '^(slink2ew|csnloc|pickS|csnmags|csnmags_raw|hypodb|csnrv) ' <<< "$out")"
[ "$n_rows" -eq 7 ] && ok "list: una fila por Process declarado (7)" \
    || ko "list: $n_rows filas (esperadas 7) [$out]"
grep -qE 'Ring|name/key/size' <<< "$out" && ko "list: colaron las lineas de anillos del status" \
    || ok "list: ignora las lineas de anillos (2.a columna numerica) del status"
grep -qF "$TMP/params/startstop_unix.d" "$TMP/status.log" \
    && ok "list: usa el .d de EW_PARAMS (sobrevive al source de ew8_unix.sh)" \
    || ko "list: no paso el .d al status [$(cat "$TMP/status.log" 2>/dev/null)]"

# startstop caido: la lista sale solo del .d y no falla
reset
out="$( export EWMON_TEST_NOSTARTSTOP=1; mon list 2>&1 )"; rc=$?
[ "$rc" -eq 0 ] && ok "list sin startstop: rc 0" || ko "list sin startstop: rc=$rc [$out]"
grep -q 'NOTA' <<< "$out" && ok "list sin startstop: avisa" || ko "list sin startstop: sin aviso [$out]"
[ "$(grep -c 'ausente' <<< "$out")" -ge 6 ] && ok "list sin startstop: todo ausente" \
    || ko "list sin startstop: pocos ausentes [$out]"

# ---------------------------------------------------------------------------
# 4) resolucion de <modulo>: nombre ambiguo = exit 2 y NO se envia nada
# ---------------------------------------------------------------------------
reset
out="$(mon restart csnmags 2>&1)"; rc=$?
[ "$rc" -eq 2 ] && ok "ambiguo: rc 2" || ko "ambiguo: rc=$rc [$out]"
{ grep -q 9003 <<< "$out" && grep -q 9004 <<< "$out"; } && ok "ambiguo: lista los dos candidatos" \
    || ko "ambiguo: no lista candidatos [$out]"
[ ! -s "$TMP/restart.log" ] && ok "ambiguo: no se envio nada" \
    || ko "ambiguo: se envio [$(cat "$TMP/restart.log")]"

reset
out="$(mon restart no-existe 2>&1)"; rc=$?
[ "$rc" -eq 2 ] && ok "desconocido: rc 2" || ko "desconocido: rc=$rc [$out]"
[ ! -s "$TMP/restart.log" ] && ok "desconocido: no se envio nada" || ko "desconocido: se envio algo"

# ---------------------------------------------------------------------------
# 5) restart por tallo del .d y por nombre cuando binario y .d NO coinciden
# ---------------------------------------------------------------------------
reset
out="$(mon restart csnmags_raw 2>&1)"; rc=$?
[ "$rc" -eq 0 ] && ok "restart <tallo>: rc 0" || ko "restart <tallo>: rc=$rc [$out]"
grep -qE -- "-c $TMP/params/startstop_unix\.d 9004$" "$TMP/restart.log" \
    && ok "restart <tallo>: manda el pid 9004 (no el del otro homonimo)" \
    || ko "restart <tallo>: pid equivocado [$(cat "$TMP/restart.log" 2>/dev/null)]"
grep -qE '9004 -> 9104' <<< "$out" && ok "restart <tallo>: confirma el pid nuevo" \
    || ko "restart <tallo>: sin confirmacion [$out]"

reset
out="$(mon restart hypodb 2>&1)"; rc=$?
[ "$rc" -eq 0 ] && ok "restart <tallo con otro binario>: rc 0" || ko "restart <tallo con otro binario>: rc=$rc [$out]"
grep -qE -- '-c .* 9006$' "$TMP/restart.log" && ok "restart <tallo con otro binario>: pid 9006" \
    || ko "restart <tallo con otro binario>: pid equivocado [$(cat "$TMP/restart.log" 2>/dev/null)]"

# ---------------------------------------------------------------------------
# 6) verificacion: sin confirmacion el script NO puede decir que fue bien
# ---------------------------------------------------------------------------
reset
out="$(mon restart pickS 2>&1)"; rc=$?
grep -qE -- "-c .* 9005$" "$TMP/restart.log" && ok "verificacion: se envio el restart" \
    || ko "verificacion: no se envio"
[ "$rc" -eq 4 ] && ok "verificacion: rc 4 (no volvio con pid nuevo)" || ko "verificacion: rc=$rc [$out]"
grep -q 'no volvio' <<< "$out" && ok "verificacion: lo dice" || ko "verificacion: no lo dice [$out]"

# ---------------------------------------------------------------------------
# 7) stop <modulo> y --dry-run / guarda de binario ausente
# ---------------------------------------------------------------------------
reset
out="$(mon stop pickS 2>&1)"; rc=$?
[ "$rc" -eq 0 ] && ok "stop <modulo>: rc 0" || ko "stop <modulo>: rc=$rc [$out]"
grep -qE -- "^$TMP/params/startstop_unix\.d 9005$" "$TMP/stopmodule.log" \
    && ok "stop <modulo>: stopmodule con el pid" \
    || ko "stop <modulo>: mal invocado [$(cat "$TMP/stopmodule.log" 2>/dev/null)]"

reset
out="$(mon restart --dry-run csnloc 2>&1)"; rc=$?
[ "$rc" -eq 0 ] && ok "dry-run: rc 0" || ko "dry-run: rc=$rc [$out]"
grep -q 'DRY-RUN' <<< "$out" && ok "dry-run: lo anuncia" || ko "dry-run: sin anuncio [$out]"
[ ! -s "$TMP/restart.log" ] && ok "dry-run: no envio nada" || ko "dry-run: envio algo"

reset
out="$(mon restart csnrv 2>&1)"; rc=$?
[ "$rc" -eq 4 ] && ok "binario ausente: rc 4" || ko "binario ausente: rc=$rc [$out]"
grep -q 'NoExec' <<< "$out" && ok "binario ausente: explica el NoExec" || ko "binario ausente: sin explicacion [$out]"
[ ! -s "$TMP/restart.log" ] && ok "binario ausente: no envio nada" || ko "binario ausente: envio algo"

reset
out="$(mon restart --force pickS 2>&1)"
grep -qE -- "-c .* 9005$" "$TMP/restart.log" && ok "--force: salta la guarda" || ko "--force: no salto la guarda"

# ---------------------------------------------------------------------------
# 8) `stop` del stack: los huerfanos por pgrep -x DERIVADO del .d, nunca -f
# ---------------------------------------------------------------------------
reset
mon stop >/dev/null 2>&1
alt="$(grep -E '^-x ' "$TMP/pgrep.log" | grep -v 'startstop' | head -n1)"
case "$alt" in
    *hypodb_ingest*) ok "stop stack: el patron sale del .d ($alt)" ;;
    *) ko "stop stack: patron no derivado [$alt]" ;;
esac
grep -q -- '-f' "$TMP/pgrep.log" && ko "stop stack: uso pgrep -f" || ok "stop stack: sin pgrep -f"
grep -q 9001 "$TMP/kill.log" && ok "stop stack: mato a los hijos capturados antes" \
    || ko "stop stack: no mato al hijo [$(cat "$TMP/kill.log" 2>/dev/null)]"

# ---------------------------------------------------------------------------
# 9) EW_MONITOR_CFG manda sobre el .d por defecto; -h no rompe nada
# ---------------------------------------------------------------------------
printf ' Process    "csnloc csnloc.d"\n' > "$TMP/params/startstop_replay.d"
reset
out="$( export EW_MONITOR_CFG="$TMP/params/startstop_replay.d"; mon list 2>&1 )"
grep -qE 'csnloc +csnloc +csnloc\.d +9002' <<< "$out" && ok "EW_MONITOR_CFG: usa ese .d" || ko "EW_MONITOR_CFG [$out]"
grep -qE '^slink2ew ' <<< "$out" && ko "EW_MONITOR_CFG: sigue leyendo el .d por defecto" \
    || ok "EW_MONITOR_CFG: no mezcla el .d por defecto"
grep -qE '^csnmags ' <<< "$out" && ok "EW_MONITOR_CFG: los vivos que el .d no declara se añaden" \
    || ko "EW_MONITOR_CFG: perdio los modulos vivos [$out]"

out="$(mon -h 2>&1)"; rc=$?
[ "$rc" -eq 0 ] && ok "-h: rc 0" || ko "-h: rc=$rc"
grep -q 'list' <<< "$out" && ok "-h: menciona list" || ko "-h: sin list [$out]"

out="$(mon loquesea 2>&1)"; rc=$?
[ "$rc" -eq 2 ] && ok "subcomando invalido: rc 2" || ko "subcomando invalido: rc=$rc [$out]"

if [ "$fail" -gt 0 ]; then echo; echo "$fail FALLOS"; exit 1; fi
echo; echo "OK test_ew_monitor_module"
