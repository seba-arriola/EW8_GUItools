#!/bin/sh
# Modos instrumentados offscreen de csntvpQT (sin anillos ni DISPLAY).
#
# El fixture es propio: 3 estaciones con canales distintos (HHZ/HHN/HHE) para
# que el volcado y los colores por componente sean observables sin tocar
# params/ ni el .sta de produccion.
set -u

BIN="${BIN:-./csntvpQT}"
fail=0

ok()  { echo "ok  : $1"; }
bad() { echo "FAIL: $1"; fail=1; }

contains() { # contains <salida> <patron-ERE> <descripcion>
    if printf '%s\n' "$1" | grep -qE -- "$2"; then ok "$3"; else bad "$3 ($2)"; fi
}

if [ ! -x "$BIN" ]; then
    bad "falta $BIN (compila antes de este test)"
    exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/fixture.sta" <<EOF
AAA HHZ XX -- 1.0
BBB HHN XX -- 1.0
CCC HHE XX -- 1.0
EOF

cat > "$tmp/fixture.d" <<EOF
MyModuleId      MOD_CSNTVPQT
InRing          SLINK_RING
OutRing         PICK_RING
HeartBeatInt    30
LogFile         0
StaFile         $tmp/fixture.sta
StationsPerScreen 3
TimeWindow      1
EOF

run() { # run <modo> [args]
    QT_QPA_PLATFORM=offscreen CSNTVPQT_MODE="$1" "$BIN" "$tmp/fixture.d" 2>/dev/null
}

# --- 1. --dump -------------------------------------------------------------
out="$(run --dump)"
contains "$out" '^window='                        "dump: ventana"
contains "$out" '^module=MOD_CSNTVPQT'            "dump: modulo del .d"
contains "$out" '^n_sta=3'                        "dump: 3 estaciones"
contains "$out" '^stations_per_screen=3'           "dump: StationsPerScreen del .d"
contains "$out" '^time_window_min=1'               "dump: TimeWindow del .d"
contains "$out" '^picks_p=1 picks_s=1'             "dump: 2 picadas automaticas"
contains "$out" '^pick_phase=none'                 "dump: sin fase armada por defecto"
contains "$out" '^sta=AAA chan=HHZ'                "dump: canal HHZ"
contains "$out" '^sta=BBB chan=HHN'                "dump: canal HHN"
contains "$out" '^sta=CCC chan=HHE'                "dump: canal HHE"
contains "$out" '^canvas=[1-9][0-9]*x[1-9][0-9]*'  "dump: canvas con tamano"
contains "$out" '^menu=6'                          "dump: 6 menus"

# --- 2. --layout: el minimo no puede inflarse (WSLg Error 71) --------------
out="$(run --layout)"
contains "$out" '^minimum=[0-9]+x[0-9]+' "layout: minimo reportado"
mw=$(printf '%s\n' "$out" | sed -n 's/^minimum=\([0-9]*\)x\([0-9]*\)$/\1/p')
mh=$(printf '%s\n' "$out" | sed -n 's/^minimum=\([0-9]*\)x\([0-9]*\)$/\2/p')
if [ -n "$mw" ] && [ "$mw" -le 700 ] && [ "$mh" -le 800 ]; then
    ok "layout: minimo ${mw}x${mh} cabe en 700x800"
else
    bad "layout: minimo ${mw:-?}x${mh:-?} excede 700x800"
fi
contains "$out" '^track_h=[1-9][0-9]*' "layout: alto de pista"

# --- 3. --shot -------------------------------------------------------------
shot="$tmp/shot.png"
CSNTVPQT_SHOT="$shot" QT_QPA_PLATFORM=offscreen CSNTVPQT_MODE=--shot \
    "$BIN" "$tmp/fixture.d" >/dev/null 2>&1
if [ -s "$shot" ]; then ok "shot: PNG escrito"; else bad "shot: PNG vacio/ausente"; fi

# --- 4. --interact ---------------------------------------------------------
out="$(run --interact)"
contains "$out" '^hold_frozen=1'                  "interact: HOLD congela la ventana"
contains "$out" '^pick_default=none'              "interact: por defecto sin fase"
contains "$out" '^click_without_phase_ignored=1'  "interact: clic sin fase es no-op"
contains "$out" '^pick_without_phase=0'           "interact: place_pick sin fase no pica"
contains "$out" '^pick_without_hold=0'            "interact: sin HOLD no se pica"
contains "$out" '^arm_p=1'                        "interact: p arma P"
contains "$out" '^cursor_armed=1'                 "interact: cursor con la letra al armar"
contains "$out" '^pick_placed=1'                  "interact: picada colocada"
contains "$out" '^pick_still_armed=1'             "interact: tras picar sigue armado"
contains "$out" '^esc_clears=1'                   "interact: Esc desarma"
contains "$out" '^cursor_off=1'                   "interact: cursor normal tras Esc"
contains "$out" '^disarm_on_release=1'            "interact: soltar HOLD desarma"
contains "$out" '^pick_msg=.* BBB\.HHN\.XX\.-- '  "interact: SCNL de la pista picada"
contains "$out" '^pick_msg=.* P M$'               "interact: fase P y origen M"
contains "$out" '^phase=S'                        "interact: cambio de fase a S"
contains "$out" '^zoom=1\.000->1\.500'            "interact: zoom vertical x1.5"
contains "$out" '^clean_removed=3'                "interact: clean view limpia lo que no llega"
contains "$out" '^n_sta_after_clean=0'            "interact: vista vacia tras clean"
contains "$out" '^reload_ok=1'                    "interact: reload del StaFile"
contains "$out" '^n_sta_after_reload=3'           "interact: reload restaura las 3"

# dt del clic (ms): debe ser < 1 ms
dt=$(printf '%s\n' "$out" | sed -n 's/^hit_dt_ms=\(.*\)$/\1/p')
if [ -n "$dt" ] && LC_ALL=C awk -v v="$dt" 'BEGIN { exit !(v + 0 < 1.0) }'; then
    ok "interact: clic -> tiempo exacto (${dt} ms)"
else
    bad "interact: clic -> tiempo impreciso (${dt:-?} ms)"
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: modos instrumentados de csntvpQT"
else
    echo "FALLAS: revisar los FAIL de arriba"
fi
exit "$fail"
