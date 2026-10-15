# Migración a GTK4 + libadwaita — plan de traspaso

Documento de continuidad para el refactor incremental de los módulos gráficos de
`ew_gui_tools/` (`csntvp`, `csnhypodbp`, `csnstaevdisp`, `csnrv`, `ew_controller`)
y su posterior migración de **GTK3 + cairo** a **GTK4 + libadwaita**.

Está pensado para retomarse en otra sesión o por otro agente. Léelo entero antes de
tocar código.

---

## 0. Cómo retomar este trabajo

1. Carga el entorno: `source ./ew8_unix.sh`.
2. Estado de avance: ver la tabla de **Fases** (§7). Marca `[x]` lo completado.
3. Regla de oro: **cada tarea se implementa, se compila y se testea antes de pasar a
   la siguiente**. No se avanza sin el reporte de `make check` / test correspondiente.
4. Antes de la Fase 6 (GTK4) verifica las **precondiciones** de §8.
5. Si algo del plan no cubre una decisión, **pregunta; no improvises**.

**Estado actual:** Fases 0-5 **completas**. Fase 6: **los 5 módulos GUI sobre GTK4 + libadwaita**
(`csnrv`, `csnstaevdisp`, `csntvp`, `csnhypodbp`, `ew_controller`), todos con `AdwApplication` +
`AdwHeaderBar` + modo oscuro y enlazando `libewgui_gtk4.a`. Detalles del port y mejoras de cierre
en **§11** (WSLg/CSS/LogDir) y **§12** (export SVG/PDF/PNG, AdwApplication, fluidez).
Pendiente solo **T6.5** (retirar el build GTK3).
`GSK_RENDERER=cairo` (y **sin** forzar `GDK_BACKEND`: backend por defecto) en `ew8_gtk_env.sh`,
fuente única que `ew8_unix.sh` hace `source`.

---

## 1. Diagnóstico: el cuello de botella no es el toolkit

El problema real no es GTK3 ni cairo. Es:

- **Arquitectura**: lógica de dominio (anillos `tport`, `wave_serverV`, DSP, parseo)
  mezclada con la UI en monolitos (`csntvp.c` ~1428 líneas sin `.h`, `csnrv.c`,
  `csnstaevdisp.c`, `ew_controller.c`). Solo `csnhypodbp` está modularizado
  (`main`/`gui`/`ws`/`dsp` + `.h`) y es el **patrón de referencia**.
- **Duplicación**: `ew_background_tasks` copiada en 4 módulos; proyección de mapa +
  grilla + zoom/pan clonada en `csnrv`/`csnstaevdisp`; filtro IIR duplicado en
  `csntvp.c` y `csnhypodbp_dsp.c`; `ReadConfig` repetido 5 veces.
- **Falta de tests**: no hay capa de dominio testeable sin X.

Dato clave: **`csntvp` ya dibuja bien** (dirty flag `g_envelope_updated`,
caché de envolvente con clave `abs_start/abs_end + filtro`, buffer circular,
throttle, zoom sin invalidar caché). El refactor **preserva ese diseño** y solo lo
extrae a componentes compartidos y testeables.

Evidencia (archivo:línea) en §10.

---

## 2. Objetivo y alcance

**Objetivo:** que el dominio sea testeable sin X, que no haya duplicación entre
módulos, que el render sea un componente aislado y medible, y que la migración a
GTK4 sea un *port de la capa de vista*, no una reescritura.

**Dentro:** separación de capas, librería común `libewgui.a`, DSP unificado, modelo
de trazas compartido, abstracción de canvas, acciones/menús portables, threading de
I/O y cómputo, y la migración GTK3→GTK4+libadwaita.

**Fuera:** cambiar de lenguaje (se mantiene **C puro**), cambiar EarthWorm, rediseñar
`csnloc`/`pickS`, y alterar el comportamiento observable de las GUIs (el refactor es
*behavior-preserving*).

**Decisiones fijadas con el usuario:**
- Se crea **librería común compartida** `libewgui.a` y se migran los 5 módulos.
- Primero refactor incremental en **C + GTK3**; después **GTK4 + libadwaita**.
- El look debe ser **moderno (2026)** y el rendimiento, **fluido**.

---

## 3. Arquitectura objetivo

Librería interna estática `ew_gui_tools/common/` → `libewgui.a`.

```
ew_gui_tools/common/
  Makefile
  include/ewgui/
    config.h    log.h    ring.h    dsp.h    wave.h    geo.h    sta.h
    view.h      actions.h  cfgfile.h  ctrl.h
  src/
    ewgui_config.c  ewgui_log.c  ewgui_ring.c
    ewgui_dsp.c     ewgui_wave.c ewgui_geo.c  ewgui_sta.c
    ewgui_cfgfile.c ewgui_ctrl.c           # dominio sin GTK (Fase 4)
    ewgui_view_gtk3.c                      # única parte con GTK3 (Fase 4)
    ewgui_actions.c                        # GMenu/GSimpleAction (portable a GTK4)
  tests/
    check_no_gtk.sh        # capas puras no incluyen gtk/gtk.h
    baseline.sh            # golden de tests actuales
    test_config.c  test_dsp.c  test_wave.c  test_geo.c  ...
```

### 3.1 Contrato de capas (firmas esperadas)

```c
/* config.h — envuelve kom; getters tipados + claves obligatorias */
int  ewgui_config_load(const char *path, const EwKeySpec *spec, size_t n);
int  ewgui_config_get_str(const char *key, const char **out);
int  ewgui_config_get_int(const char *key, int *out);
int  ewgui_config_get_dbl(const char *key, double *out);
int  ewgui_config_get_color(const char *key, double rgb[3]);   /* hex -> rgb */

/* log.h — envuelve logit/logit_init */
int  ewgui_log_init(const char *module, const char *logfile);
void ewgui_log(const char *tag, const char *fmt, ...);         /* "et","e","t" */

/* ring.h — envuelve tport + GetType + heartbeat */
typedef struct EwGuiRing EwGuiRing;
typedef int (*EwMsgFn)(unsigned char type, const char *msg, int len, void *ud);
EwGuiRing *ewgui_ring_attach(const char *ring, const char *module_id,
                             const char *const *logo_types, size_t n_logo);
int  ewgui_ring_drain(EwGuiRing *r, const char *logo, EwMsgFn cb, void *ud);
int  ewgui_ring_put(EwGuiRing *r, unsigned char type, const char *msg, int len);
int  ewgui_ring_should_quit(EwGuiRing *r);   /* tport_getflag == TERMINATE */
int  ewgui_heartbeat_tick(EwGuiHeartbeat *hb, double now, int interval_s);

/* dsp.h — DSP puro (unifica csntvp y csnhypodbp) */
typedef enum { EW_FILTER_HP=1, EW_FILTER_LP=2 } EwFilterType;
void   ewgui_filter_iir(int32_t *data, size_t n, double fs,
                        EwFilterType t, double fc, int order);
void   ewgui_filter_bandpass(int32_t *data, size_t n, double fs,
                             double f1, double f2, int order);   /* HP(f1)+LP(f2) */
void   ewgui_interpolate_short_gaps(int32_t *data, size_t n, double fs);
void   ewgui_demean(const int32_t *in, size_t n, int32_t *out);
size_t ewgui_find_data_end(const int32_t *data, size_t n);

/* wave.h — modelo de traza + envolvente con caché (extraído de csntvp) */
typedef struct EwGuiTrace EwGuiTrace;
typedef struct { double tmin, tmax; int64_t abs_start, abs_end;
                 int width; int valid; double max_abs; } EwEnvelope;
EwGuiTrace *ewgui_trace_new(const char *sta, const char *chan, double rate);
void ewgui_trace_append(EwGuiTrace*, double t, const int32_t *s, size_t n);
void ewgui_trace_invalidate(EwGuiTrace*);
int  ewgui_trace_envelope(EwGuiTrace*, const EwFilterParams*, int width,
                          int64_t abs_start, int64_t abs_end, EwEnvelope *out);
/* devuelve 1 si recalculó, 0 si cache-hit */

/* geo.h — proyección/grilla/estado de mapa (csnrv + csnstaevdisp) */
void   ewgui_project_eq(double lat, double lon, int w, int h, double *x, double *y);
double ewgui_grid_step(double zoom);
typedef struct { double zoom, pan_x, pan_y; int dragging; double mx, my;
                 double center_lat, center_lon; } EwMapView;
void   ewgui_mapview_apply(EwMapView*, cairo_t *cr);
int    ewgui_mapview_scroll(EwMapView*, double dy, double mx, double my);

/* view.h — abstracción de canvas (ÚNICO punto que cambia en GTK4) */
typedef struct EwGuiCanvas EwGuiCanvas;
typedef void (*EwDrawFn)(EwGuiCanvas*, cairo_t*, int w, int h, void *ud);
EwGuiCanvas *ewgui_canvas_new(void);
void         ewgui_canvas_set_draw(EwGuiCanvas*, EwDrawFn, void *ud);
void         ewgui_canvas_queue_draw(EwGuiCanvas*);
void         ewgui_canvas_queue_draw_rect(EwGuiCanvas*, int x, int y, int w, int h);
void         ewgui_canvas_get_size(EwGuiCanvas*, int *w, int *h);
GtkWidget   *ewgui_canvas_widget(EwGuiCanvas*);  /* solo desde la capa app */

/* actions.h — menús/acciones declarativos (portable a GTK4) */
void        ewgui_actions_register(const EwActionSpec *spec, size_t n);
void        ewgui_actions_invoke(const char *name, GVariant *param);
GMenuModel *ewgui_actions_menubar_model(void);
```

### 3.2 Reglas de frontera (garantizan migración barata)

1. `core/dsp/wave/geo` **nunca** incluyen `gtk/gtk.h` (solo `glib`/`gio`, estables
   en GTK3 y GTK4). Se testean headless.
2. El dibujo entra por `EwDrawFn` (recibe `cairo_t*`): en GTK3 se conecta al signal
   `draw`; en GTK4 se implementa con `set_draw_func`/`snapshot` **sin tocar las capas
   de arriba**.
3. Los menús se declaran como datos (`EwActionSpec` + `GMenu`), no como
   `GtkMenuItem` a mano.
4. Las tablas se construyen sobre `GListStore` (`GListModel`), no sobre
   `GtkListStore` directamente.

---

## 4. Threading (obligatorio, no lo hace el toolkit)

GTK **no es thread-safe**; todas las llamadas GTK van en el hilo principal
(«GTK is not thread-safe» / «all GTK API calls from the same thread»,
`docs.gtk.org/gtk4/section-threading.html`). La construcción del scene graph
(`snapshot`) ocurre en el hilo principal; el toolkit solo delega a GPU el envío de
nodos con los renderers GSK. **La concurrencia de tus datos es tuya.**

Modelo (idéntico en GTK3 y GTK4):

- **Hilo principal = dueño del modelo y widgets.** Drena el anillo (barato,
  `tport_getmsg` es *poll* no bloqueante), actualiza `EwGuiTrace`, llama
  `queue_draw`. `gtk_widget_queue_draw` **no** es thread-safe.
- **Workers** = lo bloqueante/caro: I/O de red (`wsGetTraceBinL`) y cómputo
  (IIR, envolvente, relocalización).
- **Frontera** = snapshot inmutable + publicación con `g_idle_add()` /
  `g_main_context_invoke()` / `GAsyncQueue`. Para cómputo: `GTask` /
  `g_task_run_in_thread()` o `GThreadPool`. Nunca tocar widgets desde un worker.
- Reglas: medir antes de hilar; mover solo I/O bloqueante o cómputo > presupuesto
  (~4-8 ms); un hilo por anillo; animación continua → `GdkFrameClock` /
  `gtk_widget_add_tick_callback`, dato periódico → `g_timeout_add`.

**Bloqueo real hoy:** `csnhypodbp_ws.c:271` (`wsAppendMenu`) + `:313`
(`wsGetTraceBinL`, TCP con `WsTimeout`) corren en el hilo de UI dentro de
`FetchWaveformsForEvent` → congelan la ventana al seleccionar un sismo. Se ataca en
**T1.5** (worker de I/O), no esperar a Fase 5.

---

## 5. Inventario de duplicación a eliminar

| Duplicado | Dónde (archivo:línea) | Se unifica en |
|---|---|---|
| `ew_background_tasks` + heartbeat + `tport_getflag` | csnrv:181, csnstaevdisp:201, csntvp:407, csnhypodbp_main:185 | `ewgui_ring`/`ewgui_heartbeat` |
| Proyección equirectangular + grilla + zoom/pan | csnrv:467-528, csnstaevdisp:480-566 | `ewgui_geo` |
| Filtro IIR + gaps + demean | csntvp.c:224, csnhypodbp_dsp.c:39-175 | `ewgui_dsp` |
| `ReadConfig` con kom | los 5 módulos | `ewgui_config` |
| Modelo de traza circular + envolvente | csntvp.c:120-163, csnhypodbp.h:33-62 | `ewgui_wave` |
| Carga de estaciones / `rd_mag` / parseo ARC | csnrv:304, csnhypodbp_ws.c:227 | `ewgui_core` (opcional) |

---

## 6. Errores y casos borde

- Clave obligatoria de config ausente → error explícito nombrando la clave.
- Anillo no adjuntable / tipo no encontrado → log `e`, estado «sin datos», no abortar.
- `rate` desconocida o 0 → recalcular buffer; nunca dividir por 0.
- Gaps (`INT_MAX`): nunca filtran estado; no interpolar si el hueco > umbral.
- Frame con `sys - last > DATA_STALE` → marcar «STALE» (ya existe en csntvp:1327).
- Publicación desde worker con la ventana cerrándose → descartar si `should_quit`.
- Envelope con `width<=0` o ventana vacía → `valid=0`, sin dividir.

---

## 7. Fases

`[ ]` pendiente · `[x]` hecho. Cada tarea: id | archivos | interfaz | test | done.

### Fase 0 — Baseline, red de seguridad y esqueleto
- [ ] **T0.1** Medir con sysprof/GTK Inspector `csntvp` y `csnhypodbp` (ms/frame, nº
  redraw) → `docs/baseline.md`. *(pendiente: requiere DISPLAY)*
- [x] **T0.2** Golden: `common/tests/baseline.sh` corre `csntvp/test_filter`,
  `csnhypodbp/test_filter`, `csnrv --test-mag` y guarda hashes. *(PASS x3)*
- [x] **T0.3** Esqueleto `ew_gui_tools/common/` → `libewgui.a` + target `check`.
- [x] **T0.4** `common/tests/check_no_gtk.sh` (falla si core/dsp/wave/geo incluyen GTK).

### Fase 1 — Core compartido
- [x] **T1.1** `ewgui_config` (`ewgui_config.c`) | `ewgui_config_load` + claves
  tipadas (STR/INT/DBL/LONG/COLOR) | unit `test_config` (incluye obligatoria ausente
  y hex inválido).
- [x] **T1.2** `ewgui_ring` + `ewgui_heartbeat_due` | unit `test_heartbeat`; el
  anillo real queda por integración documentada.
- [x] **T1.3** Migrar `ew_background_tasks` de csnrv/csnstaevdisp/csntvp/csnhypodbp
  a `ewgui_heartbeat_due` + `ewgui_ring_should_quit` | build OK en los 4 + golden.
- [x] **T1.4** Los 4 Makefiles enlazan `libewgui.a` (regla `../common/libewgui.a`) |
  build OK con `-Wall`.
- [x] **T1.5** Worker de I/O para `wave_serverV` (`csnhypodbp_ws.c`): fetch en hilo
  worker + publicación con `g_idle_add`; parseo/filtrado en el hilo principal |
  compila + golden. *(C7 no congelación: validar con GUI)*
- [x] **T1.6** `ew_controller` adopta el core (`ewgui_config`/`ewgui_heartbeat_due`)
  y enlaza `libewgui.a` | build OK.
- [x] **T1.7** Cleanup: eliminados los globales `timeLastBeat` (csnrv/csnstaevdisp/
  csntvp/csnhypodbp_main + `csnhypodbp.h`) | build 4 + golden.

> **D1 (docs):** `AGENTS.md` ya lista `ew_gui_tools/common/` y la frontera; este doc
> (§3, §7) refleja T1.6/T1.7 y la extracción `ewgui_cfgfile`/`ewgui_ctrl`.

### Fase 2 — DSP unificado
- [x] **T2.1** `ewgui_dsp` (`filter_iir`/`filter_bandpass`/`interpolate_short_gaps`/
  `demean`/`find_data_end`) | `tests/test_dsp.c` (13 checks, portados de ambos
  `test_filter.c`).
- [x] **T2.2** `csnhypodbp_dsp.c` delega en `ewgui_dsp`; `csntvp` migra `plProcBuf` a
  `int32_t` y usa `ewgui_filter_iir`/`ewgui_filter_bandpass` | golden idéntico.

### Fase 3 — Modelo de formas de onda compartido
- [x] **T3.1** `ewgui_wave` (`EwTraceCache` + `ewgui_trace_envelope`) | `tests/test_wave.c`
  (determinismo, cache-hit, cambio de filtro, rango inválido).
- [x] **T3.2** `csntvp` usa `ewgui_trace_envelope`; los 18 campos de caché/procesado de
  `DEV_STATION` se sustituyen por un `EwTraceCache cache` | compila + golden.
  *(validar render con GUI/DISPLAY)*
- [x] **T3.3** `csnhypodbp` usa `EwGuiTrace` (posee buffer crudo + vista filtrada);
  `ApplySelectedFilter`→`ewgui_trace_filter`, `on_draw_signal` lee la vista filtrada;
  se elimina el shim `csnhypodbp_dsp.c` | compila + `test_filter` + golden.
  *(validar render con GUI)*
- [x] **T3.4** `ewgui_sta` (parser/cargador de estaciones, formato `Sta Net Chan Loc
  Lat Lon Elev Sens`) | `tests/test_sta.c`; `csnstaevdisp` migrado. *(El cargador de
  `csnhypodbp` conserva su variante legacy de 4 campos; pendiente de unificar.)*

### Fase 4 — Separación dominio/UI y abstracción de vista
- [x] **T4.1** `csntvp.c` partido en `csntvp.h` (declaraciones) + `csntvp_main.c`
  (config/ring/globals/main) + `csntvp_data.c` (modelo, fetch, envolvente) +
  `csntvp_gui.c` (dibujo + callbacks) | compila/enlaza + golden. *(validar con GUI)*
- [x] **T4.2** `EwGuiCanvas` (`ewgui_view_gtk3.c`, GtkDrawingArea + signal `draw`):
  `csntvp` (2 canvas: trazas y eje), `csnhypodbp`, `csnrv` y `csnstaevdisp` migrados
  (`ewgui_canvas_new` + `ewgui_canvas_set_draw` + `ewgui_canvas_queue_draw`); los
  callbacks de dibujo pasan a `EwDrawFn` (`canvas, cr, width, height, ud`). *(validar
  render con GUI)*
- [x] **T4.3** `ewgui_actions` (`GSimpleAction` + `GMenu`, sin GTK) | `tests/test_actions.c`
  (acción con/sin parámetro, construcción de menú); menús de `csnstaevdisp` y `csntvp`
  migrados a `GMenuModel` + `gtk_menu_bar_new_from_model` (portable a GTK4). *(validar
  menús con GUI)*
  **Patrón obligatorio:** las acciones van en un `GSimpleActionGroup` insertado con
  `gtk_widget_insert_action_group(window, "win", ...)`. **`GtkWindow` NO es `GActionMap`**:
  `G_ACTION_MAP(window)` provoca SIGSEGV en `g_action_map_add_action`. Los ítems usan
  `"win.<acción>"`.
- [x] **T4.4** `ewgui_geo` (`project`/`grid_step`/`zoom_step`) | `tests/test_geo.c`;
  `csnrv` y `csnstaevdisp` migrados (proyección, grilla y zoom por rueda).
  *(El estado zoom/pan permanece en cada módulo: está entrelazado con grosores/radios
  dependientes del zoom. Validar mapas con GUI.)*
- [x] **T4.5a** `ewgui_cfgfile` (editor de `.d`, sin GTK) | `tests/test_cfgfile.c`
  (parse, round-trip con `.bak`, `resolve` con `EW_PARAMS`); `ew_controller` migrado.
- [x] **T4.5b** `ewgui_ctrl` (protocolo `CONTROL_RING` + parser `TYPE_STATUS`, sin GTK)
  | `tests/test_ctrl.c`; `ew_controller` migrado (`EwCtrlStatus g_status`,
  `enviar`→`ewgui_ctrl_send`).
- [x] **T4.5c** `ew_controller.c` → `ew_controller.h` + `ew_controller_main.c`
  (config/ring/control/main) + `ew_controller_gui.c` (árboles, logs, editor de config) |
  compila/enlaza.
- [ ] **T4.6** `csnrv`/`csnstaevdisp` usan `ewgui_geo`/`EwGuiCanvas` | unit + golden.

### Fase 5 — Rendimiento y hilos
- [x] **T5.1** Bench headless de la envolvente (C6): `common/tests/bench_wave.c` +
  `make -C ew_gui_tools/common bench`. **Medido: 64 trazas × 7500 sps → 10.33 ms**
  (< 16 ms). Con ese margen, dirty-rect/fondo cacheado **no aportan** al ritmo actual
  (el redibujado ya está limitado por el *dirty flag* + caché de envolvente); quedan
  como opción si sube la carga. *(sysprof requiere GUI)*
- [x] **T5.2** Worker para filtro/envelope: **no necesario** al ritmo actual (el cómputo
  cabe en el presupuesto: 10.33 ms para 64 trazas). Se deja en reserva para cargas
  mayores; el fetch de `wave_serverV` (el bloqueo real) ya corre en worker (T1.5).
- [x] **T5.3** ADR del backend de render: `docs/adr-0001-render-backend.md`.

### Fase 6 — Migración GTK3→GTK4 + libadwaita
- [x] **T6.1** Build GTK4+libadwaita en paralelo (librería):
  `make -C ew_gui_tools/common GUI_TOOLKIT=gtk4` (GTK4 4.14.5 + libadwaita 1.5.0).
  GTK3 sigue siendo el default. Verificado: `check` y `bench` pasan en ambos toolkits.
- [x] **T6.2** `ewgui_view_gtk4.c` (GtkDrawingArea + `set_draw_func`; el `cairo_t` se
  integra en el snapshot de GTK4). Misma API (`ewgui_canvas_*`), los módulos no cambian.
  *(Optimización `GskPath`/`GtkGLArea` posterior; ver ADR-0001.)*
- [ ] **T6.3** Tablas a `GListModel` + `GtkColumnView`/`ListView`.
- [ ] **T6.4** `AdwApplicationWindow`+`AdwToolbarView`+`AdwHeaderBar`+`AdwStyleManager`.
- [ ] **T6.5** Retirar build GTK3.

**Progreso por módulo (port GTK4):**
- `csnrv` — ✅ portado a GTK4+libadwaita (`gtk_init()`, `gtk_window_set_child`,
  `gtk_box_append`, `GtkEventController*`/`GtkGestureClick`, `g_main_loop`, ventana con
  `close-request`; Makefile enlaza `../common/libewgui_gtk4.a`). Compila, `--test-mag`
  y golden OK.
- `csnstaevdisp` — ✅ portado a GTK4+libadwaita (menú `GtkMenuBar`→`GtkPopoverMenuBar`;
  diálogo `gtk_dialog_run`→`AdwMessageDialog` asíncrono; `GtkEventController*`;
  `g_main_loop`). Compila y `test-data` OK.
- `csntvp` — ✅ portado a GTK4+libadwaita (2 canvas; menú → `GtkPopoverMenuBar`;
  `gtk_dialog_run` → helper de diálogo modal síncrono (`run_dialog`); selector de color
  → `GtkColorDialog` asíncrono; combos → `GtkDropDown`; `GtkEventControllerKey` y
  `GtkGestureClick`; CSS con `load_from_string`/`add_provider_for_display`; `g_main_loop`).
  Compila y golden OK.
- `csnhypodbp` — ✅ portado a GTK4+libadwaita. Tabla `GtkTreeView`→**`GtkColumnView`**
  + `GtkSingleSelection` + `GtkSignalListItemFactory` (13 columnas); modelo
  `GtkListStore`→**`GListStore` de `CsnhypodbpRow`** (nuevo `csnhypodbp_row.[ch]`,
  GObject con propiedades); `cargar_sismos_iniciales`/`procesar_mensaje_sismo`/
  `procesar_mensaje_mag` reescritos sobre el nuevo modelo; combos→`GtkDropDown`;
  entries→`gtk_editable_*`; `GtkEventControllerKey`/`GtkGestureClick`; `g_main_loop`.
  Compila y `test`/golden OK.
- `csnhypodbp` (refactor UI/rendimiento): pestañas **"Eventos"** (lista) y **"Ondas"**
  (trazas) en un `GtkNotebook`; la de ondas se abre con **doble clic** en la lista (que
  también dispara el fetch). La **selección simple ya no dispara el fetch**. Buffers de
  traza **perezosos** (se crean al recibir datos, no para las 137 estaciones al arrancar)
  y **reset solo de las estaciones con datos**. Poll de recarga reducido a 1 s (solo
  reintento si hay un fetch en curso). *(Filtrado en worker: pendiente; el bench mostró
  que cabe en el presupuesto.)*
- `ew_controller` — ✅ portado a GTK4. `GtkNotebook` + `GtkPaned`; dos tablas
  `GtkTreeView`→**`GtkColumnView`** + `GListStore` de `EcModRow`/`EcRingRow` (nuevo
  `ew_controller_row.[ch]`); combos→`GtkDropDown`; `GtkMessageDialog`/`gtk_dialog_run`
  →diálogo modal síncrono (`confirmar`); editor de config sobre `GtkGrid`; `GtkTextView`
  para logs; `g_main_loop`. Compila y golden OK.

**Todos los módulos GUI están en GTK4.** Pendiente de la Fase 6: **T6.4** (ventanas
`AdwApplicationWindow`/`AdwToolbarView`/`AdwHeaderBar` + `AdwStyleManager` para el look
libadwaita) y **T6.5** (retirar el build GTK3: los módulos ya enlazan `libewgui_gtk4.a`,
`libewgui.a`/`ewgui_view_gtk3.c` quedan sin uso).

**Trampa GTK4 (GtkColumnView):** en GTK **4.14.5** (Ubuntu 24.04) `GtkColumnViewColumn`
**no** toma su propia referencia de la `GtkListItemFactory`. Si se hace `g_object_unref()`
de la factory tras `gtk_column_view_column_new()`, queda colgante y **segfaultea** al crear
las celdas. **No liberar la factory** (o conservar una referencia propia). Verificado con
repro aislado; `GtkListView` no tiene este problema.
- Infraestructura: la librería se construye por toolkit (`libewgui.a` GTK3 por defecto,
  `libewgui_gtk4.a` con `GUI_TOOLKIT=gtk4`); cada módulo enlaza la suya. Al terminar
  T6.5 se retira la GTK3.

---

## 8. Fase 6 — Brief de migración a GTK4 + libadwaita

**OBJETIVO:** portar los 5 módulos de GTK3 a **GTK4 + libadwaita** en C puro, con
look moderno (2026) y **sin regresión de rendimiento**. El port toca **solo** la capa
de vista/UI; `ewgui_dsp`/`wave`/`geo`/`ring` **no se modifican**.

**PRECONDICIONES (no empezar si falla):**
- Fases 0-5 completas: existe `libewgui.a`, todos pasan `make check`, y se cumple
  «GTK solo en `*_gui.c`/`*_view_gtk*.c`».
- Benchmark baseline (C6/C7) y golden de comportamiento (C5) capturados.

**SUELO DE VERSIONES:** GTK **≥ 4.14** (necesario para `GskPath`/`GskPathBuilder`) y
libadwaita **≥ 1.5**. Disponibilidad: Ubuntu 24.04 → 4.14/1.5; Debian 13 → 4.17/1.7;
Fedora 41 → 4.16/1.6.

**MAPEO DE API (gotchas):**
- `GtkWidget::draw` + `cairo_t` → `GtkWidgetClass.snapshot` /
  `gtk_drawing_area_set_draw_func` + `gtk_snapshot_append_cairo()`.
- `GtkMenuBar`/`GtkMenuItem` a mano → `GMenu` + `GSimpleAction` +
  `GtkPopoverMenuBar` (ya viene de `ewgui_actions`).
- `GtkTreeView`/`GtkListStore`/`GtkTreeViewColumn` → `GListModel`/`GListStore` +
  `GtkColumnView`/`GtkListView` + `GtkSingleSelection`.
- Signals `button-press-event`/`motion` → `GtkGestureClick`/`GtkEventControllerMotion`/
  `GtkEventControllerScroll`.
- `gtk_container_add`/`pack_start` → `set_child`/`append`; `expand`/`fill`/
  `GtkAlignment` → `hexpand`/`vexpand`/`halign`/`valign`.
- `gtk_main()` → `GtkApplication`; `GtkWidget::destroy` eliminado.

**DECISIÓN DE RENDER (crítica):**
- **NO** mantener `GtkDrawingArea`+cairo-por-frame en GTK4: se resube la textura CPU
  a GPU cada frame y es **más lento que en GTK3**. Prohibido.
- **Por defecto:** `set_draw_func` con dibujo cacheado en nodos GSK: `GskPath`/
  `GskPathBuilder` + `gtk_snapshot_append_stroke()`; cachear capas estáticas (fondo,
  grilla, eje) como `GskRenderNode` y reconstruir solo lo que cambia. **Un `GskPath`
  por traza, nunca un nodo por segmento.**
- **Escape (solo si el benchmark lo exige):** `GtkGLArea` (GL propio / NanoVG) para el
  canvas de trazas. La decisión se toma **con medición**.
- Conservar dirty flag y caché de envolvente.

**TOQUE MODERNO (libadwaita):**
- `AdwApplicationWindow` + `AdwToolbarView` + `AdwHeaderBar`.
- `AdwNavigationView`, `AdwTabView`/`AdwTabBar` (csnhypodbp/csntvp),
  `AdwStatusPage` (sin datos), `AdwToast` (feedback), `AdwBanner` (avisos).
- Configurador `ew_controller`: `AdwPreferencesWindow`/`AdwPreferencesPage`/
  `AdwPreferencesGroup`/`AdwEntryRow`/`AdwSwitchRow`/`AdwComboRow`.
- Responsive: `AdwBreakpoint` (Adw ≥ 1.4).
- Tema: `AdwStyleManager` para claro/oscuro; CSS propio vía `AdwApplication`
  (`style.css`, variables `--window-bg-color`, `--accent-bg-color`, …).
  **Prohibido `GTK_THEME`** (libadwaita fuerza `Adwaita-empty`).
- Colores configurables de traza de `csntvp`: CSS propio del canvas, no hardcodear.

**THREADING:** sin cambios respecto a GTK3 (§4). Nunca widgets desde worker;
`queue_draw`/`gtk_gl_area_queue_render` solo desde el hilo principal (marshalar).

**PRESUPUESTO Y VERIFICACIÓN:**
- Bench headless de envolvente (C6) igual o mejor que en GTK3.
- sysprof + GTK Inspector (`GTK_DEBUG=interactive`): ms/frame antes vs después.
- Diagnóstico de *fallbacks* con `GDK_DEBUG=fallback`; aislar backend con
  `GSK_RENDERER=ngl|vulkan|cairo|gl` (defaults: Vulkan en Wayland, NGL en el resto;
  hay regresiones conocidas según driver → documentar el elegido).
- Golden de render offscreen si es viable; si no, checklist visual por módulo + a11y
  básica.
- `git diff` de la Fase 6 **limitado a** `*_gui*.c`, `*_main.c`, `view_gtk*.c` y
  Makefiles (C8). Si toca `ewgui_dsp/wave/geo`, el refactor previo no se hizo bien.

**PROHIBIDO:** cairo-por-frame en `DrawingArea`; `GtkTreeView`/`GtkListStore`;
`gtk_main`; `GTK_THEME`; iconos por `GdkPixbuf` (usar `GtkImage`/GResource); tocar el
dominio desde el port.

**ORDEN SUGERIDO:** `csnrv` (más simple) → `ew_controller` → `csntvp` →
`csnhypodbp` → retirada del build GTK3.

---

## 9. Criterios de aceptación (1 test mínimo por criterio)

| # | Criterio | Test |
|---|---|---|
| C1 | `core/dsp/wave/geo` compilan y testean sin GTK | `common/tests/check_no_gtk.sh` + `make -C common check` |
| C2 | El filtro unificado reproduce los vectores actuales | portar `csntvp/test_filter.c` y `csnhypodbp/test_filter.c` |
| C3 | La envolvente es determinista y hace cache-hit | 2 corridas idénticas; 2.ª llamada `return 0` |
| C4 | Proyección/grilla correctas | puntos conocidos → píxeles; `grid_step` por tramos |
| C5 | Comportamiento de las GUIs sin cambios | golden (T0.2) + checklist manual |
| C6 | `N=64` trazas × `M=250` sps → envolvente < 16 ms (headless) | `make -C common bench` |
| C7 | Sin congelación con carga | latencia del main loop con worker < 50 ms |
| C8 | La migración GTK4 no toca dominio/modelo | `git diff` solo en vista/gui tras Fase 6 |

---

## 10. Evidencia del estado actual (para no re-explorar)

- GTK3 en los 5: `pkg-config gtk+-3.0` (csntvp/Makefile:4,6; csnhypodbp/Makefile:6,9;
  csnstaevdisp/Makefile:9,25; csnrv/Makefile:6,9; ew_controller/Makefile:6,9).
  Ningún `.ui`/GtkBuilder; widgets a mano; sin `GtkGLArea`.
- cairo en `GtkDrawingArea` vía signal `draw`: csntvp.c:1219/1316/1416/1420;
  csnhypodbp_gui.c:257 + main.c:249/253; csnstaevdisp.c:480/796; csnrv.c:467/756.
- Timers `g_timeout_add` (sin hilos): csntvp.c:1421-1422; csnhypodbp_main.c:321-323;
  csnstaevdisp.c:801-802; csnrv.c:761/764; ew_controller.c:1619-1621.
- `csntvp` dirty flag + caché: csntvp.c:971-979, 1005-1012, 1189-1205;
  `DEV_STATION` en csntvp.c:120-163.
- `csnhypodbp` modular (patrón): `csnhypodbp.h` (contrato), `_main.c`/`_gui.c`/
  `_ws.c`/`_dsp.c`; `_dsp.c` es DSP puro y `test_filter.c` lo testea headless.
- Tests actuales: csntvp/Makefile:21-25 (`test_filter`), csnhypodbp/Makefile:28-32
  (`test_filter`), csnrv/Makefile:25-27 (`test_config.sh` + `--test-mag`);
  csnstaevdisp solo `test-data`. Flags: `-Wall -O2 -g` (sin `-Wextra`).
- EarthWorm por C: `earthworm.h`, `transport.h`, `kom.h`, `trace_buf.h`,
  `ws_clientII.h`, `rw_mag.h`; enlace `libew_mt.a`, `ws_clientII.o`, `rw_mag.o`.

---

## 11. Detalles comunes tras el port GTK4 (WSLg, CSS y logs)

Correcciones de cierre posteriores al port, transversales o de paridad con GTK3. Los
detalles nuevos que aparezcan en despliegue se tratan en sesiones independientes
(depuración / funcionalidades nuevas de GTK4).

### 11.1 Redimensionado de ventanas y popovers (todos los módulos) — entorno

Los visores son `AdwApplicationWindow` con CSD (§12.2). Bajo **WSLg** la elección de backend
afecta a dos cosas a la vez:

- **x11 (XWayland):** los popovers (`GtkDropDown`; el menú ☰ ya no usa popup, §12.5) funcionan, pero la
  **zona de arrastre de la CSD no es alcanzable** (puente Weston→RDP/RAIL): la ventana **no se
  puede redimensionar** ni aparecen flechitas. `GTK_CSD=0`/SSD tampoco lo arregla: el WM dibuja
  el marco pero el arrastre sigue sin funcionar (verificado 2026-10-09).
- **Wayland:** el compositor decora la ventana y **el resize funciona**. Históricamente los
  popovers fallaban por el grab de `xdg_popup`
  ([wslg#1226](https://github.com/microsoft/wslg/issues/1226),
  [#1390](https://github.com/microsoft/wslg/issues/1390)), y por eso se forzaba x11.

**Estado actual:** en WSLg 1.0.73.2 (weston `04d436c`) los popovers **ya funcionan en Wayland**
(verificado 2026-10-09 con la misma ventana Adw + menú ☰) y Wayland da resize. Por eso
`ew8_gtk_env.sh` **ya no fuerza x11**: deja el backend por defecto (Wayland si hay
`WAYLAND_DISPLAY`, si no X11).

```bash
export GSK_RENDERER=cairo                 # WSL no inicializa GL/EGL
if [ -n "${EWGUI_FORCE_X11:-}" ]; then    # escape: WSLg viejo / popovers rotos
    export GDK_BACKEND=x11
else                                      # por defecto: Wayland (resize) o X11
    unset GDK_BACKEND
fi
```

Es la **fuente única** del entorno GTK4; `ew8_unix.sh` la carga con `source`: `GSK_RENDERER=cairo` y
`GDK_BACKEND` sin forzar (backend por defecto: Wayland con resize; `EWGUI_FORCE_X11=1` de escape).

> **Incidente 2026-10-08:** este bloque vivía **sólo** dentro de `ew8_unix.sh`; al duplicarlo o
> reescribir ese archivo se perdía en silencio (volvieron los avisos `libEGL`/`MESA`/`ZINK` y cambió
> la decoración). Por eso ahora vive en un archivo aparte: no puede divergir.

**Maximizar CIERRA la ventana si su mínimo no cabe (2026-10-13).** Wayland sigue siendo el backend
correcto (da resize), pero el compositor de WSLg negocia el maximizado con geometría propia
(`configure` con `0x0`/tamaño real) y GDK **aborta el proceso** (`_exit(1)`, no hay excepción que
capturar) cuando el buffer adjuntado no cuadra con el estado maximizado. En `csnhypodbp`:

```text
-> xdg_toplevel.set_maximized()
   xdg_toplevel.configure(1920, 1032, array[8])      # lo que concede el compositor
-> xdg_surface.ack_configure(298)
-> xdg_toplevel.set_min_size(1379, 1139)             # ¡el mínimo del módulo!
-> wl_surface.attach(buffer 1920x1139)               # más alto que el configurado
wl_display@1.error(xdg_wm_base, 4, "xdg_surface buffer (1920 x 1139) does not match the
configured maximized state (1920 x 1032)")
Gdk-Message: … Error 71 (Protocol error) dispatching to Wayland display.
```

La causa **del módulo** era acumular mínimo: `set_size_request` grandes (canvas de mapas de 300 px,
panel `Mww` con 170+260+200 px fijos) + `gtk_paned_set_shrink_*_child(..., FALSE)` en el paned de
mapas + las columnas de las tablas grandes fijando el mínimo del `GtkScrolledWindow`, y encima un
reparto diferido de divisores que se **re-aplicaba en cada `map`** (cambiaba el mínimo justo después
del `ack_configure`). Regla (§7 de `AGENTS.md`): el mínimo de un módulo GUI no puede superar la
pantalla — acotar con `gtk_scrolled_window_set_min_content_width`, dejar encoger los panes y aplicar
el reparto inicial **una sola vez** (flag por widget). Tras el arreglo: `set_min_size(167, 809)`,
cero `Error 71` y 5 ciclos de maximizar/des-maximizar verificados con
`make -C ew_gui_tools/csnhypodbp test-live` (hook `CSNHYPODBP_MAXIMIZE_MS`). Ver el bug abierto
[wslg#1428](https://github.com/microsoft/wslg/issues/1428).

### 11.2 Botones sin color de fondo (`ew_controller`, `csnhypodbp`)

El tema pinta un `background-image` **por encima** del `background-color` (en CSS el color de
fondo se dibuja *bajo* las capas de imagen; docs.gtk.org/gtk4/css-properties). El port GTK4
había perdido el `background-image: none;` que sí tenía el CSS de GTK3, así que `color` se
aplicaba (texto) pero el fondo quedaba tapado por el tema → botón gris con texto blanco
invisible.

**Fix:** en cada regla base de botón añadir `background-image: none; box-shadow: none; border: none;`
antes de `background-color` (`ew_controller_main.c`, `csnhypodbp_main.c`). Es la paridad con
GTK3; `csntvp` ya lo llevaba.

### 11.3 `LogDir` no resuelto (`ew_controller`) → "(LogDir not accessible)"

La prioridad documentada (`EW_LOG` > config > `"logs"`) **no estaba implementada**: `LogDir`
solo se cargaba de la config y `ew_controller.d` trae `#LogDir "logs"` comentado → `""` →
`opendir("")` fallaba y el visor mostraba `(LogDir not accessible: )`. **Fix:** `resolve_logdir()`
en `ew_controller_main.c` (invocada tras `ReadConfig`) aplica esa prioridad; como `ew8_unix.sh`
exporta `EW_LOG` y `ew_controller` lo hereda vía `startstop`, el visor encuentra los
`<base>_*.log`.

### 11.4 Limpieza: CSS muerto en `csntvp`

`csntvp_main.c` cargaba CSS para `#btn_filter`/`#btn_reload`, widgets que **no existen**: el
filtro y "reload default" son acciones GIO del menú "Control Panel". Se retiró el bloque
(quedaba huérfano desde el port).

### 11.5 Estado de la Fase 6

- **T6.4** ventanas libadwaita: **hecho** (sesión de cierre, ver §12.2). Los 5 módulos usan
  `AdwApplication` + `AdwToolbarView` + `AdwHeaderBar` + `AdwStyleManager` (modo oscuro).
- **T6.5** retirar el build GTK3 (`libewgui.a`/`ewgui_view_gtk3.c`): **pendiente**; `common check`
  todavía construye GTK3 para los tests headless.

---

## 12. Mejoras de cierre (export, AdwApplication y fluidez)

Trabajo posterior al port, sin cambiar de lenguaje ni añadir dependencias (libadwaita ya estaba
enlazada). Archivos nuevos: `common/include/ewgui/export.h`, `common/src/ewgui_export.c`,
`common/src/ewgui_export_ui_gtk4.c`, `common/tests/test_export.c`.

### 12.1 Export vectorial/ráster (todos los visores)

`EwGuiCanvas` se exporta reutilizando **exactamente** su callback de dibujo (`EwDrawFn`) sobre una
superficie cairo de archivo: la figura es fiel a la pantalla (el propio callback pinta el fondo).
Como el contrato de vista es único, el export sirve para los 4 canvas sin tocar su código.

- Núcleo **puro cairo** (sin GTK), testeable headless: `ewgui_export_render(fn, ud, path, fmt, w, h, scale)`
  y `ewgui_export_format_from_path(path)` (`.svg/.pdf/.png/.ps`).
- Conveniencia: `ewgui_canvas_export(canvas, path, fmt, w, h, scale)`.
- UI GTK4: `ewgui_export_dialog_run(canvas, parent)` (en `ewgui_export_ui_gtk4.c`, **solo build GTK4**;
  usa `GtkFileDialog`). Cada módulo lo expone como acción `win.export` en el menú del headerbar
  ("Export view…"). `ew_controller` no dibuja canvas → no exporta.
- Test headless: `common/tests/test_export.c` (E1–E5: SVG/PDF/PNG válidos, path inválido → `-1`,
  inferencia por extensión).

### 12.2 `AdwApplication` + headerbar + modo oscuro (5 módulos)

Se sustituyó el ciclo de vida `gtk_init()` + `GtkWindow` + `g_main_loop_run` por
**`adw_application_new(id, G_APPLICATION_NON_UNIQUE)` + `g_application_run`**, con la ventana
construida en el `activate`. El contrato CLI se mantiene (`./modulo <configfile.d>`); el
`configfile` se consume **antes** de crear la app (`g_application_run(app, 0, NULL)`).

- Chrome: `AdwToolbarView` como contenido + `AdwHeaderBar` como barra superior (patrón oficial);
  las acciones (antes `GtkPopoverMenuBar`/toolbar casera) pasan a un botón ☰ en el headerbar que
  abre una **ventana modal propia** (§12.5), no un `GtkMenuButton`/`GtkPopoverMenu`.
- **Modo oscuro**: acción stateful `win.dark-mode` → `adw_style_manager_set_color_scheme()`
  (`FORCE_DARK` / `DEFAULT`).
- `ew_background_tasks` (señal de terminación de Earthworm) usa `g_application_quit(g_app)` en vez de
  `g_main_loop_quit`. El `keep-alive` de los diálogos modales (`run_dialog`/`confirmar`, con bucle
  anidado) sigue funcionando.
- Único archivo por módulo (no se tocó el dominio): `*_main.c` (+ `csnhypodbp.h` por incluir
  `adwaita.h`/`ewgui/actions.h`).

### 12.3 Fluidez de `csnhypodbp` (costo por frame)

En `on_draw_signal` se eliminaron dos costos redundantes por repintado:

- Orden de estaciones por distancia: **burbuja O(n²)** → `qsort`.
- Auto-escalado por estación: se **fusionó** el pre-barrido de `max_abs` con el binning min/max por
  columna, de modo que hay **una sola pasada** sobre las muestras (antes dos). Los min/max por
  columna se guardan en buffers reutilizables (`MAX_DRAW_W`). El trazado es equivalente (la escala
  es positiva, no cambia el orden de min/max).

Nota: en GTK4 no hay repintado parcial público (`ewgui_canvas_queue_draw_rect` es un stub que
repinta el widget completo), así que la fluidez se gana bajando el costo por frame, no por regiones.

### 12.4 Fix: `csntvp/Makefile` no recompilaba

`csntvp/Makefile` **no tenía target `all`**: la primera regla era la de `libewgui_gtk4.a`, así que
el goal por defecto de `make` solo comprobaba la librería y **no construía `csntvp`** (`make clean &&
make` dejaba el binario ausente y, si ya existía, quedaba obsoleto). Se añadió `all: csntvp` como
primera regla.

### 12.5 Menú propio del headerbar (ventana modal, sin popup)

El botón ☰ de los 5 módulos era un `GtkMenuButton` con `gtk_menu_button_set_menu_model()`, que
crea internamente un `GtkPopoverMenu` (superficie `xdg_popup`). Bajo **WSLg/Wayland** ese popup se
usaba una vez, quedaba **detrás** de la ventana y dejaba la UI en un estado anómalo (misma familia
que `GtkDropDown`, bug #1299, §11.1). Se sustituyó por un **menú propio**:

- `common/include/ewgui/menu_ui.h` (GTK-free) + `common/src/ewgui_menu_ui_gtk4.c` (**solo build
  GTK4**, `MENU_UI_SRC` en `common/Makefile`):
  - `ewgui_menu_button_new(groups, n)` → `GtkButton` con icono `open-menu-symbolic` (clase `flat`).
  - `ewgui_menu_dialog_run(parent, groups, n)` → **ventana modal propia** (`gtk_window_new` +
    `transient_for` + `modal`, `gtk_window_present`, no bloqueante), con una sección por
    `EwMenuGroup` y un botón por `EwMenuItem`; `label==NULL` ⇒ separador (igual que
    `ewgui_menu_build`).
- Al pulsar un ítem se cierra la modal y se activa su acción en la ventana principal
  (`gtk_widget_activate_action`), con `target_int` cuando `target_fmt` no es NULL.
- Los módulos declaran sus `EwMenuItem`/`EwMenuGroup` como `static const` (el helper guarda el
  puntero) y montan el botón con `ewgui_menu_button_new`; ya **no** llaman a `ewgui_menu_build`
  (que se conserva para el build GTK3 y `test_actions.c`).
- Invariante: `common/tests/test_menu_ui.sh` (en `common check`) verifica que el header es
  GTK-free, que el helper no usa popup, que los 5 módulos no usan `GtkMenuButton` y que sus
  acciones `win.*` siguen presentes.

### 12.6 Tablas ordenables y refresco de filas en `GtkColumnView` (el `bind` es un snapshot)

El helper compartido de tablas ordenables (`common/include/ewgui/table_ui.h` +
`common/src/ewgui_table_ui_gtk4.c`, §T6.3) nació en `csnhypodbp`; ahora también lo usan las **dos
tablas de `ew_controller`** (*Modules* `Module/PID/Status/Details` y *Rings*
`Ring/Key/Size KB`). La cadena es `GListStore → GtkSortListModel → GtkSingleSelection/
GtkNoSelection → GtkColumnView`; cada columna se marca con `ewgui_table_column_sortable(col,
prop, "name", EWGUI_TABLE_ASC)` (desempate `name`: orden estable si el valor se repite) y al final
`ewgui_table_enable_sorting(view, sort_model, col_inicial, ASC)`, que es quien cuelga el sorter de
la vista (`gtk_column_view_get_sorter()`) del sort model: **sin eso las cabeceras no ordenan**.

**Por qué una fila "no se actualizaba" (y el estado parecía congelado).** El `bind` de
`GtkSignalListItemFactory` es un **snapshot**: se emite al enlazar el `GtkListItem` con un item
(`docs.gtk.org/gtk4/class.SignalListItemFactory.html`; `gtklistitemfactory.c`) y **no** vuelve a
ejecutarse cuando muta una propiedad del objeto fila; `GListModel` sólo reporta membresía
(añadir/quitar), no mutación de items. `GtkSortListModel` sólo reordena por `items-changed` o por
`GtkSorter::changed` (`gtksortlistmodel.c:1003/1174`), y `g_list_model_items_changed(pos,1,1)` con
el mismo item (o `(0,0)`) **no** están documentados como "repintar esta fila" (es un hack, en la
práctica fuerza unbind/rebind). Consecuencia medida: actualizar la fila in situ con
`g_object_set(row,"status",…)` **no se ve nunca** hasta que la fila se recicla al hacer scroll.

- Decisión: ante cualquier cambio visible se **reconstruyen las filas** (`ec_rebuild_rows`,
  `ew_controller_gui.c`). `ewgui_ctrl_sync_plan()` se conserva porque `EWCTRL_SYNC_NONE` (nada
  visible cambió) sigue evitando todo trabajo.
- Al reconstruir se pierde la identidad del item (`g_list_store_remove_all`), así que la selección
  se **restaura por nombre** de módulo (`g_sel_name`) con `g_row_sel_suppress` para que
  `notify::selected` no la ensucie. `GtkSingleSelection` sólo conserva la selección por identidad
  de puntero **dentro de la misma emisión** `items-changed`.
- **Trampa del sort model:** el índice de la vista deja de ser el del store (y el de
  `g_status.mods`). La fila seleccionada se resuelve con
  `gtk_single_selection_get_selected_item()` (no `g_list_model_get_item(g_store, idx)`) y la
  posición para re-seleccionar se busca dentro del **sort model** (`ec_view_pos_of_name`). Sin
  esto, `Stop`/`Restart` actuarían sobre el módulo equivocado y la re-selección marcaría otra fila.

**Trampas de `tport_getmsg` (Earthworm).** `GET_OK`, `GET_MISS` y `GET_NOTRACK` **ya copiaron el
mensaje** en el buffer del llamante (`earthworm_8.0/src/libsrc/unix/transport.c:844-865`; el salto
de secuencia se marca *después*, `:903`) y son el idioma habitual: hay que parsearlos y **seguir
drenando**, o un anillo con mensajes desincronizados se vacía a razón de un mensaje por tick.
`GET_NONE` (nada casó) y `GET_TOOBIG` (casó pero no hubo copia: `*length = hd.size`, `:832-834`)
**no** son un mensaje; por eso `recsize` se acota antes de `msg[recsize] = '\0'`.

**Estado "siempre `Alive`" con un módulo muerto.** `startstop` sólo emite `TYPE_STATUS` **bajo
demanda** (`startstop_unix_generic.c:424-425`) y detecta un hijo muerto de forma **perezosa**,
dentro de `EncodeStatus` (`waitpid(..., WNOHANG|__WALL)`, `:1579`): el primer status tras la muerte
dice `Zombie` (`:1577`) y el segundo `Dead` (`:1592-1601`). Si **nadie pide estado, nadie cosecha**:
un hijo muerto queda de zombi indefinidamente (observado al dejar un módulo muerto sin preguntar su
estado, `[ew_controller] <defunct>`). Además `startstop` **no autorreinicia** (eso es de `statmgr`, `statmgr.c:483-502`). Por
eso la GUI pinta `?` en `Status` cuando no hay respuesta durante `PollInt*4` s (`ec_set_noreply`)
en vez de mantener un `Alive` que ya no es cierto.

**Tests.** `make -C ew_gui_tools/ew_controller test` (invariantes de fuente,
`test_ui_invariants.sh`) y `make -C ew_gui_tools/ew_controller test-live`
(`tests/test_sort_and_status.sh`): contra el EarthWorm en marcha inyecta un `TYPE_STATUS` sintético
con `tests/fake_status` (nombres inventados `AA_probe`/`ZZ_probe`, fixtures en `tests/fixtures/`) y
comprueba que **un cambio de estado se ve en la celda** y que el orden inicial es el del sorter.
Las cabeceras de `GtkColumnView` no exponen acción AT-SPI (bug #8354) y el clic por coordenadas
depende del entorno (bajo Xvfb sin gestor de ventanas puede no entregarse), así que ese paso es
*best-effort* con aviso: el enganche del sorter lo cubren los invariantes y el orden inicial.

### 12.7 Layout: mínimos, divisores y columnas (`ew_controller`)

Las dos tablas se veían "cortadas" y no se podía repartir el espacio: **cinco** causas, todas de
layout y todas medidas en el arnés GUI (`--dump` / `--rect`):

| síntoma medido | causa | arreglo |
|---|---|---|
| ventana clavada en **1051×720** pese a `default_width` 980 | el mínimo de un `GtkLabel` es **su texto completo**: la línea de estado (`g_lbl_header`) lo fijaba, y el `GtkBox` vertical toma el máximo de los mínimos de sus hijos | `gtk_label_set_ellipsize(END/MIDDLE)` en las etiquetas de estado |
| ídem (antes de eso) | `gtk_widget_set_size_request(sw_mod,620,300)` y `(sw_rings,340,120)`: fija el **mínimo** ("leaves the window at least as large") | fuera; el reparto lo hace el divisor |
| el divisor no se podía arrastrar hacia dentro | `shrink_*=FALSE` ("Setting shrink to false allows the application to set a minimum size"; el default es **TRUE**) | `shrink_*=TRUE` |
| **221 px** en blanco a la derecha de la última columna (`Details` 204 px de ancho en una tabla de 670) | sin ninguna columna con `expand`, el ancho sobrante de la vista **no se reparte** (el reparto "a medias" solo existe *entre* columnas `expand`) | `set_expand(TRUE)` en las columnas de texto variable, `set_fixed_width()` en las cortas, todas `set_resizable(TRUE)` |
| tabla de Anillos clavada en **120 px** y sin nada que arrastrar entre ella y los logs | Rings y Logs eran hijos de un `GtkBox` (sin divisor) | `GtkPaned VERTICAL` (Anillos arriba / Logs abajo) |
| el texto largo del contenido (tabla o logs) seguía empujando el mínimo | un `GtkScrolledWindow` deriva su mínimo del contenido (`min-content-width` por defecto) | `gtk_scrolled_window_set_min_content_width()` en los tres |

Después: ventana **980×720** (= su default, y encogible), hueco a la derecha de la tabla **9 px**,
panel de Anillos **264 px** de alto. Detalles que no son evidentes:

- **Reparto inicial una sola vez, al mapear**: `gtk_paned_set_position` no está documentado antes de
  mapear, así que `ec_split_h`/`ec_split_v` se cuelgan de `"map"` y reintentan con `g_timeout_add(50)`
  mientras `gtk_widget_get_width/height()` sea 0. El flag por widget (`g_object_set_data`) evita
  **pisar el reparto del usuario** cuando la página se vuelve a mapear (el `GtkNotebook` desmapea la
  página al ir a *Configuration* y la remapea al volver, reemitiendo `"map"`).
- **Crecimiento**: `resize_start_child=TRUE` / `resize_end_child=FALSE` en el horizontal (el ancho
  extra va a la tabla) y `resize_start_child=FALSE` / `resize_end_child=TRUE` en el vertical (el alto
  extra va a los logs). Con `resize_start_child=FALSE` el alto inicial de los anillos lo fija la
  **posición** (la requisición de una tabla vacía los dejaría en nada).
- Los tres `GtkScrolledWindow` mantienen `AUTOMATIC` en horizontal: si la ventana es más estrecha que
  las columnas aparece scroll, **nunca** recorte.
- Tests: invariantes de fuente en `test_ui_invariants.sh` (estas propiedades, con `ckn` para exigir
  que las **dos** tablas las cumplan) y el modo `--layout` de `tests/test_sort_and_status.sh`, que mide
  el hueco a la derecha de la última columna y el alto del panel de Anillos. Los divisores en sí no se
  pueden automatizar (el árbol no expone el rol `separator` y el clic por coordenadas no es fiable
  sin gestor de ventanas): arrastrar los dos divisores y encoger la ventana es verificación manual.
