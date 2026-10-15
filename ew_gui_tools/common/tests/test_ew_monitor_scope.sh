#!/usr/bin/env bash
# Aislamiento de DOS stacks: `ew_monitor.sh` solo puede ver SU instancia.
#
# Los otros tests usan pids sintéticos (EWMON_PID_SCOPE=loose). Aquí se usan
# procesos REALES llamados `startstop` (copias de /bin/sleep) con EW_PARAMS
# distintos: es exactamente el caso de dos worktrees vivos a la vez.
#
# Invariantes (CA-3 del plan de worktrees):
#   1. `status` de A reporta el pid de A y NO ve el de B.
#   2. `stop` de A no toca el proceso de B.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
MON="$ROOT/ew_monitor.sh"
TMP="$(mktemp -d)"
PID_A=""
PID_B=""
trap 'kill $PID_A $PID_B 2>/dev/null; rm -rf "$TMP"' EXIT

pass=0
fail=0
ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1" >&2; fail=$((fail + 1)); }

mkdir -p "$TMP/A/params" "$TMP/B/params" "$TMP/log" "$TMP/bin"
printf '# sin Process declarados\n' > "$TMP/A/params/startstop_unix.d"
printf '# sin Process declarados\n' > "$TMP/B/params/startstop_unix.d"
cp /bin/sleep "$TMP/bin/startstop" || { echo "no puedo copiar /bin/sleep"; exit 2; }
chmod +x "$TMP/bin/startstop"

EW_PARAMS="$TMP/A/params" "$TMP/bin/startstop" 120 &
PID_A=$!
EW_PARAMS="$TMP/B/params" "$TMP/bin/startstop" 120 &
PID_B=$!
sleep 0.5

# premisas: los dos existen y su `comm` es startstop (si no, el test no prueba nada)
[ "$(cat "/proc/$PID_A/comm" 2>/dev/null)" = startstop ] || { echo "premisa: A no es startstop"; exit 2; }
[ "$(cat "/proc/$PID_B/comm" 2>/dev/null)" = startstop ] || { echo "premisa: B no es startstop"; exit 2; }

mon() {   # mon <params> <args de ew_monitor.sh>
    local p="$1"; shift
    PATH="$TMP/bin:/usr/bin:/bin" \
    EW_UNIX="$TMP/unix.sh" \
    EW_LOG_DIR="$TMP/log" \
    EWMON_PIDFILE="$TMP/log/startstop.pid" \
    EW_PARAMS="$p" \
    STOP_TIMEOUT=3 \
    EWMON_VERIFY_TIMEOUT=1 \
        bash "$MON" "$@"
}

# --- 1) `status` ve el pid de SU instancia -------------------------------
out="$(mon "$TMP/A/params" status 2>&1)"
case "$out" in
    *"pid $PID_A"*) ok "status de A reporta el pid de A ($PID_A)" ;;
    *) bad "status de A no reporta $PID_A: $(printf '%s' "$out" | head -n 1)" ;;
esac
case "$out" in
    *"pid $PID_B"*) bad "status de A reporto el pid de B ($PID_B): aislamiento roto" ;;
    *) ok "status de A no ve el pid de B ($PID_B)" ;;
esac

# --- 2) `stop` de A no toca B --------------------------------------------
mon "$TMP/A/params" stop >/dev/null 2>&1
sleep 1
if kill -0 "$PID_A" 2>/dev/null; then
    bad "stop de A no paro A ($PID_A)"
else
    ok "stop de A paro A ($PID_A)"
fi
if kill -0 "$PID_B" 2>/dev/null; then
    ok "stop de A NO toco B ($PID_B)"
else
    bad "stop de A mato el stack de B ($PID_B)"
fi

echo "----"
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ] || exit 1
