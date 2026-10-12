#!/bin/bash
# Importa los datos de perfil (slab/topografia) de un repo de datos a este repo.
#
#   ./import_profiles.sh [--dry-run] [--src DIR] [--dst DIR]
#
# Copia slab<name>.tmp y topo<name>.tmp a resources/profiles/ y deja
# PROVENANCE.txt con el origen y el significado de las columnas. Idempotente:
# no recopia lo que ya esta identico. El modulo solo usa las columnas 3 (lon) y
# 7 (valor) -> ProfileCols 3 7 en csnhypodbp.d.
set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="${EWGUI_PROFILES_SRC:-/home/seba/Dev/test_db/scriptmapas/grillas}"
DST="${ROOT}/resources/profiles"
DRY=0

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY=1; shift ;;
        --src) SRC="${2:-}"; shift 2 ;;
        --dst) DST="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "opcion desconocida: $1" >&2; exit 2 ;;
    esac
done

[ -d "$SRC" ] || { echo "ERROR: no existe el directorio de origen $SRC" >&2; exit 1; }
nfiles=$(ls -1 "$SRC"/slab*.tmp "$SRC"/topo*.tmp 2>/dev/null | wc -l)
[ "$nfiles" -gt 0 ] || { echo "ERROR: no hay slab*.tmp/topo*.tmp en $SRC" >&2; exit 1; }

echo "origen:  $SRC ($nfiles archivos)"
echo "destino: $DST"
[ "$DRY" -eq 1 ] || mkdir -p "$DST" || exit 1

copied=0
skipped=0
for f in "$SRC"/slab*.tmp "$SRC"/topo*.tmp; do
    b=$(basename "$f")
    if [ -f "$DST/$b" ] && cmp -s "$f" "$DST/$b"; then
        skipped=$((skipped + 1))
        continue
    fi
    echo "  + $b"
    copied=$((copied + 1))
    [ "$DRY" -eq 1 ] && continue
    cp -f "$f" "$DST/$b" || exit 1
done
echo "copiados: $copied   sin cambios: $skipped"

if [ "$DRY" -eq 0 ]; then
    cat > "$DST/PROVENANCE.txt" <<EOF
Datos de perfil (slab y topografia) del panel de profundidad de csnhypodbp.

Origen:  $SRC
Copiado: ew_gui_tools/common/tools/import_profiles.sh

Formato: slab<name>.tmp / topo<name>.tmp, 7 columnas separadas por espacio/tab.
  col 1  distancia a lo largo del perfil (km)
  col 2  distancia perpendicular a la recta (km)
  col 3  longitud de la traza (grados)
  col 4  latitud de la traza (grados)
  col 5  longitud del nodo de grilla (grados)
  col 6  latitud del nodo de grilla (grados)
  col 7  slab: profundidad de la interfaz (km, negativa; NaN = sin slab)
         topo: elevacion (m)

El modulo usa las columnas 3 (lon) y 7 (valor): ProfileCols 3 7.
Las rectas de los perfiles viven en csnhypodbp.d (claves Profile) y se validan
contra la traza de cada archivo con:
    csnhypodbp --check-profiles <csnhypodbp.d>
EOF
    echo "escrito: $DST/PROVENANCE.txt"
fi
