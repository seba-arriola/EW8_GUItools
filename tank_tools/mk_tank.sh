#!/bin/bash
# =============================================================================
#  mk_tank.sh - Convierte miniSEED a "tank" reproducible por tankplayer
#  Proyecto: EW8_GUItools
#
#  Uso:
#     ./tank_tools/mk_tank.sh <in.mseed> <outdir> [opciones]
#
#  Un "tank" de tankplayer es una secuencia de mensajes TYPE_TRACEBUF2
#  (cabecera TRACE2_HEADER + muestras int32) escrita en binario. No tiene
#  indice ni compresion: es exactamente lo que tankplayer sabe leer.
#
#  Opciones:
#     --nsamp N            muestras por tracebuf. Default 200, maximo 1008.
#     --multiplier M       multiplica las muestras (datos float -> int). Default 1.
#     --chunk-seconds S    ademas del tank completo, trocea con tankcut en
#                          ventanas de S segundos: chunk_000.tank, chunk_001.tank...
#     --force-net NN       sobreescribe el codigo de RED del mseed   (max 2 chars)
#     --force-sta SSSSS    sobreescribe el codigo de ESTACION         (max 5 chars)
#     --force-loc LL       sobreescribe el LOCATION CODE (usa '--' si vacio) (max 2)
#     --force-chan CCC     sobreescribe el CANAL                     (max 3 chars)
#     --remux              reordena con remux_tbuf. Solo necesario si el tank NO
#                          esta cronologico. OJO: carga el archivo en memoria;
#                          usarlo sobre chunks, no sobre un master de GB.
#     -h, --help           muestra esta ayuda
#
#  Salidas en <outdir>:
#     master.tank          conversion completa
#     tanksniff.txt        inventario: SCNL, nsamp, samprate y ventana temporal
#     tanksniff.err        salida de error de tanksniff
#     chunk_%03d.tank      si se usa --chunk-seconds
#
#  Codigos de salida:
#     0  ok
#     1  error de uso (argumentos invalidos)
#     2  tanksniff fallo, o se detecto desorden cronologico por canal
#     3  entrada inexistente/vacia, o ms2tank fallo
#
#  Ejemplo:
#     ./tank_tools/mk_tank.sh mseed/test2.mseed replay/tanks --nsamp 1008 --chunk-seconds 600
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# El entorno de EarthWorm define EW_HOME/EW_VERSION/EW_PARAMS/EW_LOG y pone los
# binarios en el PATH. Si no esta cargado, lo cargamos aqui.
if [ -z "${EW_HOME:-}" ] && [ -f "$ROOT_DIR/ew8_unix.sh" ]; then
    # shellcheck disable=SC1091
    source "$ROOT_DIR/ew8_unix.sh" >/dev/null 2>&1 || true
fi

EW_BIN="${EW_HOME:-$ROOT_DIR}/${EW_VERSION:-earthworm_8.0}/bin"
MS2TANK="$EW_BIN/ms2tank"
TANKSNIFF="$EW_BIN/tanksniff"
TANKCUT="$EW_BIN/tankcut"
REMUX="$EW_BIN/remux_tbuf"

usage() {
    cat <<'EOF'
Uso:
   ./tank_tools/mk_tank.sh <in.mseed> <outdir> [opciones]

Convierte un archivo miniSEED en un "tank" reproducible por tankplayer.
Un tank es una secuencia binaria de mensajes TYPE_TRACEBUF2 (cabecera
TRACE2_HEADER + muestras int32), sin indice ni compresion.

Opciones:
   --nsamp N            muestras por tracebuf. Default 200, maximo 1008.
                        1008 = (4096 bytes de mensaje - 64 de cabecera) / 4.
   --multiplier M       multiplica las muestras (datos float -> int). Default 1.
   --chunk-seconds S    ademas del tank completo, trocea con tankcut en
                        ventanas de S segundos: chunk_000.tank, chunk_001.tank...
   --force-net NN       sobreescribe el codigo de RED del mseed      (max 2)
   --force-sta SSSSS    sobreescribe el codigo de ESTACION            (max 5)
   --force-loc LL       sobreescribe el LOCATION CODE (usa '--' si vacio) (max 2)
   --force-chan CCC     sobreescribe el CANAL                        (max 3)
   --remux              reordena por endtime con remux_tbuf. NECESARIO si el
                        master no pasa el chequeo de "pauta": ms2tank NO
                        garantiza el orden (con mseed/test2.mseed deja 2704
                        paquetes fuera de orden). Solo usa un indice en memoria
                        (~24 bytes/mensaje), no carga el fichero entero.
   -h, --help           muestra esta ayuda

Orden: hay DOS chequeos, y no significan lo mismo.
  (a) PAUTA (el que importa): endtime globalmente no decreciente. tankplayer
      calcula su espera con Ptime = endtime de cada paquete; si un paquete
      tiene endtime anterior al previo su espera sale negativa y se emite de
      inmediato, y la reproduccion deja de ser fiel. remux_tbuf y tankcut SI
      ordenan por endtime.
  (b) POR CANAL: starttime no decreciente dentro de cada SCNL.
Si (a) falla y NO pides --chunk-seconds, el master es el entregable y el
script sale con 2. Si pides --chunk-seconds, los chunks los produce tankcut
(ya ordenados) y el codigo de salida depende de los chunks.

Salidas en <outdir>:
   master.tank          conversion completa
   tanksniff.txt        inventario: SCNL, nsamp, samprate, ventana temporal
   tanksniff.err        salida de error de tanksniff
   chunk_%03d.tank      si se usa --chunk-seconds

Codigos de salida:
   0  ok
   1  error de uso (argumentos invalidos)
   2  tanksniff fallo, o el resultado no es reproducible con pauta correcta
   3  entrada inexistente/vacia, o ms2tank fallo

Ejemplo:
   ./tank_tools/mk_tank.sh mseed/test2.mseed replay/tanks --nsamp 1008 \
       --remux --chunk-seconds 600
EOF
}

die() { echo "ERROR: $*" >&2; exit 3; }

# -----------------------------------------------------------------------------
#  Argumentos
# -----------------------------------------------------------------------------
case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    "")        usage >&2; exit 1 ;;
esac

IN="$1"
OUTDIR="${2:-}"

if [ -z "$OUTDIR" ]; then
    echo "ERROR: falta <outdir>" >&2
    usage >&2
    exit 1
fi
shift 2

NSAMP=""; MULT=""; CHUNK=""
FNET=""; FSTA=""; FLOC=""; FCHAN=""
DO_REMUX=0

while [ $# -gt 0 ]; do
    case "$1" in
        --nsamp)         NSAMP="${2:-}";   shift 2 ;;
        --multiplier)    MULT="${2:-}";    shift 2 ;;
        --chunk-seconds) CHUNK="${2:-}";   shift 2 ;;
        --force-net)     FNET="${2:-}";    shift 2 ;;
        --force-sta)     FSTA="${2:-}";    shift 2 ;;
        --force-loc)     FLOC="${2:-}";    shift 2 ;;
        --force-chan)    FCHAN="${2:-}";   shift 2 ;;
        --remux)         DO_REMUX=1;       shift ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "ERROR: opcion desconocida: $1" >&2; usage >&2; exit 1 ;;
    esac
done

[ -x "$MS2TANK" ]  || die "no existe o no es ejecutable $MS2TANK  (carga ./ew8_unix.sh)"
[ -x "$TANKSNIFF" ] || die "no existe o no es ejecutable $TANKSNIFF"
[ -f "$IN" ]       || die "no existe el archivo de entrada: $IN"
[ -s "$IN" ]       || die "el archivo de entrada esta vacio: $IN"

mkdir -p "$OUTDIR" || die "no se pudo crear el directorio de salida: $OUTDIR"

# -----------------------------------------------------------------------------
#  1) miniSEED -> tank  (ms2tank escribe en stdout; aqui lo redirigimos)
# -----------------------------------------------------------------------------
ms_args=()
[ -n "$NSAMP" ] && ms_args+=( -n "$NSAMP" )
[ -n "$MULT" ]  && ms_args+=( -m "$MULT" )
[ -n "$FNET" ]  && ms_args+=( -N "$FNET" )
[ -n "$FSTA" ]  && ms_args+=( -S "$FSTA" )
[ -n "$FLOC" ]  && ms_args+=( -L "$FLOC" )
[ -n "$FCHAN" ] && ms_args+=( -C "$FCHAN" )

MASTER="$OUTDIR/master.tank"
echo "[mk_tank] entrada : $IN"
echo "[mk_tank] ms2tank : ${ms_args[*]:-(sin opciones)} $IN > $MASTER"

if ! "$MS2TANK" ${ms_args[@]+"${ms_args[@]}"} "$IN" > "$MASTER"; then
    rm -f "$MASTER"
    die "ms2tank fallo. Causas tipicas: encoding miniSEED no soportado (solo
  Steim1/Steim2/INT32/FLOAT32/FLOAT64; INT16 y otros NO), archivo corrupto,
  o el mseed contiene varios SCNL y uno de ellos no es valido."
fi

if [ ! -s "$MASTER" ]; then
    rm -f "$MASTER"
    die "ms2tank no escribio datos en $MASTER"
fi

# -----------------------------------------------------------------------------
#  2) reordenado opcional
# -----------------------------------------------------------------------------
if [ "$DO_REMUX" -eq 1 ]; then
    if [ -x "$REMUX" ]; then
        echo "[mk_tank] remux_tbuf: reordenando por endtime $MASTER"
        if "$REMUX" "$MASTER" "$MASTER.remux" >/dev/null; then
            mv -f "$MASTER.remux" "$MASTER"
        else
            echo "AVISO: remux_tbuf fallo; se conserva el tank sin reordenar" >&2
            rm -f "$MASTER.remux"
        fi
    else
        echo "AVISO: no existe $REMUX; se omite --remux" >&2
    fi
fi

# -----------------------------------------------------------------------------
#  3) inventario con tanksniff
# -----------------------------------------------------------------------------
SNIFFTXT="$OUTDIR/tanksniff.txt"
SNIFFERR="$OUTDIR/tanksniff.err"

"$TANKSNIFF" "$MASTER" > "$SNIFFTXT" 2> "$SNIFFERR"
sniff_rc=$?
nmsgs=$(wc -l < "$SNIFFTXT" | tr -d ' ')

if [ "$sniff_rc" -ne 0 ] || [ "$nmsgs" -eq 0 ]; then
    echo "ERROR: tanksniff fallo (rc=$sniff_rc, mensajes=$nmsgs). Ver $SNIFFERR" >&2
    exit 2
fi
echo "[mk_tank] tanksniff: $nmsgs mensajes -> $SNIFFTXT"

# Formato de salida de tanksniff (una linea por mensaje):
#   STA.CHAN.NET.LOC (VV) <pinno> <datatype> <nsamp> <samprate> \
#       <stime> (<starttime>) <etime> (<endtime>) <Nbytes>
# El 4.o campo entre parentesis es el starttime epoch y el 6.o el endtime.
#
# DOS criterios distintos:
#  (a) PAUTA (el que importa): endtime globalmente NO decreciente. tankplayer
#      calcula su espera con Ptime = endtime de cada paquete
#      (tankplayer.c:607 y :1104); si un paquete tiene endtime anterior al
#      previo, su espera sale negativa y se emite de inmediato -> la
#      reproduccion deja de ser fiel. ms2tank NO garantiza este orden;
#      remux_tbuf y tankcut SI (ordenan por endtime, remux_code.c:109/149/210).
#  (b) POR CANAL: starttime no decreciente dentro de cada SCNL. Detecta
#      empaquetados rotos aunque (a) pase.
global_bad=$(awk '
    { n = split($0, p, /[()]/)
      if (n >= 6) { e = p[6] + 0
                    if (seen && e < prev_e) bad++
                    prev_e = e; seen = 1 } }
    END { printf "%d", bad + 0 }' "$SNIFFTXT")
chan_bad=$(awk '
    { key = $1; n = split($0, p, /[()]/)
      if (n >= 4) { t = p[4] + 0
                    if (key in last && t < last[key]) bad++
                    last[key] = t } }
    END { printf "%d", bad + 0 }' "$SNIFFTXT")

if [ "$global_bad" -eq 0 ]; then
    echo "[mk_tank] pauta    : endtime no decreciente (OK)"
else
    echo "[mk_tank] pauta    : $global_bad paquetes con endtime anterior al previo"
fi
if [ "$chan_bad" -eq 0 ]; then
    echo "[mk_tank] por canal: cronologico (OK)"
else
    echo "[mk_tank] por canal: $chan_bad inversiones de starttime en un mismo SCNL"
fi

MASTER_PLAYABLE=1
if [ "$global_bad" -ne 0 ] || [ "$chan_bad" -ne 0 ]; then
    MASTER_PLAYABLE=0
fi

if [ "$MASTER_PLAYABLE" -eq 0 ]; then
    if [ -z "$CHUNK" ]; then
        echo "ERROR: $MASTER no es reproducible con pauta correcta." >&2
        echo "       Repetir con --remux (reordena por endtime con remux_tbuf)." >&2
        exit 2
    fi
    echo "AVISO: el master no es reproducible con pauta correcta, pero los chunks" >&2
    echo "       SI lo seran (tankcut ordena por endtime). Usa --remux si vas a" >&2
    echo "       reproducir el master directamente." >&2
fi

# -----------------------------------------------------------------------------
#  4) troceado opcional con tankcut
# -----------------------------------------------------------------------------
chunk_bad=0
nchunks=0
if [ -n "$CHUNK" ]; then
    if ! [ -x "$TANKCUT" ]; then
        echo "AVISO: no existe $TANKCUT; se omite --chunk-seconds" >&2
    else
        first_epoch=$(awk 'NR==1 { n=split($0,p,/[()]/); if (n>=4) printf "%.0f", p[4] }'  "$SNIFFTXT")
        last_epoch=$(awk  '{ n=split($0,p,/[()]/); if (n>=4) last=p[4] }
                            END { if (last != "") printf "%.0f", last }'                  "$SNIFFTXT")

        if [ -z "$first_epoch" ] || [ -z "$last_epoch" ]; then
            echo "AVISO: no se pudo determinar la ventana temporal; se omite --chunk-seconds" >&2
        else
            span=$(( last_epoch - first_epoch ))
            echo "[mk_tank] ventana  : $(date -u -d "@$first_epoch" '+%Y-%m-%d %H:%M:%S') UTC .. $(date -u -d "@$last_epoch" '+%Y-%m-%d %H:%M:%S') UTC ($span s)"

            k=0
            off=0
            CHK="$OUTDIR/.chunk_check.tmp"
            while [ "$off" -le "$span" ]; do
                s=$(( first_epoch + off ))
                ts=$(date -u -d "@$s" +%Y%m%d%H%M%S)
                out=$(printf '%s/chunk_%03d.tank' "$OUTDIR" "$k")
                if "$TANKCUT" -s "$ts" -d "$CHUNK" "$MASTER" "$out" >/dev/null 2>&1 && [ -s "$out" ]; then
                    "$TANKSNIFF" "$out" > "$CHK" 2>/dev/null
                    read -r cmsgs cmins cmaxe cspan cbad <<< "$(awk '
                        { n = split($0, p, /[()]/)
                          if (n >= 6) { s = p[4] + 0; e = p[6] + 0
                                        if (cnt == 0) { mins = s; maxe = e }
                                        else { if (s < mins) mins = s; if (e > maxe) maxe = e }
                                        if (cnt > 0 && e < prev_e) bad++
                                        prev_e = e; cnt++ } }
                        END { printf "%d %.0f %.0f %.0f %d\n", cnt + 0, mins + 0, maxe + 0, maxe - mins, bad + 0 }' "$CHK")"
                    printf '[mk_tank] chunk %03d: %s + %ss -> %s\n' "$k" "$ts" "$CHUNK" "$out"
                    printf '            mensajes=%s span=%ss  pauta=%s\n' \
                           "$cmsgs" "$cspan" "$([ "$cbad" -eq 0 ] && echo OK || echo "MAL ($cbad inversiones)")"
                    nchunks=$(( nchunks + 1 ))
                    [ "$cbad" -ne 0 ] && chunk_bad=$(( chunk_bad + 1 ))
                else
                    echo "AVISO: tankcut fallo o salio vacio para el inicio $ts (chunk omitido)" >&2
                    rm -f "$out"
                fi
                k=$(( k + 1 ))
                off=$(( off + CHUNK ))
            done
            rm -f "$CHK"
        fi
    fi
fi

if [ "$chunk_bad" -gt 0 ]; then
    echo "ERROR: $chunk_bad chunk(s) no son reproducibles con pauta correcta." >&2
    exit 2
fi

echo "[mk_tank] listo."
echo "  tank      : $MASTER"
echo "  inventario: $SNIFFTXT"
if [ -n "$CHUNK" ]; then
    echo "  chunks    : $nchunks en $OUTDIR/chunk_*.tank"
fi
exit 0
