#!/bin/bash
# Invariante de FUENTE y de ENTORNO: en este repo `timeout` debe ser SIEMPRE el
# de GNU coreutils.
#
# Por que: en este entorno ~/seiscomp/bin queda ANTES de /usr/bin en el PATH y su
# `timeout` NO es el de GNU coreutils: rechaza la sintaxis estandar
# `timeout <segundos> <comando>` y falla con "couldn't exec <N>". Ver AGENTS.md §7.
#
# Verifica:
#   (a) ningun *.sh / Makefile / *.mk invoca el binario sin ruta absoluta;
#   (b) ew8_unix.sh antepone tools/gnubin al PATH y con eso `timeout` a secas
#       resuelve al shim;
#   (c) el shim existe, es ejecutable y delega en el timeout de GNU.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SHIM_DIR="$ROOT/ew_gui_tools/common/tools/gnubin"
SHIM="$SHIM_DIR/timeout"
# Token `timeout` usado COMO COMANDO: no le precede alfanumerico, '_', '.', '-'
# ni '/' (eso excluye --timeout, TIMEOUT, ws_timeout, /usr/bin/timeout...) y tras
# el token viene una duracion, una opcion, una variable o una comilla doble
# (excluye prosa como "timeout de SeisComP").
BARE='(^|[^[:alnum:]_./-])timeout[[:space:]]+([0-9]|\$|"|-)'
# Consultar donde vive el binario no es invocarlo.
QUERY='command[[:space:]]+-[vV]|^[[:space:]]*(which|type)[[:space:]]'
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# --- (c) el shim -----------------------------------------------------------
if [ ! -f "$SHIM" ]; then
    bad "falta el shim $SHIM"
elif [ ! -x "$SHIM" ]; then
    bad "el shim no es ejecutable: $SHIM"
else
    ok "shim presente y ejecutable"
    if grep -q 'exec' "$SHIM" && grep -q '/usr/bin/timeout' "$SHIM"; then
        ok "el shim delega en GNU coreutils"
    else
        bad "el shim no delega en /usr/bin/timeout"
    fi
    "$SHIM" 0.2 sleep 5 >/dev/null 2>&1
    rc=$?
    if [ "$rc" -eq 124 ]; then
        ok "el shim respeta el rc 124 al vencer"
    else
        bad "el shim no devuelve 124 al vencer (rc=$rc)"
    fi
    "$SHIM" 5 true >/dev/null 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        ok "el shim devuelve 0 con un comando exitoso"
    else
        bad "el shim no devuelve 0 con un comando exitoso (rc=$rc)"
    fi
fi

# --- (b) ew8_unix.sh antepone el shim al PATH ------------------------------
if ! grep -qF 'tools/gnubin' "$ROOT/ew8_unix.sh"; then
    bad "ew8_unix.sh no antepone tools/gnubin al PATH"
else
    if ( . "$ROOT/ew8_unix.sh" >/dev/null 2>&1; [ "$(command -v timeout)" = "$SHIM" ] ); then
        ok "con ew8_unix.sh cargado el shim es el primero del PATH"
    else
        bad "con ew8_unix.sh cargado el shim NO queda primero en el PATH"
    fi
fi

# --- autocontrol del patron (para que un patron mal escrito no pase en silencio)
tcmd=timeout
if printf '%s 15 sniffring\n' "$tcmd" | awk -v bare="$BARE" '$0 ~ bare {f=1} END{exit !f}'; then
    ok "el patron detecta una invocacion sin ruta"
else
    bad "el patron NO detecta una invocacion sin ruta"
fi
if printf '%s 15 sniffring\n' "/usr/bin/$tcmd" | awk -v bare="$BARE" '$0 ~ bare {f=1} END{exit !f}'; then
    bad "el patron marca (mal) una ruta absoluta"
else
    ok "el patron ignora las rutas absolutas"
fi

# --- (a) barrido de fuentes ------------------------------------------------
HITS="$(
    find "$ROOT" \
        \( -path '*/.git' -o -path '*/.opencode' -o -path '*/earthworm_8.0' \
           -o -path '*/mseed' -o -path '*/tank_repo' -o -path '*/resources' \
           -o -path '*/runs' -o -path '*/picks' -o -path '*/replay' \
           -o -path '*/build' \) -prune -o \
        -type f \( -name '*.sh' -o -name 'Makefile' -o -name '*.mk' \) -print |
    while IFS= read -r f; do
        awk -v name="${f#"$ROOT"/}" -v bare="$BARE" -v query="$QUERY" '
            { line = $0; sub(/#.*/, "", line)
              if (line ~ query) next
              if (line ~ bare) printf "%s:%d\n", name, NR }
        ' "$f"
    done
)"
if [ -n "$HITS" ]; then
    echo "FAIL: hay invocaciones del binario sin ruta absoluta (usa /usr/bin/timeout):"
    printf '%s\n' "$HITS"
    fail=1
else
    ok "ningun *.sh/Makefile/*.mk invoca el binario a secas"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: 'timeout' es el de GNU coreutils en el repo"
fi
exit $fail
