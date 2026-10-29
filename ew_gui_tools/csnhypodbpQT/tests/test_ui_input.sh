#!/bin/sh
# test_ui_input.sh - Scroll, teclado y magnitudes del csnhypodbpQT, headless.
#
# Corre el binario real con QT_QPA_PLATFORM=offscreen sobre el fixture `pins`
# (sin anillos ni BD) y comprueba, por el dump:
#
#   - tracesPerScreen=1 deja 3 filas (HHZ/HHN/HHE) y una sola visible;
#   - la rueda sin Ctrl mueve `scroll` y NO la ventana temporal;
#   - P con foco en la tabla arma el picado (no hace type-ahead);
#   - Down con foco en la tabla mueve el canal visible;
#   - P con foco en una caja de texto NO arma el picado;
#   - la pestana magnitudes llena una banda por canal (ntr=3) y tiene ancla;
#   - el eje: banda gris, izq->der acerca y der->izq restituye;
#   - dos clics a la izquierda del ancla restituyen la vista (sin undo);
#   - la tabla de magnitudes oculta las estaciones fuera de la puerta;
#   - el preview es una tabla 2x10 sin fondo amarillo;
#   - la P teorica (IASP91) existe y abre ventana en la tabla de magnitudes;
#   - los checks A/M/Teo propios de magnitudes filtran el calculo;
#   - picar desarma el modo y el siguiente clic vuelve a ser zoom;
#   - seleccion cruzada picking <-> magnitudes y created por solucion.
#
# Uso: sh tests/test_ui_input.sh

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
params=$root/run_working_v8/params
bin=$here/../csnhypodbpQT
[ -x "$bin" ] || bin=$root/ew_8.0/bin/csnhypodbpQT

if [ ! -x "$bin" ]; then
    echo "SKIP: no existe csnhypodbpQT (construye con 'make all')"
    exit 0
fi
if [ ! -f "$params/csnhypodbpqt.d" ]; then
    echo "SKIP: falta $params/csnhypodbpqt.d"
    exit 0
fi

state=$(mktemp -d "${TMPDIR:-/tmp}/bpq_input.XXXXXX") || exit 2
trap 'rm -rf "$state"' EXIT INT TERM

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "ok   $*"; }
bad() { fail=$((fail + 1)); echo "FALLA $*"; }

out=
run_dump() {  # <tab> [VAR=valor...]
    tab=$1
    shift
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_TAB="$tab" \
        BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_dump_kind() {  # <kind>  (fixture + magnitud activa, para el filtro)
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_TAB=magnitudes \
        BPQ_MAG_KIND="$1" BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_dump_cand() {  # candidata del locate (color del titulo)
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_TEST_CAND=1 BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_input() {  # <pares VAR=valor...>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--input BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_sort() {  # <pares VAR=valor...>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--sort BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_axis() {  # <pares VAR=valor...>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--input BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_INPUT_TARGET=axis BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_clicks() {  # <pares VAR=valor...>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--input BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_INPUT_TARGET=waves BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_pick() {  # <x,y> [fase]
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--input BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_INPUT_TARGET=waves BPQ_INPUT_PICK="$1" \
        BPQ_INPUT_PHASE="${2:-P}" BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_locate() {  # <pares VAR=valor...>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--locate BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_dump_wc() {  # <pares VAR=valor...>: cache/precarga con ondas sinteticas
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_TAB=picking \
        BPQ_WAVE_FAKE=1 BPQ_WAVE_REFETCH=1 BPQ_WAVE_WAIT_PREFETCH=1 \
        BPQ_WAVE_WAIT_MS=1200 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
run_dump_magopt() {  # <pares VAR=valor...>
    out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
        BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_TAB=magnitudes \
        BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
        "$@" "$bin" csnhypodbpqt.d 2>/dev/null )
}
ck() {  # <descripcion> <regex ERE>
    if printf '%s\n' "$out" | grep -qE "$2"; then
        ok "$1"
    else
        bad "$1"
        printf '%s\n' "$out" | grep -E '^(traces|pick|mag|input|sort|table|tablecols|origins|stations|preview|checks|locate) ' | sed 's/^/       /'
    fi
}
checked_on() { printf '%s\n' "$out" | sed -n 's/^checks on=\([0-9]*\) total=.*/\1/p'; }

# Cada columna del volcado `tablecols` debe ser >= su minimo calculado.
check_tablecols() {  # <tab>
    _mins=$(printf '%s\n' "$out" | sed -n "s/^tablecols $1 mins=\([0-9,]*\) sizes=.*/\1/p")
    _sizes=$(printf '%s\n' "$out" | sed -n "s/^tablecols $1 mins=[0-9,]* sizes=\([0-9,]*\)$/\1/p")
    if [ -n "$_mins" ] && [ -n "$_sizes" ] && \
       LC_ALL=C awk -v m="$_mins" -v s="$_sizes" 'BEGIN {
           n = split(m, mm, ","); k = split(s, ss, ",");
           if (n != k || n == 0) exit 1;
           for (i = 1; i <= n; i++) if (ss[i] < mm[i]) exit 1;
           exit 0;
       }'; then
        ok "tablecols $1: cada columna >= su minimo"
    else
        bad "tablecols $1: alguna columna bajo el minimo <$_mins | $_sizes>"
    fi
}

# --- 1. filas por canal y tracesPerScreen --------------------------------
run_dump picking
ck "3 filas por canal, 1 visible"     '^traces rows=3 with=[0-9]+ first=0 nvis=1 scroll=0 per=1 '
ck "hay un canal por componente"      '^stations rows=[0-9]+ picked=[1-9]'

# --- 1a. preview: tabla de 2 filas, sin fondo amarillo -------------------
run_dump picking
ck "preview: tabla 2x10 sin fondo"     '^preview grid rows=2 cols=10 headers=Lat,Lon,Depth.km.,Nph,RMS.s.,ML,Mwp,Mb,Ms,Mww values=.* bg=none title_color=#8a5a00$'
run_dump_cand
ck "preview: candidata en rojo"        '^preview grid .* bg=none title_color=#a02020$'
run_locate BPQ_LOCATE_FAIL=1
ck "locate: fallo visible, sin candidata vieja" '^locate=0 err="fallo simulado" cand=0$'
run_locate BPQ_LOCATE_MASK=X
ck "locate: sin picadas dice el conteo" '^locate=0 err="sin picadas para el localizador \(P=0 S=0 M=0 A=0 T=0\)'

# --- 1d. casillas por fila (P y S independientes) -------------------------
run_dump picking
on_before=$(checked_on)
run_input BPQ_INPUT_TARGET=sta BPQ_INPUT_UNCHECK=PB01,P
ck "casilla: P de PB01 desmarcada"    '^input uncheck=PB01,P hits=1 '
on_after=$(checked_on)
if [ -n "$on_before" ] && [ -n "$on_after" ] && [ "$on_after" -lt "$on_before" ]; then
    ok "casilla: checks on $on_before -> $on_after"
else
    bad "casilla: no cambio el estado <$on_before/$on_after>"
fi
run_input BPQ_INPUT_TARGET=sta BPQ_INPUT_UNCHECK=PB01,S
ck "casilla: S de PB01 independiente" '^input uncheck=PB01,S hits=1 '
run_input BPQ_INPUT_TARGET=sta BPQ_INPUT_TOGGLE=PB01,P
ck "casilla: clic real la apaga"      '^input toggle=PB01,P .*checked=0 '

# --- 1c. P teorica: estaciones sin M/A + ventana de magnitud -------------
run_dump magnitudes
ck "P teorica en las estaciones sin M/A" '^stations rows=[0-9]+ picked=[1-9][0-9]* sta_teo=[1-9][0-9]*'
ck "ventanas Teo en la tabla"          '^mag .* theo=[1-9][0-9]* win=[1-9][0-9]* origins=111$'
run_dump_magopt BPQ_MAG_ORIGINS=AM
ck "checks propios de magnitudes"      '^mag .* origins=110$'
run_dump_magopt BPQ_MAG_ORIGINS=NONE
ck "los tres origenes se pueden apagar" '^mag .* origins=000$'

# --- 1b. Created propio de cada solucion (no el time(NULL) de la carga) ---
run_dump picking
created=$(printf '%s\n' "$out" | sed -n 's/^origins created=//p')
c1=${created%%,*}
rest=${created#*,}
c2=${rest%%,*}
if [ -n "$c1" ] && [ -n "$c2" ] && [ "$c1" != "$c2" ]; then
    ok "Created distinto por solucion ($c1 / $c2)"
else
    bad "Created repetido en todas las soluciones <$created>"
fi

# --- 2. teclado con foco en la tabla -------------------------------------
run_input BPQ_INPUT_TARGET=sta BPQ_INPUT_KEY=P
ck "P en tabla arma el picado"        '^pick armed=1 phase=P$'

run_input BPQ_INPUT_TARGET=sta BPQ_INPUT_KEY=Down
ck "Down en tabla mueve el canal"     '^traces rows=3 .* first=1 nvis=1 scroll=1 '
ck "Down no arma el picado"           '^pick armed=0 phase=-$'

# --- 3. guardia de texto: P no se convierte en picado --------------------
run_input BPQ_INPUT_TARGET=text BPQ_INPUT_KEY=P
ck "P en caja de texto no pica"       '^pick armed=0 phase=-$'

# --- 4. rueda: filas sin Ctrl, zoom con Ctrl ------------------------------
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_WHEEL=down
ck "rueda abajo mueve scroll"         '^traces .* first=1 nvis=1 scroll=1 '
ck "rueda abajo no cambia la ventana" '^traces .* win=\[[0-9.]+,?\+120\] '

run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_WHEEL=up BPQ_INPUT_MODS=ctrl
ck "Ctrl+rueda hace zoom"             '^traces .* win=\[[0-9.]+,?\+100\] '

# --- 4b. guia vertical: hover dentro/fuera y leave ------------------------
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_HOVER='0.5,0.5'
ck "guia: hover dentro"               '^input hover=1 guide=1 '
ck "guia: visible en el area"         '^hover guide=1 '
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_HOVER='0.01,0.5'
ck "guia: margen de etiquetas oculta" '^input hover=1 guide=0 '
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_LEAVE=1
ck "guia: leave oculta"               '^input hover=0 guide=0 '

# --- 4c. Shift+rueda: tracesPerScreen 1..3 -------------------------------
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_WHEEL=up,up,up BPQ_INPUT_MODS=shift
ck "Shift+rueda sube a 3"             '^traces .* per=3 '
ck "Shift+rueda no mueve scroll"      '^traces .* first=0 .* scroll=0 per=3 '
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_WHEEL=up,up,up,up BPQ_INPUT_MODS=shift
ck "Shift+rueda satura en 3"          '^traces .* per=3 '
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_WHEEL=down,down,down,down BPQ_INPUT_MODS=shift
ck "Shift+rueda baja a 1"             '^traces .* per=1 '

# --- 5. magnitudes: una banda por canal + ancla --------------------------
run_dump magnitudes
ck "magnitudes: 3 bandas"             '^mag .* ntr=3 anchor=[1-9][0-9]*\.'
ck "magnitudes: magcfg cargada"       '^mag .* magcfg=1 '

# --- 6. ordenacion por cabecera en las CUATRO tablas ----------------------
run_sort BPQ_SORT_TABLE=sta BPQ_SORT_COL=1 BPQ_SORT_CLICKS=1
ck "sta: primer clic distancia DESC"  '^sort table=sta col=1 clicks=1 last=1 order=desc rows='
run_sort BPQ_SORT_TABLE=sta BPQ_SORT_COL=1 BPQ_SORT_CLICKS=2
ck "sta: segundo clic invierte"       '^sort table=sta col=1 clicks=2 last=1 order=asc rows='

# Origenes arranca ordenado por Created: el primer clic invierte, el segundo vuelve.
run_sort BPQ_SORT_TABLE=origins BPQ_SORT_COL=3 BPQ_SORT_CLICKS=1
ck "origins: Created clic invierte"   '^sort table=origins col=3 clicks=1 last=3 order=asc rows='
run_sort BPQ_SORT_TABLE=origins BPQ_SORT_COL=3 BPQ_SORT_CLICKS=2
ck "origins: segundo clic vuelve"     '^sort table=origins col=3 clicks=2 last=3 order=desc rows=100:162:1,100:163:2'

# Una columna sin orden inicial: primer clic numerico DESC.
run_sort BPQ_SORT_TABLE=origins BPQ_SORT_COL=4 BPQ_SORT_CLICKS=1
ck "origins: Date primer clic DESC"   '^sort table=origins col=4 clicks=1 last=4 order=desc rows=100:163:2,100:162:1'
run_sort BPQ_SORT_TABLE=mag BPQ_SORT_COL=5 BPQ_SORT_CLICKS=1
ck "mag: primer clic magnitud DESC"   '^sort table=mag col=5 clicks=1 last=5 order=desc rows='
run_sort BPQ_SORT_TABLE=mag BPQ_SORT_COL=5 BPQ_SORT_CLICKS=2
ck "mag: segundo clic invierte"       '^sort table=mag col=5 clicks=2 last=5 order=asc rows='
run_sort BPQ_SORT_TABLE=events BPQ_SORT_COL=7 BPQ_SORT_CLICKS=1
ck "events: ID primer clic DESC"      '^sort table=events col=7 clicks=1 last=7 order=desc rows=200:162:1'

# --- 7. anchos: sin scroll horizontal en picking y magnitudes ------------
run_dump picking
ck "picking: tabla sin scroll H"      '^table sta cols=5 total=[0-9]+ viewport=[0-9]+ header=[0-9]+ vscroll=[01] hscroll=0 gap=-?[0-9]+$'
run_dump magnitudes
ck "magnitudes: tabla sin scroll H"   '^table mag cols=8 total=[0-9]+ viewport=[0-9]+ header=[0-9]+ vscroll=[01] hscroll=0 gap=-?[0-9]+$'

# --- 7b. Eventos: minimo por columna y reparto del sobrante --------------
run_dump events
ck "events: la tabla llena el panel"  '^table events cols=[0-9]+ total=[0-9]+ viewport=[0-9]+ header=[0-9]+ vscroll=[01] hscroll=[01] gap=[01]$'
check_tablecols events
ev_def=$(printf '%s\n' "$out" | sed -n 's/^table events .* total=\([0-9]*\) .*/\1/p')

run_dump origins
check_tablecols origins
og_total=$(printf '%s\n' "$out" | sed -n 's/^table origins .* total=\([0-9]*\) .*/\1/p')
og_vp=$(printf '%s\n' "$out" | sed -n 's/^table origins .* viewport=\([0-9]*\) .*/\1/p')
if [ -n "$og_total" ] && [ -n "$og_vp" ] && [ "$og_total" -ge "$og_vp" ]; then
    ok "origins: al no caber conserva los minimos ($og_total >= $og_vp)"
else
    bad "origins: encogio por debajo de los minimos <$og_total < $og_vp>"
fi

run_dump events BPQ_WIN_SIZE=1600x900
ck "events: maximizada llena el ancho" '^table events cols=[0-9]+ total=[0-9]+ viewport=[0-9]+ header=[0-9]+ vscroll=[01] hscroll=[01] gap=[01]$'
check_tablecols events
ev_wide=$(printf '%s\n' "$out" | sed -n 's/^table events .* total=\([0-9]*\) .*/\1/p')
if [ -n "$ev_def" ] && [ -n "$ev_wide" ] && [ "$ev_wide" -gt "$ev_def" ]; then
    ok "events: el ancho crece con la ventana ($ev_def -> $ev_wide)"
else
    bad "events: el ancho no crece con la ventana <$ev_def -> $ev_wide>"
fi

run_dump origins BPQ_WIN_SIZE=1600x900
ck "origins: maximizada llena el ancho" '^table origins cols=[0-9]+ total=[0-9]+ viewport=[0-9]+ header=[0-9]+ vscroll=[01] hscroll=[01] gap=[01]$'
check_tablecols origins

# --- 8. seleccion en magnitudes: misma estacion en picking ----------------
run_input BPQ_INPUT_TARGET=mag BPQ_INPUT_SELECT=PB02
ck "mag: seleccion PB02 cambia canvas" '^mag .* ntr=3 anchor=[1-9][0-9]*\.[0-9] .*sta=PB02 sel=PB02 '
ck "mag: PB02 tambien mueve picking"   '^traces .* sta=PB02 '
ck "mag: PB02 seleccionada en sta"     '^stations rows=[0-9]+ picked=[1-9]+ sta_teo=[0-9]+ sel=PB02$'
run_input BPQ_INPUT_TARGET=mag BPQ_INPUT_SELECT=PB02 BPQ_INPUT_REFRESH=1
ck "mag: el refresh conserva PB02"     '^input select=PB02 .*mag_sel=PB02 traces_sta=PB02'
run_input BPQ_INPUT_TARGET=sta BPQ_INPUT_SELECT=PB01
ck "sta: seleccion PB01 vuelve a mag"  '^input select=PB01 .*mag_sel=PB01 traces_sta=PB01'

# --- 9. arrastre sobre el eje: banda, zoom y reset total -------------------
run_axis BPQ_INPUT_AXIS=0.2,0.7
ck "eje: gesto termina sin banda"      '^input axis gestures=1 .* rubber=0$'
w=$(printf '%s\n' "$out" | sed -n 's/^input axis .* win=\([0-9.]*\) .*/\1/p')
if [ -n "$w" ] && [ "$(LC_ALL=C awk -v w="$w" 'BEGIN{print (w < 119.0) ? 1 : 0}')" = 1 ]; then
    ok "eje: izq->der hace zoom (win=$w)"
else
    bad "eje: izq->der no hizo zoom <$w>"
fi
run_axis BPQ_INPUT_AXIS='0.2,0.7;0.8,0.2'
ck "eje: der->izq restituye la vista"  '^input axis gestures=2 win=120.0 t0=1767323040.00 rubber=0$'

# --- 10. dos clics: derecha acerca, izquierda restituye (sin undo) --------
run_clicks BPQ_INPUT_CLICKS='0.3,0.3;0.7,0.3'
w=$(printf '%s\n' "$out" | sed -n 's/^input clicks=.* win=\([0-9.]*\) .*/\1/p')
if [ -n "$w" ] && [ "$(LC_ALL=C awk -v w="$w" 'BEGIN{print (w < 119.0) ? 1 : 0}')" = 1 ]; then
    ok "clics: derecha acerca (win=$w)"
else
    bad "clics: derecha no hizo zoom <$w>"
fi
run_clicks BPQ_INPUT_CLICKS='0.3,0.3;0.7,0.3;0.7,0.3;0.3,0.3'
ck "clics: izquierda vuelve al inicio" '^input clicks=4 win=120.0 t0=1767323040.00 anchor=0.0$'

# --- 11. la tabla de magnitudes solo lista lo que pasa la puerta ----------
run_dump magnitudes
ck "ML: la tabla muestra PB01 (1.1 grados)" '^mag .* sta=PB01 sel=PB01 '
ml_rows=$(printf '%s\n' "$out" | sed -n 's/^mag .* rows=\([0-9]*\) all=.*/\1/p')
ml_all=$(printf '%s\n' "$out" | sed -n 's/^mag .* all=\([0-9]*\) .*/\1/p')
if [ -n "$ml_rows" ] && [ -n "$ml_all" ] && [ "$ml_rows" -lt "$ml_all" ]; then
    ok "ML: oculta las de >8 grados ($ml_rows/$ml_all)"
else
    bad "ML: no filtro la tabla <rows=$ml_rows all=$ml_all>"
fi

run_dump_kind Mwp
ck "Mwp: oculta PB01 (<3 grados) de la tabla" '^mag .* sta=PB01 sel=- '
ck "Mwp: PB01 sigue en picking"       '^stations rows=[0-9]+ picked=[1-9]+ sta_teo=[0-9]+ sel=PB01$'
ck "Mwp: PB01 sigue en el canvas"     '^traces .* sta=PB01 '
mwp_rows=$(printf '%s\n' "$out" | sed -n 's/^mag .* rows=\([0-9]*\) all=.*/\1/p')
mwp_all=$(printf '%s\n' "$out" | sed -n 's/^mag .* all=\([0-9]*\) .*/\1/p')
if [ -n "$mwp_rows" ] && [ -n "$mwp_all" ] &&
   [ "$mwp_rows" -ne "$ml_rows" ] && [ "$mwp_rows" -lt "$mwp_all" ]; then
    ok "Mwp: tabla distinta y filtrada ($mwp_rows != $ml_rows, < $mwp_all)"
else
    bad "Mwp: no filtro la tabla <mwp=$mwp_rows ml=$ml_rows all=$mwp_all>"
fi

# --- 12. picar desarma y devuelve el puntero al zoom -----------------------
run_pick 0.60,0.08
ck "picar coloca y desarma"            '^input pick phase=P placed=1 armed=0 manual=[1-9][0-9]* anchor=0.0$'
ck "tras picar, el clic vuelve a zoom" '^input pick_zoom anchor=[1-9][0-9]{8}'
run_pick 0.01,0.50
ck "clic en la cuneta no desarma"      '^input pick phase=P placed=0 armed=1 manual=[0-9]+'
run_input BPQ_INPUT_TARGET=waves BPQ_INPUT_KEY=P,Esc
ck "Esc desarma la picada"             '^input keyseq=P,Esc target=waves armed=0'

# --- 12b. persistencia entre procesos: la picada es del EVENTO --------------
run_dump picking
ck "manual: la picada persiste por evento" '^manual key=[^ ]+ count=1 '

# --- 13. cache de ondas y prefetch de vecinas -----------------------------
run_dump_wc
ck "prefetch: llena 2 estaciones vecinas"              '^wavecache .* prefetch=2 fail=0 '
ck "prefetch: entran a la cache (9 canales)"           '^wavecache .* entries=9 '
sel=$(printf '%s\n' "$out" | sed -n 's/^stations .* sel=\([A-Z0-9]*\)$/\1/p' | head -n 1)
first=$(printf '%s\n' "$out" | sed -n 's/^wavecache .* order=\([A-Z0-9]*\).*/\1/p' | head -n 1)
if [ -n "$sel" ] && [ "$sel" = "$first" ]; then
    ok "prefetch: la estacion pedida se entrega primero ($sel)"
else
    bad "prefetch: orden de entrega <sel=$sel first=$first>"
fi
run_dump_wc BPQ_PREFETCH=0
ck "cache: el refetch es hit (sin volver al store/SDS)" '^wavecache hit=3 miss=3 '
ck "prefetch off: no encola vecinas"                   '^wavecache .* prefetch=0 fail=0 '
ck "prefetch off: solo la estacion actual"             '^wavecache .* entries=3 '

# --- 14. P teorica con el modelo de capas de hyp2000 ----------------------
run_dump picking
ck "tt: modelo de capas del .hyp del localizador" '^tt model=CHILE_POTIN_N.* max_km=500 d_km=[0-9]'
ck "tt: corrimiento publicado (s)"                 '^tt .* shift=-?[0-9]+\.[0-9][0-9]$'
out=$( cd "$params" && env QT_QPA_PLATFORM=offscreen \
    BPQ_MODE=--dump BPQ_TEST_FIXTURE=pins BPQ_TAB=picking BPQ_THEO=iasp91 \
    BPQ_WAVE_WAIT_MS=100 BPQ_STATE_DIR="$state" \
    "$bin" csnhypodbpqt.d 2>/dev/null )
ck "tt off: vuelve a IASP91 sin corrimiento" '^tt model=iasp91 max_km=500 d_km=0.0 shift=0.00$'

echo "----"
echo "== ui input checks=$((pass + fail)) fallas=$fail =="
[ "$fail" -eq 0 ] || exit 1
exit 0
