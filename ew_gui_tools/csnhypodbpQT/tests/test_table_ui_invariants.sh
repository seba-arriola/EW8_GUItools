#!/bin/sh
# test_table_ui_invariants.sh - Invariantes de fuente de tablas, ejes, filtro y preview.
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
axis=$here/../c/bpq_axis.c
traces=$here/../c/bpq_traces.c
mag=$here/../c/bpq_draw_mag.c
panel=$here/../ui/bpq_preview_panel.cxx

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

for f in "$ui" "$db" "$ev" "$table" "$axis" "$traces" "$mag" "$panel"; do
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

# 5) Eje unico y reset de zoom sin pila.
if grep -q 'bpq_axis_paint' "$traces" && grep -q 'bpq_axis_paint' "$mag"; then
    ok "los dos ejes usan bpq_axis_paint"
else
    bad "un eje no delega en bpq_axis_paint"
fi
if grep -q 'rubber_active' "$traces" && grep -q 'bpq_traces_axis_release' "$traces"; then
    ok "banda de arrastre + gesto de release en el eje"
else
    bad "falta la banda o el gesto del eje"
fi
if grep -q 'm_zoomUndo\|takeLast' "$ui"; then
    bad "quedo la pila de undo del zoom"
else
    ok "sin pila de undo (reset total)"
fi
if grep -q 'resetTimeView' "$ui" && grep -q 'bpq_traces_axis_release' "$ui"; then
    ok "clic izquierda y eje comparten resetTimeView"
else
    bad "reset del clic/eje no comparten helper"
fi

# 6) La tabla de magnitudes filtra por in_range; el canvas conserva TODAS.
if grep -q 'bpq_magrows_filter_in_range' "$ui" &&
   grep -q 'setRows(m_magTableRows)' "$ui"; then
    ok "la tabla de magnitudes usa las filas filtradas"
else
    bad "la tabla de magnitudes no filtra por in_range"
fi
if grep -q 'fillMagCtx' "$ui" && grep -q 'm_magRows' "$ui"; then
    ok "el canvas/estado siguen leyendo m_magRows (todas)"
else
    bad "el canvas perdio la lista completa"
fi

# 7) Preview: tabla de 2 filas y SIN relleno amarillo/rojo.
if grep -q 'QString BpqPreviewPanel::dump' "$panel" &&
   grep -q 'rows=2' "$panel"; then
    ok "preview expone su estructura 2xN"
else
    bad "preview sin dump de la tabla"
fi
if grep -q 'setBg\|kCandidateBg\|kSelectedBg\|fdeceb\|fdf3dc' "$panel"; then
    bad "preview conserva el fondo amarillo/rojo"
else
    ok "preview sin relleno de fondo"
fi
if grep -q 'm_preview->dump()' "$ui"; then
    ok "el dump incluye la estructura del preview"
else
    bad "el dump no incluye el preview"
fi

echo "----"
echo "== table ui invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
