# csnhypodbp

Visor de hipocentros y re-picker (GTK4 + libadwaita) del pipeline EW8.

## Entradas

- `InRing` (por defecto `HYPO_RING_REF`): soluciones **refinadas** de `hyp2000_ring` (163) y
  `nlloc_ring` (164).
- `InRingRaw` (opcional, por defecto `HYPO_RING`): soluciones **crudas** de `csnloc` (162) en vivo.
- `OutRing` / `InRingPick` (por defecto `PICK_RING`): anillo de picadas. Se **escribe** el repick
  manual y se **lee** `TYPE_PICK_SCNL` para capturar fase (token 11) y origen (token 12).
- `StateFile` (`csnloc.events`): estado persistido, solo lectura al arrancar.

## Agrupamiento de soluciones

Cada solución (mensaje `TYPE_HYP2000ARC`) pertenece a un **evento** identificado por su `qid` de
`csnloc`. Dos soluciones se agrupan si comparten `qid` **o** si su **firma de picadas** es
compatible: al menos `GroupMinMatchPicks` picadas **en común** dentro de `GroupPickTolSec` (por
defecto 0.1 s), comparando `(estación, fase P/S, tiempo de pick)` sin canal. No se exige que una
sea subconjunto de la otra: la ventana deslizante de `csnloc` añade y quita picadas, así que dos
emisiones del **mismo** sismo (una con `CO06`, otra con `LMEL`) pueden no ser subconjunto una de
otra y, con el criterio antiguo, quedaban en grupos separados (el mismo evento aparecía repetido
en la maestra). El grupo guarda la unión de picadas de sus miembros, de modo que la evolución
incremental queda en un mismo evento.

### Solución preferida

- Si el evento tiene alguna solución **manual**, la preferida es la manual más reciente.
- Si no, la automática con **más fases** y, a igualdad, **menor RMS**.

## Magnitudes

`csnloc` **no** calcula magnitudes (no tiene formas de onda). Las calcula `csnmags`, que lee
`HYPO_RING_REF`. Para que un evento **solo-csnloc** (aún sin refinar) también tenga magnitud, se
corre una **segunda instancia** `csnmags csnmags_raw.d` que lee `HYPO_RING` y publica ahí
(`MOD_MAGNITUDES_RAW`). `csnhypodbp` las recibe por ambos anillos (`InRing` + `InRingRaw`).

En el `locate`, `csnhypodbp` pide a `csnmags` **las cuatro magnitudes** (ML/Mwp/Mb/Ms; Mww es
`csnwphase`, no se recalcula). La entrada son las **mismas P efectivas** de la tabla de picking
(`M>A>Teo`, con las casillas por fila y los filtros A/M/Teo); las S solo van al localizador. Cada
ventana se ancla en el `t_pick` de esa P y usa la franja ajustada del panel de magnitudes si existe;
si no, `Ml/Mwp/Mb/Ms_Window` del `.d`. Se emiten overrides para las 4 magnitudes de cada estación,
así la ventana sigue a una picada manual/teórica y no al tiempo del ARC refinado.

Las magnitudes son **por solución** (`qid` + módulo origen), no por evento: `csnloc` crudo (162),
`hyp2000` (163) y `nlloc` (164) publican ARCs con el **mismo `qid`**, así que el `qid` solo no
distingue una solución de otra. `csnmags` indexa sus eventos por `(qid, mod)` (`mag_key.c`) y
estampa el módulo origen en `MAG_INFO.origin_version`; `csnhypodbp` guarda cada magnitud por
`(qid, mod)` (`csnhypodbp_mag_store_*`) y la aplica **solo** a la fila de esa solución
(`csnhypodbp_mag_match`). Cada solución muestra el valor y el nº de estaciones calculados desde sus
propios parámetros hipocentrales, y la maestra muestra la magnitud de la preferida (o `-` si aún no
llegó). Si una magnitud llega antes de que exista la solución, queda en un buffer y se aplica al
crearla. Una magnitud con `origin_version==0` (csnmags viejo) se aplica a cualquier solución del
`qid` (compatibilidad).

## Marca de solución manual

El origen de cada picada viaja en el 12.º token de `TYPE_PICK_SCNL` (`A`/`M`).
`csnloc` lo persiste en su `StateFile` y marca la solución en el byte **174** de la línea 1 del
ARC (`'M'` manual, `'A'` automática); `hyp2000_ring` y `nlloc_ring` copian ese byte al ARC
refinado. El contrato compartido vive en `ewgui/arc_layout.h`.

## UI

Pestaña **Eventos**: `GtkPaned` con
- **maestro**: una fila por evento (la solución preferida; columnas `…`, `N` nº de soluciones, `Est.`);
- **detalle**: al seleccionar un evento, la lista de sus soluciones con la preferida primero.

La fila maestra se identifica por el **grupo** (`csnhypodbp_master.{h,c}`), no por el `qid` de la
solución: un grupo fusionado puede contener soluciones de varios `qid` (csnloc re-detecta el mismo
sismo) y la preferida puede cambiar de uno a otro. Identificar por `qid` hacía que, al cambiar la
preferida a una fila de otro `qid`, el upsert no la encontrara y la **re-agregara en cada mensaje**
(filas repetidas con el mismo ID).

El reparto inicial es ~mitad y mitad, y el **detalle llega hasta el fondo** de la ventana (el
separador es arrastrable). Sin eventos no hay filas: se ve solo el fondo de la lista.

Seleccionar una solución del detalle fija el evento para descargar ondas (doble clic abre la
pestaña *Ondas*). La lista de soluciones históricas se muestra subordinada: encerrada en un
`GtkFrame`, con fuente más pequeña y fondo ámbar claro en todas sus filas (la seleccionada, más
intensa); el fondo entre filas queda sin teñir.

## Búsqueda hipocentral (pestaña Eventos)

El grupo **`Search`** del menú ☰ (visible solo en *Eventos*) abre **`Search events…`**: un formulario
con rangos **min/max** de **tiempo de origen, latitud, longitud, profundidad y magnitud**. Solo el
rango de **tiempo de origen** es obligatorio; un rango a un solo lado se completa con la cota
natural del parámetro. Fechas en UTC con formato `YYYY-MM-DD` o `YYYY-MM-DD HH:MM:SS`
(`csnhypodbp_search_build`, núcleo puro). El resultado se consulta con `ewguidb_search_events`
(filtra por el **origen/magnitud preferidos** del evento, orden por `origin_time` DESC) y
**reemplaza** el contenido de la vista: la maestra (título **`Events`**) muestra una fila por evento
y el detalle (título **`Origins`**) sus soluciones. El tope de filas es `SearchMaxEvents`
(def. 1000). Con 0 resultados no se toca la vista actual.

`Clear search` (ítem del menú ☰ y **botón** junto al título `Events`, habilitado solo en modo
búsqueda) vuelve a la vista **en vivo + historial**. Mientras hay una búsqueda activa, los eventos
nuevos de los anillos se siguen agrupando en memoria pero **no** se añaden a la tabla.

## Pestaña Ondas (navegación por estación)

El panel izquierdo es una **tabla de estaciones** (`GtkColumnView` con **separadores de fila y de
columna**, el mismo estilo que la tabla de *Eventos*) dentro de un `GtkFrame` con scroll propio, en
fuente `0.75em` y con borde propio (clases `.sta-table` y `.detail-frame`). **Sin barra horizontal**:
el marco se dimensiona a lo que pide la tabla (`station` se estira y las demás van a ancho fijo), así
que las 5 columnas entran completas.
**Una fila por (estación, fase)** ⇒ **2 filas por estación**, y las dos son **autónomas**: ambas
muestran `station` (`[☑]` + nombre), `dist[km]`, `pick` (`P`|`S`), `stat` (`M`|`A`|`Teo`) y
`res` (residual `%+.3f`; vacío si no aplica). La casilla va **junto al nombre** y es la del **pick de
esa fila**: como cada fila es una fase suelta, desmarcarla desactiva **esa fase**, no la estación; si
esa fase **no existe**, queda **vacía y no sensible** (marcada pero inerte
confundía). Las casillas **solo filtran el envío al localizador** (no el dibujo). **Clic derecho**
sobre cualquier celda quita las picadas manuales de esa estación. Cualquiera de las 2 filas
selecciona la estación.
La fila seleccionada se resalta y **por defecto no se dibuja ninguna traza**: al hacer clic en una
estación se descargan (bajo demanda) y se dibujan **solo sus canales**, en el orden de
`channelsOrder` × `componentsOrder` (`HHZ, HHN, HHE, BHZ, …`), omitiendo los no disponibles. Solo se
piden canales de las **bandas que la estación ya tiene** (p. ej. un `.sta` con `HHZ` → `HHZ/HHN/HHE`;
no se intenta `BH*`/`HN*`/`LH*`). Al seleccionar la estación se piden **todos** sus canales de esas
bandas (no solo el del `.sta`): cualquier fila sin `fetched` fuerza la descarga. Cada canal ocupa una
banda ≈ la pantalla (una componente a la vez); el resto se ve con scroll. Cada traza se rotula con su
**componente** (`HHZ`, `HHN`, …); la estación va en el título del canvas.

Por estación se dibuja **una sola P y una sola S** (la de mayor rango `M > A > Teo`; empate → la más
cercana a la teórica). Si hay una picada manual, **desplaza** a la automática y a la teórica (no se
dibujan ambas). La traza se **alinea con la P** de la estación (manual/automática, o la **P teórica**
IASP91 si no hay); la ventana es **común a la estación** (`winStart` compartido) para que todas las
trazas y el eje UTC coincidan. La traza es **siempre negra**; el origen de la picada se distingue
por el **color de su marca**: manual (`pColor`/`sColor`), automática (`pColorAuto`/`sColorAuto`) y
teórica (`pColorTheo`/`sColorTheo`). Si las claves `*Auto`/`*Theo` no se declaran, su color se
**deriva del manual** aclarándolo al 40 % / 70 % en cada dibujo (cambiar el manual las recolorea);
elegirlas en el menú las vuelve explícitas. Regla del origen idéntica a `csntvp`: token 12 `M` ⇒
manual; ausente ⇒ `A`; sin picada ⇒ teórica. Ya **no** hay LED verde/rojo por traza (era ambiguo).

### Interacción: zoom, eje de tiempo y picado

- **Exageración vertical**: `Shift+↑` / `Shift+↓` multiplican la escala vertical (`g_zoom_factor`,
  ×1.5 / ÷1.5, acotada a `[0.1, 50]`); `Shift+0` la resetea **y también el zoom temporal**. Las
  `↑`/`↓` **solas** siguen navegando la tabla de estaciones (la tabla conserva el foco); el
  controlador de teclas de la ventana está en fase **capture** y solo consume las variantes con
  `Shift` (y `p`/`S`/`Esc`) cuando la pestaña *Ondas* está activa.
- **Zoom temporal** (`Shift+→` / `Shift+←`): expande / contrae el ancho visible del eje de tiempo
  (span ×1.5 / ÷1.5) **anclado al centro** de la ventana; estira también el registro (trazas, marcas
  de fase y eje). Acotado a `[CSNHYPODBP_ZOOM_MIN_SPAN, max(CSNHYPODBP_TIME_SPAN_MAX, def_span)]`
  (el tope nunca queda por debajo de *Time window*); puede mostrar tiempo anterior a la P.
- **Zoom tipo Seisan** (dos clics sobre el canvas): el 1.er clic deja un **ancla** (marca
  discontinua naranja); el 2.º clic **a la derecha** (≥ 0.1 s) hace zoom a ese rango; el 2.º clic
  **a la izquierda** (o encima) vuelve a la ventana por defecto. El zoom es un intervalo **relativo
  a `winStart`** y aplica a **todas** las trazas de la estación. Se resetea al cambiar de estación,
  de evento o de *Time window*.
- **Continuidad de la traza** (regla general, válida en todo nivel de zoom y con los tres modos de
  zoom): la traza se dibuja **siempre como una trayectoria continua** por tramo de datos, nunca como
  segmentos sueltos.
  - `spp ≥ CSNHYPODBP_POLYLINE_SPP` → **banda envolvente cerrada y rellena** (envolvente `max`
    izq→der + `min` der→izq + cierre), construida por tramos contiguos con datos
    (`csnhypodbp_view_band_runs`). Al conectar las columnas no quedan huecos con `spp` intermedio.
  - `spp < CSNHYPODBP_POLYLINE_SPP` (zoom alto, un píxel cubre <2 muestras) → **polilínea por
    muestras**, continua, que se corta en los gaps.
  - **Grosor mínimo**: cada columna se expande en `±csnhypodbp_view_min_half(scale, 0.8)` para que
    una zona plana (`min==max`) siga dejando trazo visible.
  - Un **hueco real** de datos (`INT_MAX`) sí corta la trayectoria.
- **Eje de tiempo UTC**: tira fija (`EwGuiCanvas`, 26 px) **debajo** del canvas scrollable (no se
  desplaza con el scroll), con ticks "bonitos" y etiquetas `HH:MM:SS` (el arranque fija `TZ=GMT`).
  Refleja el zoom.
- **Zoom por arrastre en el eje**: mantener pulsado sobre la barra del eje y arrastrar **izq→der**
  dibuja una **banda gris** desde el ancla hasta el puntero; al soltar hace zoom a ese rango.
  Arrastrar **der→izq** vuelve a la ventana por defecto. Un arrastre < `CSNHYPODBP_ZOOM_MIN_SPAN`
  se ignora, y un press fuera del área dibujable (margen izquierdo/derecho) no inicia nada.
- **Selección y zoom**: el zoom **sobrevive** a los cambios que no hace el usuario. Re-seleccionar
  la **misma estación** o reconstruir el detalle del **mismo evento** no lo resetea (solo cambia con
  estación/evento/solución distintos). Un evento nuevo del anillo se añade a la lista pero **no roba**
  la selección si ya hay una (`csnhypodbp_should_autoselect`). La lista de estaciones **no pierde** la
  fila seleccionada al repoblarse tras un fetch (`g_rebuilding_stations`), así el canvas no se vacía.
- **Picado P/S**: `P`/`S` activan el modo picado (los comandos están en **☰ → Help → Commands…**);
  el **siguiente clic
  izquierdo** coloca la fase en la traza clickeada y **vuelve al modo zoom**; `Esc` cancela el modo
  picado (y el ancla de un zoom a medias) **sin** tocar el zoom. Mientras
  el modo picado está activo, un **círculo del color de la fase con la letra blanca** sigue al
  puntero sobre el canvas (no se cambia el cursor) y desaparece al marcar. La picada queda en un
  **overlay local** (aún **no** se emite al `PICK_RING`). El **clic derecho** quita las picadas
  manuales de una traza (canvas) o de toda una estación (lista). `Cancel` limpia todo el overlay del
  evento.

Barra inferior: a la **izquierda** `Cancel`; a la **derecha** los checkboxes de origen
**`A` / `M` / `Teo`** (M marcada por defecto; deciden qué picadas se mandan al localizador), el
grupo **`depth [caja] [☑ Fix]`** (profundidad fija de la corrida; la caja solo es sensible con
`Fix` marcado, se aplica al confirmar —Enter o salir del campo— y un valor inválido deja `locate`
insensible), la caja con el **localizador activo** y los botones `locators` y `locate`. `Fix` no se
persiste en el `.d`: viaja por corrida en una copia temporal de la configuración del localizador.
Con `csnloc`/`hyp2000_ring` la solución sale **exactamente** en esa profundidad; con `nlloc_ring`
NLLoc no la soporta de forma nativa y se emula en una ventana de ±`FixDepthSpacingKm`/2 (0.05 km
por defecto), como avisa el tooltip. El resto de controles viven en el
menú **☰** del headerbar, que es **dependiente de la vista**: en *Eventos* los grupos `Search`
(`Search events…`, `Clear search`) y `View`; en *Ondas* añade, **primero**, el grupo
`Stations` con la casilla `Hide stations without data`; después el grupo `Waveforms` con
`Filters…` (presets del `.d` + tipo/F1/F2/orden manual, ventana modal), `Fetch waveforms` (distancia) y
`Time window…` (ventana de registro); el grupo `Colours` con
`P manual` / `P automatic` / `P theoretical` / `S manual` / `S automatic` / `S theoretical`
(diálogo de color GTK4; el hex de las seis claves queda en el log para copiarlo al `.d`) y, **al
final**, el grupo `Help` con `Commands…`, que abre una ventana modal con la lista de
comandos de teclado/ratón.

`Hide stations without data` es un **modo dinámico no destructivo** con una **casilla** en su
propio grupo (`Stations`, el primero del menú): la fila se dibuja centrada y en negrita como el
resto de las opciones, con el indicador como única marca de estado y **toda la fila clicable**.
Filtra la lista dejando solo las estaciones con datos en la ventana del evento; no borra
filas, así que una estación que vuelve a tener datos (p. ej. por SeedLink) reaparece en la
siguiente pasada. La casilla refleja y alterna el estado de la **sesión**; el valor inicial lo fija
la clave `HideStationsWithoutData` (`1` = ocultas al arrancar, `0` = visibles) y alternarla **nunca
reescribe el `.d`**. Con `1` y antes del primer `fetch` la tabla queda vacía (`bHasData[]` nace en
0) hasta que llegan las ondas; es el comportamiento esperado.

`locators` abre la lista de localizadores declarados con `locator <nombre> <bin> <cfg>`; la caja de
la barra inferior muestra el **activo**. `locate` ejecuta la **cadena offline** en un worker con las
picadas de la **tabla** (una P y una S por estación, **con su canal**) filtradas por la selección de
estaciones/picks y por los checkboxes `A`/`M`/`Teo` (las `Teo` son la P teórica IASP91 de las
estaciones sin P A ni M). El set enviado es **exactamente lo que muestra la tabla**: una estación que
no aparece (por `MaxDistKm` o `HideStationsWithoutData`) no se manda. La cadena escribe las picadas →
`csnloc` offline (`DumpHypo 1`; el ARC se extrae del log) → refinador
elegido (`hyp2000_ring`/`nlloc_ring`) → magnitud (`hypos.jsonl` + `tank` temporal → `csnmags`
offline). El **residual por fase** lo aporta el propio localizador: el JSONL que `csnloc` emite a
stdout y las columnas 35-38 (P) / 51-54 (S) del ARC de los refinadores; se muestra en la tabla.
`nlloc_ring`/`hyp2000_ring` en modo offline **dicen la condición** que falló (`MinPhases`, `MaxRMS`,
fuera de `GridFile`, sin banda, 0 fases, sin `LOCFILES`, NLLoc/LOC anormal), así que el error del
botón trae la causa y no un `sin localizacion` mudo.

Detalles de la cadena que importan si vuelve a fallar:

- Las picadas se escriben con **net y loc reales** del ARC (`STA.CHAN.NET.LOC`); con `--.--`
  HYPOINVERSE no empareja la estación contra `estaciones_hyp.sta` (`LET 5 2 3 0 0`) y **rechaza todas
  las fases**. Los segundos se formatean `SS.mmm` (nunca ` 7.000` con espacio: `csnloc` descartaría
  la picada).
- **Si la solución base perdió el canal o la red** (p.ej. un ARC refinado por `nlloc_ring` antes del
  arreglo), se completan con la metadata de la estación (`StaFile`/`estaciones_107.txt`, que trae la
  red que el `.sta` necesita) en `build_row_for` y otra vez al armar `job->base`. Si aun así queda
  alguna sin canal/red, el `locate` **falla con un mensaje explícito** en vez de dar un
  `sin localizacion` mudo. El ARC que arma `Confirm` (`csnhypodbp_arc_build`) y las picadas manuales
  (`emit_pick_scnl`) ya llevan el SCNL completo, y `ManualStateFile` lo persiste.
- **Los residuales de la solución elegida se ven sin pulsar `locate`**: cada solución del grupo
  guarda sus propias picadas (`EventSol.picks`, con su residual) y al seleccionarla en la tabla
  *Origins* pasan a ser la fuente del `res` de la tabla de *Ondas* (`csnhypodbp_resid_source`:
  candidata de `locate` si está vigente > solución elegida). Las picadas que se envían al localizador
  van con `residual = NAN` (lo produce el localizador) y el JSONL de `csnloc` manda siempre. El
  residual se persiste en `pick.uncertainty` (`hypodb` → `ewguidb_query_picks` → `pk->residual`).
- **Orden por cabecera, fecha unificada y calendario**: las tres tablas usan el helper
  `ewgui/table_ui.h` (`ewgui_table_column_sortable` + `ewgui_table_enable_sorting`); la fecha se
  muestra como `YYYY-MM-DD HH:MM:SS` (`csnhypodbp_search_fmt_utc`) y el detalle añade `Created`
  (orden por defecto). Las magnitudes ordenan por su propiedad numérica (`nmag0..nmag3`), porque el
  texto de la celda es `"mag-nst"` y no es un número. El calendario del buscador es una ventana modal
  con `GtkCalendar` (`day-selected` guarda la fecha: el widget se destruye con la ventana).
- Del log de `csnloc` (que puede traer **varios** bloques `HYP2000ARC`, uno por pasada de la ventana
  deslizante) se toma el **último**, que es el representante (mismo criterio que el JSONL). El
  primero suele ser una solución parcial y con ella los refinadores se quedan sin fases.
- Los refinadores **silencian su stdout** mientras corren el motor (`freopen /dev/null`), porque
  HYPOINVERSE imprime `NNN STATIONS READ IN.` antes del ARC y contaminaría el ARC que se parsea;
  `csnhypodbp_arc_skip_preamble()` además salta cualquier preámbulo por si acaso.
- Clave **`LocateKeepTmp 1`**: no borra el directorio temporal al fallar y **anexa su ruta al error**
  (`[tmp=…]`), donde quedan `picks.txt`, `event.arc`, `csnloc.err` y `ref.err`. Arnés manual sin GTK:
  `make -C ew_gui_tools/csnhypodbp chain` y
  `./locate_chain <arc_entrada> [refinador] [tmp_root] [--fix-depth <km>] [--timeout <s>]` corre la cadena
  completa con los mismos parámetros que la GUI y no borra nada.
El resultado se muestra en un **cuadro de dos mitades** al tope de *Procesar* (compartido por
`picking` y `magnitudes`): a la izquierda el título, `Origin time` y los parámetros
`Lat`/`Lon`/`Depth[km]`/`Nph`/`RMS[s]`; a la derecha el bloque `Magnitude`
(`ML`/`Mwp`/`Mb`/`Ms`/`Mww`) con un radio compacto por magnitud para elegir la preferida al
confirmar. Una grilla única de 4 filas y un separador vertical mantienen las filas alineadas y
evitan la franja vacía que dejaba la fila de radios sobre los parámetros. El cuadro **siempre está
visible**: muestra primero la solución elegida en el detalle y, tras `locate`, la candidata con un
**fondo rojo muy leve** (`.preview-box.candidate`) para que no pase desapercibida; sin selección
muestra guiones.
Mientras corre el `locate`, a la derecha del `Origin time` aparece un spinner y
`calculando <localizador>… N s` (timer de 1 s), **para todos los localizadores**, y los diez valores
del cuadro quedan en **guión** (no se muestran cifras de la solución elegida). Solo cuenta la
solución del **localizador elegido**: `csnloc` es un paso interno de la cadena y no publica
candidato intermedio. `LocatorTimeoutSec` (600 s en `csnhypodbp.d`) corta el refinador; si no hay
solución nueva, el renglón muestra `sin solución nueva con <localizador>: <detalle real>`
(`timeout`, `ref.err` o ARC inválido), los parámetros siguen en guión y el mensaje se borra al
elegir otro origen en *Origins*. El refinador recibe un `.d` temporal con un `WorkDir` propio
(`../nlloc_ring_run_<...>`) y, si `Fix` está marcado, `FixedDepthKm`; el ARC se siembra con la
solución elegida (lat/lon/profundidad/qid) para que NLLoc busque en la banda correcta y el candidato
siga perteneciendo al evento.
`Confirm` la convierte en la **solución manual preferida** (se reinyecta por el camino normal de ARC,
se emiten **una vez** las picadas a `PICK_RING` con origen `M`, se publica el **ARC confirmado** en
`ConfRing` —`HYPO_RING_REF2`— para que `hypodb` guarde una fila `origin` propia e inmutable
`<event>:<mod>:<version>`, y se persiste en `ManualStateFile`, que se relee al arrancar); `Discard`
la descarta para seguir picando (vuelve a mostrar la elegida). Cada confirmación es una fila más: la
**más reciente** es la preferida del evento y las anteriores quedan como orígenes manuales históricos
en *Origins*.
El texto y el formato de los valores viven en la lógica pura `csnhypodbp_preview.{h,c}`
(`make test` → `test_preview`), y la capa GTK solo vuelca el resultado en las etiquetas desde
`csnhypodbp_preview_update()`, el **único** punto que escribe la tablita.

### Mapas: columna derecha (planta + perfil)

La ventana tiene una **columna derecha** compartida por las dos pestañas (`GtkPaned` horizontal:
`notebook` | columna), con dos paneles apilados:

- **Planta** (`on_draw_map_plan`): mapa equirectangular con el **mismo contrato que `csnrv`** —
  `MapImageFile` (imagen 2:1 de mundo completo, validable con `common/tools/check_map_image.py`),
  capas `OverlayFile`/`OverlayColor`/`OverlayWidth`/`OverlayHide` (formato `.lines`), grilla con
  **etiquetas numéricas de latitud y longitud**, zoom con la rueda y paneo arrastrando con el botón 1.
  Encima dibuja las **rectas de los perfiles** (la activa en azul) y las soluciones del evento. El
  auto-encuadre tiene un ancho mínimo de `MapMinSpanDeg` grados (default 5), para que un grupo de
  soluciones apretado no deje la vista pegada.
  **`MapImageFile` es opcional**: si la clave está comentada (o la imagen no se puede cargar) el panel
  **no se apaga**; se proyecta sobre un **lienzo virtual** equirectangular 2:1 de 10800×5400
  (`CSNHYPODBP_MAP_VIRTUAL_W/H`, la escala del mapa de mundo entregado) y se siguen dibujando la
  grilla con etiquetas, las rectas de perfil, las capas `.lines` y los marcadores. Sin soluciones el
  encuadre inicial es el **mundo completo**. El paso de la grilla sale del **span visible**
  (`csnhypodbp_map_grid_step()`, ~8 divisiones) y no del zoom crudo: así a 5° de vista hay meridianos
  y paralelos en vez de ninguno. `--print-config` informa `MapCanvas image` o
  `MapCanvas virtual 10800x5400`.
- **Perfil** (`on_draw_map_profile`): corte con **x = longitud** e **y = profundidad** (hacia abajo)
  del perfil **más cercano a la solución preferida**, con la **subducción** (línea del slab, en km) y
  la **topografía** (elevación/1000) del archivo de ese perfil. Arranca encuadrado **solo a los datos
  del perfil** (slab ∪ topografía, recortados al marco, así nunca se salen del cuadro) y **no** se
  estira para abarcar todas las soluciones; tiene el mismo zoom (rueda) y paneo (arrastre) que la
  planta, para mirar una solución que quede fuera del rango.

Ambos paneles dibujan la **solución preferida** (rojo, escalada por magnitud), **todas** las soluciones
del evento en *Origins* (gris azulado), la **candidata** del `locate` (el mismo rojo claro de la
tablita del preview) y la solución **elegida en *Origins***, que va **verde** (`#00C853`, con borde
blanco) y se dibuja al final, encima de las demás: reemplaza al rojo de la preferida mientras sea la
elegida. El puntero **no** resalta nada (ni color ni cursor al pasar por encima); **el clic sin
arrastre sobre un marcador selecciona ese origen** en la tabla *Origins* (el arrastre sigue paneando) y
la fila se busca **por identidad en la vista ordenada** del detalle, no por posición del store. Se
actualizan al elegir evento/solución, al llegar un ARC nuevo y con cada `locate`/`Confirm`/`Discard`:
`csnhypodbp_maps_refresh()` es el **único** punto que recalcula el estado (puntos, solución elegida,
perfil activo, datos y el texto de estado) y repinta. Los dos cuadros **no muestran título**: la
información de estado (evento, nº de soluciones, perfil activo y solución elegida) vive en etiquetas
ocultas y en la descripción accesible de cada canvas; como GTK4 no expone el canvas en el árbol de
accesibilidad, la verificación de estos paneles se hace por píxeles (ver `docs/GUI-TESTS.md`).

Perfiles y datos: las líneas se declaran con claves `Profile <name> <lon1> <lat1> <lon2> <lat2>`
(cantidad y geometría arbitrarias; `ProfileFile` permite declararlas en un archivo aparte) y los datos
salen de `ProfileDataDir/slab<name>.tmp` y `topo<name>.tmp`, leídos con las columnas
`ProfileCols <lon_col> <val_col>` (1-based; por defecto `3 7`, el formato de los `.tmp` del repo
`test_db/scriptmapas`). Se importan una vez con:

```bash
./ew_gui_tools/common/tools/import_profiles.sh   # copia a resources/profiles/ + PROVENANCE.txt
```

Modos headless (no arrancan GTK ni EarthWorm; mismo patrón que `csnrv`):

```bash
csnhypodbp --print-config   run_working_v8/params/csnhypodbp.d   # claves y perfiles declarados
csnhypodbp --check-profiles run_working_v8/params/csnhypodbp.d   # datos legibles y traza vs recta
```

`--check-profiles` exige que los dos archivos de cada perfil se lean y compara la traza del slab
(columnas 3/4) contra la recta declarada; si la desviación media supera 0.05° avisa y sale con rc≠0
(es la red que detecta una línea mal declarada). Los valores de las 13 líneas de `csnhypodbp.d` son el
ajuste por mínimos cuadrados de cada traza: **las trazas de los scripts del repo `test_db` no son
paralelas** (pendiente 0.2758 en P01 → 0.2444 en P013).

La geometría vive en la lógica pura `csnhypodbp_profile.{h,c}` (recta, distancia perpendicular,
perfil más cercano y lector de polilíneas) y `csnhypodbp_mapview.{h,c}` (bbox de puntos, auto-encuadre
y marco del perfil), con `test_profile` y `test_mapview` en `make test`.

La P teórica se calcula con el motor IASP91 de `csnloc` (librería `csnloc/libcsnloc_tt.a`), leyendo
`iasp91.tbl`/`.hed` desde el cwd (`EW_PARAMS`, igual que `csnloc`). Si faltan las tablas, la teórica
cae a la hora origen sin romper el arranque.

### Diagnóstico del fetch de ondas

Cada descarga deja traza en el log de `csnhypodbp` (las ondas salen del store y del SDS):

- `fetch inicio sta=… ventana=[…] …s nsta=…` al lanzar el worker.
- `fetch sds <sta>.<chan>.<net>.<loc> res=<código> len=<bytes>` por SCNL pedido al SDS:
  `res=0` con `len>0` ⇒ hay datos.
- `fetch fin sta=… con_datos=N/M` y `fetch descartado (stale)` si cambió la selección.

La traza fija su ancla con `ewgui_trace_set_span` (`oldest`+`rate`+`length` de una vez), **también
en la primera descarga** (cuando la traza se crea perezosa); el draw usa `oldest` para mapear las
muestras a la ventana.

Al arrancar, la config efectiva queda en el log:

```
config channelsOrder=[HH BH HN LH] componentsOrder=[Z N E] HideStationsWithoutData=0
```

`kom` **no** tiene `k_rest()`: `k_str()` devuelve **un solo token**, así que `channelsOrder` y
`componentsOrder` (listas separadas por espacios) se leen token a token y se re-unen. Si el log
mostrara solo `[HH]`/`[Z]`, el parseo estaría truncado y solo se pediría `HHZ`.

## Backend SDS (eventos antiguos)

Las ondas se piden, por orden: **store** (`wstore`) y, si no las tiene, el archivo **SDS de
SeisComP**. Con `SdsEnabled 1`, un evento más antiguo que `SdsFallbackHours` (def. 168 h ≈ 7
días, alineado con la retención del `wstore`) se consulta **directo al SDS** (`SdsRoot`, def.
`/mnt/sds/archive`), porque la retención local ya no lo tiene. En ese caso (**evento antiguo**) el
orden es **SDS → store**, y si ninguno tiene datos la estación queda sin traza. Para eventos
recientes el SDS es el **último recurso**. El backend TCP (`wave_serverV`) se **eliminó** del
código: solo quedan el store y el SDS.

La ruta se arma con `ewgui_sds_path` (`common`):
`<root>/<YYYY>/<NET>/<STA>/<CHAN>.D/<NET>.<STA>.<LOC>.<CHAN>.D.<YYYY>.<DDD>`, y
`csnhypodbp_sds_fetch` lee el día (o los dos días de una ventana que cruza medianoche) con
**libmseed2** y ensambla un buffer `TRACE2_HEADER + i4` — el mismo contrato que `store_fetch_sta`,
así que `ws_fetch_finish` no distingue el origen. Las muestras `int16`/`float`/`double` se convierten
a `int32`; los huecos entre registros quedan en 0. Si el día no está en el SDS, el archivo ni se abre
(no ensucia el log con el `Cannot open file` de libmseed2).

**Location code.** El SDS de SeisComP escribe el location vacío (`C1.LL07..HHZ.D…`), mientras
EarthWorm usa el literal `"--"` (`LOC_NULL_STRING`, `trace_buf.h`). `ewgui_sds_path` traduce
`--`/vacío/solo-espacios → vacío; los locations reales (`00`, `10`, `04`, …) pasan literales. El
`TRACE2_HEADER` que devuelve `csnhypodbp_sds_fetch` conserva el `loc` original (`--`), porque el
resto del módulo (y EarthWorm) lo espera así.

## Configuración (`csnhypodbp.d`)

| clave | default | función |
|---|---|---|
| `InRingRaw` | (vacío) | 2.º anillo de entrada (`HYPO_RING`) |
| `InRingPick` | `OutRing` | anillo de picks a leer (`TYPE_PICK_SCNL`) |
| `TauModel` | `iasp91` | modelo de tiempos de viaje (P teórica) |
| `PickCaptureWindowSec` | `600` | ventana (s) para asociar una picada capturada al evento |
| `GroupPickTolSec` | `0.1` | tolerancia de tiempo de la firma |
| `GroupMinMatchPicks` | `3` | mínimo de picadas para fusionar por firma |
| `channelsOrder` | `HH BH HN LH` | orden externo de canales de la pestaña Ondas |
| `componentsOrder` | `Z N E` | orden interno de componentes (Z/N/E) |
| `tracesPerScreen` | `1` | trazas (canales de la estación seleccionada) visibles a la vez en la pestaña picking: **1, 2 o 3**; el resto se alcanza con la rueda. Un valor fuera de `[1,3]` no se aplica: se avisa en el log y se conserva el previo |
| `pColor` | `FF0000` | color de la marca P **manual** (y círculo del indicador de picado) |
| `pColorAuto` | heredado (`FF6666`) | marca P automática; ausente = `pColor` aclarado 40 % |
| `pColorTheo` | heredado (`FFB3B3`) | marca P teórica; ausente = `pColor` aclarado 70 % |
| `sColor` | `009900` | color de la marca S **manual** |
| `sColorAuto` | heredado (`66C266`) | marca S automática; ausente = `sColor` aclarado 40 % |
| `sColorTheo` | heredado (`B3E0B3`) | marca S teórica; ausente = `sColor` aclarado 70 % |
| `locator` | — | localizador offline: `locator <nombre> <bin> <cfg>` |
| `ManualStateFile` | `csnhypodbp_manual.events` | soluciones manuales propias (locate+confirm) |
| `LocatorTimeoutSec` | `600` | timeout (s) por localizador offline (NLLoc es lento) |
| `PrefetchNeighbors` | `2` | estaciones vecinas a precargar |
| `HideStationsWithoutData` | `0` | estado **inicial** de la tabla: `1` = ocultar las estaciones sin datos (el menú lo alterna en la sesión sin reescribir el `.d`) |
| `MaxDistKm` | `500` | distancia (km) máxima de las estaciones de la tabla (y por tanto del `locate`) |
| `FilterPhase` | `zero` | fase del filtro de la vista: `zero` (forward+backward, sin desfase) o `causal` (una pasada; retrasa el onset). Ver `docs/FILTERS.md` |
| `FilterMenu` | `f` | tecla que abre `Filters…` |
| `FilterRaw` | `0` | tecla que vuelve a la traza cruda |
| `Filter` | ver README/`.d` | preset con atajo: `Filter [<nombre>] <HP|LP|BP> <tecla> <f1> [<f2>] <orden>` (repetible; orden 2 o 4) |
| `StaLta` | `t 0.5 10 3 1.5` | STA/LTA visual: `<tecla> <sta_s> <lta_s> <on> <off> [<corr_s>]`; solo dibuja CF y marcas, no crea picks |
| `LocateKeepTmp` | `0` | `1` = no borrar el directorio temporal del `locate` y anexar su ruta al error |
| `GroupMaxSolutions` | `100` | tope de versiones por evento |
| `SearchMaxEvents` | `1000` | tope de eventos que devuelve la búsqueda hipocentral |
| `SdsEnabled` | `1` | habilitar el backend SDS (archivo SeisComP) |
| `SdsRoot` | `/mnt/sds/archive` | raíz del archivo SDS |
| `SdsFallbackHours` | `168` | edad (h) a partir de la cual se consulta el SDS |

## Tests

```bash
source ../../ew8_unix.sh
make -C ew_gui_tools/csnhypodbp test    # DSP + agrupación + parseo + trazas + IASP91 (headless)
```

`test_trace` cubre el parseo de `TYPE_PICK_SCNL` (fase/origen) y la captura por estación;
`test_tt` carga el motor IASP91 y valida el tiempo teórico de P; `test_master_store` verifica que
la lista maestra tenga **una fila por grupo** (identidad por grupo, no por `qid`); `test_magstore`
verifica el almacén por `(qid, mod)` y `csnhypodbp_mag_match`; `test_mag_msg` verifica que el
módulo origen viaja y se recupera en `MAG_INFO.origin_version`; `test_station_order` cubre el orden
de canales y las decisiones de la vista (banda/`needs_fetch`/`has_data`); `test_view` cubre el
estado del zoom por dos clics, los mapeos tiempo↔x, los ticks del eje y la exageración vertical.
`test_search` valida el formulario de búsqueda (fechas obligatorias, rangos a un solo lado, límite);
`test_statable` cubre la tabla de estaciones (dedup `M > A > T`, residual de la solución, canal de la
picada, color de traza y el filtro de picadas `csnhypodbp_locate_picks`/`csnhypodbp_pick_allowed` del
localizador); `test_staitem` cubre el modelo de fila del `GtkColumnView` (una fila por (estación,
fase), `stat` = M/A/Teo, `res` = `%+.3f`, `has-pick`); `test_preview` cubre la tablita del preview
(título por estado, formato de los 7 valores y guion cuando falta el dato); `test_profile` cubre los
perfiles (recta declarada, distancia perpendicular, perfil más cercano con N arbitrario, parser de
declaraciones y lector de polilíneas de slab/topografía con huecos `NaN` y truncación);
`test_mapview` cubre la geometría de los paneles (bbox de puntos, auto-encuadre del mapa de planta,
marco del perfil, zoom/paneo, el acierto del clic y la búsqueda de la solución elegida por id);
`test_config.sh` genera un `.d` y datos
sintéticos y valida `--print-config` / `--check-profiles` (incluidos los errores: recta mal declarada,
datos ausentes, clave obligatoria faltante); `test_locate` cubre además la
escritura de picadas con **net/loc reales**, que `arc_extract` devuelva el **último** bloque y el
salto del preámbulo del refinador;
`test_ui_invariants.sh` (grep, sin GTK) comprueba las invariantes de UI (tabla, barra inferior,
Help, filtros `A`/`M`/`Teo`); `test_sds_read` genera un miniSEED sintético en el layout SDS y
comprueba que el lector devuelve el
buffer `TRACE2_HEADER + i4` con las muestras intactas, que un `loc "--"` encuentra el archivo escrito
con loc vacío, que un día ausente no escribe en stderr y el predicado `csnhypodbp_sds_prefer`. En
`common`, `test_sds_path` valida la construcción de la ruta SDS (incluido `loc "--"` → `..`) y
`test_db_search` (integración, gated por `EWGUI_DB_TEST_ENV`) cubre los
rangos, el orden y el `LIMIT` de `ewguidb_search_events`.
