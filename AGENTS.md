# AGENTS.md - Directrices del proyecto para agentes de IA y desarrolladores

**EW8_GUItools**: sistema portátil de adquisición, procesamiento en tiempo real y análisis de datos
sísmicos, construido sobre **EarthWorm 8.0** y ampliado con herramientas GUI propias y módulos de
procesamiento sismológico (`ew_gui_tools`). La asociación de fases y la primera localización las
hace **`csnloc`** localmente.

---

## 1. Descripción general del proyecto

### Indicaciones:
- ESTAMOS EN UN AMBIENTE DE PRUEBAS, NADA AQUÍ ES PRODUCCIÓN REAL.
- `ew_8.0/` es la instalación EarthWorm 8 **vendorizada** (headers, librerías estáticas y los
  binarios de fábrica, todos versionados; ver `ew_8.0/VENDORED.md`). No es un árbol de fuentes:
  no busques ahí código de módulos ni intentes regenerarlo.

### Estructura de directorios
- `ew_8.0/`: instalación base de EarthWorm 8 versionada (headers, `.a`/`.o` para enlazar y los
  binarios de fábrica: `startstop`, `status`, `restart`, `stopmodule`, `slink2ew`, `pick_FP`,
  `sniffring`, `putpick`). Los binarios de nuestros módulos se instalan aquí con `make install` y
  **no** se versionan (`.gitignore` §3b).
- `ew_gui_tools/`: módulos y herramientas GUI de desarrollo propio:
  - `csnmags/`: módulo de cálculo de magnitud (ML/Mwp/Mb/Ms).
  - `csnhypodbp/`: base de datos de hipocentros y utilidades GUI/procesamiento. Agrupa las
    soluciones del mismo evento (un ID por evento, aunque sean de módulos distintos) por `qid`
    **o** por firma de picadas compatible (subconjunto dentro de `GroupPickTolSec` con al menos
    `GroupMinMatchPicks`); la picada de tiempo no incluye canal y el ARC viaja con las fases P y S.
    UI **maestro-detalle**: arriba una fila por evento (la solución **preferida**: la manual más
    reciente si hay alguna manual, si no la automática con más fases y menor RMS) y abajo las
    soluciones del evento al seleccionarlo. Lee `HYPO_RING_REF` (refinados 163/164) **y**, con
    `InRingRaw`, `HYPO_RING` (csnloc crudo 162) en vivo. La marca de solución manual viaja en el
    ARC en el byte 174 (`'M'`/`'A'`, ver `common/include/ewgui/arc_layout.h`), propagada por
    `csnloc` (12.º token `A/M` de `TYPE_PICK_SCNL`) y copiada por `hyp2000_ring`/`nlloc_ring`.
    Las **magnitudes** (`csnmags`, por `qid`) se guardan a nivel de evento y se proyectan a todas
    las soluciones, así la maestra las muestra siempre; para eventos solo-csnloc corre una 2.ª
    instancia `csnmags csnmags_raw.d` sobre `HYPO_RING` (`MOD_MAGNITUDES_RAW`).
    **Búsqueda hipocentral** en el menú ☰ de la pestaña *Eventos* (`Search events…`): rangos min/max
    de tiempo de origen (obligatorio), lat/lon/profundidad/magnitud vía `ewguidb_search_events`
    (`common`, filtro por origen/magnitud preferidos, tope `SearchMaxEvents`); el resultado
    **reemplaza** la vista y `Clear search` (ítem del ☰ + botón junto al título `Events`) vuelve a
    vivo+historial. Las ondas se piden al **store** (`wstore`) y, si no las tiene, al **SDS de
    SeisComP** (`SdsRoot`, layout `YYYY/NET/STA/CHAN/…`): un evento más antiguo que
    `SdsFallbackHours` se consulta directo
    al SDS (`csnhypodbp_sds_fetch`, libmseed2; ruta pura `ewgui_sds_path` en `common`) y se omite
    la retención local (misma ventana que el store). `ewgui_sds_path` traduce el location nulo de
    EarthWorm `--` (y vacío/espacios) a vacío — el SDS escribe `NET.STA..CHAN`; los locations reales
    (`00`, `10`, …) pasan literales, y el `TRACE2_HEADER` de salida conserva `--`. Día ausente en el
    SDS ⇒ no se abre el archivo (sin ruido `Cannot open file` de libmseed2).
    La pestaña *Ondas* muestra una **tabla de estaciones** en el panel izquierdo (`GtkColumnView` con
    separadores de fila y columna, como la de *Eventos*; fuente `0.75em`, borde propio y **sin scroll
    horizontal**: el marco se dimensiona a la tabla):
    **una fila por (estación, fase)** ⇒ 2 por estación, las dos **autónomas** (estación, distancia y
    casilla en ambas), columnas `station`(`[☑]`)/`dist[km]`/`pick`(`P`|`S`)/`stat`(`M`|`A`|`Teo`)/`res`;
    la casilla va **junto al nombre** y es la del **pick de esa fila** (desmarcarla desactiva esa fase,
    no la estación) y queda **vacía y no sensible** si esa fase no existe; las casillas **solo** filtran
    el envío al localizador. Por estación
    se dibuja **una sola P y una sola S** (`M > A > Teo`; la manual desplaza a la automática y a la
    teórica) y la traza va negra si la P es leída (real o manual), gris 0.55 si solo teórica. La barra
    inferior tiene `Cancel` a la izquierda y, a la derecha, los checkboxes **A/M/Teo** (M por defecto),
    la caja del localizador activo y `locators`/`locate`; `locate` manda **exactamente** las picadas de
    la tabla (con su canal, filtradas por esos checks; `MaxDistKm` 500 km), y el **residual por fase**
    lo aporta el propio localizador (JSONL de `csnloc` en stdout; cols. 35-38 P / 51-54 S del ARC de
    los refinadores). Los comandos de teclado están en ☰ → `Help` → `Commands…` (ya no hay texto de
    ayuda en la barra). La pestaña raíz **`Procesar`** agrupa dos sub-pestañas:
    **`picking`** (la vista de ondas de siempre) y **`magnitudes`** (una fila por
    estación con `dist[km]`, el ancla de la ventana (`M`/`A`/`Teo`), `t0`/`t1`, la
    magnitud y su residual, el canvas de la traza procesada por magnitud con la
    franja de la ventana arrastrable, y una barra inferior con los checkboxes
    `A`/`M`/`Teo` —origen de la ventana— y el override de la puerta de distancia de
    la magnitud activa). La tablita de la solución
    (`Lat/Lon/Depth/Nph/RMS/ML/Mwp/Mb/Ms`) vive **por encima del sub-notebook**, así
    que se ve igual en las dos. La GUI lee del **mismo `.d` de csnmags**
    (`MagConfigFile`) las ventanas y las puertas de distancia
    (`Ml/Mwp/Mb/Ms_MinDelta/MaxDelta`, `Ml_MaxDist`, `Ml_DistType`) y **no lista** las
    estaciones que el motor descarta; con red regional Ms/Mb quedan vacías por la
    puerta estándar (20–160° / 5–105°) y se bajan a mano con el override.
    `MagFetchSeconds` (700 s) alarga la petición para que Ms tenga su ventana. Ver
    `docs/MAGNITUDES.md`. La pestaña *magnitudes* tiene además un panel **`Mww`**
    (inversión de fase W on-demand, ver `csnwphase/` y `docs/WPHASE.md`): el área central
    es una pila `Estaciones` | `Mww` y sus claves son `MwwConfigFile` (default
    `csnwphase.d`) y `MwwWorkDir` (run dirs de las corridas). Las **tres tablas ordenan pulsando la cabecera** (helper general
    `ewgui/table_ui.h` + `common/src/ewgui_table_ui_gtk4.c`: primer clic descendente en numéricas y
    fechas y alfabético en texto, el segundo invierte; las magnitudes ordenan por `nmag0..nmag3`
    porque su celda es `"mag-nst"`). La fecha y hora van juntas (`YYYY-MM-DD HH:MM:SS`, UTC,
    `csnhypodbp_search_fmt_utc`) y la tabla *Origins* añade **`Created`** (hora de creación de la
    solución: llegada al módulo u `origin.creation_time`), que es su orden por defecto. El buscador
    tiene un **calendario** (`GtkCalendar` en ventana modal, sin popups por WSLg) que escribe la fecha
    conservando la hora, o `00:00:00` si el campo no la traía. Los **residuales** de la solución
    elegida se ven en la tabla de *Ondas* sin pulsar `locate` (`EventSol.picks` +
    `csnhypodbp_resid_source`), y se persisten en `pick.uncertainty`.
    El resultado del `locate` y la solución elegida se ven en una **tablita de 2 filas** al tope de la
    pestaña *Ondas* (cabecera `Lat`/`Lon`/`Depth[km]`/`Nph`/`RMS[s]`/`ML`/`Mwp` + valores; lógica pura
    en `csnhypodbp_preview.{h,c}` y **un solo** render, `csnhypodbp_preview_update()`, para que estado,
    valores, color y botones no se desincronicen): **fondo rojo muy leve** (`.preview-box.candidate`)
    cuando es la candidata del `locate`, ámbar suave cuando es la elegida y guiones si no hay
    selección; `Confirm`/`Discard` a la derecha, sensibles **solo** con candidata (`Discard` vuelve a
    mostrar la elegida, ya no oculta la tablita).
    La **columna derecha** (a nivel de ventana, compartida por las dos pestañas) tiene dos paneles:
    **planta** (imagen equirectangular + capas `.lines` con el mismo contrato que `csnrv`:
    `MapImageFile`/`OverlayFile`/`OverlayColor`/`OverlayWidth`, zoom con rueda y paneo con arrastre) y
    **perfil** (x = longitud, y = profundidad, con la **subducción** y la **topografía** del perfil más
    cercano a la solución preferida). Ambos dibujan la solución preferida (destacada), **todas** las
    soluciones del evento en *Origins* y la **candidata** del `locate`. Los perfiles se declaran con
    claves `Profile <name> <lon1> <lat1> <lon2> <lat2>` (cantidad y geometría arbitrarias) y sus datos
    salen de `ProfileDataDir/slab<name>.tmp` y `topo<name>.tmp` (columnas `ProfileCols`, por defecto
    `3 7`); los datos se importan una vez con `common/tools/import_profiles.sh` y se validan con
    `csnhypodbp --check-profiles <d>` (headless, igual que `--print-config`).
    El **reparto vertical** es 65% planta / 35% perfil (el usuario puede moverlo) y el ancho mínimo
    del auto-encuadre de la planta se fija con `MapMinSpanDeg` (default 5°, para no quedar pegado con
    un grupo de soluciones apretado). El panel de perfil arranca encuadrado **solo a los datos del
    perfil** (slab ∪ topografía, recortados al marco), con rueda = zoom y arrastre = paneo igual que
    la planta, y **no** se estira para abarcar todas las soluciones. El puntero **no** resalta nada: el
    marcador **verde** es el de la solución **elegida en la tabla *Origins*** (en **ambos** cuadros a la
    vez; reemplaza al rojo de la preferida mientras esté elegida) y el clic sin arrastre sobre un
    marcador **selecciona ese origen** en *Origins* (el arrastre sigue paneando). No muestran ningún
    título; las capas `.lines` se pueden apagar por nombre con `OverlayHide` (clave también en
    `csnrv`/`csnstaevdisp`).
    Lógica pura headless en `csnhypodbp_group.{h,c}`, `csnhypodbp_search.{h,c}`,
    `csnhypodbp_statable.{h,c}`, `csnhypodbp_staitem.{h,c}`, `csnhypodbp_preview.{h,c}`,
    `csnhypodbp_profile.{h,c}` y `csnhypodbp_mapview.{h,c}` (`make test`).
  - `csnrv/`: visor regional / herramienta de visualización. Visor del último sismo (historial de la BD hipocentral + anillo en vivo), con mapa equirectangular, tabla Preferred/Ml/Mwp y zoom/paneo. Zoom inicial configurable con la clave opcional `InitialZoom` de `csnrv.d` (default 8.0; 2.0 = vista regional). El mapa base (clave `MapImageFile`) debe ser una imagen equirectangular de mundo completo (ancho = 2·alto, `lon −180..180`/`lat −90..90`, sin alpha); validación: `common/tools/check_map_image.py`. Dibuja además una **capa vectorial de líneas** (márgenes de placas del mundo, subducción, fallas, límites de regiones) declarada con `OverlayFile`/`OverlayColor`/`OverlayWidth`/`OverlayHide` (formato `.lines`, datos en `resources/overlays/`; ver `common/tools/OVERLAYS.md`); `--check-overlays` reporta por capa `visible`, `color`, `width` y `polylines`. Test headless de configuración y overlays: `make test` (incluye `--print-config` y `--check-overlays`).
  - `csntvp/`: visor de trazas / visor de estaciones. Picks: etiqueta la fase y el origen (`P(A)`/`S(A)` automáticos; `P(M)`/`S(M)` manuales) leídos del 11.º/12.º token de `TYPE_PICK_SCNL` (fase y origen `A`/`M`; ausentes ⇒ P/A). Empareja por **estación** (ignora canal) para superponer S de `pickS` (HHE/HHN) sobre la traza HHZ. Colores de pick configurables con `pColor`/`sColor` en `csntvp.d` (defaults rojo/negro) y desde el menú *Colours* ("P picks"/"S picks"). En HOLD, las teclas `p`/`S` eligen la fase a picar y el clic izquierdo la coloca. Lógica pura testeable headless en `csntvp_util.{h,c}` (`make test`).
  - `csnstaevdisp/`: visor de estaciones y eventos (mapa equirectangular, triángulos de estación coloreados por elevación, círculos de sismo escalados por magnitud). Usa la BD hipocentral (historial) y `HYPO_RING` (en vivo). Mismo contrato de mapa base que `csnrv` (`MapImageFile`: equirectangular 2:1 de mundo completo, sin alpha) y misma **capa de líneas** (`OverlayFile`/`OverlayColor`/`OverlayWidth`/`OverlayHide`, ver `common/tools/OVERLAYS.md`); test headless `make test-config`.
  - `hypodb/`: **ingester** (C, headless) que lee los anillos hipocentrales y los escribe en
    PostgreSQL (esquema QuakeML simplificado, `db/schema.sql`). Entradas: `HYPO_RING` (csnloc 162)
    y `HYPO_RING_REF` (hyp2000 163 / nlloc 164, distinguidos por el `mod` del logo) + `PICK_RING`
    (para `pick_status` manual). Agrupa soluciones por **firma de picadas** (reutiliza
    `csnhypodbp_group`) para asignar un `event_id` por sismo aunque cambie el qid: el id es
    **`E<qid>-<yyyymmddhhmmss>`** (hora origen UTC de la 1.ª solución del grupo), porque el rango de
    qid de `csnloc` se repite cada ~27,7 h; el atajo de agrupación por qid exige además
    `|Δotime| <= EventMaxDtSec` (clave de `hypodb.d`, default 300 s) para no fusionar sismos de
    sesiones distintas. `origin.author` es constante (`csnhypodbp`), `origin.software` sale de
    `$EW_VERSION` y el localizador real queda en `origin.location_algorithm`;
    `magnitude.calculation_method` copia el `algorithm` del mensaje (`CSNNet` automática, `CSNMan`
    ajustada a mano). `TYPE_MAGNITUDE` → `magnitude` + `station_magnitude` (por estación).
    Credenciales en `run_working_v8/params/db.env` (`DbEnvFile`). Mapeo campo a campo (ida y vuelta)
    y contrato de orden de las tablas: `docs/DATA-MAPPING.md`; lo que falta decidir:
    `docs/DATA-MAPPING-PENDIENTES.md`. Ver también `docs/HYPODB.md` y `db/README.md`.
  - `csnloc/`: módulo nativo (C) de asociación de fases + localización rápida. Entrada `PICK_RING` (`TYPE_PICK_SCNL`), salida `HYPO_RING` (`TYPE_HYP2000ARC`). Config `csnloc.d` con **grillas anidadas** (global/regional/local, cada una en un archivo `.grid`; ver `csnloc/README.md`); tests con `make check` (unitarios, incluye grillas) y `make check-offline` (modo offline determinista). Además del modo anillo admite un **modo offline** `csnloc <csnloc.d> <picksfile>` (reloj virtual, un JSON por evento). Ver `csnloc/README.md`.
  - `pickS/`: picker nativo (C) de **onda S**. Lee ondas `TYPE_TRACEBUF2` de
    `SLINK_RING` (horizontales `HH?`/`BH?`) y picks P de `PICK_RING`, y publica
    picks S (`TYPE_PICK_SCNL` con 11.º token de fase `S`) en `PICK_RING`. Algoritmo
    STA/LTA sobre la envolvente horizontal + refinamiento AIC + polarización 3C
    (Jurkevics) para el criterio de calidad (weight 0-4). En modo `hybrid` exige
    **siempre** una P previa de la misma estación (ventana `GuideMin/MaxDtSec`):
    la S debe seguir a una P, lo que descarta el ruido horizontal posterior a la P.
    `MetricsLog 1` emite una línea `METRICS` por detección (canal lateral, solo log;
    no toca el pick ni el anillo) para calibrar `Q_*`/`MinSnr`; `Q_Plan_*`/`Q_Hv_*`
    (default 0 = off) permiten sumar planaridad y H/V al weight. `GateIncidMinDeg`/
    `GateIncidMaxDeg` (Max≤Min = off) son una compuerta dura de incidencia.
    Modo **offline**
    (`pickS <pickS.d> <tank> [ppicks]`) análogo a `pick_FP`, con config
    `pickS.d` y lista `pickS.sta`. Tests con `make check` (unitarios) y
    `make check-offline` (integrado determinista). Ver `pickS/README.md`.
  - `csnwphase/`: inversión de **fase W** (motor `eost/wphase` vendorizado) como cálculo
    **on-demand**: NO es módulo de anillos (no se declara en `startstop_unix.d`) y se
    dispara desde la GUI (*Procesar → magnitudes → `Mww`*) o por CLI. Prepara la entrada
    (ondas del **store/SDS**, `SAC_PZs_*` desde los `.pz` del repo, `i_master` +
    `CMTSOLUTION`), deja un `run.sh` auditable y parsea la salida a `wphase.json`. Config
    `csnwphase.d`; el panel lee el mismo `.d` (`MwwConfigFile`) y las corridas van a
    `MwwWorkDir`. `GfDir` es **absoluta y fuera del repo** (~21 GB). El panel `Mww` trae la tabla de parámetros (con el grid search) **editable** (t0/lat/lon/prof/magnitud semilla/ventana de ondas/screening; la banda se deriva de la magnitud y se recalcula), `Calculate`/`Cancel` con hilo,
    el resumen con la **playera** (cairo) y la salida completa del motor **lado a lado** y con la misma altura (`MWW_ROW_H`; el alto sobrante es para los parámetros; fuentes al 60 %; todo fallo se muestra con su texto real, nunca un "sin resultado" mudo), y el mecanismo focal
    se guarda **al confirmar la solución** con el radio `Mww` del preview de *Ondas* (quinto radio, con `Confirm`): escribe `magnitude(Mww)` + `tensor` sobre el `origin` de la solución confirmada y crea `event`+`origin` si faltan (ya no hay botón `Save to DB`); el
    **centroide no se escribe** en `origin`. Ver `docs/WPHASE.md` y `docs/pendienteMww.md`.
  - `ew_controller/`: panel de control gráfico para `startstop` (detener/reiniciar/iniciar módulos, ver logs, editar archivos `.d`). Usa un `CONTROL_RING` dedicado (clave 1060). **La identidad de un módulo es su `pid`, nunca el nombre**: `startstop_unix.d` tiene dos `Process` llamados `csnmags` (con `csnmags.d` y `csnmags_raw.d`), y resolver por nombre apuntaba siempre a la 1.ª instancia, así que la fila de la 2.ª mandaba Stop/Restart con el pid de la 1.ª. La fila seleccionada, el objetivo de Stop/Restart/Start (se lee del **item** seleccionado), la restauración de la selección tras un rebuild y el panel de log se resuelven por pid; el nombre sólo se usa para mostrar y para la política "a `startstop` no se le manda Restart". Los selectores de log/Configuration muestran el `.d` **sólo** cuando el nombre está repetido (`csnmags (csnmags_raw.d)`). Al reiniciar un módulo cambia su pid, así que la selección se pierde (precio de no emparejar por nombre). Las dos tablas (*Modules*: `Module/PID/Status/Details`; *Rings*: `Ring/Key/Size KB`) **ordenan pulsando la cabecera** con el helper compartido (`ewgui/table_ui.h`: `GListStore → GtkSortListModel → GtkSingleSelection/GtkNoSelection → GtkColumnView`, desempate `name`, primer clic ascendente). Como el `bind` de `GtkSignalListItemFactory` es un **snapshot** (§7), cualquier cambio visible **reconstruye las filas** (el `ewgui_ctrl_sync_plan`, en `common`, sólo evita el trabajo cuando nada cambió) y la selección se conserva por **pid** de módulo; **con el sort model en medio el índice de la vista no es el del store**, así que la fila seleccionada se resuelve por *item*. El **layout** son dos divisores ajustables (horizontal: tabla de *Modules* ↔ columna derecha; vertical en la derecha: Anillos ↕ Logs): al agrandar, el ancho extra se lo lleva la tabla y el alto extra los logs; el reparto inicial (panel derecho ~400 px, anillos la mitad) se fija **una sola vez** al mapear (`ec_split_h`/`ec_split_v`) y no hay `set_size_request` ni `shrink=FALSE` (fijaban ~1051 px de mínimo a la ventana, que así no podía encoger); las columnas de texto variable llevan `set_expand` + `set_resizable` (sin ninguna `expand` el ancho sobrante **no** se reparte y queda en blanco) y los `GtkScrolledWindow`/etiquetas de estado están acotados (`min_content_width`, elipsis: el mínimo de un `GtkLabel` es su texto completo). El refresco **se congela mientras la pestaña *Configuration* está activa** (no se reconstruye lo que no se mira) y pide confirmación al salir con cambios sin guardar. Si `startstop` no responde durante `PollInt*4` s, la columna `Status` muestra `?` en vez de un `Alive` ya falso. Los selectores de módulo **no** son `GtkDropDown` (su popup queda detrás de la ventana bajo WSLg/Wayland, bug #1299): son botones que abren una ventana modal con la lista. Tests: `make -C ew_gui_tools/ew_controller test` (invariantes de fuente) y `test-live` (GUI sobre
el EarthWorm en marcha: orden/estado, `--layout`, `--select` y `--dup`, con el estado sintético
inyectado en un anillo PROPIO del arnés (`TEST_CTRL_RING`); ver §7 y `docs/GUI-TESTS.md`).
  - `common/`: **librería estática compartida `libewgui.a`** para los módulos GUI
    (`config`, `ring`, `log`, `dsp`, `wave`, `geo`, `sta`, `view`, `actions`, `cfgfile`,
    `ctrl`, `export`). Reglas: las capas `core/dsp/wave/geo` **no incluyen `gtk/gtk.h`** (se
    testean headless con `make -C ew_gui_tools/common check`); el único punto con GTK
    es la capa de vista. Los Makefiles de los módulos la enlazan como
    `../common/libewgui.a`. `export`: volcado de cualquier `EwGuiCanvas` a SVG/PDF/PNG
    reutilizando su callback cairo (`ewgui_export.h`; núcleo headless + UI `GtkFileDialog`
    solo en el build GTK4). Los 5 módulos GUI usan **`AdwApplication` + `AdwHeaderBar`**
    (menú propio, "Export view…" y modo oscuro). El menú ☰ **no** es un `GtkMenuButton`
    (su `GtkPopoverMenu` es un popup que queda detrás de la ventana bajo WSLg/Wayland):
    es un botón que abre una **ventana modal** compartida (`menu_ui.h` +
    `ewgui_menu_ui_gtk4.c`, `ewgui_menu_button_new`). Ver `docs/GTK4-MIGRATION.md` §12.5.
  - `_legacy_atwc/`: herramientas WC/ATWC archivadas (`loc_wcatwc`, `pick_wcatwc`, `disk_wcatwc`); solo referencia histórica.
  - `libsrc/`: **legacy WC/ATWC** (EarlyBird). Ya no lo usa ningún módulo activo; se conserva solo como referencia histórica. Las tablas de tiempos de viaje IASP91 viven ahora en `csnloc/` (vendorizadas).
- `run_working_v8/`: directorio activo de configuración y logs en ejecución para EarthWorm:
  - `params/`: archivos de configuración (`startstop_unix.d`, `earthworm.d`, archivos `.d` de los
    módulos), las tablas IASP91 de `csnloc` (`iasp91.tbl`/`iasp91.hed`), las grillas (`grids/`) y
    los mapas base de los visores. `db.env` (credenciales PostgreSQL) **no** se versiona: se copia
    de `db.env.example`.
  - `log/`: archivos de log de ejecución.
- `resources/`: **datos grandes requeridos en runtime**, con rutas relativas: `nlloc/` (grillas 3D
  de tiempos + control files), `hyp2000/` (`.hyp`, `.sta`, `.crh`), `csnmags/` (`responses/`,
  `calib/`), `profiles/` (datos de perfil `slab*.tmp`/`topo*.tmp` del panel inferior de
  `csnhypodbp`, ver `common/tools/import_profiles.sh`) y `overlays/` (capas de líneas `.lines` de
  `csnrv`/`csnstaevdisp`/`csnhypodbp`; se generan con `ew_gui_tools/common/tools/make_overlays.py` y
  se validan con su subcomando `check`/`check-lines`). **No versionar** (`/resources/` en
  `.gitignore`; decenas de GB).
- `db/`: esquema SQL de la **base hipocentral** (`schema.sql`, copia literal de `test_db`),
  `docker-compose.yml` (PostgreSQL 16) y `README.md`. Credenciales en
  `run_working_v8/params/db.env` (solo se versiona `db.env.example`). Se levanta con
  `./ew8_db.sh up` (instancia 0, puerto 5435); cada worktree puede tener la suya con
  `./ew8_db.sh -i N up` (proyecto `ew8_hypodb_<N>`, puerto `5435+N`, volumen propio, sin
  auto-arranque). `-i N down` **detiene** (conserva el volumen); `-i N reset` borra los datos y
  vuelve a crear la base; `-i N destroy [--yes]` **borra** contenedor+volumen+red **sin recrear**
  y `instances` lista lo que hay (marca las huérfanas de un worktree ya borrado). `destroy` e
  `instances` funcionan aunque el `db.env` local declare otro `DB_PORT` (solo avisan); `up`,
  `psql` y `reset` sí lo exigen. Ver `docs/HYPODB.md`.
- `ew8_unix.sh`: script de inicialización del entorno (variables de EarthWorm).
- `ew_monitor.sh`: script de control de EarthWorm (`status|list|start|stop|restart`; `start/stop/restart`
  aceptan `<módulo> ...` para operar sobre uno solo, y `status`/`list` muestran el estado de cada
  módulo declarado —muertos incluidos—).

---

## 2. Arquitectura del flujo de datos

```
SeedLink (10.54.217.9:18000)
        │  slink2ew
        ▼
   SLINK_RING ──────────────► wstore_ingest ──► csnhypodbp (fallback)
        │  pick_FP
        ├────────────────────► wstore_ingest ──► store indexado (wstore)
        ▼
   PICK_RING ──────────────► csntvp / csnhypodbp (re-picks manuales)
        │
        └─ csnloc (asociador + localizador local) ─┐
                                                   ▼
   HYPO_RING (TYPE_HYP2000ARC) ──► csnmags  (ML / Mwp / Mb / Ms)  [store]
                               ──► csnhypodbp  (GUI)              [store]
                               ──► csnrv       (vista regional)
```

`csnloc` hace la asociación de fases y la localización **localmente**. El ingester `wstore_ingest`
lee `SLINK_RING` con su propio cursor y escribe el **store indexado** (`wstore`), fuente primaria de
ondas de `csnhypodbp` y `csnmags`.

---

## 3. Preparación del entorno

Antes de compilar o ejecutar cualquier componente, carga siempre el entorno:

```bash
source ./ew8_unix.sh
```

Esto define las variables de EarthWorm (`EW_HOME`, `EW_VERSION`, `EW_PARAMS`, `EW_LOG`, `PATH`).

---

## 4. Orden de arranque y parada

El único servicio activo es EarthWorm:

```bash
source ./ew8_unix.sh

./ew_monitor.sh start        # lanza startstop en segundo plano

```

Para detenerlo (`./ew_monitor.sh stop` es la parada ordenada de EarthWorm):

```bash
./ew_monitor.sh stop
```

### Scripts de control de servicios

```bash
./ew_monitor.sh {status|list|start|stop|restart} [<módulo> ...]
```

`list` muestra los módulos declarados en el `.d` de `startstop` con su pid y estado, `status` los
lista además con el estado junto a cada nombre (incluidos los muertos: un hijo muerto ya no es hijo
de `startstop`, así que el `pgrep -P`/`ps` de antes no lo veía), y `restart csnrv` (por ejemplo)
reinicia **solo** ese módulo; ver §7 para el porqué de que las acciones vayan por pid y se verifiquen.

- Todos los scripts monitor ejecutan los servicios con `setsid` para que sobrevivan a la shell padre.

---

## 5. Compilación y recompilación

Los módulos propios de `ew_gui_tools/` enlazan contra las librerías estáticas de EarthWorm. Hay que
recompilar al migrar de máquina o al modificar el código fuente.

Las **dependencias de cabeceras las garantiza el propio build**: todos los
Makefiles de módulos (y `common`) compilan con `-MMD -MP` y hacen
`-include` de los `.d`, así que cambiar un `.h` recompila solo los objetos que lo
incluyen. Sin eso, un módulo quedaba con el layout de structs viejo mientras la
librería compartida sí se recompilaba y la **ABI se rompía en silencio** (pasó con
`csnhypodbp` y `libcsnwphase.a`: el `i_master` salía con la memoria del struct
equivocado y la fase W no arrancaba). Invariante:
`common/tests/test_build_deps.sh` (dentro de `common check`).

**Ojo** con la librería compartida: su regla no depende de las fuentes, así que
tras tocar `common/` hay que reconstruirla a mano con el toolkit correcto
(`GUI_TOOLKIT=gtk4`) o los módulos fallan con `undefined reference`:

```bash
source ./ew8_unix.sh
# Si se tocó common/: reconstruir la librería de los módulos (ver nota de arriba)
make -C ew_gui_tools/common clean GUI_TOOLKIT=gtk4
make -C ew_gui_tools/common GUI_TOOLKIT=gtk4
make -C ew_gui_tools/common check      # tests headless + check_no_gtk
for dir in ew_gui_tools/csnmags ew_gui_tools/csnhypodbp ew_gui_tools/csnrv ew_gui_tools/csntvp ew_gui_tools/csnstaevdisp ew_gui_tools/ew_controller ew_gui_tools/csnloc ew_gui_tools/pickS ew_gui_tools/csnwphase; do
  if [ -d "$dir" ]; then
    echo "Compilando $dir..."
    cd "$dir"
    make clean && make
    cd - > /dev/null
  fi
done
```

---

## 6. Ejecución del sistema

1. Carga el entorno y arranca los servicios (ver sección 4).
2. Monitorea los logs de EarthWorm en `run_working_v8/log/`:
   ```bash
   tail -f run_working_v8/log/startstop_unix_*.log
   tail -f run_working_v8/log/csnloc_*.log
   tail -f run_working_v8/log/csnmags_*.log
   ```

### Arrancar un módulo suelto (para depurarlo o monitorearlo)

Los módulos **no** se pueden lanzar desde cualquier carpeta. La receta, **en una terminal independiente** (no desde la GUI ni desde un agente):

```bash
cd <ruta-del-repo>                 # 1. raíz del repo (donde está ew8_unix.sh)
source ./ew8_unix.sh               # 2. entorno EarthWorm
cd run_working_v8/params           # 3. carpeta donde están TODOS los .d
csnhypodbp csnhypodbp.d            # 4. estructura general: <binario> <binario>.d
```

**Por qué cada paso, y qué falla si falta:**

1. **Raíz del repo**: es donde vive `ew8_unix.sh`.
2. **`source ./ew8_unix.sh`**: sin esto no hay `PATH` hacia los binarios instalados
   (`ew_8.0/bin/`), ni `EW_PARAMS`/`EW_LOG`; además carga `ew8_gtk_env.sh` (fija
   `GSK_RENDERER=cairo` y el backend Wayland/X11 de los módulos GUI). Sin cargarlo, o el comando no
   existe, o el módulo arranca sin su entorno.
3. **`cd run_working_v8/params`**: **ahí viven todos los `.d` y los ficheros que ellos referencian**
   (`estaciones_107.txt`, `world_map*.jpg`, `csnwphase.d`, `pick_FP.sta`, …). Los módulos abren
   rutas **relativas** a su configuración; desde otra carpeta no encuentran su `.d` ni sus datos.
   **Ésta es la razón de que "no resulte" si lo lanzas desde otro sitio.**
4. **`<binario> <binario>.d`**: arranca el módulo **en primer plano**, como si lo hubiera lanzado
   `startstop`, pero suelto y con su salida en tu terminal. El `.d` es el mismo archivo de
   configuración que usa EarthWorm. **Se detiene con `Ctrl-C`.**

> Dos nombres **no** coinciden con su carpeta: `hypodb` se lanza como **`hypodb_ingest hypodb.d`** y
> `wavestore` como **`wstore_ingest wavestore.d`**.

**Avisos que ahorran tiempo**

- **Un módulo GUI no arranca sin `DISPLAY`** (`csnhypodbp`, `csnrv`, `csntvp`, `csnstaevdisp`,
  `ew_controller`): lánzalos desde una terminal de tu sesión gráfica (en WSLg, `:0`). `csnloc`,
  `pickS`, `csnmags`, `hypodb_ingest`, `wstore_ingest`, `hyp2000_ring`, `nlloc_ring` son headless.
- **Ningún módulo se protege de una segunda instancia** (no hay lock; la unicidad la da
  `startstop`). Si lanzas a mano un **productor** (`csnloc`, `pickS`, `csnmags`, `hyp2000_ring`,
  `nlloc_ring`) con `startstop` ya corriéndolo, tendrás **dos escribiendo en los anillos**
  (picks/soluciones duplicadas). Con los visores no hay problema.
- **Dejarlo en segundo plano y recuperar el prompt**:
  `<binario> <binario>.d >/tmp/opencode/<binario>.log 2>&1 & disown` (los scripts de §4 usan
  `setsid` para que sobreviva al cierre de la terminal).
- **GNU `timeout`**: en este entorno `~/seiscomp/bin` queda antes de `/usr/bin` y su `timeout` no
  acepta `timeout <segundos> <comando>`. `ew8_unix.sh` antepone `ew_gui_tools/common/tools/gnubin`
  (shim a `/usr/bin/timeout`) para que `timeout` a secas sea GNU; los scripts usan la ruta absoluta
  `/usr/bin/timeout` (lo exige `common/tests/test_timeout_gnu.sh`).
- **Su log**: `logit_init` lo escribe en `EW_LOG` como `<módulo>_*.log` (ver
  `run_working_v8/log/`). `wstore_ingest` y `csnwphase` **no** usan ese log: sus errores van a la
  terminal. (`hypodb_ingest` sí lo usa desde 2026-10-14: antes era mudo y por eso un fallo suyo no
  dejaba ni una línea que mirar.)
- **Qué binario estás usando**: `command -v <binario>` debe apuntar a
  `ew_8.0/bin/<binario>`; el estado de todos los instalados:
  `sh ew_gui_tools/common/tools/check_install.sh -v`.

**Casos de uso** (todos los comandos de esta tabla se ejecutaron una vez al escribirla; si cambias
uno, pruébalo)

| para qué sirve | comando | qué esperar |
|---|---|---|
| **Monitorear un módulo por sí solo**: verlo funcionar sin `startstop`, depurar su GUI | `csnhypodbp csnhypodbp.d` | se abre la ventana y la terminal muestra sus mensajes; `Ctrl-C` lo cierra |
| **Ver su log en vivo mientras corre** | `tail -f ../log/csnhypodbp_*.log` | las líneas que escribe en `EW_LOG` (desde `params`, el log queda en `../log/`) |
| **Depurar un fallo de ventana** (se cierra al maximizar, popups detrás, resize que no responde): el log de protocolo Wayland dice **qué objeto y qué regla** se violó | `WAYLAND_DEBUG=1 csnhypodbp csnhypodbp.d 2>/tmp/opencode/wl.log` | archivo grande (miles de líneas); **lo que importa es el final**: la última línea antes de `Gdk-Message: … Error 71 …` es el diagnóstico (p. ej. `xdg_surface buffer (1920 x 1139) does not match the configured maximized state (1920 x 1032)`) |
| **Guardar toda la salida de un arranque** (adjuntarla a un reporte) | `csnhypodbp csnhypodbp.d >/tmp/opencode/bp.log 2>&1` | stdout + stderr del módulo y de GTK en el archivo |
| **Escapar del bug de WSLg al maximizar** (fuerza X11) | `EWGUI_FORCE_X11=1 csnhypodbp csnhypodbp.d` | deja de cerrarse al maximizar, pero **se pierde el resize** (§7) |
| **Probar otro renderer** (descartar GPU/dmabuf) | `GSK_RENDERER=gl csnhypodbp csnhypodbp.d` | el default es `cairo` (software); `gl`/`opengl`/`vulkan` para comparar |
| **Ver avisos internos de GTK/GLib** | `G_MESSAGES_DEBUG=all csnhypodbp csnhypodbp.d` | mensajes extra en la terminal |
| **Cortar en el primer warning** (backtrace con `gdb`) | `G_DEBUG=fatal-warnings csnhypodbp csnhypodbp.d` | aborta justo donde aparece el warning |
| **No ensuciar los logs de producción** | `EW_LOG=/tmp/opencode/log csnhypodbp csnhypodbp.d` | el log del módulo va a ese directorio |
| **Probar un `.d` sin tocar el de producción** | `EW_PARAMS=/tmp/opencode/params csnhypodbp /tmp/opencode/params/csnhypodbp.d` | el módulo lee la copia aislada |
| **Comprobar que arranca y se puede matar** | `/usr/bin/timeout 20 csnhypodbp csnhypodbp.d` | corre 20 s y sale solo |
| **Validar la configuración sin abrir ventana** (sirve sin `DISPLAY`) | `csnhypodbp --print-config csnhypodbp.d` · `csnhypodbp --check-profiles csnhypodbp.d` · `csnrv --print-config csnrv.d` · `csnrv --check-overlays csnrv.d` · `csnstaevdisp --check-overlays csnstaevdisp.d` | reporte a stdout; ⚠ **el flag va ANTES del `.d`** |
| **Depurar los módulos de anillos** (headless) | `csnloc csnloc.d` · `pickS pickS.d` · `csnmags csnmags.d` · `hypodb_ingest hypodb.d` · `wstore_ingest wavestore.d` · `hyp2000_ring hyp2000_ring.d` · `nlloc_ring nlloc_ring.d` | salida en la terminal; `Ctrl-C` para parar. ⚠ Si `startstop` ya los corre, **no** los lances (duplicas la salida) |
| **La inversión de fase W** (on-demand: **no** es módulo de anillos) | `csnwphase csnwphase.d --print-config` · `csnwphase csnwphase.d --prep <rundir> <t0> <lat> <lon> <prof> <mag>` · `csnwphase csnwphase.d --run <rundir> … [--dry-run]` | escribe en el rundir (`run.log`, `wphase.json`); ⚠ **el subcomando va DESPUÉS del `.d`** |
| **Reprocesar offline** (sin anillos) | `csnloc <csnloc.d> <picksfile>` (reloj virtual; ver `csnloc/README.md`) | un JSON por evento localizado |
| **Inspeccionar los anillos** | `/usr/bin/timeout 5 sniffring -n PICK_RING` | lo que **ya** está en el anillo; sin `-n` lo drena primero (§7) |

### Recetas para agentes

Los agentes de **lista cerrada** (`plan`, `explore`, `review`) **no** pueden escribir
`source ./ew8_unix.sh && cd run_working_v8/params && <bin> <bin>.d`: el permiso de shell se
evalúa **por segmento**, cada envoltorio (`source`, `VAR=valor`, `/usr/bin/timeout`, `cd` a
ruta **absoluta**) es un comando aparte sin regla, y la llamada entera se deniega con un
mensaje opaco (`Permission denied: shell`). `build` y `runner` sí tienen `shell *` allow: usan
la receta de humano de arriba (`runner` la prefija dentro del mismo comando).

Para lo demás existe una única puerta de diagnóstico. Es **lista cerrada**: cualquier otro
subcomando sale con exit 3. Nivel A = no arranca nada y no toca la configuración:

```bash
sh ew_gui_tools/common/tools/diag.sh help              # la lista cerrada completa
sh ew_gui_tools/common/tools/diag.sh env               # EW_HOME/EW_VERSION/EW_PARAMS + binarios
sh ew_gui_tools/common/tools/diag.sh install           # check_install.sh -v
sh ew_gui_tools/common/tools/diag.sh config csnhypodbp # --print-config (csnhypodbp|csnrv|csnwphase)
sh ew_gui_tools/common/tools/diag.sh profiles          # csnhypodbp --check-profiles
sh ew_gui_tools/common/tools/diag.sh overlays csnrv    # --check-overlays (csnrv|csnstaevdisp)
sh ew_gui_tools/common/tools/diag.sh rings PICK_RING   # sin args: los anillos del .d (5 s c/u)
sh ew_gui_tools/common/tools/diag.sh log csnloc 40     # últimas 40 líneas del log más reciente
sh ew_gui_tools/common/tools/diag.sh logs              # inventario de logs
sh ew_gui_tools/common/tools/diag.sh errors            # líneas de error de los logs recientes
sh ew_gui_tools/common/tools/diag.sh process           # qué módulos del .d están corriendo
Nivel B = arranca un módulo, acotado por timeout (en plan el permiso está en ask, así
que el usuario aprueba viendo el comando exacto):
sh ew_gui_tools/common/tools/diag.sh run csnrv 30       # un módulo que NO esté corriendo
sh ew_gui_tools/common/tools/diag.sh run csnrv 30 --bg   # 2.º plano; se lee con `attached`
sh ew_gui_tools/common/tools/diag.sh attached csnrv
sh ew_gui_tools/common/tools/diag.sh stop csnrv          # mata SOLO lo que lanzó diag.sh
run se usa solo si el usuario pide explícitamente ese diagnóstico, y nunca si el módulo
ya está corriendo: sale con exit 4 (YA CORRE: <mod> (pids: …)) y se reporta, no se fuerza
(--force solo con el usuario delante). Nunca para duplicar productores
(csnloc, pickS, csnmags, hyp2000_ring, nlloc_ring): hay dos escribiendo en los anillos.
Quedan prohibidos startstop/stopmodule/restart/ew_monitor.sh start|stop|restart.



#### Coherencia de canal HHZ/BHZ

El sistema debe ser coherente en el código de canal en **tres** sitios: la fuente real
(`slink2ew_*.d`), los picks (`pick_FP.sta`) y la metadata (`estaciones_107.txt`). Si no coinciden:

- `csnmags` descarta el **100 %** de las fases **en silencio** (exige `strcmp` exacto de
  `sta+net+chan`; `mag_sta.c:49-70`) → nunca calcula magnitud de red.
- `csnhypodbp` tampoco superpone fases (`csnhypodbp_ws.c:255`).

No afecta a `csnloc` ni a `csnstaevdisp` (descartan el canal al parsear) ni a `csntvp` (usa
`stations_to_view.sta`, ya en `HHZ`, y lee del ring).




## 7. Trampas conocidas del entorno

- **`timeout` de SeisComP ensombrece coreutils** (histórico; ya **neutralizado en el repo**):
  `which timeout` resolvía a `/home/seba/seiscomp/bin/timeout`, que *no* es el `timeout` de GNU
  coreutils y rechaza la sintaxis estándar `timeout <segundos> <comando>` (falla con
  `couldn't exec <N>`). Blindaje: (1) `ew8_unix.sh` antepone `ew_gui_tools/common/tools/gnubin`, un
  shim a `/usr/bin/timeout`, así que `timeout` a secas resuelve a GNU **en una shell con el entorno
  del repo cargado**; (2) los scripts usan la ruta absoluta `/usr/bin/timeout`; (3)
  `common/tests/test_timeout_gnu.sh` (dentro de `common check`) prohíbe invocaciones sin ruta. Fuera
  del entorno del repo (shells que **no** sourcean `ew8_unix.sh`) el `timeout` de SeisComP puede
  seguir apareciendo. Para lanzar un módulo suelto (GUI incluida) ver §6.
- **`awk`/`mawk` y `LC_NUMERIC`**: con un locale no-C (p. ej. `es_CL.UTF-8`), `mawk` deja de
  interpretar `3.3807` como número y compara como **string** (`"3.3807" >= "180"` es *true*).
  Fuerza `LC_ALL=C` (o `LC_NUMERIC=C`) en cualquier script que use `awk` para comparar números.
- **Display X**: los módulos GUI (`csntvp`, `csnhypodbp`, `csnrv`) requieren un `DISPLAY` funcional
  (WSLg `:0` funciona al lanzarlos desde una sesión con acceso X). La validación de configuración
  **no** lo necesita: `--print-config`/`--check-profiles`/`--check-overlays` corren antes de crear la
  aplicación (`csnhypodbp_main.c`: el flag se resuelve antes de `adw_application_new`).
- **GTK4 en WSLg: backend y ventanas**: `ew8_gtk_env.sh` (fuente única) fija `GSK_RENDERER=cairo`
  (avisos libEGL/MESA) y **no fuerza `GDK_BACKEND`**: usa el backend por defecto (Wayland si hay
  `WAYLAND_DISPLAY`, si no X11). Motivo: con x11 los popovers funcionaban pero la ventana **no se
  puede redimensionar** (la CSD no tiene zona de arrastre alcanzable en WSLg/RAIL; los popovers
  de `GtkDropDown` dependen de `xdg_popup` y en Wayland viejo fallaban — WSLg #1226/#1390).
  En Wayland el compositor decora la ventana y el resize funciona, y desde WSLg 1.0.73 los
  popovers también. Escape para WSLg viejo: `EWGUI_FORCE_X11=1` (vuelve a x11; se pierde el
  resize). `ew8_unix.sh` la hace `source`. Ojo: ese bloque **no** debe volver a escribirse inline
  en `ew8_unix.sh`: vive sólo en `ew8_gtk_env.sh` (duplicarlo se perdía en silencio).
- **`GtkDropDown` en WSLg/Wayland (bug #1299)**: el popup aparece **detrás** de la ventana y no
  cierra al elegir. Por eso los selectores **no** usan `GtkDropDown`: `ew_controller` abre una
  **ventana modal con la lista** (Accept/Cancel), y `csnhypodbp`/`csntvp` (filtro/orden, pocas
  opciones) usan **radios** `GtkCheckButton`.
- **Menú ☰ del headerbar (misma familia que #1299)**: `GtkMenuButton`+`GMenuModel` crea un
  `GtkPopoverMenu` (superficie `xdg_popup`); bajo WSLg/Wayland se usaba una vez, quedaba detrás
  de la ventana y dejaba la UI anómala. Por eso **ningún** módulo lo usa: el ☰ es un `GtkButton`
  (`ewgui_menu_button_new`) que abre una **ventana modal propia** (`ewgui_menu_dialog_run`,
  `common/src/ewgui_menu_ui_gtk4.c`). Invariante en `common/tests/test_menu_ui.sh`
  (parte de `common check`).
- **Mapa base de `csnrv`/`csnstaevdisp`: contrato estricto**: `MapImageFile` (`csnrv.d`,
  `csnstaevdisp.d`) debe ser una imagen **equirectangular de mundo completo**: cubrir
  `lon −180..180` y `lat −90..90`, con **ancho = 2·alto** exacto y **sin canal alpha**
  (`ewgui_geo_project`, `common/src/ewgui_geo.c`, mapea linealmente; si el aspecto o la extensión
  no cuadran, las posiciones de estaciones/sismos quedan mal **en silencio**). Valídala con
  `python3 ew_gui_tools/common/tools/check_map_image.py <img> [--min-width N]` (stdlib; exit 0 ⇔
  2:1, sin alpha, ≥ min-width). Imagen entregada: Natural Earth 1:50m `HYP_50M_SR_W` (10800×5400,
  dominio público), JPEG RGB en `run_working_v8/params/world_map*.jpg`. Test:
  `make -C ew_gui_tools/common check` (incluye `test_check_map_image.sh`).
- **GTK4: color de fondo de botones por CSS**: hay que poner `background-image: none;` (además de
  `background-color`) o el tema pinta su imagen de fondo encima y el color no se ve (`color` sí
  se aplica, el fondo no). Ver `docs/GTK4-MIGRATION.md` §11.2.
- **GTK4/WSLg: maximizar CIERRA la ventana (`Error 71`, EPROTO) si el mínimo no cabe**: GDK termina
  el proceso por `_exit(1)` cuando `wl_display_dispatch` devuelve error
  (`gdk/wayland/gdkeventsource.c`), así que **no hay excepción que capturar**: hay que evitar la
  causa. Con `WAYLAND_DEBUG=1` el motivo es literal:
  `wl_display@1.error(xdg_wm_base, 4, "xdg_surface buffer (1920 x 1139) does not match the
  configured maximized state (1920 x 1032)")` ⇒ **el mínimo de la ventana era mayor que el alto
  maximizado** que concede el compositor (WSLg negocia `configure` con `0x0`/geometría propia; bug
  abierto microsoft/wslg#1428), GTK adjunta un buffer más alto que el configurado y el compositor
  corta la conexión. Regla: **nada en un módulo GUI puede inflar el mínimo por encima de la
  pantalla** — ni `gtk_paned_set_shrink_*_child(..., FALSE)`, ni `set_size_request` grandes, ni un
  `GtkColumnView` cuyas columnas fijen el mínimo (acotarlo con
  `gtk_scrolled_window_set_min_content_width`, como `ew_controller`); y los repartos diferidos de
  divisores (`"map"` + `g_timeout_add(50,…)`) se aplican **una sola vez** (flag por widget,
  patrón `ec-split`), porque re-aplicarlos mueve la geometría justo cuando el compositor negocia el
  maximizado. Diagnóstico y verificación: `CSNHYPODBP_MAXIMIZE_MS=<ms>` maximiza y des-maximiza 5
  ciclos sin clics, y `make -C ew_gui_tools/csnhypodbp test-live` comprueba que el proceso
  sobrevive. Medida objetiva: el `set_min_size(W, H)` del log de protocolo (en `csnhypodbp` pasó de
  `1379×1139` a `167×809`). Escape en WSLg viejo: `EWGUI_FORCE_X11=1` (pierde el resize).
- **El motor de fase W (eost/wphase) tiene dos rarezas que rompen el pipeline si no se conocen**:
  (1) **`make_resp_lookup_table` devuelve rc=1 SIEMPRE** aunque escriba la LUT completa (incluso con
  una sola estación o con lista vacía) ⇒ con `set -e` el `run.sh` **abortaba en ese paso** y la
  inversión **nunca** arrancaba; el síntoma es traicionero: el log está lleno de números, no hay ni
  un `ERROR`, y termina con `fin run.sh (rc=256)`. El generador lo tolera **sólo** si
  `coeffs_rec_lut` quedó con contenido (envoltorio `lut` de `csnwphase_run.c`). (2) Los GF se
  nombran `<STA>.<NET>.<CHAN>.<LOC>` y `wpinversion` los busca con el location **de la traza**: para
  el location nulo usa `--`, no vacío ⇒ la normalización a vacío vale **solo para el SDS**; para la
  traza y la búsqueda de GF hay que **restaurar `--`** (ida y vuelta `set_loc` en
  `csnwphase_wave.c`; el SDS ya traduce por su cuenta con `ewgui_sds_path`). Si el GF se pide con
  location vacío, muere con
  `ERROR opening ./GF/gf_rr/STA.NET.CHAN..SAC.sac.bp` (visto con `CX.PB16`, la estación que sólo
  entra con el hipocentro de la BD). Además el `run.sh` **marca cada paso**
  (`##### paso N: <orden> #####` y `##### paso N: rc=X #####`) y el panel muestra la orden que
  falló, para que ningún fallo del motor vuelva a quedar mudo. Ver `docs/WPHASE.md`.
- **`rd_chan_mag`/`rd_mag` (EarthWorm) no acotan sus escrituras y DESTRUYEN el mensaje**: el 4.º
  argumento de `rd_chan_mag` solo alimenta el `memset` inicial
  (`rw_mag.c:114` de EarthWorm 8) y el bucle escribe `pMci[nchan++]` sin comprobar
  nada ⇒ si el array se dimensiona con un conteo menor que las entradas que el motor escribe,
  **escribe fuera del heap** y glibc lo detecta minutos después con `SIGABRT` (signal 6), sin log.
  Y contar `'\n'` **no** vale de proxy: el formato no parte solo por `'\n'` (medido: 2 `'\n'` para 3
  estaciones). Además parsea in-place (`tmpMsg = msg`, `:119`, y `line[len] = '\0'`, `:134`):
  **destruye el buffer del llamante**, así que hay que darle una copia (y con holgura a ceros,
  porque además lee 1 byte más allá del mensaje). Ojo también con el buffer de anillo: **no** viene
  terminado en NUL. Todo esto se pagó en `hypodb_ingest`, que moría entre 9 y 77 minutos después de
  arrancar (medido 2026-10-14); ver `ew_gui_tools/hypodb/test_mag_lines.c` y
  `ew_gui_tools/hypodb/test_source_invariants.sh`.
- **`ew_monitor.sh` ya no trunca la consola de `startstop`**: `$STARTSTOP_CONSOLE` se **añade**
  (`>>`) con una cabecera UTC por arranque. Antes (`>`) borraba la salida del arranque anterior, que
  es justo lo que hacía falta mirar cuando un módulo moría (p. ej. el `SIGABRT` de
  `hypodb_ingest`). Invariante: `common/tests/test_ew_monitor.sh` (`make -C ew_gui_tools/common
  check`).
- **Los módulos de `ew_monitor.sh` salen del `.d` y las acciones van por pid**: `list` combina los
  `Process` de `$EW_PARAMS/startstop_unix.d` (`EW_MONITOR_CFG` lo sobreescribe) con la tabla que
  imprime el binario de fábrica **`status`**. Esa tabla trae
  **antes** de los procesos la lista de anillos (`Ring  1 name/key/size:  …`, cuya 2.ª columna es un
  número) y la fila del propio `startstop` (va como fila del *padre*, no en la tabla de hijos): hay
  que anclarse al separador `-------` y descartar el pid de `startstop`, o `list` inventa módulos.
  `restart <módulo>`/`stop <módulo>` usan los binarios `restart`/`stopmodule` (pid por el anillo
  meta) porque **la consola de `startstop` no sirve desacoplada**: su `fgets(stdin)` sale con
  `ESPIPE`/`ENOTTY` y el hilo de consola muere (por eso `ew_monitor.sh` lanza con `</dev/null`).
  Ninguno de los dos binarios verifica nada (salen 0 aunque no hagan nada, y `restart` **no imprime
  nada** en éxito), así que la confirmación —pid nuevo y `Alive`, o `Dead`— la hace el script y sin
  ella sale **4**. Un nombre ambiguo (dos instancias de `csnmags`) se rechaza con los candidatos,
  jamás "el primero". La guarda de "binario ausente" evita relanzar algo que quedaría en `NoExec`.
  **La identidad del stack es el pid, no el nombre**: con dos worktrees vivos hay dos `startstop` y
  dos juegos de módulos con los MISMOS nombres, así que el pid sale de `$EW_LOG/startstop.pid` (del
  que se exige evidencia: `/proc/<pid>/comm` = `startstop` y, si es legible, `EW_PARAMS` nuestro) y,
  si no, de `pgrep -x startstop` **filtrado** por el `EW_PARAMS` del proceso; con varios candidatos
  aborta. Lo mismo para los huérfanos de `stop` (`ewmon_leftovers`). Solo se descarta lo que se puede
  probar ajeno: sin `/proc` legible (pids sintéticos de los tests) se acepta, y por eso los tests
  pasan `EWMON_PID_SCOPE=loose` y fijan `EWMON_PIDFILE` en su tmp. `diag.sh` hace lo mismo con
  `DIAG_PID_SCOPE=loose`, su `TMP` por defecto vive dentro del worktree (`$LOGDIR/diag`) y
  `process`/`run` filtran por `EW_PARAMS`. Invariante: `common/tests/test_ew_monitor_scope.sh` (dos
  `startstop` reales con `EW_PARAMS` distintos: `status` ve el suyo y `stop` no toca el otro).
  `status` imprime **una fila por módulo declarado con su estado**: `EncodeStatus` conserva la fila
  de un hijo muerto (el `pgrep -P` de antes no, y por eso los muertos no aparecían). `Zombie` es la
  1.ª consulta tras una muerte (es cuando `startstop` reapea): se reintenta **una** vez para poder
  reportar `Dead`. Ojo con el TSV interno de este script: nunca lleva campos vacíos, porque para
  `read` el tabulador cuenta como *whitespace* **aunque se ponga en `IFS`**, así que un campo vacío
  en medio se colapsa y desplaza las columnas (el `.d` ausente viaja como `-`).
  Invariante: `common/tests/test_ew_monitor_module.sh` (prohíbe además que vuelva a aparecer un
  nombre de módulo literal en el script).
- **GTK4: el `bind` de `GtkSignalListItemFactory` es un SNAPSHOT**: sólo se emite al enlazar el
  `GtkListItem` con un item; **no** se repite al mutar una propiedad del objeto fila (`GListModel`
  reporta membresía, no mutación) y `GtkSortListModel` sólo reordena por `items-changed` /
  `GtkSorter::changed`. Así que `g_object_set(row,"status",…)` **no se ve** hasta que la fila se
  recicla al hacer scroll, y `items-changed(pos,1,1)` no es un "repintar" documentado. Toda tabla
  sobre datos mutables debe **reconstruir sus filas** (es lo que hace `ew_controller`, con
  `ewgui_ctrl_sync_plan` sólo para saltarse el trabajo cuando nada cambió). Ver
  `docs/GTK4-MIGRATION.md` §12.6.
- **GTK4: las referencias PRESTADAS de los getters de modelo no se liberan (use-after-free)**:
  `gtk_single_selection_get_selected_item()` devuelve un **préstamo** (`transfer none`; GIR
  `<return-value transfer-ownership="none">`, doc: *"the returned data is owned by the instance"*),
  mientras que `g_list_model_get_item()` es **transfer full**. Copiar el `g_object_unref` de una a
  otra suelta una referencia que no es tuya: el `GListStore`/`GtkSortListModel` se quedan apuntando
  a una fila **ya finalizada** y el siguiente rebuild (`g_list_store_remove_all`) hace
  `g_object_unref` sobre memoria liberada. Síntoma real en `ew_controller`: la ventana
  **desaparece sin mensaje** y `dmesg` muestra `segfault ... in libgobject` en
  `instance->g_class->g_type` (`mov (%rax),%rax`) con `g_class` basura; y ocurre **diferido**, no en
  el clic (se veía al pulsar *Restart*, con el cuadro de confirmación abierto sin tocar nada, o al
  pulsar *Yes*: el que revienta es el rebuild siguiente). Cubierto por `test_ui_invariants.sh`
  (`on_row_selected` no debe llamar a `g_object_unref`) y por el modo `--select` de
  `tests/test_sort_and_status.sh`. Si dudas de si está compilado en el binario instalado:
  `objdump -d --disassemble=on_row_selected ew_8.0/bin/ew_controller | grep g_object_unref`.
- **La consola de `startstop` resuelve `restart <algo>`/`stop <algo>` como `nombre || pid` con el
  PRIMER match** (`startstop_unix_generic.c:2478`/`:2497`): con dos `Process` del mismo nombre
  (`csnmags` y `csnmags_raw`) `restart csnmags` reinicia **siempre el primero**. Desde la consola
  hay que dar el **pid** (`restart 909348`). Por eso `ew_controller` no resuelve nada por nombre:
  identifica por pid (ver el bullet del módulo y `test_ui_invariants.sh` §7).
- **`slink2ew`** depende del servidor SeedLink externo configurado (ej `10.54.217.9:18000`) que debería estar siempre operativo; sin él no fluyen formas de onda ni picks, pero el resto de la cadena sigue funcionando (prueba con `putpick` / `sniffring`).
- **`ew_monitor.sh stop` ya no mata por patrón de línea de comandos**: usaba
  `pgrep -f 'slink2ew|pick_FP|…'` con una lista **literal**, así que (1) cualquier proceso cuya línea
  de comandos contuviera esos nombres —incluido **el propio script que llama** a `ew_monitor.sh`—
  recibía `SIGKILL`, y (2) la lista se quedaba vieja: omitía `pickS`, `csnloc`, `hyp2000_ring`,
  `nlloc_ring`, `wstore_ingest` y `hypodb_ingest`. Ahora los huérfanos se juntan de dos fuentes, sin
  ningún nombre escrito en el script: el **snapshot de los hijos** de `startstop` (tomado *antes* de
  matarlo: después quedan reparentados y no se enumeran) y `pgrep -x` con los binarios **derivados
  del `.d`** (nombre exacto de proceso, nunca `-f`). Si necesitas parar EarthWorm desde otro script,
  sigue siendo preferible la parada acotada (SIGTERM a `startstop` y a sus hijos vía `pgrep -P`),
  nunca `ew_monitor.sh stop` (mata por nombre en todo el sistema).
- **`sniffring` sin `-n` drena el ring**: `sniffring.c:242` vacía el ring antes de mostrar nada, así
  que parece vacío aunque tenga datos. Usa `sniffring -n <ring>` para inspeccionar lo que ya hay.
  No termina solo: córtalo con `Ctrl-C` o `/usr/bin/timeout 10 ... | head`.
- **`tport_getmsg`: `GET_MISS`/`GET_NOTRACK` sí traen mensaje**: el mensaje **ya está copiado** en el
  buffer del llamante (`transport.c:844-865` de EarthWorm 8) y el salto de secuencia
  se marca *después* (`:903`), así que hay que parsearlos y **seguir drenando** — si no, un anillo
  con mensajes desincronizados se vacía a un mensaje por tick. Sólo `GET_NONE` (nada casó) y
  `GET_TOOBIG` (casó pero **no** hubo copia: `*length = hd.size`, `:832-834`) no son un mensaje: por
  eso acota `recsize` antes de `msg[recsize] = '\0'`.
- **`startstop` no ve morir a un hijo si nadie le pide estado**: emite `TYPE_STATUS` sólo bajo
  `TYPE_REQSTATUS` (`startstop_unix_generic.c:424-425`) y cosecha de forma **perezosa**, dentro de
  `EncodeStatus` (`waitpid(..., WNOHANG|__WALL)`, `:1579`): el primer status tras la muerte dice
  `Zombie` (`:1577`) y el segundo `Dead` (`:1592-1601`). Un hijo muerto queda de **zombi** mientras
  nadie pregunte (visto al dejar un módulo muerto sin preguntar su estado: `[ew_controller] <defunct>`), y `startstop` **no
  autorreinicia** (eso es de `statmgr`, `statmgr.c:483-502`). De ahí que `ew_controller` muestre `?`
  en `Status` si no hay respuesta durante `PollInt*4` s.
- **La instalación de los binarios es atómica y siempre se ejecuta**: la hace
  `common/tools/install_bin.sh` (copia a un temporal + `mv`, target `.PHONY: install`). Así
  funciona **aunque el módulo esté corriendo** (un `cp` directo falla con `Text file busy` y `make`
  aborta con `Error 1`) y **aunque el binario local ya esté al día** (antes, `make` respondía
  `Nothing to be done`, no copiaba, y el instalado quedaba **ausente en silencio**: `startstop` no
  podía arrancarlo — `execvp failed` → `CHILD FATAL ERROR`: muere **el hijo**, el padre sigue vivo
  e incluso loguea `successfully restarted`, así que el módulo quedaba en `NoExec` sin que nadie
  chistara —justo lo que evita la guarda de binario ausente de `ew_monitor.sh`—). Estado con
  `sh ew_gui_tools/common/tools/check_install.sh -v` (lo cubre `common check`; su inventario se
  **deriva** de la receta `install_bin.sh` de cada Makefile de módulo, así que un módulo nuevo o un
  binario renombrado entran solos, sin tocar el script); los binarios nuevos
  entran en el siguiente reinicio de EarthWorm. Ojo: en `csnmags` y `pickS` el `all:` no era el
  primer target del fichero, así que `make` a secas sólo compilaba objetos — van con
  `.DEFAULT_GOAL := all`.
- **Los horizontales faltan en `estaciones_hyp.sta`** (misma familia que la coherencia HHZ/BHZ): el
  `.sta` de HYPOINVERSE se genera con `mk_sta.py` y hasta ahora emitía **sólo el canal vertical**,
  pero `pickS` pica las S en `HHN`/`HHE`. HYPOINVERSE empareja por `STA+NET+COMP3+LOC`
  (`hyphs.for`), así que rechazaba **todas** las S con `*** SKIP PHASE CARD WITH UNKNOWN STATION`
  (visible sólo con `PRT`/`KPR` en el `.hyp`) y la S nunca entraba a la solución — lo que además
  hacía inerte `POS`/`PSM` (Vp/Vs). `mk_sta.py` ahora emite las tres componentes por estación.
  Ojo con **dónde** vive el `.sta`: el modo anillo usa el de `params/`, y un arnés de
  refinadores que copie el `.sta` a su WorkDir **sólo si falta** esconde el cambio.
- **La línea-1 del ARC no admite texto en 122..135**: HYPOINVERSE la lee como su resumen
  (`hypoinverse/hyphs.for`, `FORMAT 1212 = T123,2(A1,F3.2,F3.1)`) y en ese tramo espera las
  magnitudes (`BMTYPI/BMAGI/TEMPI`). Escribir ahí el modelo de velocidades (como hacía `csnloc` con
  `EWGUI_ARC_VMODEL_OFF 123`) aborta LOC con `Bad value during floating point read`, y `hyp2000_ring`
  **pierde el evento**: le pasa el ARC de entrada **verbatim** a LOC (`hyp2000_ring.c` → `arcIn`).
  El campo vive ahora en **182..194** (`EWGUI_ARC_VMODEL_OFF`) y `hyp2000_ring` **limpia 122..135**
  antes de escribir `arcIn` (red para ARCs viejos o ya en el anillo). Invariante:
  `common/tests/test_arc_layout.sh` (`make -C ew_gui_tools/common check`). Síntoma de la regresión:
  `hyp2000_ring` deja de refinar **en masa** (`LOC termino anormalmente (status=0x200)`), también en
  el camino del anillo.
- **El esquema tiene columnas que ningún código escribe (siempre NULL)**: `origin.latitude_uncertainty`,
  `origin.longitude_uncertainty`, `origin.depth_uncertainty`, `origin.time_uncertainty`,
  `origin.error_ellipse`, `pick.polarity` y `pick.probability`. El ARC que circula
  no las transporta (`pick.polarity` no tiene fuente: el ARC no lleva primer movimiento). **`tensor` ya
  no está en esta lista**: lo escribe `ewguidb_upsert_tensor` con el mecanismo focal de la inversión
  de fase W (radio `Mww` del preview + `Confirm`; ver `docs/WPHASE.md` y `docs/pendienteMww.md`). El
  invariante `common/tests/test_schema_coverage.sh` (`make -C ew_gui_tools/common check`) exige que
  toda columna de `db/schema.sql` esté escrita por `ewgui_db.c` **o** declarada "vacía por diseño"
  con su motivo: si añades una columna al esquema y no la escribe nadie, el test falla. Mapeo campo
  a campo (ida y vuelta) y contrato de orden de las tablas: `docs/DATA-MAPPING.md`; lo que queda
  por decidir, en `docs/DATA-MAPPING-PENDIENTES.md`.
- **El arnés de validación GUI no necesita tocar el código de los módulos**: `common/tools/gui_run.sh`
  (Xvfb + `dbus-run-session` + AT-SPI + `xdotool`) corre el binario real en un display virtual y
  `common/tools/gui_probe.py` lee el árbol de accesibilidad, pulsa widgets por acción y devuelve
  geometría. Requiere `xvfb`, `dbus-run-session` y `python3-pyatspi`; el módulo debe arrancar (es
  decir, EarthWorm arriba, porque es cliente de anillos). Dos trampas medidas: GTK4 **no traduce**
  las posiciones de los widgets a coordenadas de pantalla por AT-SPI (todos los objetos reportan
  `0,0`; la sonda acumula tamaños de hermanos y toma la posición del toplevel de X) y las cabeceras
  ordenables de `GtkColumnView` **no exponen acción AT-SPI** (bug #8354), así que se pulsan por
  coordenadas. Ver `docs/GUI-TESTS.md`. Lo que **no** cubre: estética y comportamiento del
  compositor de WSLg (bug #1299), que se valida en la sesión real.
- **El residual por fase de una solución viaja en `pick.uncertainty`**: `hypodb` lo escribe,
  `ewguidb_query_picks` lo lee y `csnhypodbp` lo muestra en la columna `res` de la tabla de *Ondas*
  al seleccionar la solución en el detalle (no hace falta pulsar `locate`). Las picadas que se
  envían al localizador van **sin** residual (`NAN`): el residual lo produce el localizador y manda
  el JSONL de `csnloc`. Cada solución del grupo guarda **sus** picadas (`EventSol.picks`), no solo la
  unión.
- **`nlloc_ring` perdía la RED y el CANAL en su ARC de salida**: NLLoc escribe la línea de fase como
  `comp`(1 letra)+`inst`(3) y deja `inst` en blanco, porque el OBS sólo lleva el canal en el campo
  COMP (`NLLocLib.c:13171-13177`); su ARC quedaba con `NET` y `COMP3` en blanco. Al seleccionar ese
  evento y pulsar `locate`, `csnhypodbp` sacaba de ahí las picadas → `csnloc` producía un ARC sin
  canal → HYPOINVERSE (que empareja `STA+NET+COMP3+LOC`) rechazaba **todas** las fases
  (`hyp2000_ring: sin localizacion`) y el OBS de NLLoc quedaba con **13 campos** en vez de 14 (NLLoc
  descarta el arribo → `ARC de salida vacio`). `arc_rewrite_id` ahora copia red/COMP3/LOC de cada
  fase del **ARC de entrada**, y `csnhypodbp` completa lo que falte con la metadata de la estación
  (`estaciones_107.txt`, que trae justo la red que el `.sta` necesita).
- **La cadena offline del `locate` (`csnhypodbp`) tenía tres trampas que dejaban a los refinadores sin
  solución**: (1) las picadas se escriben con los **net/loc reales del ARC** (`STA.CHAN.NET.LOC`); con
  `--.--` HYPOINVERSE no empareja la estación contra `estaciones_hyp.sta` (`LET 5 2 3 0 0`) y
  **rechaza todas las fases** (`hyp2000_ring` → `sin localizacion`); (2) el log de `csnloc` trae
  **varios** bloques `HYP2000ARC` (uno por pasada de la ventana deslizante) y hay que quedarse con el
  **último** — el representante — porque el primero suele ser una
  solución parcial; (3) los refinadores en modo offline imprimen el ARC por **stdout**, así que
  `hyp2000_ring`/`nlloc_ring` corren con `freopen("/dev/null","w",stdout)` — sin eso HYPOINVERSE
  antepone `NNN STATIONS READ IN.` y nadie puede parsear el ARC (la cadena de `csnhypodbp` no lanza
  el motor: salta el preámbulo con `csnhypodbp_arc_skip_preamble`). Para diagnosticar:
  `LocateKeepTmp 1` (no borra el temporal y lo pone en el error como `[tmp=…]`) y el arnés
  `make -C ew_gui_tools/csnhypodbp chain` + `./locate_chain <arc> [refinador] [tmp_root]`.
- **`run_working_v8/params/` es SOLO configuración versionada; el runtime de cada módulo va a su
  carpeta en `run_working_v8/`**: ningún módulo nuestro escribe en `params/`. Los refinadores
  declaran su `WorkDir` **relativo** (`../hyp2000_ring`, `../nlloc_ring`), que se resuelve contra
  `$EW_PARAMS` (el cwd que `startstop` da a los hijos) y el propio módulo lo crea al arrancar
  (`mkdir -p`) — igual que el `loc/` bajo `OutRoot` de nlloc y la carpeta del estado manual de la
  GUI (`csnhypodbp`, junto a sus corridas `mww/`). Los recursos pesados **no** se copian ni se
  symlinkean: se referencian por ruta **absoluta de sólo lectura** en el `.d` (`nlloc_ring.d`:
  `ModelBand` → `/mnt/d/resources/nlloc/time3d/…`, y `TtimeRoot`/`ControlFile`/`ModelFallback` →
  `/mnt/d/resources/nlloc/…`; también `GfDir`, `SdsRoot`, `ResponseDir`, `OverlayFile`,
  `ProfileDataDir`). Las entradas pequeñas de HYPOINVERSE (`.hyp`, `estaciones_hyp.sta`, los `.crh`)
  siguen versionadas en `params/` y el `.hyp` las referencia como `../params/…`, porque el motor
  corre con cwd = `WorkDir`. Las **únicas** escrituras inevitables en `params/` son el estado de dos
  binarios de **fábrica**, que no tienen clave de ruta en su `.d`: `slink2ew` → `slink<modid>.state`
  y `pick_FP` → `pick_FP_<modid>.ndx` (ignorados por patrón). Invariante:
  `common/tests/test_params_stays_clean.sh` (`make -C ew_gui_tools/common check`). El procedimiento
  completo para levantar dos stacks en paralelo (instancia N, `worktree_bootstrap.sh`, BD propia)
  está en `docs/WORKTREES.md`.
