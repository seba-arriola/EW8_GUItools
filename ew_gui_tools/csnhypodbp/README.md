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

## Pestaña Ondas (navegación por estación)

El panel izquierdo lista las **estaciones** (una fila por nombre, orden por distancia) dentro de un
`GtkFrame` con scroll propio; la fila seleccionada se resalta. **Por defecto no se dibuja ninguna
traza**: al hacer clic en una estación se descargan (bajo demanda) y se dibujan **solo sus
canales**, en el orden de `channelsOrder` × `componentsOrder` (`HHZ, HHN, HHE, BHZ, …`), omitiendo
los no disponibles. Solo se piden canales de las **bandas que la estación ya tiene** (p. ej. un
`.sta` con `HHZ` → `HHZ/HHN/HHE`; no se intenta `BH*`/`HN*`/`LH*`). Al seleccionar la estación se
piden **todos** sus canales de esas bandas (no solo el del `.sta`): cualquier fila sin `fetched`
fuerza la descarga. Cada canal ocupa una banda ≈ la pantalla (una componente a la vez); el resto se
ve con scroll. Cada traza se rotula con su **componente** (`HHZ`, `HHN`, …); la estación va en el
título del canvas.

Cada traza se **alinea con la P de la estación**: se toma la picada capturada del `PICK_RING`
(manual `M` o automática `A`) si algún canal la tiene; si no, la **P teórica** (IASP91). Como la P
capturada vive solo en la fila que la recibió (p. ej. `HHZ`), la ventana es **común a la estación**
(`winStart` compartido) para que todas las trazas y el eje UTC coincidan. Traza **negra** si la P es
real; **gris** si solo teórica. Se dibujan además las **S** capturadas (A/M) y los picks manuales
del overlay, con los colores de fase configurables (`pColor`/`sColor`).
Regla del origen idéntica a `csntvp`: token 12 `M` ⇒ manual; ausente ⇒ `A`; sin picada ⇒ teórica.

### Interacción: zoom, eje de tiempo y picado

- **Exageración vertical**: `Shift+↑` / `Shift+↓` multiplican la escala vertical (`g_zoom_factor`,
  ×1.5 / ÷1.5, acotada a `[0.1, 50]`); `Shift+0` la resetea **y también el zoom temporal**. Las
  `↑`/`↓` **solas** siguen navegando la lista de estaciones (el `GtkListBox` conserva el foco); el
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
- **Picado P/S**: `P`/`S` activan el modo picado (la ayuda inferior lo indica); el **siguiente clic
  izquierdo** coloca la fase en la traza clickeada y **vuelve al modo zoom**; `Esc` cancela el modo
  picado (y el ancla de un zoom a medias) **sin** tocar el zoom. Mientras
  el modo picado está activo, un **círculo del color de la fase con la letra blanca** sigue al
  puntero sobre el canvas (no se cambia el cursor) y desaparece al marcar. La picada queda en un
  **overlay local** (aún **no** se emite al `PICK_RING`). El **clic derecho** quita las picadas
  manuales de una traza (canvas) o de toda una estación (lista). `Cancel` limpia todo el overlay del
  evento.

Barra inferior: `Cancel`, `locators` y `locate`. El resto de controles viven en el menú **☰** del
headerbar, que es **dependiente de la vista**: en *Eventos* solo `View`; en *Ondas* añade el grupo
`Waveforms` con `Filters…` (tipo/F1/F2/orden, ventana modal), `Fetch waveforms` (distancia),
`Time window…` (ventana de registro) y `Hide stations without data`, y el grupo `Colours` con
`P phase` / `S phase` (diálogo de color GTK4; el hex resultante queda en el log para copiarlo al
`.d`).

`Hide stations without data` (clave `HideEmptyStations`) es un **modo dinámico no destructivo**:
filtra la lista dejando solo las estaciones con datos en la ventana del evento; no borra filas, así
que una estación que vuelve a tener datos (p. ej. por SeedLink) reaparece en la siguiente pasada.

`locators` abre la lista de localizadores declarados con `locator <nombre> <bin> <cfg>`.
`locate` ejecuta la **cadena offline** en un worker: escribe las picadas (solución + overlay) →
`csnloc` offline (`DumpHypo 1`; el ARC se extrae del log) → refinador elegido
(`hyp2000_ring`/`nlloc_ring`) → magnitud (`hypos.jsonl` + `tank` temporal → `csnmags` offline).
El resultado se muestra en un **preview** al tope de la pestaña; `Confirm` lo convierte en la
**solución manual preferida** (se reinyecta por el camino normal de ARC, se emiten **una vez** las
picadas a `PICK_RING` con origen `M` y se persiste en `ManualStateFile`, que se relee al arrancar).

La P teórica se calcula con el motor IASP91 de `csnloc` (librería `csnloc/libcsnloc_tt.a`), leyendo
`iasp91.tbl`/`.hed` desde el cwd (`EW_PARAMS`, igual que `csnloc`). Si faltan las tablas, la teórica
cae a la hora origen sin romper el arranque.

### Diagnóstico del fetch de ondas

Cada descarga deja traza en el log de `csnhypodbp` (el camino GUI → `ws_clientII` → `wave_serverV`
es mudo por diseño; estas líneas lo hacen observable):

- `fetch inicio sta=… ventana=[…] …s nsta=…` al lanzar el worker.
- `fetch <sta>.<chan>.<net>.<loc> res=<código> actLen=<bytes>` por SCNL pedido: `res=0` con
  `actLen>0` ⇒ hay datos; `res=-4` ⇒ SCNL fuera del menú del server; `res=-11/-10` ⇒ conexión
  rota/timeout (se reinicia el menú).
- `fetch fin sta=… con_datos=N/M` y `fetch descartado (stale)` si cambió la selección.
- `fetch: wsAppendMenu fallo (ip:puerto)` si no se pudo construir el menú SCNL.

La traza fija su ancla con `ewgui_trace_set_span` (`oldest`+`rate`+`length` de una vez), **también
en la primera descarga** (cuando la traza se crea perezosa); el draw usa `oldest` para mapear las
muestras a la ventana.

Al arrancar, la config efectiva queda en el log:

```
config channelsOrder=[HH BH HN LH] componentsOrder=[Z N E] HideEmptyStations=0
```

`kom` **no** tiene `k_rest()`: `k_str()` devuelve **un solo token**, así que `channelsOrder` y
`componentsOrder` (listas separadas por espacios) se leen token a token y se re-unen. Si el log
mostrara solo `[HH]`/`[Z]`, el parseo estaría truncado y solo se pediría `HHZ`.

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
| `pColor` | `FF0000` | color de la fase P (marca + círculo del indicador) |
| `sColor` | `009900` | color de la fase S (marcas + círculo del indicador) |
| `locator` | — | localizador offline: `locator <nombre> <bin> <cfg>` |
| `ManualStateFile` | `csnhypodbp_manual.events` | soluciones manuales propias (locate+confirm) |
| `LocatorTimeoutSec` | `180` | timeout (s) por localizador offline |
| `PrefetchNeighbors` | `2` | estaciones vecinas a precargar |
| `HideEmptyStations` | `0` | ocultar de la lista las estaciones sin datos en la ventana |
| `GroupMaxSolutions` | `100` | tope de versiones por evento |

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
