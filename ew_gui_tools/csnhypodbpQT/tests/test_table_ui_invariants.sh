#!/bin/sh
# test_table_ui_invariants.sh - Invariantes de fuente de las tablas.
#
# Trampas que se pagaron:
#   - make_view activaba setSortingEnabled(true) pero ningun modelo implementa
#     sort(): los clics no hacian nada. El orden debe pasar por SortProxy +
#     wire_header_sort en las CUATRO tablas.
#   - bpq_db.c leia origin.creation_time y lo tiraba: bpq_events.c sellaba
#     time(NULL) y todas las soluciones de una carga coincidian.
#   - el ancho por defecto (~100 px) + stretchLastSection obligaba a scroll H;
#     las tablas de Procesar se ajustan con ewguiqt::fit_columns.
#
# Uso: sh tests/test_table_ui_invariants.sh
set -u

here=$(cd "$(dirname "$0")" && pwd)
ui=$here/../ui/bpq_mainwindow.cxx
db=$here/../c/bpq_db.c
ev=$here/../c/bpq_events.c
table=$here/../../commonqt/src/table.cxx

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

for f in "$ui" "$db" "$ev" "$table"; do
    if [ -f "$f" ]; then ok "existe $(basename "$f")"; else bad "falta $f"; fi
done

# 1) Las cuatro tablas usan el proxy compartido.
n=$(grep -c 'ewguiqt::wire_header_sort' "$ui" 2>/dev/null || true)
if [ "${n:-0}" -ge 4 ]; then
    ok "wire_header_sort en las 4 tablas ($n)"
else
    bad "faltan wire_header_sort (<4): $n"
fi
if grep -q 'setSortingEnabled(false)' "$ui"; then
    ok "make_view no usa el orden propio de Qt"
else
    bad "make_view sigue con setSortingEnabled(true)"
fi
for p in m_originsProxy m_staProxy m_magProxy; do
    if grep -q "$p" "$ui"; then ok "proxy $p"; else bad "falta $p"; fi
done

# 2) -1 = sin orden inicial (conserva el orden natural hasta el primer clic).
if grep -q 'initialColumn >= 0' "$table" &&
   grep -A4 'column < 0' "$table" | grep -q 'return'; then
    ok "SortProxy admite initialColumn < 0"
else
    bad "SortProxy no distingue el orden inicial -1"
fi

# 3) Created sale de origin.creation_time, no de time(NULL) al insertar.
if grep -q 'creation_time' "$db" && grep -q 'bpq_events_feed_arc_at' "$db"; then
    ok "la carga de BD propaga origin.creation_time"
else
    bad "bpq_db.c no usa creation_time/feed_arc_at"
fi
if grep -q 'bpq_events_feed_arc_at' "$ev" && grep -q 'created > 0.0' "$ev"; then
    ok "feed_arc_at respeta el created explicito"
else
    bad "bpq_events.c no conserva el created de la BD"
fi
if grep -q 'g->meta\[found\].created = (double)time(NULL)' "$ev"; then
    bad "queda el time(NULL) incondicional en feed_arc"
else
    ok "sin time(NULL) incondicional por solucion"
fi

# 4) Anchos exactos en las tablas de Procesar.
if grep -q '^int fit_columns' "$table" && grep -q 'fitProcesarTable' "$ui"; then
    ok "fit_columns + fitProcesarTable"
else
    bad "falta el ajuste de anchos"
fi
if grep -q 'setStretchLastSection(false)' "$table"; then
    ok "fit_columns desactiva el stretch de la ultima"
else
    bad "fit_columns no desactiva stretchLastSection"
fi

echo "----"
echo "== table ui invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
