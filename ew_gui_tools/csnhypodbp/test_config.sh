#!/bin/sh
# Test del parseo de la config de mapas/perfiles (claves nuevas) y de los modos
# headless --print-config / --check-profiles de csnhypodbp.
#
# Autocontenido: genera un .d minimo y datos de perfil sinteticos (con la recta
# lat = m*lon + b) en un directorio temporal. Si el binario del modulo no esta
# compilado intenta compilarlo y, si no puede (p.ej. el `cp` a bin/ falla con el
# modulo corriendo), se salta con aviso.
set -u
cd "$(dirname "$0")" || exit 1
LC_ALL=C
export LC_ALL
fails=0

TMP="${TMPDIR:-/tmp}/csnhypodbp_cfg_$$"
rm -rf "$TMP"; mkdir -p "$TMP/prof" || exit 1

ck() {  # ck <descripcion> <patron ERE> <archivo>
    if grep -qE "$2" "$3"; then echo "ok  : $1"; else echo "FAIL: $1"; fails=$((fails+1)); fi
}
nck() { # nck <descripcion> <patron ERE> <archivo>
    if grep -qE "$2" "$3"; then echo "FAIL: $1"; fails=$((fails+1)); else echo "ok  : $1"; fi
}

BIN=./csnhypodbp
[ -x "$BIN" ] || make -C . csnhypodbp >/dev/null 2>&1 || true
if [ ! -x "$BIN" ]; then
    echo "skip: falta ./csnhypodbp (compila con make); se omite el test de config"
    rm -rf "$TMP"
    exit 0
fi

# --- datos sinteticos: traza sobre lat = (1/3.6)*lon + b ---
gen() {  # gen <archivo> <b> <valor col7>
    awk -v b="$2" -v v="$3" 'BEGIN{
        for (i = 0; i < 40; i++) {
            lon = -72 + i * 0.15;
            lat = (1 / 3.6) * lon + b;
            printf "%d\t0\t%.4f\t%.4f\t%.4f\t%.4f\t%s\n", i * 16, lon, lat, lon, lat, v;
        }
    }' > "$1"
}
gen "$TMP/prof/slabA.tmp" 0.0 -30
gen "$TMP/prof/topoA.tmp" 0.0 -4000
gen "$TMP/prof/slabB.tmp" -2.0 -45
gen "$TMP/prof/topoB.tmp" -2.0 -3500

# Un hueco en el slab (NaN) para cubrir el caso costa afuera.
awk 'NR==3 || NR==4 { sub(/-30$/, "NaN") } { print }' "$TMP/prof/slabA.tmp" > "$TMP/t" && \
    mv "$TMP/t" "$TMP/prof/slabA.tmp"

cfg() {  # cfg <archivo.d> <b_linea_B>
    cat > "$1" <<EOF
MyModuleId      MOD_TEST
InRing          RING_IN
OutRing         RING_OUT
HeartBeatInt    30
LogFile         1
StaFile         estaciones.txt
StateFile       state.events
MapImageFile    mapa.jpg
OverlayFile     ov1.lines
OverlayColor    capa FF0000
OverlayWidth    1.2
OverlayHide     fallas
ProfileDataDir  $TMP/prof
MagConfigFile   $TMP/mags.d
ProfileCols     3 7
ProfileDepthMinKm 250
Profile A -72 -20.000000 -66 -18.333333
Profile B -72 $2 -66 $3
EOF
}

# Recta correcta de B: b = -2 -> lat(-72) = -22, lat(-66) = -20.333333
# .d de csnmags del que la sub-pestaña `magnitudes` saca sus defaults: se cambian
# Ml_TA y Ms_Window para comprobar que se leen de ahi y no de constantes.
cat > "$TMP/mags.d" <<EOF
# comentario
WoodAndersonCoefs 0.8 0.7 2080
Ml_TA 7.5
Ml_TB 45
Mwp_T0 95
Mwp_StartOff 0
Mb_Window 30
Ms_Window 380
Mb_Band 0.5 2.0
Ms_Band 0.045 0.056
EOF
cfg "$TMP/ok.d" -22.000000 -20.333333

"$BIN" --print-config "$TMP/ok.d" > "$TMP/pc.txt" 2>&1
rc=$?
[ "$rc" -eq 0 ] && echo "ok  : print-config rc 0" || { echo "FAIL: print-config rc=$rc"; fails=$((fails+1)); }
ck  "print-config: MapImageFile"    '^MapImageFile mapa\.jpg$'            "$TMP/pc.txt"
ck  "print-config: 1 overlay"       '^OverlayFiles 1$'                    "$TMP/pc.txt"
ck  "print-config: OverlayColor"    '^OverlayColor capa FF0000$'          "$TMP/pc.txt"
ck  "print-config: OverlayWidth"    '^OverlayWidth 1\.200$'               "$TMP/pc.txt"
ck  "print-config: OverlayHides"    '^OverlayHides 1$'                    "$TMP/pc.txt"
ck  "print-config: OverlayHide"     '^OverlayHide fallas$'                "$TMP/pc.txt"
ck  "print-config: ProfileDataDir"  "^ProfileDataDir $TMP/prof$"          "$TMP/pc.txt"
ck  "print-config: ProfileCols"     '^ProfileCols 3 7$'                   "$TMP/pc.txt"
ck  "print-config: DepthMinKm"      '^ProfileDepthMinKm 250\.0$'          "$TMP/pc.txt"
ck  "print-config: 2 perfiles"      '^Profiles 2$'                        "$TMP/pc.txt"
ck  "print-config: convencion slab" "^Profile A .* $TMP/prof/slabA\.tmp $TMP/prof/topoA\.tmp$" "$TMP/pc.txt"
ck  "print-config: perfil B"        '^Profile B '                         "$TMP/pc.txt"
ck  "print-config: MagConfigFile"   "^MagConfigFile $TMP/mags\.d$"         "$TMP/pc.txt"
ck  "print-config: Ml_TA del .d"    '^MagWindow ML 7\.500 45\.000$'        "$TMP/pc.txt"
ck  "print-config: ventana Mwp"     '^MagWindow Mwp 0\.000 95\.000$'       "$TMP/pc.txt"
ck  "print-config: ventana Mb"      '^MagWindow Mb 0\.000 30\.000$'        "$TMP/pc.txt"
ck  "print-config: Ms_Window del .d" '^MagWindow Ms 0\.000 380\.000$'      "$TMP/pc.txt"
ck  "print-config: Wood-Anderson"   '^MagWoodAnderson 0\.800 0\.700 2080\.0$' "$TMP/pc.txt"
ck  "print-config: banda Mb"        '^MagBand Mb 0\.500 2\.000$'           "$TMP/pc.txt"
ck  "print-config: banda Ms"        '^MagBand Ms 0\.045 0\.056$'           "$TMP/pc.txt"
ck  "print-config: ventana de peticion" '^MagFetchSeconds 700\.0$'          "$TMP/pc.txt"
ck  "print-config: puerta de Ms"    '^MagGate Ms 20\.0 160\.0 0\.0$'        "$TMP/pc.txt"
ck  "print-config: puerta de ML"    '^MagGate ML 0\.0 8\.0 600\.0$'         "$TMP/pc.txt"

"$BIN" --check-profiles "$TMP/ok.d" > "$TMP/cp.txt" 2>&1
rc=$?
[ "$rc" -eq 0 ] && echo "ok  : check-profiles rc 0" || { echo "FAIL: check-profiles rc=$rc"; fails=$((fails+1)); }
ck  "check-profiles: 2 perfiles 0 fallas" '^perfiles=2 fallas=0$'         "$TMP/cp.txt"
ck  "check-profiles: traza de A"    '^profile A slab=40 topo=40 traza=40 ' "$TMP/cp.txt"
ck  "check-profiles: desviacion baja" 'dev_media=0\.0[0-9]+'              "$TMP/cp.txt"

# Recta mal declarada (b = -6 en vez de -2): la traza no la sigue.
cfg "$TMP/bad_line.d" -26.000000 -24.333333
"$BIN" --check-profiles "$TMP/bad_line.d" > "$TMP/cpl.txt" 2>&1
rc=$?
[ "$rc" -ne 0 ] && echo "ok  : recta mal declarada -> rc != 0" || { echo "FAIL: recta mal declarada rc=$rc"; fails=$((fails+1)); }
ck  "check-profiles: avisa la recta" 'no sigue la recta declarada'        "$TMP/cpl.txt"

# Datos ausentes: un perfil sin sus archivos.
sed "s|ProfileDataDir  $TMP/prof|ProfileDataDir  $TMP/vacio|" "$TMP/ok.d" > "$TMP/nodata.d"
mkdir -p "$TMP/vacio"
"$BIN" --check-profiles "$TMP/nodata.d" > "$TMP/cpn.txt" 2>&1
rc=$?
[ "$rc" -ne 0 ] && echo "ok  : datos ausentes -> rc != 0" || { echo "FAIL: datos ausentes rc=$rc"; fails=$((fails+1)); }
ck  "check-profiles: slab FALLA"    'slab FALLA'                          "$TMP/cpn.txt"

# Falta una clave obligatoria (StateFile).
grep -v '^StateFile' "$TMP/ok.d" > "$TMP/nomiss.d"
"$BIN" --print-config "$TMP/nomiss.d" > "$TMP/nom.txt" 2>&1
rc=$?
[ "$rc" -ne 0 ] && echo "ok  : falta clave obligatoria -> rc != 0" || { echo "FAIL: falta clave rc=$rc"; fails=$((fails+1)); }

# ProfileCols invalido: se ignora y quedan los defaults.
sed 's/^ProfileCols     3 7/ProfileCols     0 -1/' "$TMP/ok.d" > "$TMP/badcols.d"
"$BIN" --print-config "$TMP/badcols.d" > "$TMP/bc.txt" 2>&1
ck  "print-config: ProfileCols invalido se ignora" '^ProfileCols 3 7$'    "$TMP/bc.txt"

# La clave Profile mal formada es error de config.
grep -v '^Profile B' "$TMP/ok.d" > "$TMP/badprof.d"
echo "Profile C -72 -20 -66" >> "$TMP/badprof.d"
"$BIN" --print-config "$TMP/badprof.d" > "$TMP/bp.txt" 2>&1
rc=$?
[ "$rc" -ne 0 ] && echo "ok  : Profile mal formada -> rc != 0" || { echo "FAIL: Profile mal formada rc=$rc"; fails=$((fails+1)); }

# MagConfigFile ausente: aviso por stderr y defaults de fabrica (no rompe).
sed "s|MagConfigFile   $TMP/mags.d|MagConfigFile   $TMP/no_existe_mags.d|" "$TMP/ok.d" > "$TMP/nomags.d"
"$BIN" --print-config "$TMP/nomags.d" > "$TMP/nm.txt" 2>&1
rc=$?
[ "$rc" -eq 0 ] && echo "ok  : MagConfigFile ausente no rompe" || { echo "FAIL: MagConfigFile ausente rc=$rc"; fails=$((fails+1)); }
ck  "print-config: avisa si falta MagConfigFile" 'no se pudo leer MagConfigFile' "$TMP/nm.txt"
ck  "print-config: sin .d quedan los defaults" '^MagWindow ML 10\.000 45\.000$' "$TMP/nm.txt"

rm -rf "$TMP"
if [ "$fails" -gt 0 ]; then echo "FALLOS: $fails"; exit 1; fi
echo "OK test_config"
exit 0
