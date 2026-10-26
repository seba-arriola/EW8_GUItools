#!/bin/sh
# test_traces_invariants.sh - Invariantes de fuente del render de trazas.
#
# Trampa medida (2026-10-23): el canvas entrega `w/h` en pixeles de DISPOSITIVO
# (ancho_logico * dpr), asi que el grosor de la traza y el minimo de la banda
# van multiplicados por dpr; sin la rama polilinea (spp < 2) el zoom alto se ve
# como escalones de 1 px, y si la escala vertical sale de TODA la traza el zoom
# no revela detalle. El STA/LTA debe leer la traza dibujada (filtrada), como el
# modulo GTK. Estos invariantes no necesitan DISPLAY ni compilar.
#
# Uso: sh tests/test_traces_invariants.sh

set -u

here=$(cd "$(dirname "$0")" && pwd)
traces=$here/../c/bpq_traces.c
wave=$here/../c/bpq_wave.c
mag=$here/../c/bpq_draw_mag.c
ui=$here/../ui/bpq_mainwindow.cxx
canvas=$here/../../commonqt/src/canvas.cxx
locate=$here/../c/bpq_locate.c
coreloc=$here/../../csnhypodbp/csnhypodbp_locate.c
worker=$here/../ui/bpq_locate_worker.cxx
capi=$here/../c/bpq_capi.h

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

for f in "$traces" "$wave" "$mag" "$ui"; do
    if [ -f "$f" ]; then ok "existe $(basename "$f")"; else bad "falta $f"; fi
done

# Grosor en pixeles logicos: ninguna linea de traza puede quedar sin * dpr.
if grep 'cairo_set_line_width' "$traces" | grep -v 'dpr' >/dev/null 2>&1; then
    bad "hay cairo_set_line_width sin dpr en bpq_traces.c"
else
    ok "todo cairo_set_line_width de bpq_traces.c usa dpr"
fi

# Zoom alto: polilinea muestra a muestra con los helpers del nucleo GTK.
if grep -q 'csnhypodbp_view_use_polyline' "$traces" &&
   grep -q 'csnhypodbp_view_sample_x' "$traces"; then
    ok "zoom alto: polilinea por muestra (use_polyline/sample_x)"
else
    bad "falta la rama polilinea del zoom alto"
fi

# Escala vertical por VENTANA VISIBLE: el maximo |muestra| no puede salir de un
# barrido de toda la traza (patron `for (i = 0; i < n; i++)` del render viejo).
if grep -q 'for (i = 0; i < n; i++)' "$traces"; then
    bad "queda el barrido global de amplitud (escala de toda la traza)"
else
    ok "sin barrido global: la escala sale de la ventana"
fi
if grep -q 'csnhypodbp_view_min_half' "$traces"; then
    ok "envolvente con grosor minimo (min_half)"
else
    bad "la envolvente no usa min_half"
fi

# STA/LTA sobre la traza dibujada (filtrada), como el GTK.
if grep -q 'bpq_trace_filtered' "$wave"; then
    ok "STA/LTA lee el buffer filtrado"
else
    bad "bpq_stalta_new no usa bpq_trace_filtered"
fi

# Scroll de filas: barra vertical + helper del nucleo, y la rueda sin Ctrl no
# cambia la ventana temporal.
if grep -q 'QScrollBar' "$ui" && grep -q 'bpq_traces_scroll_to' "$ui" &&
   grep -q 'syncTracesScroll' "$ui"; then
    ok "scroll: QScrollBar + bpq_traces_scroll_to"
else
    bad "falta la barra o el scroll acotado del canvas"
fi
if grep -q 'm_tracesCtx.scroll_row - steps' "$ui"; then
    ok "rueda sin Ctrl desplaza filas"
else
    bad "la rueda no desplaza filas"
fi

# Guia vertical: hover sin boton en el canvas + dibujo condicionado al area.
if grep -q 'setMouseTracking(true)' "$canvas" &&
   grep -q 'CanvasWidget::hovered' "$ui" &&
   grep -q 'guide_valid' "$traces"; then
    ok "guia vertical: hover + guide_valid"
else
    bad "falta el hover o el dibujo de la guia"
fi

# Shift+rueda: helper del nucleo y rama propia en el filtro (no scroll).
if grep -q 'bpq_traces_per_screen_step' "$ui" &&
   grep -q 'ShiftModifier' "$ui" &&
   grep -q 'm_perScreenWheelAccum' "$ui"; then
    ok "Shift+rueda cambia tracesPerScreen"
else
    bad "Shift+rueda no tiene rama propia"
fi

# Casillas por fila: locateNow arma pick_on desde el modelo, por indice de
# catalogo (rows[i].idx), no con todas marcadas.
if grep -q 'm_staModel->isChecked' "$ui" &&
   grep -q 'on.resize(nmax \* 2)' "$ui" &&
   ! grep -q 'on.append(1)' "$ui"; then
    ok "casillas por fila -> pick_on por indice de catalogo"
else
    bad "locateNow no lee las casillas por fila"
fi

# Magnitudes: la lista del tanque sale de la puerta de la magnitud pedida y no
# se corta en 24 antes de saber si hay traza.
if grep -q 'bpq_magpanel_fetch_rows' "$ui" &&
   ! grep -q 'req.size() < 24' "$ui"; then
    ok "magnitudes: peticion por puerta, sin tope previo de 24"
else
    bad "calculate sigue pidiendo las 24 primeras sin mirar la puerta"
fi

# Overlay manual: la clave es el EVENTO (qid:otime), no la solucion elegida.
# Cambiar de Origin no puede borrar las picadas; guardar exige la misma clave.
if grep -q 'm_selEventKey != m_manKey' "$ui" &&
   ! grep -q 'm_selKey != m_manKey' "$ui" &&
   grep -q 'm_manKey != m_selEventKey' "$ui"; then
    ok "overlay manual por evento (no por solucion)"
else
    bad "el overlay manual sigue atado a la solucion"
fi

# Preflight del locate: el conteo por origen hace visible "solo M" con 0 M.
if grep -q 'bpq_locate_count_origins' "$ui" &&
   grep -q 'bpq_locate_count_origins' "$locate"; then
    ok "locate: preflight con conteo por origen"
else
    bad "el locate no muestra el conteo por origen"
fi

# Magnitudes: distancias desde la candidata, peticion de trazas con BpqWave y
# ventana propia por magnitud (vertical Z para Mwp/Mb/Ms, N/E para ML).
if grep -q 'bpq_stations_rebase' "$ui" &&
   grep -q 'buildMagReqs' "$ui" &&
   grep -q 'bpq_magpanel_req_window' "$ui" &&
   grep -qF "chans[k][l - 1] != 'Z'" "$ui" &&
   grep -q 'BPQ_TRACE_MAX' "$capi" &&
   grep -q 'req.at(i).t0' "$worker" &&
   grep -q 'H=%d' "$worker"; then
    ok "magnitudes: ventana por magnitud (Z/N/E) via BpqWave y H en el error"
else
    bad "la peticion de trazas no usa la ventana/canal de cada magnitud"
fi
if grep -q 'El SCNL de la picada manda' "$coreloc"; then
    ok "hypos_write conserva el SCNL de la picada"
else
    bad "hypos_write vuelve a escribir net/loc en '--'"
fi

# Teclado: filtro de aplicacion + tabla sin type-ahead.
if grep -q 'qApp->installEventFilter' "$ui" &&
   grep -q 'handlePickingKey' "$ui"; then
    ok "teclas del picking en la ventana (cualquier foco)"
else
    bad "las teclas siguen dependiendo del foco del canvas"
fi
if grep -q 'setFocusPolicy(Qt::NoFocus)' "$ui" &&
   ! grep -q 'keyboardSearch' "$ui"; then
    ok "tabla de estaciones sin type-ahead"
else
    bad "la tabla puede robar las teclas"
fi

# Magnitudes: una banda por canal, no una sola traza Z.
if grep -q 'BpqMagTrace' "$mag" && grep -q 'c->trs' "$mag" &&
   grep -q 'c->ntr' "$mag"; then
    ok "canvas de magnitudes multicanal (trs/ntr)"
else
    bad "el canvas de magnitudes sigue limitado a una traza"
fi
if grep -q 'fillMagCtx' "$ui" && grep -q 'm_magTrs' "$ui"; then
    ok "UI llena todas las bandas de la estacion"
else
    bad "renderMagnitudes no llena el contexto multicanal"
fi

echo "----"
echo "== traces invariants checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
