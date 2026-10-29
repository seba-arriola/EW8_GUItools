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
main=$here/../ui/main.cxx
cfg=$here/../c/bpq_config.c
tt=$here/../c/bpq_tt.c
magpanel=$here/../c/bpq_magpanel.c

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

for f in "$ui" "$db" "$ev" "$table" "$axis" "$traces" "$mag" "$panel" \
         "$main" "$cfg" "$tt" "$magpanel"; do
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
if grep -q 'ColumnFitter(m_eventsView' "$ui" &&
   grep -q 'ColumnFitter(m_originsView' "$ui" &&
   grep -q 'm_eventsFit->fit' "$ui" &&
   grep -q 'm_originsFit->fit' "$ui" &&
   grep -q 'fitEventTables' "$ui"; then
    ok "Eventos y Origins reparten el ancho (ColumnFitter)"
else
    bad "Eventos/Origins no usan ColumnFitter"
fi
if grep -q 'fit_columns(m_eventsView' "$ui" ||
   grep -q 'fit_columns(m_originsView' "$ui"; then
    bad "Eventos/Origins volvieron a fit_columns (no reparte el sobrante)"
else
    ok "Eventos/Origins no resetean los anchos con fit_columns"
fi
if grep -q 'setColumnWidth' "$ui" || grep -q 'resizeColumnsToContents' "$ui"; then
    bad "hay anchos hardcodeados/ResizeToContents en la UI"
else
    ok "sin anchos hardcodeados en la UI"
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

# 8) Picada: marcar desarma (no persistente); Esc tambien.
if grep -A30 'void BpqMainWindow::emitManualPick' "$ui" | grep -q 'm_pickArmed = false'; then
    ok "emitManualPick desarma al colocar"
else
    bad "el clic de picada deja el modo armado"
fi
if grep -A30 'void BpqMainWindow::emitManualPick' "$ui" | grep -q 'updatePickCursor()'; then
    ok "el desarme restaura el puntero por defecto"
else
    bad "el desarme no llama a updatePickCursor"
fi

# 9) P teorica: TauModel con default iasp91 y modelo cargado al arrancar.
if grep -q 'set_str(c->tau_model' "$cfg" && grep -q '"iasp91"' "$cfg"; then
    ok "TauModel tiene default iasp91"
else
    bad "config_defaults no inicializa TauModel"
fi
if grep -q 'bpq_init_tt' "$main" && grep -q 'csnhypodbp_tt_init' "$tt"; then
    ok "el arranque carga el modelo de tiempos"
else
    bad "nadie inicializa csnhypodbp_tt"
fi

# 10) Magnitudes: checks propios + base reconstruida para csnmags.
if grep -A12 'void BpqMainWindow::renderMagnitudes' "$ui" | grep -q 'm_magChk'; then
    ok "renderMagnitudes usa los checks propios de magnitudes"
else
    bad "magnitudes sigue con los checks del picking"
fi
if grep -A60 'void BpqMainWindow::calculateMagnitudes' "$ui" |
   grep -q 'bpq_magpanel_calc_solution'; then
    ok "calculate reconstruye las P efectivas"
else
    bad "calculate manda la candidata sin picadas efectivas"
fi
if grep -q '^int bpq_magpanel_calc_solution' "$magpanel"; then
    ok "helper de calculate implementado en el nucleo"
else
    bad "falta bpq_magpanel_calc_solution"
fi

# 11) Cache de ondas: consulta antes del fetch, prefetch opcional y cancelable.
worker=$here/../ui/bpq_wave_worker.cxx
workerh=$here/../ui/bpq_wave_worker.h
wcache=$here/../c/bpq_wavecache.c
for f in "$worker" "$workerh" "$wcache"; do
    [ -f "$f" ] && ok "existe $(basename "$f")" || bad "falta $(basename "$f")"
done
if grep -A20 'BpqTrace \*BpqWaveWorker::serve' "$worker" | grep -q 'bpq_wavecache_get'; then
    ok "el worker consulta la cache antes de ir al store/SDS"
else
    bad "el worker no consulta la cache"
fi
if grep -q 'runPrefetch' "$workerh" && grep -q 'gen->load() != jobId' "$worker"; then
    ok "el prefetch se aborta al cambiar de generacion"
else
    bad "el prefetch no se cancela"
fi
if grep -q 'if (queued >= m_prefetchN) break;' "$ui"; then
    ok "PrefetchNeighbors acota las estaciones precargadas"
else
    bad "el prefetch no respeta PrefetchNeighbors"
fi
if grep -q 'if (m_prefetchTimer && m_prefetchN > 0)' "$ui"; then
    ok "prefetch 0 no arranca el timer"
else
    bad "el prefetch no se puede apagar"
fi
if grep -q 'wavecache hit=' "$ui"; then
    ok "el dump publica la cache (--dump)"
else
    bad "el dump no publica wavecache"
fi
if grep -q 'c->wave_cache_mb' "$cfg" && grep -q '"WaveCacheMB"' "$cfg"; then
    ok "WaveCacheMB tiene default y parser"
else
    bad "config no conoce WaveCacheMB"
fi
if grep -q 'max_bytes == 0' "$wcache" && grep -q 'bpq_trace_clone' "$wcache"; then
    ok "0 MB deshabilita la cache y el hit es una copia"
else
    bad "la cache no se deshabilita o no copia"
fi

# 12) P teorica en capas (hyp2000): sale del .hyp del localizador, acotada al
#     dominio regional y con respaldo IASP91 por fila.
tt1d=$here/../c/bpq_tt1d.c
sta=$here/../c/bpq_stations.c
for f in "$tt1d" "$here/../c/bpq_tt1d.h"; do
    [ -f "$f" ] && ok "existe $(basename "$f")" || bad "falta $(basename "$f")"
done
if grep -q 'CommandFile' "$tt" && grep -q 'bpq_tt1d_hyp_load' "$tt"; then
    ok "la P teorica sale del .hyp del localizador (CommandFile)"
else
    bad "no se localiza el .hyp del refinador"
fi
if grep -q 'NOD' "$tt1d" && grep -q 'BPQ_TT1D_DEG2KM' "$tt1d"; then
    ok "bandas por nodo con la metrica de hytra (111.195 km/deg)"
else
    bad "falta la seleccion de banda por epicentro"
fi
if grep -q 'km > g_max_km' "$tt"; then
    ok "el modelo de capas se acota al dominio regional"
else
    bad "sin tope de distancia para la P teorica"
fi
if grep -q 'bpq_tt_theo_p(&theo' "$sta" && \
   grep -q 'csnhypodbp_tt_p(si.dist_deg' "$sta"; then
    ok "stations usa las capas y cae a IASP91 por fila"
else
    bad "stations no usa el modelo de capas"
fi
if grep -q 'theo_max_delta_km' "$cfg" && grep -q 'TheoModel' "$cfg"; then
    ok "TheoModel / TheoMaxDeltaKm tienen default y parser"
else
    bad "config no conoce las claves de la P teorica"
fi

echo "----"
echo "== table ui invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
