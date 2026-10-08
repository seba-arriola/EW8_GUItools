#!/usr/bin/env bash
#
# add_horizontal_config.sh - Integra los canales horizontales (HHE/HHN) en la
# configuracion del picker de onda S y en el pipeline de adquisicion/almacenaje.
#
# Acciones (por defecto en modo --dry-run, NO toca nada):
#   1) Genera run_working_v8/params/pickS.sta a partir de estaciones_107.txt
#      (una linea 3C por estacion: "1 <pin> STA NET LOC HHE HHN HHZ").
#   2) Anade lineas `Tank ... HHE/HHN` a wave_serverV.d replicando cada linea
#      `Tank ... HHZ` existente (mismo formato, canal y ruta .tnk).
#   3) Amplia los selectores de slink2ew_HHZ.d: "HHZ.D" -> "HH?.D"
#      (y variantes con location code).
#
# Uso:
#   ./tank_tools/add_horizontal_config.sh [--params DIR] [--apply] [--dry-run]
#
#   --params DIR  directorio de params (default: run_working_v8/params)
#   --apply       aplica los cambios (con respaldo .bak.<fecha>)
#   --dry-run     solo informa (default)
#   -h, --help    ayuda
#
# Codigos: 0 ok | 1 error de uso | 2 ficheros de entrada faltantes
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PARAMS_DIR="$ROOT_DIR/run_working_v8/params"
APPLY=0

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }
die() { echo "[hconf] ERROR: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --params)  PARAMS_DIR="$2"; shift 2 ;;
        --apply)   APPLY=1; shift ;;
        --dry-run) APPLY=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "opcion desconocida: $1" ;;
    esac
done

ESTA="$PARAMS_DIR/estaciones_107.txt"
WSV="$PARAMS_DIR/wave_serverV.d"
SLINK="$PARAMS_DIR/slink2ew_HHZ.d"
STA="$PARAMS_DIR/pickS.sta"

[ -f "$ESTA" ] || die "no existe $ESTA"
[ -f "$WSV" ]  || die "no existe $WSV"
[ -f "$SLINK" ] || die "no existe $SLINK"

STAMP="$(date +%Y%m%d-%H%M%S)"
bk() { if [ "$APPLY" -eq 1 ] && [ -f "$1" ]; then cp -f "$1" "$1.bak.$STAMP"; fi; }

# Genera pickS.sta desde estaciones_107.txt (STA NET CHAN LOC ...).
gen_sta() {
    awk 'BEGIN{pin=0}
         /^[[:space:]]*#/ {next}
         NF>=4 {
             pin++;
             printf "1 %d %s %s %s HHE HHN HHZ\n", pin, $1, $2, $4;
         }' "$ESTA"
}

# Genera wave_serverV.d anadiendo HHE/HHN tras cada Tank HHZ (idempotente).
gen_wsv() {
    awk 'function horiz(line, newch,   n,a,i,v,out) {
             n=split(line,a," ");
             if (n<11) return "";
             v=a[11]; gsub(/_HHZ_/, "_" newch "_", v);
             out="Tank " a[2] " " newch " " a[4] " " a[5];
             for (i=6;i<=10;i++) out=out " " a[i];
             out=out " " v;
             return out;
         }
         /^Tank / {
             if ($3=="HHZ") {
                 print $0;
                 print horiz($0,"HHE");
                 print horiz($0,"HHN");
             } else print $0;
             next;
         }
         { print }' "$WSV"
}

n_esta=$(grep -cvE '^[[:space:]]*(#|$)' "$ESTA" || true)
n_tank=$(grep -cE '^Tank ' "$WSV" || true)
n_sel=$(grep -c 'HHZ\.D' "$SLINK" || true)

echo "[hconf] estaciones_107.txt : $n_esta lineas -> pickS.sta ($(gen_sta | wc -l) lineas 3C)"
gen_sta | sed -n '1,3p' | sed 's/^/        /'
echo "[hconf] wave_serverV.d    : $n_tank lineas Tank HHZ -> +$((n_tank*2)) HHE/HHN"
echo "        ejemplo:"
gen_wsv | grep -E '^Tank .* HHE ' | sed -n '1p' | sed 's/^/        /'
gen_wsv | grep -E '^Tank .* HHN ' | sed -n '1p' | sed 's/^/        /'
echo "[hconf] slink2ew_HHZ.d   : $n_sel selectores HHZ.D -> HH?.D"
echo

if [ "$APPLY" -eq 0 ]; then
    echo "[hconf] DRY-RUN: nada modificado. Usa --apply para aplicar."
    exit 0
fi

bk "$STA"; gen_sta > "$STA"
echo "[hconf] escrito $STA"

bk "$WSV"
if grep -qE '^Tank [^ ]+ (HHE|HHN) ' "$WSV"; then
    echo "[hconf] wave_serverV.d ya tiene horizontales; no se re-anaden"
else
    gen_wsv > "$WSV.new"
    mv "$WSV.new" "$WSV"
    echo "[hconf] wave_serverV.d: $(grep -cE '^Tank ' "$WSV") lineas Tank"
fi

bk "$SLINK"
sed -i -E 's/([0-9]{2})?HHZ\.D/\1HH?.D/g' "$SLINK"
echo "[hconf] slink2ew_HHZ.d: $(grep -c 'HH?\.D' "$SLINK") selectores horizontales"

echo "[hconf] listo (respaldos .bak.$STAMP)"
