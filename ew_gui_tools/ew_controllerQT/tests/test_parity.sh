#!/bin/sh
# test_parity.sh - C14/C8/C11: el volcado del port Qt reproduce EXACTAMENTE el
# estado que el modulo GTK pinta, sobre los mismos fixtures y el mismo parser
# (ewgui_ctrl_parse_status, compartido).
#
# Monta un EW_PARAMS propio con TEST_CTRL_RING (nunca CONTROL_RING: el arnés no
# debe tocar el stack real), publica un TYPE_STATUS con fake_status y lee
# `ECQ_MODE=--dump` del binario con QT_QPA_PLATFORM=offscreen.
#
# Determinista y sin DISPLAY. Solo falla si algo no cuadra.
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../ew_controllerQT
[ -x "$bin" ] || bin=$root/ew_8.0/bin/ew_controllerQT
fake=$here/../build/fake_status
fixtures=$root/ew_gui_tools/ew_controller/tests/fixtures
tmp=${ECQ_GUI_TMP:-/tmp/opencode/ew_controllerQT_gui}

fail=0
nok=0
ok()  { echo "ok   $*"; nok=$((nok + 1)); }
bad() { echo "FALLA $*"; fail=$((fail + 1)); }

if [ ! -x "$bin" ]; then
    bad "no existe $bin (make -C ew_gui_tools/ew_controllerQT)"
    exit 1
fi
if [ ! -x "$fake" ]; then
    bad "no existe $fake (make -C ew_gui_tools/ew_controllerQT check)"
    exit 1
fi
if [ ! -f "$params/earthworm.d.base" ]; then
    bad "falta $params/earthworm.d.base"
    exit 1
fi

# --- fixture: params aislados con un anillo PROPIO --------------------------
rm -rf "$tmp"
mkdir -p "$tmp/params" "$tmp/log"
# kom necesita earthworm_global.d (tipos y modulos) en EW_PARAMS.
cp "$params/earthworm.d.base" "$tmp/params/earthworm.d"
[ -f "$params/earthworm_global.d" ] && cp "$params/earthworm_global.d" "$tmp/params/"
grep -q 'MOD_EW_CONTROLLERQT' "$tmp/params/earthworm.d" || \
    printf ' Module   MOD_EW_CONTROLLERQT   173    # test\n' >> "$tmp/params/earthworm.d"
grep -q 'TEST_CTRL_RING' "$tmp/params/earthworm.d" || \
    printf ' Ring   TEST_CTRL_RING     1061\n' >> "$tmp/params/earthworm.d"

cat > "$tmp/params/test_ctrl.d" <<EOF
MyModuleId      MOD_EW_CONTROLLERQT
Ring            TEST_CTRL_RING
HeartBeatInt    30
PollInt         3600
LogFile         1
LogDir          $tmp/log
EOF

export EW_PARAMS="$tmp/params"
export EW_LOG="$tmp/log"

if ! "$fake" --create TEST_CTRL_RING 512 >/dev/null 2>&1; then
    bad "no se pudo crear TEST_CTRL_RING"
    exit 1
fi

# --- un escenario = publicar un fixture y volcar ---------------------------
run_dump() {  # run_dump <fixture> <pid a seleccionar>
    "$fake" TEST_CTRL_RING "$fixtures/$1" >/dev/null 2>&1 || return 1
    ( cd "$tmp/params" && \
      QT_QPA_PLATFORM=offscreen ECQ_MODE=--dump ECQ_SELECT_PID="$2" \
      /usr/bin/timeout 30 "$bin" test_ctrl.d 2>/dev/null )
}

expect() {  # expect <dump> <linea exacta>
    if printf '%s\n' "$1" | grep -qxF "$2"; then
        ok "$2"
    else
        bad "falta: $2"
    fi
}

# --- 1. alive ---------------------------------------------------------------
dump=$(run_dump status_alive.txt 200)
expect "$dump" 'row "ZZ_probe|33|Alive|??/ 0 00:00:50  probe_z.d|probe_z.d"'
expect "$dump" 'row "AA_probe|200|Alive|??/ 0 00:00:50  probe_a.d|probe_a.d"'
expect "$dump" 'ring "WILDCARD_RING|1000|16"'
expect "$dump" 'ring "ZZZ_RING|20|512"'
expect "$dump" 'target "AA_probe|200"'
expect "$dump" 'button "stop|1"'
expect "$dump" 'button "restart|1"'
expect "$dump" 'button "start|0"'
expect "$dump" 'button "reconfig|1"'

# --- 2. dead: Start si, Stop no --------------------------------------------
dump=$(run_dump status_dead.txt 200)
expect "$dump" 'row "AA_probe|200|Dead|??/ 0 00:00:50  probe_a.d|probe_a.d"'
expect "$dump" 'button "stop|0"'
expect "$dump" 'button "start|1"'
expect "$dump" 'button "restart|1"'

# --- 3. dos modulos con el mismo nombre: identidad por PID ------------------
dump=$(run_dump status_dup_a.txt 201)
expect "$dump" 'row "AA_probe|200|Alive|??/ 0 00:00:50  probe_a.d|probe_a.d"'
expect "$dump" 'row "AA_probe|201|Dead|??/ 0 00:00:50  probe_b.d|probe_b.d"'
expect "$dump" 'target "AA_probe|201"'
expect "$dump" 'button "stop|0"'
expect "$dump" 'button "start|1"'

# --- 4. el anillo propio, nunca el de produccion ---------------------------
if printf '%s\n' "$dump" | grep -q 'CONTROL_RING'; then
    bad "el dump menciona CONTROL_RING (el arnés debe usar TEST_CTRL_RING)"
else
    ok "sin CONTROL_RING en el arnés"
fi

echo "== paridad checks=$((nok + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
