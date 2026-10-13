#!/usr/bin/env bash
# Invariante de COBERTURA del esquema hipocentral (db/schema.sql).
#
# Toda columna del esquema debe estar clasificada en uno de estos dos grupos:
#   1) la escribe algun INSERT/UPDATE de ew_gui_tools/common/src/ewgui_db.c;
#   2) esta declarada "vacia por diseno" (EMPTY_BY_DESIGN, con su motivo).
# Si alguien anade una columna al esquema y no la escribe nadie, este test falla:
# es la red que evita que el mapeo modulo -> BD se desincronice en silencio.
# Ademas comprueba que la lista blanca no se quede obsoleta (columna inexistente)
# y que docs/DATA-MAPPING.md las mencione.
#
# Se ejecuta desde `make -C ew_gui_tools/common check`.
set -euo pipefail
export LC_ALL=C

repo="$(cd "$(dirname "$0")/../../.." && pwd)"
schema="$repo/db/schema.sql"
writer="$repo/ew_gui_tools/common/src/ewgui_db.c"
doc="$repo/docs/DATA-MAPPING.md"

# Vacias por diseno (ver docs/DATA-MAPPING-PENDIENTES.md). `tabla.*` = tabla entera.
EMPTY_BY_DESIGN=(
    event.creation_time            # DEFAULT CURRENT_TIMESTAMP (hora de insercion)
    origin.creation_time           # idem
    origin.latitude_uncertainty    # el ARC no las transporta
    origin.longitude_uncertainty
    origin.depth_uncertainty
    origin.time_uncertainty
    origin.error_ellipse
    pick.polarity                  # el ARC no lleva primer movimiento
    pick.probability               # no hay picker probabilistico publicando
    tensor.*                       # sin productor
)

fail=0
for f in "$schema" "$writer"; do
    if [ ! -f "$f" ]; then echo "FAIL: falta $f"; exit 1; fi
done

# tabla columna, una por linea, de los CREATE TABLE del esquema.
cols=$(awk '
    /^CREATE TABLE/ { tbl = $3; gsub(/\(/, "", tbl); inb = 1; next }
    inb && /^\);/   { inb = 0; next }
    inb {
        line = $0
        sub(/^[ \t]+/, "", line)
        if (line == "" || line ~ /^--/) next
        col = line
        sub(/[ \t].*/, "", col)
        if (col ~ /^(PRIMARY|FOREIGN|UNIQUE|CHECK|CONSTRAINT)$/) next
        print tbl, col
    }
' "$schema")

is_whitelisted() {   # $1 = tabla, $2 = columna
    local e
    for e in "${EMPTY_BY_DESIGN[@]}"; do
        if [ "$e" = "$1.$2" ] || [ "$e" = "$1.*" ]; then return 0; fi
    done
    return 1
}

word() {  # $1 = palabra, $2 = archivo
    grep -qE "(^|[^A-Za-z0-9_])$1([^A-Za-z0-9_]|$)" "$2"
}

n=0; nwrite=0; nempty=0
while read -r t c; do
    [ -n "${t:-}" ] || continue
    n=$((n + 1))
    if is_whitelisted "$t" "$c"; then
        nempty=$((nempty + 1))
        if ! word "$c" "$doc" && ! grep -qF "$t.*" "$doc"; then
            echo "FAIL: $t.$c es 'vacia por diseno' pero docs/DATA-MAPPING.md no la menciona"
            fail=1
        fi
    elif word "$c" "$writer"; then
        nwrite=$((nwrite + 1))
    else
        echo "FAIL: $t.$c no la escribe ningun INSERT/UPDATE de ewgui_db.c"
        echo "      (anadela al writer o a EMPTY_BY_DESIGN con su motivo)"
        fail=1
    fi
done <<< "$cols"

# La lista blanca no puede quedarse obsoleta.
for e in "${EMPTY_BY_DESIGN[@]}"; do
    t="${e%%.*}"; c="${e#*.}"
    if [ "$c" = "*" ]; then
        if ! printf '%s\n' "$cols" | grep -qE "^$t "; then
            echo "FAIL: EMPTY_BY_DESIGN cita la tabla inexistente $t"; fail=1
        fi
    elif ! printf '%s\n' "$cols" | grep -qxF "$t $c"; then
        echo "FAIL: EMPTY_BY_DESIGN cita $e, que ya no existe en el esquema"; fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "test_schema_coverage: FALLOS"
    exit 1
fi
echo "SCHEMA COVERAGE OK: $n columnas ($nwrite escritas por ewgui_db.c, $nempty vacias por diseno)"
