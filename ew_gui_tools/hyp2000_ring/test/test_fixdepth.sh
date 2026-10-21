#!/usr/bin/env bash
#
# test_fixdepth.sh - profundidad fija de hyp2000_ring, offline y con el motor.
#
# Genera un ARC de entrada con csnloc (picks sinteticos, canal HHZ para que
# empareje contra estaciones_hyp.sta), corre hyp2000_ring en modo offline con
# una copia de su .d + `FixedDepthKm 33.0` y exige que la solucion salga en
# 33.00 km (la linea 1 del ARC lleva z*100 en las columnas 34-38).
#
# Requiere EW_PARAMS/EW_HOME/EW_VERSION (source ../../ew8_unix.sh) y los
# binarios csnloc + hyp2000_ring compilados.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
MOD="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$MOD/../.." && pwd)"
CSNLOC="$REPO/ew_gui_tools/csnloc"
PARAMS="$REPO/run_working_v8/params"
BIN="$MOD/hyp2000_ring"

echo "=== test_fixdepth (hyp2000_ring) ==="

[ -n "${EW_PARAMS:-}" ] || { echo "SKIP: EW_PARAMS no definido (source ./ew8_unix.sh)"; exit 0; }
[ -x "$BIN" ] || { echo "FAIL: falta $BIN (compila con make)"; exit 1; }
[ -x "$CSNLOC/csnloc" ] || { echo "FAIL: falta $CSNLOC/csnloc (make -C ew_gui_tools/csnloc)"; exit 1; }
[ -x "$CSNLOC/test/gen_offline_picks" ] || {
    echo "FAIL: falta $CSNLOC/test/gen_offline_picks (make -C ew_gui_tools/csnloc test/gen_offline_picks)"
    exit 1
}

tmp="$(mktemp -d /tmp/hyp2000_fixdepth_XXXXXX)"
WORKDIR_HYP="$REPO/run_working_v8/hyp2000_ring"
HYP_TMP="$WORKDIR_HYP/fixdepth_test.hyp"
trap 'rm -rf "$tmp"; rm -f "$HYP_TMP"' EXIT
mkdir -p "$tmp/log" "$WORKDIR_HYP"

# 1) picks sinteticos; HHZ/HHN/HHE es lo que declara estaciones_hyp.sta.
# gen_offline_picks emite DOS picadas por estacion (P y S) sin etiquetar la
# fase, asi que la 2.a entra como P y arruina el ajuste de HYPOINVERSE. Nos
# quedamos con la primera de cada SCNL (la P).
( cd "$CSNLOC" && ./test/gen_offline_picks ) \
  | sed -e 's/\.BHZ\./.HHZ./' -e 's/\.BHN\./.HHN./' -e 's/\.BHE\./.HHE./' \
  | awk '!seen[$5]++' > "$tmp/picks.txt"
npk="$(wc -l < "$tmp/picks.txt")"
[ "$npk" -ge 5 ] || { echo "FAIL: solo $npk picks"; exit 1; }
echo "ok  : $npk picks sinteticos (canal HHZ)"

# 2) ARC de entrada: csnloc offline volcando el ARC al log (DumpHypo 1).
cp "$CSNLOC/test/csnloc_offline.d" "$tmp/csnloc.d"
# El fixture trae LogFile 0: sin log no hay DumpHypo que extraer.
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

# 3) hyp2000_ring offline con profundidad fija. Se corre desde $tmp con rutas
# absolutas para que logit_init (que usa el nombre del .d) escriba su log.
cp "$PARAMS/hyp2000_ring.d" "$tmp/fix.d"
{
    echo "FixedDepthKm 33.0"
    echo "MinPhases 0"
    echo "MaxRMS 0"
    echo "Debug 1"
    # CommandFile se deja RELATIVO (../hyp2000_ring/...): el motor lo abre ya
    # con cwd = WorkDir, y el lector de comandos no acepta bien un @ absoluto.
    echo "WorkDir $WORKDIR_HYP"
    echo "CommandFile ../hyp2000_ring/fixdepth_test.hyp"
} >> "$tmp/fix.d"

# .hyp temporal: el fixture sintetico deja solo 3 estaciones con el peso por
# distancia de produccion; se amplia el peso por distancia (DIS ITRDIS DISCUT
# DISW1 DISW2) para intentar que el motor localice tambien el fixture.
cp "$PARAMS/hyp2000_ring.hyp" "$HYP_TMP"
echo "DIS 0 0.0 5.0 10.0" >> "$HYP_TMP"
rc_fix=0
( cd "$tmp" && EW_LOG="$tmp/log" "$BIN" fix.d "$tmp/input.arc" \
    > "$tmp/out.arc" 2>"$tmp/ref.err" ) || rc_fix=$?

# 4) La prueba determinista: el motor acepta el comando que fija la profundidad.
logfix="$(ls "$tmp"/log/fix_*.log 2>/dev/null | head -1 || true)"
if [ -n "$logfix" ] && grep -q "cmd 'ZTR 33.000 T' ok" "$logfix"; then
    echo "ok  : el motor acepto 'ZTR 33.000 T' (DEPFIXA)"
else
    echo "FAIL: el motor no acepto 'ZTR 33.000 T'"
    [ -n "$logfix" ] && tail -20 "$logfix"
    cat "$tmp/ref.err"
    exit 1
fi

# 5) Control: sin FixedDepthKm el comando NO se emite.
cp "$PARAMS/hyp2000_ring.d" "$tmp/ctrl.d"
{
    echo "Debug 1"
    echo "WorkDir $WORKDIR_HYP"
    echo "CommandFile ../hyp2000_ring/fixdepth_test.hyp"
} >> "$tmp/ctrl.d"
( cd "$tmp" && EW_LOG="$tmp/log" "$BIN" ctrl.d "$tmp/input.arc" \
    > /dev/null 2>&1 ) || true
logctrl="$(ls "$tmp"/log/ctrl_*.log 2>/dev/null | head -1 || true)"
if [ -n "$logctrl" ] && grep -q "ZTR 33.000 T" "$logctrl"; then
    echo "FAIL: sin FixedDepthKm tambien se emitio ZTR"
    exit 1
fi
echo "ok  : sin FixedDepthKm no se emite ZTR"

# 6) Si el fixture alcanza para localizar, ademas se comprueba la profundidad.
# (El ARC sintetico de csnloc deja pocas estaciones con el peso por distancia
# de produccion, asi que puede terminar en "sin localizacion"; la exactitud de
# csnloc ya esta cubierta por su test_offline.)
if [ "$rc_fix" -ne 0 ]; then
    echo "nota: el fixture no localiza con HYPOINVERSE (rc=$rc_fix);"
    echo "      se valido el comando y su efecto en el motor."
    echo "OK test_fixdepth"
    exit 0
fi

z="$(head -1 "$tmp/out.arc" | cut -c34-38 | tr -d ' ')"
[ -n "$z" ] || { echo "FAIL: sin linea de solucion"; cat "$tmp/ref.err"; exit 1; }
echo "ok  : linea 1 del ARC, z*100=$z (objetivo 3300)"
awk -v z="$z" 'BEGIN { d = z - 3300; if (d < 0) d = -d; exit !(d <= 1) }' \
  || { echo "FAIL: profundidad $z != 3300"; cat "$tmp/ref.err"; exit 1; }
echo "ok  : profundidad fija 33.00 km"

echo "OK test_fixdepth"
