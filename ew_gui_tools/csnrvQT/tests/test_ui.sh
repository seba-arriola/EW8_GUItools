#!/bin/sh
# UI determinista y offscreen: --dump/--layout/--shot, sin anillo ni BD.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIR="$ROOT/ew_gui_tools/csnrvQT"
PARAMS="$ROOT/run_working_v8/params"
BIN="$DIR/csnrvQT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail=0
ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

if [ ! -x "$BIN" ]; then
    bad "falta $BIN"
    exit 1
fi

cat > "$TMP/ui.d" <<EOF
MyModuleId      MOD_CSNRV
RingName        HYPO_RING_REF
HeartBeatInt    30
LogFile         0
MapImageFile    world_map.jpg
EOF

# --- --dump -----------------------------------------------------------------
OUT="$(cd "$PARAMS" && QT_QPA_PLATFORM=offscreen CSNRVQT_MODE=--dump \
       "$BIN" "$TMP/ui.d" 2>"$TMP/err")"
RC=$?
[ "$RC" -eq 0 ] && ok "--dump rc=0" || bad "--dump rc=$RC"
printf '%s\n' "$OUT" | grep -q 'has_data=1' && ok "has_data" || bad "sin has_data"
printf '%s\n' "$OUT" | grep -q 'qid=42 version=3' && ok "qid/version" || bad "qid/version"
printf '%s\n' "$OUT" | grep -q 'mag=4.7/5 6.0/4 5.2/6 5.6/5' \
    && ok "cuatro magnitudes" || bad "magnitudes mal"
printf '%s\n' "$OUT" | grep -q 'table=4x3' && ok "tabla 4x3" || bad "tabla mal"
printf '%s\n' "$OUT" | grep -q 'menu=3' && ok "menu File/View/Overlay" || bad "menu mal"
printf '%s\n' "$OUT" | grep -q 'bg=1' && ok "fondo cargado" || bad "sin fondo"

# --- --layout: el minimo no puede inflarse (WSLg Error 71) ------------------
LAY="$(cd "$PARAMS" && QT_QPA_PLATFORM=offscreen CSNRVQT_MODE=--layout \
       "$BIN" "$TMP/ui.d" 2>/dev/null)"
W=$(printf '%s\n' "$LAY" | sed -n 's/^minimum=\([0-9]*\)x.*/\1/p')
H=$(printf '%s\n' "$LAY" | sed -n 's/^minimum=[0-9]*x\([0-9]*\).*/\1/p')
if [ -n "$W" ] && [ -n "$H" ]; then
    if [ "$W" -le 700 ] && [ "$H" -le 800 ]; then
        ok "minimo ${W}x${H} <= 700x800"
    else
        bad "minimo ${W}x${H} > 700x800"
    fi
else
    bad "no se pudo leer el minimo"
fi

# --- --shot -----------------------------------------------------------------
QT_QPA_PLATFORM=offscreen CSNRVQT_MODE=--shot CSNRVQT_SHOT="$TMP/shot.png" \
    "$BIN" "$TMP/ui.d" >"$TMP/shot.out" 2>"$TMP/shot.err"
RC=$?
[ "$RC" -eq 0 ] && ok "--shot rc=0" || bad "--shot rc=$RC"
if [ -s "$TMP/shot.png" ]; then
    ok "PNG de la ventana ($(wc -c < "$TMP/shot.png") bytes)"
else
    bad "no se escribio el PNG"
fi

# --- --interact: rueda anclada al cursor y arrastre -------------------------
INT="$(cd "$PARAMS" && QT_QPA_PLATFORM=offscreen CSNRVQT_MODE=--interact \
       "$BIN" "$TMP/ui.d" 2>"$TMP/int.err")"
RC=$?
[ "$RC" -eq 0 ] && ok "--interact rc=0" || bad "--interact rc=$RC"

val() { printf '%s\n' "$INT" | sed -n "s/^$1=//p" | head -n 1; }

DPR=$(val dpr)
Z0=$(val zoom_before)
Z1=$(val zoom_after_wheel)
AX=$(val anchor_before_x)
AY=$(val anchor_before_y)
BX=$(val anchor_after_x)
BY=$(val anchor_after_y)
PWX=$(val pan_wheel_x)
PWY=$(val pan_wheel_y)
PDX=$(val pan_drag_x)
PDY=$(val pan_drag_y)
PCX=$(val pan_click_x)
PCY=$(val pan_click_y)
PRX=$(val pan_right_x)
PRY=$(val pan_right_y)

if [ -z "$DPR" ] || [ -z "$Z1" ]; then
    bad "--interact sin salida (ver $TMP/int.err)"
else
    LC_ALL=C awk -v z0="$Z0" -v z1="$Z1" \
        'BEGIN { exit !(z1 > z0*1.2 - 0.001 && z1 < z0*1.2 + 0.001) }' \
        && ok "rueda arriba: zoom x1.2 ($Z0 -> $Z1)" \
        || bad "rueda arriba no aplica 1.2 ($Z0 -> $Z1)"

    LC_ALL=C awk -v ax="$AX" -v ay="$AY" -v bx="$BX" -v by="$BY" \
        'BEGIN { dx=bx-ax; dy=by-ay; if (dx<0) dx=-dx; if (dy<0) dy=-dy;
                 exit !(dx <= 0.5 && dy <= 0.5) }' \
        && ok "zoom anclado al cursor (deriva <= 0.5 px)" \
        || bad "el zoom no queda anclado ($AX,$AY -> $BX,$BY)"

    LC_ALL=C awk -v d="$DPR" -v x0="$PWX" -v y0="$PWY" \
                         -v x1="$PDX" -v y1="$PDY" \
        'BEGIN { dx=(x1-x0)-30*d; dy=(y1-y0)-20*d;
                 if (dx<0) dx=-dx; if (dy<0) dy=-dy;
                 exit !(dx <= 0.01 && dy <= 0.01) }' \
        && ok "arrastre panea por delta x dpr (30x20)" \
        || bad "arrastre no panea ($PWX,$PWY -> $PDX,$PDY)"

    LC_ALL=C awk -v a="$PCX" -v b="$PDX" -v c="$PCY" -v d="$PDY" \
        'BEGIN { exit !(a == b && c == d) }' \
        && ok "clic sin mover no panea" || bad "el clic paneo ($PCX,$PCY)"
    LC_ALL=C awk -v a="$PRX" -v b="$PDX" -v c="$PRY" -v d="$PDY" \
        'BEGIN { exit !(a == b && c == d) }' \
        && ok "boton derecho no panea" || bad "el boton derecho paneo ($PRX,$PRY)"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: UI offscreen (dump/layout/shot/interact)"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
