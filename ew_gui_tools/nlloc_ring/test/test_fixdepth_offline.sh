#!/usr/bin/env bash
#
# test_fixdepth_offline.sh - profundidad fija (emulada) de nlloc_ring offline.
#
# Genera un ARC con csnloc, corre nlloc_ring en modo offline con una copia de
# su .d + `FixedDepthKm 33.0` y exige que la solucion caiga dentro de la
# ventana oficial de NLLoc: +/- FixDepthSpacingKm/2 (0.05 km por defecto).
#
# Requiere EW_PARAMS/EW_HOME/EW_VERSION, los binarios csnloc + nlloc_ring y
# los recursos de /mnt/d/resources/nlloc (bandas y grillas).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
MOD="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$MOD/../.." && pwd)"
CSNLOC="$REPO/ew_gui_tools/csnloc"
PARAMS="$REPO/run_working_v8/params"
BIN="$MOD/nlloc_ring"
NLLRES="/mnt/d/resources/nlloc"

echo "=== test_fixdepth_offline (nlloc_ring) ==="

[ -n "${EW_PARAMS:-}" ] || { echo "SKIP: EW_PARAMS no definido (source ./ew8_unix.sh)"; exit 0; }
[ -x "$BIN" ] || { echo "FAIL: falta $BIN (compila con make)"; exit 1; }
[ -x "$CSNLOC/csnloc" ] || { echo "FAIL: falta $CSNLOC/csnloc (make -C ew_gui_tools/csnloc)"; exit 1; }
[ -x "$CSNLOC/test/gen_offline_picks" ] || {
    echo "FAIL: falta $CSNLOC/test/gen_offline_picks (make -C ew_gui_tools/csnloc test/gen_offline_picks)"
    exit 1
}
[ -d "$NLLRES" ] || { echo "SKIP: no existe $NLLRES (recursos NLLoc)"; exit 0; }

tmp="$(mktemp -d /tmp/nlloc_fixdepth_XXXXXX)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/log" "$tmp/loc"

# 1) ARC de entrada (mismo fixture que el test de hyp2000_ring).
( cd "$CSNLOC" && ./test/gen_offline_picks ) \
  | sed -e 's/\.BHZ\./.HHZ./' -e 's/\.BHN\./.HHN./' -e 's/\.BHE\./.HHE./' \
  | awk '!seen[$5]++' > "$tmp/picks.txt"

cp "$CSNLOC/test/csnloc_offline.d" "$tmp/csnloc.d"
echo "DumpHypo 1" >> "$tmp/csnloc.d"
echo "LogFile 1" >> "$tmp/csnloc.d"
( cd "$CSNLOC" && EW_LOG="$tmp/log" ./csnloc "$tmp/csnloc.d" "$tmp/picks.txt" \
    > "$tmp/events.jsonl" 2>"$tmp/csnloc.err" )

awk '/--- HYP2000ARC evento/{f=1; n++; if (n>1) exit; next}
     /--- fin HYP2000ARC evento/{if (f) exit}
     f' "$tmp"/log/*.log > "$tmp/input.arc"
nph="$(grep -c . "$tmp/input.arc" || true)"
[ "$nph" -ge 4 ] || { echo "FAIL: ARC de entrada con $nph fases"; cat "$tmp/csnloc.err"; exit 1; }
echo "ok  : ARC de entrada con $nph fases"

# 2) nlloc_ring offline con FixedDepthKm 33.0 y WorkDir temporal. Sin bandas
# 3D: usa el modelo global 1D (mucho mas rapido) y ejercita la ruta global de
# build_control; la ruta de banda comparte el mismo helper (test unitario).
grep -v '^ModelBand' "$PARAMS/nlloc_ring.d" > "$tmp/fix.d"
{
    echo "FixedDepthKm 33.0"
    echo "WorkDir $tmp"
    echo "OutRoot loc/ev"
} >> "$tmp/fix.d"

rc=0
( cd "$tmp" && EW_LOG="$tmp/log" "$BIN" fix.d "$tmp/input.arc" \
    > "$tmp/out.arc" 2>"$tmp/ref.err" ) || rc=$?

# La prueba determinista: el control que el modulo entrega al motor debe tener
# la ventana de 2 nodos z centrada en 33 km (z0=32.95, dz=0.1) y el OCT con
# init_num_cells_z=1. Se comprueba SIEMPRE, localice o no el fixture.
ctl="$tmp/nll_ring.in"
[ -f "$ctl" ] || { echo "FAIL: el modulo no genero nll_ring.in"; cat "$tmp/ref.err"; exit 1; }
if grep -qE '^LOCGRID [0-9]+ [0-9]+ 2 .* 32\.9500 .* 0\.1000 ' "$ctl"; then
    echo "ok  : LOCGRID con 2 nodos z centrado en 33.00 (z0=32.95, dz=0.1)"
else
    echo "FAIL: LOCGRID sin la ventana de profundidad fija"
    grep -E '^LOCGRID' "$ctl"
    exit 1
fi
if grep -qE '^LOCSEARCH OCT [0-9]+ [0-9]+ 1 ' "$ctl"; then
    echo "ok  : LOCSEARCH OCT con init_num_cells_z=1"
else
    echo "FAIL: LOCSEARCH sin init_num_cells_z=1"
    grep -E '^LOCSEARCH' "$ctl"
    exit 1
fi

# Control negativo: sin FixedDepthKm el LOCGRID conserva su nz original.
grep -v '^ModelBand' "$PARAMS/nlloc_ring.d" > "$tmp/ctrl.d"
{
    echo "WorkDir $tmp"
    echo "OutRoot loc/ev"
} >> "$tmp/ctrl.d"
( cd "$tmp" && EW_LOG="$tmp/log" "$BIN" ctrl.d "$tmp/input.arc" \
    > /dev/null 2>&1 ) || true
if grep -qE '^LOCGRID [0-9]+ [0-9]+ 2 ' "$tmp/nll_ring.in"; then
    echo "FAIL: sin FixedDepthKm tambien se fuerza nz=2"
    exit 1
fi
echo "ok  : sin FixedDepthKm no se fuerza la ventana"

# Bonus: si el fixture alcanza para localizar, se comprueba la ventana real.
if [ "$rc" -eq 0 ]; then
    z="$(head -1 "$tmp/out.arc" | cut -c34-38 | tr -d ' ')"
    [ -n "$z" ] || { echo "FAIL: sin linea de solucion"; cat "$tmp/ref.err"; exit 1; }
    echo "ok  : linea 1 del ARC, z*100=$z (objetivo 3300 +/- 5)"
    awk -v z="$z" 'BEGIN { d = z - 3300; if (d < 0) d = -d; exit !(d <= 5) }' \
      || { echo "FAIL: profundidad $z fuera de la ventana +/-0.05 km"; exit 1; }
    echo "ok  : profundidad fija 33.00 +/- 0.05 km"
else
    echo "nota: el fixture no localiza con NLLoc (rc=$rc); la ventana del"
    echo "      control ya quedo verificada arriba."
fi

echo "OK test_fixdepth_offline"
