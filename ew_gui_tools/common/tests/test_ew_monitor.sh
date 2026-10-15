#!/bin/bash
# test_ew_monitor - la consola de `startstop` se ACUMULA y cada arranque queda
# delimitado por su cabecera.
#
# Por que: `ew_monitor.sh` abria la consola con `>` en cada `start`, así que la
# salida del arranque anterior desaparecía justo cuando hacía falta (p. ej. el
# `SIGABRT` de `hypodb_ingest`, que no dejaba rastro en ningún otro sitio).
#
# No arranca nada real: `pgrep` y `startstop` de mentira en el `PATH`, `EW_UNIX`
# apuntando a un fichero vacío y `STARTSTOP_CONSOLE`/`EW_LOG_DIR` a un temporal.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
MON="$ROOT/ew_monitor.sh"
fail=0

[ -f "$MON" ] || { echo "FAIL: no existe $MON"; exit 1; }

need() {  # need <descripcion> <texto literal>
    if grep -qF -- "$2" "$MON"; then
        echo "ok  : $1"
    else
        echo "FAIL: $1 (falta en ew_monitor.sh: $2)"
        fail=1
    fi
}
nneedre() { # nneedre <descripcion> <patron ERE>  (no debe aparecer)
    if grep -qE -- "$2" "$MON"; then
        echo "FAIL: $1 (no debería estar en ew_monitor.sh: $2)"
        fail=1
    else
        echo "ok  : $1"
    fi
}

# --- invariantes de fuente ---
need  "consola: se añade con >>"          '>>"$STARTSTOP_CONSOLE"'
nneedre "consola: ya no se trunca con >"  '[^>]>"\$STARTSTOP_CONSOLE"'
need  "consola: cabecera por arranque"    'arranque de startstop'
need  "consola: cabecera con fecha UTC"   "date -u '+%Y-%m-%dT%H:%M:%SZ'"
need  "status: resuelve la ruta de params" 'params="$(ewmon_params_dir 2>/dev/null)"'
need  "status: imprime la ruta resuelta"   'echo "  EW_PARAMS : ${params:-<no definido>}"'

# --- funcional: dos arranques y la salida del primero debe seguir ahí ---
TMP="$(mktemp -d)" || exit 1
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/log"
: > "$TMP/unix.sh"                     # EW_UNIX de mentira: existe y no hace nada

cat > "$TMP/bin/startstop" <<'EOF'
#!/bin/sh
echo "stub-startstop run $$"
exit 0
EOF

# `pgrep` de mentira con estado por arranque: la PRIMERA llamada (la de
# `is_running`) responde vacío y crea el marcador; las siguientes (el sondeo que
# hace `start` tras lanzar) devuelven un pid, para no esperar 20 s.
cat > "$TMP/bin/pgrep" <<EOF
#!/bin/sh
if [ -f "$TMP/running" ]; then echo 4242; exit 0; fi
echo 4242 > "$TMP/running" 2>/dev/null || true
exit 1
EOF

chmod +x "$TMP/bin/startstop" "$TMP/bin/pgrep"

CONSOLE="$TMP/log/startstop_console.log"

arrancar() {   # arrancar <n>  -> lanza `ew_monitor.sh start` con los stubs
    # Arranque "de cero": se borra el estado del fixture Y el nuestro (el
    # pidfile), o `start` creeria que ya hay un stack corriendo.
    rm -f "$TMP/running" "$TMP/log/startstop.pid"
    PATH="$TMP/bin:$PATH" \
    EW_UNIX="$TMP/unix.sh" \
    EW_LOG_DIR="$TMP/log" \
    EWMON_PIDFILE="$TMP/log/startstop.pid" \
    STARTSTOP_CONSOLE="$CONSOLE" \
    EWMON_PID_SCOPE=loose \
        bash "$MON" start >/dev/null 2>&1
    # `start` lanza con setsid: esperamos a que la salida del stub llegue al fichero.
    i=0
    while [ "$i" -lt 30 ]; do
        [ "$(grep -c "stub-startstop run" "$CONSOLE" 2>/dev/null)" -ge "$1" ] && break
        sleep 0.1
        i=$((i + 1))
    done
}

arrancar 1
arrancar 2

if [ -f "$CONSOLE" ]; then
    runs="$(grep -c "stub-startstop run" "$CONSOLE")"
    heads="$(grep -c "arranque de startstop" "$CONSOLE")"
    if [ "$runs" -ge 2 ]; then
        echo "ok  : consola: sobrevive la salida del arranque anterior ($runs salidas)"
    else
        echo "FAIL: consola: sólo $runs salidas de startstop (¿se truncó?)"
        fail=1
    fi
    if [ "$heads" -ge 2 ]; then
        echo "ok  : consola: una cabecera por arranque ($heads cabeceras)"
    else
        echo "FAIL: consola: sólo $heads cabeceras"
        fail=1
    fi
else
    echo "FAIL: consola: no se creó $CONSOLE"
    fail=1
fi

# --- funcional: `status` resuelve la ruta de params -------------------------
# Imprimir la variable del shell que invoca daba "<no definido>" siempre que el
# entorno no estuviera cargado, aunque el script SI sepa de qué instancia habla.
# EW_MONITOR_CFG apunta a un fichero que no existe: `ewmon_status_modules`
# termina enseguida y no hace falta el binario de fábrica `status`.
params_line() {   # params_line <ew_unix> [ew_params]
    if [ -n "${2:-}" ]; then
        env -u EW_PARAMS "EW_PARAMS=$2" "EW_UNIX=$1" "PATH=$TMP/bin:$PATH" \
            "EW_LOG_DIR=$TMP/log" "EWMON_PIDFILE=$TMP/nopid.pid" \
            "EWMON_PID_SCOPE=loose" "EW_MONITOR_CFG=$TMP/nocfg.d" \
            bash "$MON" status 2>/dev/null | sed -n 's/^  EW_PARAMS : //p'
    else
        env -u EW_PARAMS "EW_UNIX=$1" "PATH=$TMP/bin:$PATH" \
            "EW_LOG_DIR=$TMP/log" "EWMON_PIDFILE=$TMP/nopid.pid" \
            "EWMON_PID_SCOPE=loose" "EW_MONITOR_CFG=$TMP/nocfg.d" \
            bash "$MON" status 2>/dev/null | sed -n 's/^  EW_PARAMS : //p'
    fi
}

got="$(params_line "$ROOT/ew8_unix.sh")"
if [ "$got" = "$ROOT/run_working_v8/params" ]; then
    echo "ok  : status: resuelve el EW_PARAMS del repo"
else
    echo "FAIL: status: EW_PARAMS dio '$got' (esperaba $ROOT/run_working_v8/params)"
    fail=1
fi

got="$(params_line "$TMP/unix.sh")"
if [ "$got" = "<no definido>" ]; then
    echo "ok  : status: si no puede deducirlo, dice '<no definido>'"
else
    echo "FAIL: status: sin deducción dio '$got' (esperaba <no definido>)"
    fail=1
fi

got="$(params_line "$TMP/unix.sh" "/tmp/x-params")"
if [ "$got" = "/tmp/x-params" ]; then
    echo "ok  : status: respeta el EW_PARAMS del llamante"
else
    echo "FAIL: status: el override del llamante dio '$got'"
    fail=1
fi

if [ "$fail" -gt 0 ]; then echo; echo "$fail FALLOS"; exit 1; fi
echo; echo "OK test_ew_monitor"
