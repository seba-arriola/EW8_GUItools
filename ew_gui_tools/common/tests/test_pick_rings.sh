#!/usr/bin/env bash
# Invariante del reparto de anillos y de FASES de los PICKS:
#   pick_FP            -> PICK_RING          (solo P: es su unica fase)
#   pickml_iquique     -> PICK_RING          (solo S: PublishPhases S)
#   pickS + pickml_eqt -> LEGACY_PICK_RING   (sin consumidor)
#   csnloc             <- PICK_RING
#
# En un anillo que alguien lee, UN productor por fase: dos productores de la
# MISMA fase meten dos arribos por fase/estacion (con tiempos y canales
# distintos) y csnloc asocia el doble, empeorando la solucion en silencio. Si un
# picker publica en un anillo que nadie lee, sus picks se pierden tambien en
# silencio.
#
# Se ejecuta desde `make -C ew_gui_tools/common check`.
set -euo pipefail
export LC_ALL=C

root="$(cd "$(dirname "$0")/../../.." && pwd)"   # raiz del repo
params="$root/run_working_v8/params"
fail=0

err() { echo "FAIL: $*"; fail=1; }

ss="$params/startstop_unix.d"
base="$params/earthworm.d.base"
fp="$params/pick_FP.d"
ps="$params/pickS.d"
ml="$params/pickml_phasenetwc.d"
me="$params/pickml_eqt.d"
mi="$params/pickml_iquique.d"
loc="$params/csnloc.d"

for f in "$ss" "$base" "$fp" "$ps" "$ml" "$me" "$mi" "$loc"; do
    [ -f "$f" ] || { echo "FAIL: falta $f"; exit 1; }
done

get() { awk -v k="$2" '$1 == k { print $2; exit }' "$1"; }

# 1) Reparto por FASE en PICK_RING (el anillo que lee csnloc): pick_FP aporta la
#    P (es su unica fase) y pickml_iquique aporta SOLO S. Los dos escriben en el
#    mismo anillo a proposito; lo que no puede repetirse es la fase.
[ "$(get "$fp" OutRing)"  = "PICK_RING" ] || \
    err "pick_FP.d OutRing != PICK_RING (su P no llegaria a csnloc)"
[ "$(get "$mi" OutRing)"  = "PICK_RING" ] || \
    err "pickml_iquique.d OutRing != PICK_RING"
[ "$(get "$mi" PublishPhases)" = "S" ] || \
    err "pickml_iquique.d PublishPhases != S (la P de PICK_RING es de pick_FP)"
# pickS sigue en su pipeline aparte: ni su guia P ni sus S tocan PICK_RING.
[ "$(get "$ps" PickRing)" = "LEGACY_PICK_RING" ] || \
    err "pickS.d PickRing != LEGACY_PICK_RING (guia P cruzada con pick_FP)"
[ "$(get "$ps" OutRing)" = "LEGACY_PICK_RING" ] || \
    err "pickS.d OutRing != LEGACY_PICK_RING"

# 2) El pipeline vivo: un pickml activo publica en PICK_RING y csnloc lo lee de
#    ahi. Cual pickml es (y con que anillo) lo fija el §5; el .d de PhaseNetWC
#    conserva PICK_RING para poder volver a el.
[ "$(get "$ml" OutRing)" = "PICK_RING" ] || \
    err "pickml_phasenetwc.d OutRing != PICK_RING"
[ "$(get "$loc" InRing)" = "PICK_RING" ] || \
    err "csnloc.d InRing != PICK_RING (no leeria a pickml)"

# 3) La publicacion esta encendida y con valores validos.
[ "$(get "$ml" PublishRing)" = "1" ] || \
    err "pickml_phasenetwc.d PublishRing != 1"
case "$(get "$ml" PublishPhases)" in
    P|S|PS) ;;
    *) err "PublishPhases invalido (P|S|PS)" ;;
esac
case "$(get "$ml" PublishMaxWeight)" in
    0|1|2|3) ;;
    *) err "PublishMaxWeight fuera de 0..3" ;;
esac

# 4) El id del modulo del anillo coincide con su Module.
mod="$(get "$ml" MyModuleId)"
[ -n "$mod" ] || err "pickml_phasenetwc.d no declara MyModuleId"
if [ -n "$mod" ]; then
    awk -v m="$mod" '$1 == "Module" && $2 == m { f = 1 } END { exit !f }' "$base" || \
        err "earthworm.d.base no declara Module $mod (GetModId fallaria)"
fi

# 5) Cada pickml ACTIVO en startstop tiene su .d, SIN flags de CLI (el modo vivo
#    es el default: startstop solo pasa el .d) y con un OutRing PROPIO. Dos
#    pickers vivos en el mismo anillo meten dos arribos por fase/estacion y
#    csnloc asocia el doble.
plines="$(grep -E '^[[:space:]]*Process[[:space:]]+"pickml[[:space:]]' "$ss" || true)"
[ -n "$plines" ] || err "startstop_unix.d no declara ningun Process pickml"
seen_rings=" "
while IFS= read -r line; do
    [ -n "$line" ] || continue
    if ! printf '%s' "$line" | grep -Eq '^[[:space:]]*Process[[:space:]]+"pickml[[:space:]]+[A-Za-z0-9_.-]+\.d"[[:space:]]*$'; then
        err "Process pickml con flags de CLI (el modo vivo es el default):$line"
        continue
    fi
    dname="$(printf '%s' "$line" | sed -E 's/.*"pickml[[:space:]]+([^"]+)".*/\1/')"
    dfile="$params/$dname"
    if [ ! -f "$dfile" ]; then
        err "Process pickml declara $dname y el .d no existe"
        continue
    fi
    [ "$(get "$dfile" PublishRing)" = "1" ] || err "$dname: PublishRing != 1"
    ring="$(get "$dfile" OutRing)"
    [ -n "$ring" ] || { err "$dname: sin OutRing"; continue; }
    case "$seen_rings" in
        *" $ring "*) err "dos pickml activos publican en el mismo anillo: $ring" ;;
    esac
    seen_rings="$seen_rings$ring "
done <<EOF
$plines
EOF
# csnloc tiene que leer el anillo de UNO de los pickers activos; si lee uno que
# nadie escribe, la localizacion viva se queda muda en silencio.
case "$seen_rings" in
    *" $(get "$loc" InRing) "*) ;;
    *) err "csnloc.d InRing=$(get "$loc" InRing) no es el OutRing de ningun pickml activo" ;;
esac

# 6) LEGACY_PICK_RING se crea (startstop) y tiene clave (earthworm.d.base),
#    con la clave declarada DENTRO del bloque que reescribe el bootstrap.
awk -v r="LEGACY_PICK_RING" '$1 == "Ring" && $2 == r { f = 1 } END { exit !f }' "$ss" || \
    err "startstop_unix.d no declara Ring LEGACY_PICK_RING"

awk -v r="LEGACY_PICK_RING" '
    />>> ring keys/ { on = 1 }
    /<<< ring keys/ { on = 0 }
    on && $1 == "Ring" && $2 == r { f = 1 }
    END { exit !f }' "$base" || \
    err "earthworm.d.base no da clave a LEGACY_PICK_RING dentro de los centinelas"

# 7) Clave unica (una clave repetida hace que dos anillos sean el mismo).
key="$(awk '$1 == "Ring" && $2 == "LEGACY_PICK_RING" { print $3; exit }' "$base" | tr -d '\r')"
if [ -z "$key" ]; then
    err "LEGACY_PICK_RING sin clave en earthworm.d.base"
else
    n="$(awk -v k="$key" '$1 == "Ring" && $3 == k { n++ } END { print n + 0 }' "$base")"
    [ "$n" -eq 1 ] || err "la clave $key de LEGACY_PICK_RING aparece $n veces"
fi

# 8) nRing coincide con las lineas Ring (un anillo de mas NO se crea).
n_decl="$(awk '$1 == "nRing" { print $2; exit }' "$ss" | tr -d '\r')"
n_ring="$(awk '$1 == "Ring" { n++ } END { print n + 0 }' "$ss")"
[ -n "$n_decl" ] || err "startstop_unix.d no declara nRing"
if [ -n "$n_decl" ] && [ "$n_decl" != "$n_ring" ]; then
    err "nRing=$n_decl pero hay $n_ring lineas Ring"
fi

# 9) LEGACY_PICK_RING no alimenta el pipeline: solo lo nombran sus productores
#    (pickS/pickml_eqt) y el visor que se abre a proposito para mirarlo
#    (csntvp_2). Fuera de eso nadie puede nombrarlo, o alguien lo estaria leyendo.
bad="$(grep -Hn 'LEGACY_PICK_RING' "$params"/*.d "$params/earthworm.d.base" 2>/dev/null \
       | grep -vE '/(pickS\.d|pickml_eqt\.d|csntvp_2\.d|startstop_unix\.d|earthworm\.d|earthworm\.d\.base):' \
       | grep -vE ':[0-9]+:[[:space:]]*#' || true)"
[ -z "$bad" ] || err "LEGACY_PICK_RING fuera de los productores (pickS/pickml_eqt) y su visor (csntvp_2): $bad"

# 10) Los dos modelos EQT: ventana nativa 6000 en ambos (el motor la compara con
#     el grafo al arrancar y aborta si no coincide, asi que copiar el 3001 de
#     PhaseNetWC rompe el arranque) y el ModelFile que cada uno dice ser.
[ "$(get "$me" InRing)"  = "SLINK_RING" ] || err "pickml_eqt.d InRing != SLINK_RING"
[ "$(get "$me" OutRing)" = "LEGACY_PICK_RING" ] || \
    err "pickml_eqt.d OutRing != LEGACY_PICK_RING (anillo propio, sin consumidor)"
[ "$(get "$me" PublishRing)" = "1" ]      || err "pickml_eqt.d PublishRing != 1"
[ "$(get "$me" StaFile)" = "pickS.sta" ]  || err "pickml_eqt.d StaFile != pickS.sta"
[ "$(get "$me" Rate)" = "100.0" ]         || err "pickml_eqt.d Rate != 100.0"
[ "$(get "$me" WindowSamples)" = "6000" ] || \
    err "pickml_eqt.d WindowSamples != 6000 (ventana nativa de EQT, no 3001)"
case "$(get "$me" ModelFile)" in
    */eqt.onnx) ;;
    *) err "pickml_eqt.d ModelFile no apunta a eqt.onnx" ;;
esac

if [ -f "$mi" ]; then
    [ "$(get "$mi" InRing)"  = "SLINK_RING" ] || err "pickml_iquique.d InRing != SLINK_RING"
    [ "$(get "$mi" OutRing)" = "PICK_RING" ]  || err "pickml_iquique.d OutRing != PICK_RING"
    [ "$(get "$mi" PublishRing)" = "1" ]      || err "pickml_iquique.d PublishRing != 1"
    [ "$(get "$mi" StaFile)" = "pickS.sta" ]  || err "pickml_iquique.d StaFile != pickS.sta"
    [ "$(get "$mi" Rate)" = "100.0" ]         || err "pickml_iquique.d Rate != 100.0"
    [ "$(get "$mi" WindowSamples)" = "6000" ] || \
        err "pickml_iquique.d WindowSamples != 6000 (ventana nativa de EQT)"
    case "$(get "$mi" ModelFile)" in
        */eqt_iquique.onnx) ;;
        *) err "pickml_iquique.d ModelFile no apunta a eqt_iquique.onnx" ;;
    esac
fi

if [ "$fail" -eq 0 ]; then
    echo "OK: P (pick_FP) + S (pickml_iquique) en PICK_RING; LEGACY_PICK_RING sin consumidor"
fi
exit "$fail"
