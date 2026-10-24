# csnhypodbpQT — prototipo Qt6 de `csnhypodbp`

Port del GUI `csnhypodbp` (GTK4/libadwaita) a **Qt 6 Widgets**, sin modificar una sola línea
del código existente. El plan completo, el análisis del módulo original y los criterios de
aceptación están en [`docs/testLibQT.md`](../../docs/testLibQT.md).

## Idea en una línea

No se reimplementa la lógica: se **compila in situ** el código C puro de `../csnhypodbp/` y el
subconjunto puro de `../common/src/`, y se reescribe solo la capa de presentación (Qt) y la
fontanería de datos que hoy vive dentro de los archivos GTK.

## Estado

| fase | contenido | estado |
|---|---|---|
| 0.1 | Makefile con detección de Qt6, instalación atómica y `VALIDATE_FLAGS` | **hecho y verificado** (`--list` + `--validate` OK) |
| 0.2 | `c/bpq_config.c`: port fiel de `ReadConfig()`/`maps_print_config()`/`maps_check_profiles()` | **hecho y verificado** (`checks=97 fallas=0`; `--print-config` **idéntico** al GTK, 66 líneas) |
| 0.4 | `c/bpq_canvas.c` (cairo→buffer) + `ui/` (ventana, pestañas, lienzos, `--dump/--layout/--shot`) | **hecho y verificado** (`canvas checks=72 fallas=0`; ventana offscreen 1100x750; PNG de 22 KB) |
| 1 | anillos (`bpq_rings`), mensajes de magnitud (`bpq_magmsg`), emisores (`bpq_emit`), store de eventos (`bpq_events`) | **hecho y verificado** (`rings checks=44`, `events checks=74`) |
| 2 | modelos Qt de Eventos/Origins + worker de anillos (500 ms/1 s) + tablas con datos | **hecho**: la maestra y el detalle se pueblan con los eventos del anillo (verificado contra el EarthWorm en marcha) |
| 2b | mapa y perfil reales (`bpq_map`, `bpq_profile`) | **hecho**: fondo equirectangular o lienzo virtual, capas `.lines`, rectas de perfiles, marcadores por tipo y la elegida en verde; perfil slab+topo encuadrado a sus datos |
| 3 | ondas (store→SDS), tabla de estaciones, búsqueda, historial de BD | **hecho**: ondas store→SDS en `QThread`, tabla de estaciones, canvas de trazas, **historial de BD** (24 h al arrancar) y **búsqueda por rango** (menú *Events*). Queda afinar el picking (fase 4) |
| 4 | picking (trazas, filtros, STA/LTA, repick, **locate**) | **completa**: canvas, picking con el ratón (overlay local visible + persistencia), componentes por estación en el orden del `.d` + `tracesPerScreen`, Shift+flechas, filtros de vista (presets del `.d`, `Filters…`, fase cero) con sus atajos, STA/LTA y **`locate`** con el residual del localizador en la tabla |
| 5 | preview, `locate`, Confirm/Discard, export | **hecho**: preview de dos mitades con un solo `render` (candidata/elegida/guiones y `Confirm`/`Discard`), spinner `calculando <loc>… N s` junto al `Origin time`, `Fix`/`FixedDepthKm` en la barra, `Confirm` que publica el ARC (byte 174 = `M`) en `ConfRing` y lo deja como origen confirmado en el store, y `Export view…` con el contexto real |
| 6 | magnitudes, Mww, integración final | **fase 6 completa**: *magnitudes* con tabla de 8 columnas (`magrow`), traza procesada (`magview`) con **franja arrastrable** (`magmarks`), radios ML/Mwp/Mb/Ms, puerta de distancia y `calculate`/`calculate all` (tank + `csnmags` con overrides). Panel **`Mww`** (`csnwphase`) con 31 parámetros (11 editables), `Calculate`/`Cancel` en hilo, playera + resumen y salida completa. **6.4 hecho**: al confirmar se guardan Mww + tensor (WPHASE/WCMT) sobre el `origin` del pipeline y la solución queda preferida (evento reusado por qid + Δt ≤ 300 s, `mod` del localizador). **6.5 hecha**: módulo propio `MOD_CSNHYPODBPQT 172`, `.d` propio `csnhypodbpqt.d`, `Process` activo en `startstop_unix.d` (coexiste con el GTK 158), runtime `run_working_v8/csnhypodbpqt/` y tests de integración |
| 7 | origen fijado (*pin*) | **hecho**: doble clic en una fila de *Origins* la fija y lleva a *Procesar → picking*. Mientras hay pin, *Selected solution*, *Magnitude*, la tabla de estaciones/residuales, *magnitudes* y las ondas muestran **solo** esa solución; el clic simple no lo mueve y el refresco del anillo no lo pisa. Sobrevive a que cambie la preferida (identidad estable de evento `qid` + 1.ª `otime`); se suelta al cambiar de evento o si la solución desaparece. `bpq_pin.{h,c}` + `tests/test_pin.c` (29 checks), `tests/test_pin.sh` (45 checks e2e offscreen) y `tests/test_pin_invariants.sh` (12 checks) |

### Verificación actual (`make check`)

```
== checks=98 fallas=0 ==            # config contra csnhypodbp.d real
== canvas checks=72 fallas=0 ==     # cairo -> buffer, 7 escenas, export
== rings checks=44 fallas=0 ==      # formato byte a byte del pick, rd_mag seguro, errores de apertura
== events checks=89 fallas=0 ==     # agrupacion, dedupe, preferida, magnitudes, celdas, orden y Created
== draw_map checks=86 fallas=0 ==   # planta (fondo, ejes, marcadores, auto-encuadre, zoom/paneo/clic) y perfil
== wave checks=82 fallas=0 ==      # decode TRACE2, fetch, filtro (fase cero), STA/LTA y tank
== stations checks=45 fallas=0 ==  # catalogo, distancia/orden, M > A > Teo y P teorica IASP91
== traces checks=70 fallas=0 ==    # canvas: etiquetas de canal, envolvente, escala, gaps, marcas, clic y scroll
== db checks=31 fallas=0 skips=0 == # parseo UTC, sin/con DB, busqueda real y magnitudes del historial
== locate checks=61 fallas=0 ==     # picadas efectivas M>A>Teo, checks, MaxDistKm y SCNL del catalogo
== preview checks=115 fallas=0 ==  # 5 estados, formateo, "-" sin dato y atajos de filtro
== manual checks=98 fallas=0 ==     # overlay de picadas, persistencia, canales por banda y zoom/span
== magpanel checks=93 fallas=0 ==   # ventanas, Teo, filtro de tabla, overrides, calculate y canvas
== axis checks=38 fallas=0 ==       # eje comun: fondo/filete/ticks, paso 1-2-5, banda gris y gesto release
== mww checks=111 fallas=0 ==      # semilla, tabla (rango/parseo), fallo nunca mudo, playera y guardado
== pin checks=29 fallas=0 ==       # nucleo del pin: set/replace/clear/reconcile y clave de origen
OK: sin libgtk/libadwaita/libgobject/libgio
OK: --print-config identico (66 lineas)   # contra csnhypodbp.d y csnhypodbpqt.d
== integracion checks=16 fallas=0 == # identidad 172, .d propio, startstop, runtime y binario
== mapa de fondo checks=11 fallas=0 == # MapImageFile: bg real, virtual sin clave, fallo en stderr
== pin invariants checks=12 fallas=0 == # doble clic, pin en Procesar, candidata por clave
== traces invariants checks=15 fallas=0 == # render, scroll, teclado en la ventana y magnitudes multicanal
== table ui invariants checks=39 fallas=0 == # proxies, Created, eje, filtro, preview, Teo, checks y calculate
== ui input checks=48 fallas=0 ==  # scroll, teclado, orden, eje, reset, filtro, Teo, picada/desarme y preview
== pin checks=45 fallas=0 ==       # e2e offscreen: fija, sobrevive refresh/confirmacion, suelta
== maximize ciclos=5 vivo=1 ==      # test-live: ventana Qt (DISPLAY + EarthWorm)
```

Total: **1348 checks** (1162 en C + 16 de integración + 12 de invariantes del pin + 15 del render y
teclado + 39 de tablas/preview + 48 de input/scroll + 45 e2e + 11 del mapa), 0 fallas. El lector SDS
compartido suma **26** en `make -C ew_gui_tools/csnhypodbp test_sds_read` (incluye el registro que
cruza `t1` y el arranque en fracción de segundo).

### Created real, ordenación y anchos (2026-10-24)

- **`Created` por solución**: `bpq_db.c` parsea `origin.creation_time` y lo pasa a
  `bpq_events_feed_arc_at`; un re-feed del anillo con `created=0` conserva el valor de la BD.
  Antes todas las filas de una carga compartían el `time(NULL)` de `bpq_events.c`. En vivo (sin
  fila de BD) el sello sigue siendo la **recepción**: el ARC no trae hora de creación, así que dos
  soluciones procesadas en el mismo segundo comparten ese segundo; para distinguirlas haría falta
  mostrar sub-segundos.
- **Cabeceras ordenables en las 4 tablas** (`master-events`, `detail-solutions`, `sta-table`,
  `mag-table`): `SortProxy` + `wire_header_sort`; `initialColumn=-1` conserva el orden natural
  (estaciones/mags por distancia) hasta el primer clic. Los índices de selección se mapean con
  `mapToSource`/`mapFromSource`.
- **Anchos exactos en Procesar**: `ewguiqt::fit_columns` ajusta cada columna a
  `max(cabecera, valor más largo)+padding`, sin stretch de la última; `fitProcesarTable` ensancha
  el panel del splitter lo justo y el canvas se queda con el resto. `QSizePolicy::Ignored` en la
  tabla y en la etiqueta de estado evitan que fijen el mínimo de la ventana. Si la ventana no
  alcanza, el scroll horizontal queda como último recurso.
- **Selección compartida picking ↔ magnitudes**: al seleccionar una fila de magnitudes,
  `selectStation` actualiza `m_selSta`, sincroniza ambas tablas y pide las ondas una sola vez;
  `BpqMagModel::setRows` no resetea si las filas no cambiaron, así la selección sobrevive a
  `traceReady` y al arrastre de la franja.
- Tests: `events` 89, `table ui invariants` 28, `ui input` 40; `test_pin.sh` 45 sigue verde.

### Eje, etiquetas, filtro y preview (2026-10-24)

- **Un solo eje de tiempo** (`c/bpq_axis.c`, `bpq_axis_paint`): picking y magnitudes comparten fondo
  neutro, filete superior, ticks cortos de 4 px, etiquetas `HH:MM:SS` de 9 px negras y paso 1-2-5 en
  múltiplos redondos. Se acabó la rejilla de altura completa del picking.
- **Banda de arrastre**: mientras se mantiene pulsado el eje se pinta el intervalo en gris
  (`rgba(0.5,0.5,0.5,0.35)`); al soltar, izq→der hace zoom al rango y der→izq restituye la vista del
  evento. El gesto puro es `bpq_traces_axis_release`; el mismo `resetTimeView` que usa el segundo
  clic a la izquierda del ancla. No hay pila de undo.
- **Etiquetas de traza**: solo el canal (`HHZ`, `HHN`, `HHE`, `HNZ`...), en `sans` bold de 27 px
  negro, centrado en la banda. La cuneta pasa a 72 px (`bpq_traces_left_margin`/`bpq_mag_left_margin`,
  compartido con hit-test, zoom y eje); la estación y la distancia quedan en la tabla.
- **Filtro de la tabla de magnitudes**: solo se listan las filas `in_range` de la magnitud activa
  (ML 0–8°, Mwp 3–90°, Mb 5–105°, Ms 20–160°, con el override del usuario). Las ocultas siguen en
  picking y en el canvas (`m_magRows`); la tabla usa `m_magTableRows` y
  `bpq_magrows_filter_in_range`. `BPQ_MAG_KIND` permite fijar la magnitud en los tests.
- **Preview en tabla de 2 filas**: encabezados (`Lat/Lon/Depth[km]/Nph/RMS[s]/ML/Mwp/Mb/Ms/Mww`) y
  una fila de valores; los radios de magnitud van en su celda con el valor como texto. Sin fondo
  amarillo/rojo: el estado (candidata/elegida) va en el color del título. `BpqPreviewPanel::dump()`
  publica `rows/cols/headers/values/bg/title_color` para los tests headless.
- Tests: `axis` 38, `traces` 70, `magpanel` 72, `table ui invariants` 28 y `ui input` 40 (incluye
  `preview grid` y `BPQ_MAG_KIND=Mwp`).

### Origen fijado (pin)

En la pestaña *Eventos*, un **doble clic** en una fila de *Origins* fija esa solución y
lleva a *Procesar → picking*. Mientras hay un origen fijado:

- la tabla superior (título *Selected solution* y columnas de magnitud) muestra **solo**
  las celdas de esa solución (magnitudes por origen, no el agregado del evento ni la
  preferida);
- la tabla de estaciones/residuales, la pestaña *magnitudes*, la semilla de `Mww` y las
  ondas se preparan con sus `picks` y su `otime`;
- el **clic simple y el teclado no mueven el pin** (la selección vuelve a la fila
  fijada); solo se cambia fijando otro origen (doble clic o clic en un marcador del
  mapa), y el pin solo se suelta al cambiar de evento, al buscar/limpiar o si la
  solución desaparece;
- el refresco del anillo no pisa la selección y **no vuelve a descargar ondas** de la
  misma solución; el doble clic sí fuerza la recarga;
- la candidata del `locate` conserva la precedencia mientras existe; `Discard` vuelve
  al pin y `Confirm` no lo mueve.

La identidad del evento es **estable** (`qid` + `otime` de la primera solución), así que
una confirmación que cambia la solución preferida no pierde el evento ni el pin. El
estado es en memoria (no se persiste).

Tests: `make check` incluye el núcleo C (`tests/test_pin.c`), los invariantes de fuente
(`tests/test_pin_invariants.sh`) y el end-to-end offscreen (`tests/test_pin.sh`, usa
`BPQ_TEST_FIXTURE=pins` + `BPQ_MODE=--dump`, sin DISPLAY, anillos ni BD).

`calculate` de magnitudes (fase 6.1: tank real + `csnmags` con la ventana ajustada):

```
$ BPQ_MODE=--magcalc csnhypodbpQT csnhypodbp.d
locate=1 secs=2.3 qid=933600094 ver=2 mod=0 lat=-24.3922 lon=-67.7298 depth=150.0 nph=12 rms=1.610 npicks=12
magdrag sta=AF01 kind=0 win=[10,44] touched=1     # franja arrastrada 1 s (ML 10..45 -> 10..44)
magcalc busy=0 ml='' mwp='4.8-6' mb='' ms=''      # csnmags con el override
mag kind=0 rows=137 in_use=15 touched=1 first=[AF01 t0=10 t1=44]
```

Las magnitudes vacías son el resultado real de `csnmags` con las respuestas/calibración de este entorno; el camino completo (tank → overrides → `csnmags` → preview) sí corrió.

`locate` de verdad (cadena completa, sin GUI; dura ~1.5 s):

```
$ BPQ_MODE=--locate csnhypodbpQT csnhypodbp.d            # localizador del .d (csnloc)
locate=1 secs=1.6 qid=933600023 ver=1 mod=0 lat=-22.1230 lon=-66.8970 depth=0.0 nph=7 rms=1.650 npicks=7
stations rows=137 res=7
picks P=7 S=0 max|res|=2.49

$ BPQ_LOCATE_LOC=hyp2000_ring BPQ_MODE=--locate ...      # el refinador del CSN
locate=1 secs=1.5 qid=933600023 ver=1 mod=0 lat=-21.4948 lon=-68.7758 depth=165.2 nph=6 rms=0.090 npicks=7
stations rows=137 res=7
picks P=7 S=0 max|res|=1.37
```

Estado de la columna derecha con datos (offscreen):

```
$ QT_QPA_PLATFORM=offscreen BPQ_MODE=--dump csnhypodbpQT csnhypodbp.d \
      | grep -E '^data|^panels|^stations|^traces|^db'
data events=54
data origins=15
panels pts=15 pref=6 sel=6 prof=1 zoom=2.106667 layers=5 bg=10800x5400
stations rows=274 picked=12
db avail=1 search=0
traces rows=12 with=12 win=[1792685408.26,+120]
```
`layers=5` = las 4 capas `.lines` del `.d` (placas, subducción, regiones, fallas) cargadas y
dibujadas; `prof=1` = perfil P02 (el más cercano a la solución preferida); `zoom` = auto-encuadre
con el suelo `MapMinSpanDeg`. Con `BPQ_TEST_ARC=<arc>` sobre un evento sintético sale
`pts=1 pref=0 sel=0 prof=0 zoom=2.106667`.

`bg=<ancho>x<alto>` = el `MapImageFile` ya decodificado; `bg=0x0` = lienzo virtual (clave
ausente/comentada o imagen ilegible con su motivo en `stderr`). Es el invariante de
`tests/test_map_background.sh`, que existe por una trampa medida: **Qt6 (hasta 6.4) limita
`QImageReader` a 128 MB** y el mapa 10800×5400 ocupa 233 MB en ARGB32, así que sin
`QImageReader::setAllocationLimit(1024)` en `ui/main.cxx` (mismo valor que `csnrvQT`) Qt rechaza
la imagen y el panel se quedaba **sin fondo en silencio** (sólo un mensaje de barra de estado que
el siguiente mensaje tapaba).

### Historial y busqueda en la base hipocentral (2026-10-22)

- `c/bpq_db.c` reutiliza el núcleo puro `common` (`ewguidb_*`, libpq) sin SQL propio, con el
  mismo recorrido que la GUI GTK (`csnhypodbp_ws.c:678-830`):
  `ewguidb_query_recent_events` / `ewguidb_search_events` → `ewguidb_query_origins` →
  `ewguidb_query_picks` → `bpq_events_feed_arc`. Así el historial entra por el **mismo** camino de
  agrupación/preferencia que el vivo.
- Mapeo: `ewgui_origin_id_parse` da `mod`/`version`/`confirmed`; `E<qid>-…` da el `qid`;
  `origin.status == "REVIEWED"` ⇒ solución manual; el canal `NET.STA.LOC.CHAN` se reduce a **STA**
  (es lo que empareja `statable`); `pick.uncertainty` viaja como **residual** y
  `pick_status == "manual"` como picada manual.
- Las **magnitudes** del historial también entran: `ewguidb_query_magnitudes` por origen →
  `BpqMagMsg` → `bpq_events_feed_mag`, así que la maestra muestra `valor-nst` igual que para el
  vivo (medido: `celda[8]=2.7-4`, `celda[12]=163` de módulo).
- Al arrancar se cargan las últimas **`DbHistoryHours`** horas (tope 60 eventos: son 2 consultas
  por evento). Medido: 14 eventos del anillo → **54** (historial) y el evento elegido pasó a
  **15 orígenes**.
- Menú **Events → Search events…** (diálogo propio con from/to UTC + tope de eventos) y
  **Clear search**. La búsqueda **vacía el store** (`bpq_events_clear`) y carga el resultado: el
  rango reemplaza la vista; *Clear search* vuelve a vivo + historial.
  Diferencia conocida con la GTK: el anillo **sigue alimentando** el store durante la búsqueda
  (no se pausa el worker), así que un sismo nuevo puede aparecer mezclado con el resultado.
- `PGconn` **no es thread-safe**: todo el acceso a la BD va por el hilo de la GUI (igual que la
  GTK); el fetch de ondas usa otro hilo pero no toca `EwGuiDb`.
- Pendiente: el calendario ya está en el diálogo (botón `...` junto a cada campo; escribe la fecha
  conservando la hora o `00:00:00` si no la traía, en ventana modal propia).

### Filtros de vista y STA/LTA (fase 4)

- El filtro es **solo de la vista**: `bpq_trace_filter` escribe el buffer *filtrado* de
  `EwGuiTrace` (`ewgui_trace_filter`, que hace `demean(crudo)` primero) y el canvas lee ese
  buffer con `use_filtered`. **El crudo no se toca**, que es lo que `locate` debe mandar.
- El menú **Fi*l*ter** se arma con los presets del `.d` (`Filter HP r 1.0 2`, `LP k`, `BP d`,
  `P_local 1`, `P_regional 2`, `P_teleseismic 3`, `S_local 4`), cada uno con su atajo, más `Raw`
  (solo demean) y el toggle **STA/LTA (vista)** con su tecla (`t`). `FilterPhase zero` del `.d`
  viaja como `zerophase` del filtro (fase cero = `|H|²`: orden efectivo doble).
- **STA/LTA** (`ewgui_stalta_run`/`_triggers`): curva CF en violeta normalizada a la banda de la
  fila, y una marca discontinua en el disparo **ya corregido** con `StaLta … corr` del `.d`
  (que suele ser `-sta/2`). No crea picks: es la estimación visual, como en la GTK.
- Las trazas se filtran **al llegar** (`filterOne` en `traceReady`), así el `QThread` no reparte
  trabajo de dibujo; la CF se reconstruye al terminar el pedido (`jobFinished`).
- Ojo con los huecos: el store rellena con `INT_MAX`, así que el canvas los **salta**
  (`is_gap`) antes de calcular amplitud y envolvente; si no, un gap se come la escala.

### Atajos del picking y `Filters…` (fase 4.3)

- Las teclas de filtro salen del **propio `.d`** (`FilterMenu f`, `FilterRaw 0`, `StaLta … t`) y se
  resuelven con el núcleo puro `csnhypodbp_filtercfg_key_action`: un preset lo enciende, **la tecla
  del activo lo apaga** (vuelve a `Raw`), `0` va a Raw, `f` abre `Filters…` y `t` alterna el STA/LTA.
  Nada de eso es un `QAction::setShortcut`: lo maneja `keyPressEvent`, así que **sólo actúa en la
  sub-pestaña `picking`** y **no se consume con el foco en una caja de texto** (ahí la tecla es
  texto). Verificado: con `BPQ_TAB=magnitudes` las mismas teclas no tocan el filtro.
- `Filters…` es una **ventana modal propia** (`ui/bpq_filters_dialog`, sin popups): lista los presets
  con su atajo (etiqueta de `csnhypodbp_filtercfg_format_preset`), `Raw`, y los checks
  `FilterPhase zero` y `STA/LTA`. Aceptar reaplica el filtro con la fase elegida; la fase cero viaja
  a `ewgui_trace_filter` como `zerophase` (afecta al sesgo del clic, no al audio que se localiza).

Prueba con eventos reales (`BPQ_MODE=--mouse`, la tecla se manda a la ventana):

```
== picking ==                          == magnitudes ==
mouse filter_key1 -1 -> 0              mouse filter_key1 -1 -> -1
mouse filter_key1_again -> -1          mouse filter_key1_again -> -1
mouse filter_key_t stalta=1            mouse filter_key_t stalta=0
```

(`-1 → 0` enciende `P_local`, la segunda pulsación lo apaga; en `magnitudes` no pasa nada.)

### Ratón: pick local, zoom y paneo (fases 4.3/4.5)

Qué hace cada gesto, y dónde vive el estado:

| gesto | canvas | efecto |
|---|---|---|
| rueda | planta / perfil | zoom anclado al cursor (`bpq_map_zoom_at` / `bpq_profile_zoom_at`) |
| arrastre izq. | planta / perfil | paneo; el contenido sigue al cursor (`*_pan`) |
| clic | planta | selecciona ese origen en *Origins* (marcador verde) |
| **`P` / `S`** | trazas | **arma el modo picada**: el cursor pasa a ser un **círculo del color de la fase con la letra** (`pColor`/`sColor`); `Esc` lo desarma |
| clic izq. (armado) | trazas | coloca la picada de esa fase en el overlay local y **desarma el modo** (puntero por defecto: el siguiente clic es zoom) |
| clic derecho (armado) | trazas | quita la picada manual de esa fase; **no** desarma (para quitar tras picar hay que volver a armar con `P`/`S`) |
| **dos clics izq.** | trazas | el 1.º deja un **ancla** (línea discontinua azul); el 2.º **a la derecha acerca** al intervalo y **a la izquierda restituye la vista original** |
| arrastre izq. | trazas | paneo temporal (`bpq_traces_pan`) |
| rueda | trazas | zoom temporal anclado al cursor (`bpq_traces_zoom_at`, 1 s…1 h) |
| **arrastre en el eje** | barra inferior | marca el intervalo en **gris**; **izquierda→derecha acerca** al rango y **derecha→izquierda restituye** la vista del evento |

- **El clic NO publica al anillo.** La picada va a un **overlay local** (el `csnhypodbp_manual.c`
  puro del módulo GTK, con `set` semántico: recolocar reemplaza la de esa fase y deja intacta la
  otra) y se ve **al instante**: la marca de la traza se mueve y la fila de la tabla pasa a origen
  `M` (ranking `M > A > Teo`). Recién al **Confirmar** se publican a `PICK_RING` y viajan en el ARC.
  El modo picada es **de un solo tiro**: al colocar la picada `m_pickArmed` vuelve a 0,
  `updatePickCursor()` restaura el puntero y se limpia el ancla de zoom; si el clic cae en la
  cuneta o el overlay falla, sigue armado para reintentar.
  Así el operador ajusta sin que `csnloc` asocie picadas a medio hacer — que es exactamente el
  defecto que tenía la versión anterior, donde el clic sólo escribía al anillo y la pantalla no
  cambiaba.
- **Modo picada**: `P`/`S` no sólo eligen la fase — **arman** el modo y el cursor pasa a ser un
  círculo del color de la fase con su letra, para que se vea qué se va a colocar. `Esc` desarma y
  devuelve el cursor normal. Con el modo armado, el clic izquierdo pica y el **derecho quita** la
  picada manual de esa fase (los dos actúan sólo sobre el overlay).
- **Zoom por dos clics**: el primero deja un **ancla visible** (`zoom_anchor_t` del contexto, línea
  discontinua); el segundo clic a la derecha acerca al intervalo (`bpq_traces_zoom_range`), y a la
  izquierda **restituye la vista original** (`resetTimeView`: `m_defT0`/`m_defWin`), en una sola
  acción y sin pila de undo. Sin ancla, un clic no cambia nada.
- **Arrastre en el eje**: la barra inferior de tiempos es un canvas propio. Mientras se arrastra
  pinta una **banda gris** (`rgba(0.5,0.5,0.5,0.35)`, el mismo gris de la GTK); al soltar,
  izquierda→derecha hace zoom al intervalo y derecha→izquierda vuelve a la vista del evento. El
  cálculo puro vive en `bpq_traces_axis_release` (2 px / 0.1 s de umbral).
- **El eje es el mismo en picking y magnitudes** (`c/bpq_axis.c`): fondo neutro, filete superior,
  ticks cortos de 4 px, etiquetas `HH:MM:SS` de 9 px negras y paso 1-2-5 en múltiplos redondos.
- La ventana de las trazas vive en `m_winT0`/`m_winLen` (los dos canvas la comparten y el eje la
  sigue). Se reinicia al cambiar de evento, junto con el ancla.
- El overlay **persiste** en `<ManualStateFile>.picks` (formato propio; el `.events` de la GTK no se
  toca), con la **clave del evento** en el encabezado: al cambiar de evento se ignora el de otro, y
  al volver se recupera. Se crean los directorios padre al guardar (el runtime de un binario que no
  es módulo puede no existir, y sin eso el guardado fallaba en silencio).
- El clic en el mapa se resuelve con `bpq_map_hit_test` (tolerancia 10 px de dispositivo).

Prueba sin clics (`BPQ_MODE=--mouse`): manda `QMouseEvent`/`QWheelEvent` **reales** a los tres
canvas y vuelca el estado resultante:

```
mouse map wheel zoom 2.106667 -> 2.528000
mouse map drag pan_x 0.000000 -> 40.000000
mouse prof wheel zoom 1.000000 -> 1.200000
mouse traces wheel win 120.0 -> 100.0 (t0 1792689629.45 -> 1792689635.63)
mouse traces drag t0 1792689635.63 -> 1792689591.51
mouse pick_armed=1 phase=P
mouse pick n=1 row0=PB01 P t=1792690080.643
mouse right_click manual=0
mouse pick_again manual=1
mouse esc armed=0
mouse zoom2 win 100.0 -> 45.8
mouse zoom2_reset win -> 100.0
mouse axis_drag+ win 100.0 -> 44.9
mouse axis_reset win -> 100.0
mouse pick save_reload=1/1 key=933600041:164:1
```

(Incluye el `QKeyEvent` de `P`/`Esc`, el clic y el clic derecho sobre la traza, los dos clics de
zoom con el reset total y el arrastre en el eje en los dos sentidos.)

### Componentes de la estación actual y atajos (fase 4)

- El canvas **no** muestra una fila por estación: muestra **las componentes de la estación
  seleccionada** en la tabla, en el orden del `.d` (`channelsOrder` × `componentsOrder`, el mismo
  núcleo `csnhypodbp_channel_candidates` que usa la GTK), con las picadas P/S de la estación en
  todas ellas. La disponibilidad es por **banda** (`HH`): si el catálogo lista la vertical se
  asumen las horizontales (`HHZ`, `HHN`, `HHE`), igual que `csnhypodbp_station_has_band`; el
  catálogo sólo aporta net/loc.
- **`tracesPerScreen`** (1..3) manda el alto de banda con `csnhypodbp_traces_layout`: con `1` una
  traza ocupa todo el canvas y las demás se alcanzan desplazando con **↑/↓** (la GTK scrollea; aquí
  la selección de estación está en la tabla, así que las flechas quedan para las componentes).
- **Shift+flechas** (paridad `csnhypodbp_gui.c:2602-2630`):

| tecla | efecto |
|---|---|
| `Shift+↑` / `Shift+↓` | exageración vertical ×1.5 / ÷1.5 (tope 0.1…50; la traza se recorta contra su banda) |
| `Shift+→` / `Shift+←` | ventana temporal ×1.5 / ÷1.5, **centrada** (`bpq_traces_zoom_span`) |
| `Shift+0` | vuelve a la ventana del evento (120 s) y a la exageración 1.0 |
| `↑` / `↓` (solas) | canal anterior / siguiente de la estación |

### Preview, `Confirm`/`Discard` y export (fase 5)

- `ui/bpq_preview_panel` es el panel compacto al tope de *Procesar*: arriba título + `Origin time`
  (+ el `calculando <loc>… N s`); debajo una **tabla de 2 filas** con encabezados
  (`Lat/Lon/Depth[km]/Nph/RMS[s]/ML/Mwp/Mb/Ms/Mww`) y una fila de valores; los radios de magnitud
  van en su celda con el valor como texto. **Un solo `render()`** alimenta estado, valores, color y
  botones: con dos renders parciales el estado y los valores se desincronizan (lección del módulo
  GTK). Los valores los formatea el núcleo puro `csnhypodbp_preview_*` (no se reimplementan).
- Sin relleno de fondo: la candidata y la elegida se distinguen por el **color del título** (rojo
  `#a02020` / ámbar `#8a5a00`). Los radios solo son sensibles para magnitudes con valor, y
  `Confirm`/`Discard` solo con candidata.
- `Confirm` construye el ARC con `csnhypodbp_confirmation_arc` — la **misma** función pura que usa
  el módulo GTK, así que sale byte a byte el de `csnhypodbp_confirm_manual` (verificado: byte 174 =
  `M`) —, lo publica en `ConfRing` con `bpq_emit_confirm`, y además lo mete en el store local como
  snapshot `confirmed=1` para que la solución aparezca en *Origins* sin esperar al anillo (el
  `hypodb_ingest` lo guardará como fila `origin` propia vía `InRingConf`).
  Tras confirmar, el puntero al evento se vuelve a tomar del modelo (`currentEvent()`): el feed
  puede haber reubicado el store y el puntero viejo quedaría colgando.
- `Discard` descarta la candidata y vuelve a la elegida (la tablita no se oculta).
- `Fix` + caja de profundidad en la barra del picking: se traduce en `FixedDepthKm` de la cfg
  temporal del refinador y en el `csnloc` de la corrida (`fix_depth_km` de `bpq_locate_run`; `< 0`
  = libre). La caja se acota al valor canónico + 2 para no inflar el mínimo de la ventana.
- `Export view…` estaba exportando **sin contexto** (la escena salía vacía): ahora usa
  `BpqCanvasWidget::context()` y el lienzo con el foco (o el principal de la pestaña visible).

Evidencia del confirm headless (`BPQ_MODE=--confirm`, locate incluido):

```
locate=1 secs=1.6 qid=933600024 ver=1 lat=-18.6852 lon=-69.8822 depth=5.0 nph=5 rms=1.510 npicks=5
confirm arc_build=0 len=773 qid=933600024 ver=1 nph=5
confirm arc_mark=M                       # marca manual en el byte 174 del ARC
confirm=1 origins=6                      # el snapshot confirmado entra en Origins
```

### `locate` (fase 4)

- La cadena **no se reimplementa**: `c/bpq_locate.c` arma el set de picadas y llama a
  `csnhypodbp_locate_run` (el mismo núcleo puro del módulo GTK), que escribe el ARC, corre
  `csnloc`, refina con el localizador elegido y aplica a la solución el residual por fase leído del
  JSONL (`csnhypodbp_sol_apply_jsonl`).
- Picadas que viajan: una P y una S por estación, ranking **M > A > Teo**
  (`csnhypodbp_locate_picks`), con los checks **A/M/Teo** de la barra (default M, como
  `csnhypodbp_main.c:202-204`) y la puerta **`MaxDistKm`** — que es del envío, no de la tabla.
- **Trampa de índice**: `csnhypodbp_pick_allowed` consulta `pick_on[rows[i].idx][ph]`, o sea por
  posición en el **catálogo**, no por fila: las filas van ordenadas por distancia y las dos cosas no
  coinciden. La selección se arma dimensionada por `max(idx)+1` (el test usa un catálogo
  deliberadamente desordenado para probarlo).
- **Trampa del ARC**: sin COMP3/NET/LOC el refinador rechaza las fases en silencio (`hyphs.for`
  empareja `STA+NET+COMP3+LOC`), así que cada picada se completa con la metadata del catálogo.
- Corre en su **propio `QThread`** (`ui/bpq_locate_worker`), con `jobId` para descartar resultados de
  un evento que el usuario dejó atrás y `Cancel` que pone el flag `volatile int *cancel` de la
  cadena (llamada directa: el worker está bloqueado dentro de la cadena). El botón se deshabilita y
  la barra de estado muestra `calculando <loc>... N s`.
- El residual que devuelve el localizador se proyecta a la tabla **sin volver a pedir ondas**
  (`bpq_stations_build_resid` + `csnhypodbp_resid_source`), y las filas dibujadas se refrescan
  contra la tabla nueva.
- El mensaje de error es el real de la cadena (`timeout`, `ref.err`, ARC inválido) o, si no hay
  picadas, cuántas filas tienen picada, la distancia mínima y cuántas caen dentro de la puerta.
- Desviaciones conocidas del prototipo: (1) los re-picks no se guardan en un `ManualStateFile`
  local — viajan por `PICK_RING` y vuelven a la tabla; (2) no hay casilla por picada en la tabla
  (equivalente a "todas marcadas"); (3) el **tank** de ondas se baja por estación con picada P antes
  de cada corrida (el tank del `locate` y el de `calculate` son independientes), así que una
  `calculate` sin `locate` previo no tiene base.

### Magnitudes: tabla, traza procesada y ventanas (fase 6.1)

- Estado y núcleos: `c/bpq_magpanel.c` envuelve `csnhypodbp_magmarks` (ventanas por
  `(estación, magnitud)` y `handle_hit`/`grab`), `csnhypodbp_magrow_build` (+ textos de celda) y
  `csnhypodbp_magcfg_gate(_limits)`; nada de eso se reimplementa. `ui/bpq_mag_model.cxx` es la tabla
  de 8 columnas (`station/dist/anchor/t0/t1/mag/res/stat`).
- Canvas: `c/bpq_draw_mag.c` pide la **traza procesada** a `csnhypodbp_magview_build` (Wood-Anderson
  para ML, integración/diezmado para Mwp, pasa-banda Mb/Ms con las bandas del `.d`), la dibuja en el
  área con escala automática y encima pinta la **franja** de la ventana (`anchor+t0 .. anchor+t1`),
  sus bordes y la línea del ancla. El arrastre solo se agarra desde el **handle** de arriba
  (`CSNHYPODBP_MAG_HANDLE_H`), igual que la GTK: el resto del canvas queda para el zoom.
- **P teórica**: `TauModel` (default `iasp91`) se carga al arrancar con `bpq_init_tt` →
  `csnhypodbp_tt_init`; sin tablas en el cwd se avisa por stderr y la GUI sigue. Con el modelo
  listo, toda estación sin P `M/A` recibe una P **`T`** (`M > A > Teo`) con el SCNL del catálogo:
  la tabla de *Ondas* muestra `stat=Teo`, el canvas la marca con `pColorTheo` y la de magnitudes
  obtiene **ancla y ventana** (`win_src='T'`) sin inventar nada. `bpq_stations_build_resid` ya
  hacía el cálculo; lo que faltaba era inicializar el modelo.
- **Checks propios de magnitudes**: la barra tiene `A/M/Teo` (`mag_chk_auto/manual/theo`), los tres
  marcados por defecto; son independientes de los del picking (que solo filtran el localizador) y
  gobiernan `eligible/used`, el contador “en uso” y el envío del cálculo. `BPQ_MAG_ORIGINS` los fija
  en los tests.
- `calculate` = `csnmags` sobre la **candidata** con las ventanas ajustadas (`WindowOverridesFile`
  equivalente: `csnhypodbp_mag_ovr_build_anchors` → `CsnhypodbpMagOvrRow[]`, t0/t1 absolutos) en el
  hilo del localizador; al volver refresca el preview y la tabla. `calculate all` = las cuatro
  magnitudes, `calculate` = solo la del radio activo (`kind_mask`). Antes mandaba `base = m_cand`
  tal cual y `hypos_write` solo iteraba sus P del ARC: las manuales nuevas y las teóricas **no
  llegaban** a `csnmags`. `bpq_magpanel_calc_solution` reconstruye `base.picks` desde `m_staRows`
  con el ranking y los checks de magnitudes, y es lo que recibe `runMagJob`.
- La puerta de distancia son dos cajas de texto (`GtkEntry`-like: se aplican al confirmar el campo y
  **solo si las dos valen**; `csnhypodbp_mag_ov_parse` devuelve 0 en éxito) y `reset default` vuelve
  a las del `.d`. Es la misma puerta que ve `csnmags`.
- **Desviaciones**: (1) el canvas dibuja la estación **actual del picking** — es la única con ondas en
  memoria; la tabla sí lista todas (137 en el evento de prueba) y marca `in_use` = las que entrarán a
  `csnmags`; (2) no hay cajas de profundidad (solo distancia): el override de profundidad queda en el
  `.d`. La tabla del GTK es un `GtkColumnView` con un canvas **por fila**; aquí es tabla + un canvas
  (un solo `GtkColumnView`-equivalente sería mucho más caro en Qt sin aportar al prototipo).

### Panel `Mww`: inversión de fase W on-demand (fase 6.3)

- **Fachada**: `c/bpq_mww.c` envuelve `libcsnwphase.a` y ui/ **nunca** ve `csnwphase.h`: solo
  `BpqMww*` (opaco), `BpqMwwParamRow` y textos ya formateados. La config se abre una vez desde
  `MwwConfigFile` (resuelta contra `$EW_PARAMS` con `csnwphase_resolve_path`) y el run dir sale de
  `MwwWorkDir` (`E<qid>_<ver>_<Ymd_HMS>`, uno por solución).
- **Semilla**: solución (la candidata si hay `locate`, si no la elegida) + magnitud (la del radio si
  tiene valor; si no Mwp > Ms > Mb > ML) → `CsnwphaseInput` + `mww_over_init`. Se re-siembra **solo**
  cuando cambia la identidad `(qid, mod, ver, otime, mag)`: así las celdas editadas sobreviven a los
  repintados. Sin magnitud la semilla falla con su motivo (no se inventa una).
- **Tabla**: 31 filas / 11 editables (`csnwphase_params_build` **con los overrides aplicados**: la
  tabla muestra lo que se va a correr, no la config cruda). Cada celda editable se valida con
  `mww_field_parse` (rango de `mww_field_limits`, coma decimal, sin restos tipo `12abc`); si el texto
  no vale se **descarta el campo** (`mww_over_unset`) — nunca queda aplicado un prefijo a medias — y
  la celda repone el valor vigente. t0 va por `mww_over_set_time` (fecha UTC completa).
- **Corrida** en hilo propio (`BpqMwwWorker`): `csnwphase_input_prep` (ondas del store/SDS + `SAC_PZs`
  + `i_master`/`CMTSOLUTION`) → `csnwphase_run` (`run.sh` + `run.log`) → `csnwphase_parse`. El
  resultado es la única fuente de verdad: si el wrapper sale != 0 pero hay solución, se muestra
  igual. **Todo fallo trae su texto real**: primero el del parser, si no
  `csnhypodbp_mww_first_error(run.log/full_output.txt)`.
- **UI**: playera (radiación P por píxel, misma proyección y fórmula que la GTK; se dibuja en cairo
  sobre `BPQ_SCENE_MWW_BALL`) + resumen a la izquierda y **salida completa** del motor a la derecha,
  los dos cuadros de `MWW_ROW_H` = 248 px, en una pila `traza | Mww` dentro de la sub-pestaña
  *magnitudes*. `Cancel` **descarta** el resultado en vuelo (el motor no expone cancelación) y la
  tabla se deshabilita mientras corre: su hilo escribe sobre el `BpqMww`.
- **Evidencia** (`BPQ_MODE=--mww`; el dry-run y la corrida real):
  ```
  mww seed=1 rows=31 editable=11 rundir=/tmp/bpq_m6/mww/E933600095_1_20261023_000941
  mww run=ok dry=0 err="-"
  mww summary Mw 4.62   M0 1.067e+23 N.m
  mww summary centroide: -23.800, -67.625  120.5 km   (shift 27.0 s, half-dur 27.0 s)
  mww summary planos nodales: (354/44/77) y (192/48/102)
  mww summary estaciones usadas: 1   RMS global: 0
  mww log_bytes=5815 tensor_ready=1
  ```
  El run dir queda con lo que hizo el motor (`i_master`, `CMTSOLUTION`, `WCMTSOLUTION*`,
  `xy_*`/`xyz_*`/`ts_*`, `run.sh`, `run.log`). El dry-run (sin motor) falla en el parseo pero reporta
  el motivo real (`sin xy_WCMTSOLUTION/WCMTSOLUTION ni LOG/*.log en …`) y deja `run.log` en el cuadro
  de la derecha (`full_output.txt` si existe; si no, `run.log`), nunca un "sin resultado" mudo.
- **6.4 hecho (guardado al confirmar)**: `bpq_mww_save(db, sol, mod, …)` replica
  `csnhypodbp_mww_persist` de la GTK: compone `event`/`origin` (`upsert_event` +
  `upsert_origin_if_absent`, así no pisa la fila del pipeline), sube `magnitude(Mww)` +
  `tensor(method=WPHASE, mechanism=WCMT, M0 + momentos)` y deja la solución con su Mww como
  **preferida** (`set_preferred`). Dos decisiones que importan:
  1. **`mod` explícito** (`bpq_mod_for_locator`: csnloc 162 / hyp2000 163 / nlloc 164): la solución
     de `locate` trae `mod = 0` (no viene del anillo) y `ewgui_origin_id` rechaza `mod <= 0`; el
     módulo real es el del localizador que la calculó.
  2. **Se reusa el `event` del pipeline** si existe uno con el mismo `qid` y `|Δotime| <= 300 s`
     (`EventMaxDtSec`): hypodb nombra el evento con la hora de la **primera** solución del grupo, que
     difiere en segundos de la refinada; sin esto el sismo se partía en dos eventos (se vio: `…011036`
     vs `…011035`). Verificado en la BD:
     ```
     tensor    E933600102-20261023011031:162/tensor/WPHASE | WPHASE | WCMT | m0 1.8277e25
     magnitude E933600102-20261023011031:162/mag/Mww        | 6.11 | 3 estaciones
     event     preferred_origin = …:162   preferred_magnitude = …:162/mag/Mww
     ```
     (`origin` = el del **pipeline** para ese qid/módulo, no uno nuevo.)

### Picking: clic → picada manual (fase 4)

- `bpq_traces_hit` (c/bpq_traces.c, puro y con test) traduce un clic a **fila + instante** con la
  misma geometría que el dibujo. Un clic sobre una traza publica `TYPE_PICK_SCNL` por
  `bpq_emit_pick` en `PICK_RING` con la fase activa.
- Las teclas **`p`/`s`** con el foco en el canvas eligen la fase a picar (se anuncia en la barra de
  estado). Los re-picks vuelven a la tabla por el anillo (el `locate` los manda tal cual).
- Ojo: la marca de la picada manual que se acaba de emitir no se dibuja hasta que llega por el
  anillo, porque el canvas pinta lo que hay en `CsnhypodbpStaRow` (igual que la GTK, que también
  espera el refetch).

### Canvas de trazas (2026-10-22)

- `c/bpq_traces.c` dibuja `BPQ_SCENE_TRACES` y `BPQ_SCENE_TRACE_AXIS`: una banda por estación
  con la traza como **envolvente min/max por columna** (30 k muestras en 400 px sin coste), la
  etiqueta `ESTACIÓN` + `dist km` a la izquierda y las marcas de P y S en su instante,
  coloreadas con `csnhypodbp_color_resolve` (manual / automática / teórica). El eje de tiempos va
  en su propio canvas y comparte `t0`/`window`, con paso "bonito" y `HH:MM:SS` UTC.
- `bpq_trace_new`/`bpq_trace_data` exponen el buffer crudo sin copiarlo; así el canvas no
  duplica las 30 k muestras por traza.
- La ventana se arma en `BpqMainWindow::fetchTraces`: `otime − 5 s` durante 120 s, y se piden
  sólo las estaciones **con picada** (las que no tienen ni pick ni onda no se muestran, igual que
  en la GUI GTK). `traces rows=4 with=4` en la corrida real.
- **`BPQ_TAB=picking|procesar|magnitudes`** deja esa pestaña activa, para sacar `--shot` de
  cualquiera sin clics.
- El fetch corre en un **`QThread` propio** (`ui/bpq_wave_worker`): el hilo principal le manda
  los SCNL ya resueltos (no toca el catálogo desde el worker) y recibe una traza por estación.
  Cada pedido lleva un `jobId`; los resultados de un evento que el usuario ya dejó atrás se
  **descartan y liberan** en el hilo principal. `BpqTrace` es opaco, así que el `signal` usa
  `Q_DECLARE_OPAQUE_POINTER(BpqTrace *)` (sin eso Qt pide `sizeof(T)` y no compila).
  Los modos headless llaman a `waitForWaves(3000)` antes de volcar o capturar, para que el
  `--dump` muestre lo mismo que vería el usuario.
- El fetch queda acotado a 40 estaciones y a 120 s de ventana (`otime − 5 s`).

### Tabla de estaciones (2026-10-22)

- `c/bpq_stations.c` relee **`StaFile`** en un catálogo propio
  (`<sta> <net> <chan> <loc> <lat> <lon> [elev] [gain]`): `LoadStationsFromFile()` vive en
  `csnhypodbp_ws.c`, que no es puro y llena globales (`StaArray`/`NumEstaciones`).
- La tabla la decide `csnhypodbp_statable_build` (puro): esta capa sólo arma los
  `CsnhypodbpStaInput` con la **distancia haversine en grados** (igual que
  `csnhypodbp_ws.c:1321-1326`; `dist_km = deg × 111.19` la calcula el núcleo) y ordena por
  distancia. Las picadas salen de `CsnhypodbpSolution.picks` (la solución las lleva consigo), así
  que el origen `M`/`A` y el residual son los de **esa** solución.
- Ojo con el residual: `resid_for` devuelve el **primer `residual` no-NaN** de esa estación y fase
  (`csnhypodbp_statable.c:84`), no el de la picada que gana la precedencia.
- `ui/bpq_stations_model.{h,cxx}`: una fila por (estación, fase) — 2 por estación, ambas
  autónomas — con `station`/`dist[km]`/`pick`/`stat`/`res`; `M` en azul, `A` en gris y las fases
  ausentes en gris claro. Se refresca al cambiar de evento **y** al elegir otra solución en
  *Origins*.

### Zoom/paneo y capa de ondas (2026-10-22)

- **Zoom, paneo y clic en la planta y en el perfil**. El estado del encuadre vive en los
  contextos (`bpq_map_pan`, `bpq_map_zoom_at`, `bpq_map_hit_test`, `bpq_profile_pan`,
  `bpq_profile_zoom_at`, `bpq_profile_project`), no en el widget: así se testea headless y el
  widget sólo traduce el evento a píxeles de dispositivo. El zoom se ancla al cursor (el punto
  bajo el ratón no se mueve; verificado a 0,5 px) y se acota a `1e-4..1e4`. En el perfil el
  paneo se convierte a unidades del marco (grados / profundidad), con el signo de `x` invertido
  por cómo proyecta `csnhypodbp_frame_project`. El clic sin arrastre sobre un marcador selecciona
  ese origen en *Origins* (queda verde). Una vista ajustada por el usuario **se respeta** en los
  `resize` hasta que se elige otro evento.
- **Capa de ondas** (`c/bpq_wave.c`): mismo recorrido que `csnhypodbp_ws.c` (que no es puro
  porque incluye `csnhypodbp.h` con GTK) pero sin globales:
  `csnhypodbp_sds_prefer` decide el orden, `wstore_get` da el `WStoreTrace` en int32 y
  `csnhypodbp_sds_fetch` da un stream TRACE2 que se coloca en la grilla con
  `bpq_trace2_decode`. El contenedor es `EwGuiTrace` (puro, con `finish`/filtros para la fase 4)
  y queda **opaco** para `ui/`: sólo se exponen escalares.
  `g_sds_enabled`/`g_sds_root` pasaron a `c/bpq_sds_globals.c` para que los tests de ondas no
  arrastren todo el port de configuración.
- **Prueba real** (store en vivo, ventana de 300 s):

```
$ NOW=$(date +%s); T0=$((NOW-900))
$ QT_QPA_PLATFORM=offscreen BPQ_MODE=--dump \
      BPQ_WAVE_PROBE="AC01,C1,HHE,--,$T0,300" csnhypodbpQT csnhypodbp.d | grep ^wave
wave AC01.HHE.C1.-- n=30579 rate=100.00 t0=1792685409.730 rango=[-4960,6835]
```

### Render de trazas: zoom, escala y dpr (2026-10-23)

- **Escala vertical por ventana visible**. `draw_trace` toma `max |muestra|` solo de
  `[t0, t0+window]` y usa `0.45 · alto_fila · y_gain / max_abs`, igual que el GTK
  (`csnhypodbp_gui.c:4160-4193`). Antes se normalizaba contra el máximo de **toda** la traza: al
  hacer zoom la onda quedaba aplastada y cuantizada en pocos píxeles.
- **Dos regímenes de dibujo**, con el criterio del módulo GTK (`csnhypodbp_view_use_polyline`,
  `CSNHYPODBP_POLYLINE_SPP = 2.0`):
  - spp < 2 (zoom alto): **polilínea muestra a muestra** con `csnhypodbp_view_sample_x`, cortando
    la pluma en los gaps. Sin esta rama la envolvente dibujaba un segmento vertical de 1 px por
    columna y el zoom se veía como **escalones/barras** (ver `tmp/1.png`).
  - spp ≥ 2: **banda rellena** por columnas con datos (`min_half` = 0.8 px lógicos), sin unir a
    través de un gap.
- **dpr**: el canvas entrega `w/h` en píxeles de **dispositivo**. El grosor (`0.8·dpr`), el
  `min_half` y el resto de trazos van multiplicados por `devicePixelRatioF()`; antes la traza era
  una hairline de 0.5 px lógicos en pantallas 2×.
- **Filtro de fase cero intacto**: la vista usa `m_phaseZero` del `FilterPhase` y el núcleo común
  (`ewgui_filter_*_zp`, forward+backward); `locate` sigue mandando el buffer **crudo**.
- **STA/LTA sobre la traza dibujada**: `bpq_stalta_new` lee el buffer **filtrado** (como
  `csnhypodbp_gui.c:3913-3960`), con fallback al crudo solo si la vista no filtró. Antes la CF se
  calculaba sobre el crudo y un drift de baja frecuencia movía el disparo respecto al GTK.
- Invariantes: `tests/test_traces.c` (escala por ventana, polilínea, gaps), `tests/test_wave.c`
  (impulso que no se desplaza a fase cero y CF filtrada) y `tests/test_traces_invariants.sh`
  (grosor ×dpr, ramas del render, sin barrido global, STA/LTA filtrado).

### Scroll, teclado y magnitudes multicanal (2026-10-23)

- **Una fila por canal**. La estación actual aporta `channelsOrder × componentsOrder` (p. ej.
  `HHZ,HHN,HHE`), filtradas por las bandas presentes en `StaFile`; `tracesPerScreen` decide cuántas
  caben a la vez. Antes se veía una sola porque no había barra y la rueda hacía zoom.
- **Scroll visible**: `QScrollBar` vertical en el panel de picking, rango `0..n-nvis` y
  `pageStep=nvis`, sincronizada con `bpq_traces_scroll_to`. Rueda sin modificador = mover filas;
  `Ctrl+rueda` = zoom temporal (paridad GTK: la rueda no cambia el span). Flechas, `PageUp/Down` y
  `Home/End` también mueven la banda visible; `Shift+flechas` siguen siendo ganancia/zoom.
- **Teclado en la ventana, no en el canvas**: un filtro de aplicación resuelve P/S, Esc, flechas y
  filtros aunque el foco esté en la tabla de estaciones. La tabla no hace búsqueda incremental
  (`setFocusPolicy(NoFocus)`) y las cajas de texto conservan sus teclas. `applyFilterKey` devuelve
  si consumió la tecla, así una letra sin binding no se traga.
- **Magnitudes por canal**: `BpqMagCtx` lleva `trs/ntr` y `bpq_mag_draw` dibuja una banda por canal,
  con su propio `csnhypodbp_magview_build`, en el mismo orden que el picking. Si no hay P efectiva
  muestra `sin P (ni observada ni teorica): no hay ancla`; si un canal no tiene traza, solo su banda
  dice `sin traza`. Antes el canvas usaba únicamente la primera traza terminada en `Z`.
- Invariantes: `tests/test_traces.c` (scroll acotado), `tests/test_magpanel.c` (3 bandas, sin ancla,
  canal nulo), `tests/test_traces_invariants.sh` y `tests/test_ui_input.sh` (rueda/teclado/`ntr` con
  el binario real offscreen).

### Marco, ejes e idioma (2026-10-22)

- **El panel de planta dibuja marco, grilla y etiquetas de coordenadas** (`draw_graticule` en
  `c/bpq_map.c`). Antes solo pintaba el fondo: con `MapImageFile` comentado en el `.d` y sin
  soluciones el panel quedaba **liso** (sin borde ni ejes). La grilla se dibuja en píxeles de
  pantalla, así que trazos y textos no se escalan con el zoom, y el paso se elige con
  `nice_step` (~6 divisiones sobre el tramo visible). El marco va a medio píxel para que quede
  nítido.
- **Sin soluciones el encuadre es el mundo completo** (`bpq_map_fit_world`) en vez de `zoom=1`
  con el centro en la esquina de la imagen; con soluciones sigue el auto-encuadre por bbox.
- **El perfil sin datos usa un marco por defecto** (sur de Chile) en lugar de una caja
  degenerada de ancho cero.
- **`dpr` en los contextos**: los lienzos reciben `w/h` en píxeles de dispositivo, así que
  trazos, radios de marcador y tamaños de fuente se escalan por `devicePixelRatioF()` (en 1x
  no cambia nada).
- **`LC_NUMERIC=C` en `main.cxx`**: el parser de `.lines`/perfiles/ARC usa `strtod`, y Qt llama
  `setlocale(LC_ALL, "")` al inicializar su plugin de pantalla. Con un locale de coma decimal
  (`es_CL.UTF-8`) `strtod("-70.43290")` corta en `-70` y el resto se rechaza con cientos de
  `overlay: coordenada invalida` (por eso no se veía en `QT_QPA_PLATFORM=offscreen`, donde nadie
  toca el locale). Se fija al arrancar y otra vez tras crear `QApplication`; `LC_CTYPE` se deja
  como venga.

Y el camino completo datos → tabla, contra el EarthWorm en marcha:

```
$ QT_QPA_PLATFORM=offscreen BPQ_MODE=--dump csnhypodbpQT csnhypodbp.d | grep -E '^data|^row'
data events=6
row 1: 2026-10-22 12:57:26|21.08S|68.93W|113|0.2|214|13|0933600000|2.9-8|-|-|-|nlloc|2|A
data origins=1
```

`BPQ_TEST_ARC=<arc>` inyecta un ARC de un archivo (fixture generada con
`make -C ew_gui_tools/csnhypodbpQT mk_test_arc`), útil para demos sin EarthWorm.


## Dependencias

```bash
sudo apt install qt6-base-dev qt6-base-dev-tools libxcb-cursor0
```

`cairo`, `libpq`, `libmseed2` (vendorizada en `ew_8.0`), GCC/G++ y `pkg-config` ya están.
**No** se enlaza GTK4, libadwaita, gobject ni gio: el binario no las lleva.
Ojo con una aparente sorpresa: `libglib-2.0` **sí** aparece en `ldd`, pero llega por
`libQt6Gui` (`QEventDispatcherGlib`), no por nuestro código ni por GTK.

## Compilar y probar

```bash
source ./ew8_unix.sh

# Capa C: NO necesita Qt (config + lienzos)
make -C ew_gui_tools/csnhypodbpQT check-c

# Todo (necesita Qt6): instala ew_8.0/bin/csnhypodbpQT
make -C ew_gui_tools/csnhypodbpQT all
```

El módulo entra solo en `ew8_compile.sh` (el inventario se deriva de `ew_gui_tools/*/Makefile`):

```bash
./ew8_compile.sh csnhypodbpQT --check --validate
./ew8_compile.sh --list
```

## Ejecutar

Los módulos abren rutas **relativas a `$EW_PARAMS`**, así que se arranca desde `params/`:

```bash
source ./ew8_unix.sh
cd run_working_v8/params

csnhypodbpQT --print-config csnhypodbp.d      # headless, sin DISPLAY
csnhypodbpQT --check-profiles csnhypodbp.d    # headless
csnhypodbpQT csnhypodbp.d                     # GUI
```

El prototipo puede leer el `.d` del módulo GTK (`csnhypodbp.d`) o el **suyo**
(`csnhypodbpqt.d`), que es el que usa `startstop` y declara su identidad propia:

```bash
csnhypodbpQT --print-config csnhypodbpqt.d      # headless, sin DISPLAY
csnhypodbpQT --check-profiles csnhypodbpqt.d    # headless
csnhypodbpQT csnhypodbpqt.d                     # GUI (módulo integrado, id 172)
```

El `.d` propio es copia del de la GTK con **solo** tres claves cambiadas
(`MyModuleId MOD_CSNHYPODBPQT`, `ManualStateFile ../csnhypodbpqt/…`,
`MwwWorkDir ../csnhypodbpqt/mww`); el resto del esquema es idéntico (la paridad se
comprueba con `tests/test_printconfig_parity.sh` sobre `csnhypodbp.d`).

### Integración en el stack (fase 6.5)

- `run_working_v8/params/earthworm.d.base`: `Module MOD_CSNHYPODBPQT 172`
  (el `.d` vivo lo hereda al correr `worktree_bootstrap.sh --refresh`).
- `run_working_v8/params/startstop_unix.d`: `Process "csnhypodbpQT csnhypodbpqt.d"`
  **activo**, en paralelo al GTK (158): `startstop`, `ew_monitor.sh` y
  `ew_controller` lo gestionan por pid como a cualquier módulo.
- Runtime propio: `run_working_v8/csnhypodbpqt/` (`csnhypodbp_manual.events` +
  `.picks` + `mww/`), ignorado por git. `StateFile` no se toca: `csnloc.events`
  es solo lectura y lo escribe `csnloc` en `$EW_LOG`.
- No escribe `EW_LOG` (no usa `logit`): su salida va a la consola de `startstop`.
- `tests/test_integration.sh` (16 checks, headless) valida identidad, `.d`,
  `Process`, `.gitignore` y binario; `tests/test_maximize.sh` (`make test-live`)
  comprueba 5 ciclos maximizar/des-maximizar con `DISPLAY` + EarthWorm.

### Estado de runtime aislado (tests)

`--state-dir` sigue disponible para aislar una corrida de prueba:

```bash
csnhypodbpQT csnhypodbp.d --state-dir ../csnhypodbpqt
# equivalente: BPQ_STATE_DIR=../csnhypodbpqt csnhypodbpQT csnhypodbp.d
```

Sobrescribe `StateFile`, `ManualStateFile` y `MwwWorkDir` con `<dir>/csnloc.events`,
`<dir>/csnhypodbp_manual.events` y `<dir>/mww`.

### Ganchos de test (sin clics)

```bash
QT_QPA_PLATFORM=offscreen BPQ_MODE=--dump   csnhypodbpQT csnhypodbp.d   # árbol de widgets
QT_QPA_PLATFORM=offscreen BPQ_MODE=--layout csnhypodbpQT csnhypodbp.d   # divisores + mínimo
QT_QPA_PLATFORM=offscreen BPQ_MODE=--shot BPQ_SHOT=/tmp/x.png csnhypodbpQT csnhypodbp.d
BPQ_EXIT_AFTER_MS=3000 csnhypodbpQT csnhypodbp.d                        # humo
BPQ_MODE=--locate csnhypodbpQT csnhypodbp.d                             # locate REAL
BPQ_LOCATE_MASK=AMT BPQ_LOCATE_LOC=hyp2000_ring BPQ_MODE=--locate ...   # mascaras y refinador
BPQ_MODE=--confirm csnhypodbpQT csnhypodbp.d                            # locate + Confirm (ARC)
BPQ_MODE=--mouse csnhypodbpQT csnhypodbp.d                              # eventos reales de raton
BPQ_SHOT_LOCATE=1 BPQ_MODE=--shot BPQ_SHOT=/tmp/x.png csnhypodbpQT ...  # captura con candidata
BPQ_SHOT_MOUSE=1  BPQ_MODE=--shot BPQ_SHOT=/tmp/x.png csnhypodbpQT ...  # captura tras zoom+pick
```

`--locate` corre el mismo camino que el botón (worker incluido) sobre la solución elegida y emite
`locate=1 secs=… lat=… lon=… depth=… nph=… rms=… npicks=…`, `stations rows=… res=…` y
`picks P=… S=… max|res|=…`. Antes del locate hay que dejar `EW_PARAMS/EW_HOME/EW_VERSION` cargados
(es lo que usa la cadena para resolver binarios y `.d`).

El `--dump` agrega la línea de la barra
(`locate mask=010 locator=csnloc busy=0 cand=0 res=N`) y la del preview con los valores ya
formateados (`preview state=2 cand=1 pref=0 title="New solution · csnloc" lat=… rms=… mags=…`),
así que el estado del cuadro se verifica sin mirar la pantalla.

## Invariantes (los verifica el build/tests)

1. **Ningún `.cxx`/`.h` de `ui/` incluye** headers de C/EW/cairo/glib/GTK: la única frontera es
   `c/bpq_capi.h`, que va entero dentro de `extern "C"`.
2. **Ningún `.o` se escribe fuera de `build/`**: los `.c` ajenos se compilan con `-o build/…`.
3. El Makefile cumple `common/tests/test_build_deps.sh` (`-MMD -MP -MF`, `.dep`,
   `-include $(wildcard …)`, `clean-deps`) y usa `timeout` solo por ruta absoluta.
4. El binario **no enlaza GTK/Adwaita/glib/gio** (por eso no se enlaza
   `../common/libewgui_gtk4.a`, y `ewgui_export.c` se sustituye por `c/bpq_canvas.c`).
5. `MyModuleId` propio (`MOD_CSNHYPODBPQT` = 172, `earthworm.d.base`) ⇒ **puede correr junto a la
   GUI GTK (158)**; su `.d` aísla el estado manual/Mww en `run_working_v8/csnhypodbpqt/`.
6. **El fondo del mapa se decodifica de verdad**: `--dump` publica `bg=<ancho>x<alto>`;
   `tests/test_map_background.sh` exige `bg≠0x0` con `MapImageFile` activo, `bg=0x0` sin la clave
   y el motivo real en `stderr` si la ruta no existe. El límite de `QImageReader` se fija una vez
   en `ui/main.cxx` (ver arriba).
7. **El render de trazas trabaja en píxeles de dispositivo**: todo grosor va multiplicado por
   `dpr`, la escala vertical sale de la ventana visible y el zoom alto usa polilínea
   (`tests/test_traces_invariants.sh`). El STA/LTA lee el buffer filtrado.

## Lo que se reutiliza (sin copiar)

- `../csnhypodbp/`: `group, search, statable, preview, profile, mapview, view, trace, traces,
  station, filtercfg, locate, locator, fixdepth, magmarks, magcfg, magrow, magview, magsta,
  magchan, manual, tt, sds, mwwerr, mww_over`.
- `../common/src/`: `ewgui_wave, ewgui_dsp, ewgui_geo, ewgui_sds, ewgui_sta, ewgui_config,
  ewgui_cfgfile, ewgui_db, ewgui_db_ids, ewgui_ring, ewgui_stalta, ewgui_table_cmp,
  ewgui_overlay, ewgui_version`.
- Librerías: `../csnloc/libcsnloc_tt.a`, `../wavestore/libwavestore.a`,
  `../csnwphase/libcsnwphase.a`, `ew_8.0/lib/{libmseed2.a,rw_mag.o,libew_mt.a}`.

**Excluidos a propósito**: `csnhypodbp_{row,staitem,magitem,master}.c` (GObject/GListStore, los
sustituyen `QAbstractTableModel`), `csnhypodbp_{gui,main,ws,mww}.c` (GTK; su lógica de datos se
reescribe en `c/`), y `common/src/ewgui_export.c` (incluye `view.h` → `<gtk/gtk.h>`).

## Diferencias conocidas con la versión GTK

- Los avisos de configuración van a `stderr` en vez de `logit()`: `bpq_config_load()` corre antes
  de `logit_init()`.
- No hay `--print-config` con textos de ayuda idénticos byte a byte; el test de paridad compara
  **clave y valor** normalizados.
- Los lienzos son, por ahora, un marcador de posición (marco + grilla + cruz): el camino
  cairo → `QImage` está montado y testeado; las escenas reales llegan en las fases 2/4/6.
