#!/bin/sh
# UI determinista y offscreen: --dump/--layout/--shot/--interact, sin anillo
# ni BD. El mapa real (world_map.jpg) y dos overlays temporales.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DIR="$ROOT/ew_gui_tools/csnstaevdispQT"
PARAMS="$ROOT/run_working_v8/params"
BIN="$DIR/csnstaevdispQT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail=0
ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

if [ ! -x "$BIN" ]; then
    bad "falta $BIN"
    exit 1
fi

cat > "$TMP/ov1.lines" <<EOF
#layer name=uno color=FF0000 width=2
-180 0
180 0
EOF
cat > "$TMP/ov2.lines" <<EOF
#layer name=dos color=0000FF width=1
-80 -30
-60 -30
EOF

cat > "$TMP/ui.d" <<EOF
MyModuleId      MOD_STAEVDISP
RingName        HYPO_RING
HeartBeatInt    30
LogFile         0
DbEnvFile       db.env
MapImageFile    world_map.jpg
StaFile         estaciones_107.txt
StationSize     15
OverlayFile     $TMP/ov1.lines
OverlayFile     $TMP/ov2.lines
EOF

# --- --dump -----------------------------------------------------------------
OUT="$(cd "$PARAMS" && QT_QPA_PLATFORM=offscreen CSNSTAEVDISPQT_MODE=--dump \
       "$BIN" "$TMP/ui.d" 2>"$TMP/err")"
RC=$?
[ "$RC" -eq 0 ] && ok "--dump rc=0" || bad "--dump rc=$RC"
printf '%s\n' "$OUT" | grep -q 'stations=5 events=3' \
    && ok "fixture 5 estaciones / 3 sismos" || bad "fixture incorrecta"
printf '%s\n' "$OUT" | grep -q 'elev_min=-100.0 elev_max=5000.0' \
    && ok "rango de elevacion" || bad "rango de elevacion incorrecto"
printf '%s\n' "$OUT" | grep -q 'max_events=10' \
    && ok "max_events 10" || bad "max_events incorrecto"
printf '%s\n' "$OUT" | grep -q 'layers=2 bg=1' \
    && ok "2 overlays y fondo cargado" || bad "overlays/fondo"
printf '%s\n' "$OUT" | grep -q 'window=Network & Seismicity Viewer' \
    && ok "titulo de ventana" || bad "titulo incorrecto"

# --- --layout ---------------------------------------------------------------
OUT="$(cd "$PARAMS" && QT_QPA_PLATFORM=offscreen CSNSTAEVDISPQT_MODE=--layout \
       "$BIN" "$TMP/ui.d" 2>"$TMP/err")"
RC=$?
[ "$RC" -eq 0 ] && ok "--layout rc=0" || bad "--layout rc=$RC"
MIN=$(printf '%s\n' "$OUT" | sed -n 's/^minimum=//p' | head -n 1)
MW=${MIN%x*}
MH=${MIN#*x}
if [ -n "$MIN" ] && [ "$MW" -le 700 ] && [ "$MH" -le 800 ]; then
    ok "minimo ${MW}x${MH} <= 700x800"
else
    bad "minimo ${MIN:-?} supera 700x800"
fi

# --- --shot -----------------------------------------------------------------
SHOT="$TMP/shot.png"
OUT="$(cd "$PARAMS" && QT_QPA_PLATFORM=offscreen CSNSTAEVDISPQT_MODE=--shot \
       CSNSTAEVDISPQT_SHOT="$SHOT" "$BIN" "$TMP/ui.d" 2>"$TMP/err")"
RC=$?
[ "$RC" -eq 0 ] && ok "--shot rc=0" || bad "--shot rc=$RC"
SZ=$(wc -c < "$SHOT" 2>/dev/null || echo 0)
[ "$SZ" -gt 0 ] && ok "PNG de la ventana ($SZ bytes)" || bad "PNG vacio"

# --- --interact: rueda anclada al cursor, arrastre y refresco ----------------
INT="$(cd "$PARAMS" && QT_QPA_PLATFORM=offscreen CSNSTAEVDISPQT_MODE=--interact \
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
ZS=$(val zoom_snapshot)
PSX=$(val pan_snapshot_x)
PSY=$(val pan_snapshot_y)

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

    LC_ALL=C awk -v z="$ZS" -v z1="$Z1" -v x="$PSX" -v y="$PSY" \
                         -v x1="$PDX" -v y1="$PDY" \
        'BEGIN { dx=x-x1; dy=y-y1; if (dx<0) dx=-dx; if (dy<0) dy=-dy;
                 exit !(z == z1 && dx <= 0.01 && dy <= 0.01) }' \
        && ok "el refresco de datos no resetea zoom/pan" \
        || bad "el snapshot nuevo movio la vista ($ZS; $PSX,$PSY)"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: UI offscreen (dump/layout/shot/interact)"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
