# csnhypodbpQT — Port de csnhypodbp a Qt Widgets: análisis, especificación y plan

**Fecha:** 2026-10-22 · **Autor:** agente `build` (especificación producida como agente `plan`)
**Estado:** documento de trabajo / handoff. **No se ha escrito ni una línea de código del prototipo.**
**Ámbito:** prototipo en una carpeta nueva `ew_gui_tools/csnhypodbpQT/`, sin modificar nada del código
existente, con el objetivo de llegar a algo funcional que se integre completamente en el sistema.

> Nota: este documento nació como `docs/testLibQT.d` (por eso algunos comentarios del código lo
> citan con esa extensión); vive en `docs/` como markdown y ningún módulo lo lee.

---

## Índice

1. [Resumen ejecutivo e impresiones](#1-resumen-ejecutivo-e-impresiones)
2. [Decisiones cerradas con el usuario](#2-decisiones-cerradas-con-el-usuario)
3. [Diagnóstico del toolchain (evidencia)](#3-diagnóstico-del-toolchain-evidencia)
4. [Análisis del código existente: qué hay, qué se puede reutilizar](#4-análisis-del-código-existente)
5. [Trampas del repo que el port debe respetar](#5-trampas-del-repo-que-el-port-debe-respetar)
6. [Integración sin tocar el código existente](#6-integración-sin-tocar-el-código-existente)
7. [Arquitectura propuesta](#7-arquitectura-propuesta)
8. [Contrato de comportamiento (CLI, config, E/S, concurrencia, errores)](#8-contrato-de-comportamiento)
9. [Interfaces y firmas (contrato primero)](#9-interfaces-y-firmas-contrato-primero)
10. [Contrato del Makefile](#10-contrato-del-makefile)
11. [Criterios de aceptación (AC1..AC18)](#11-criterios-de-aceptación)
12. [Plan por fases (0..6)](#12-plan-por-fases)
13. [Riesgos y mitigaciones](#13-riesgos-y-mitigaciones)
14. [Equivalencias GTK4 → Qt Widgets (chuleta)](#14-equivalencias-gtk4--qt-widgets-chuleta)
15. [Archivos existentes que no se tocan (y los 3 que sí, con permiso)](#15-archivos-existentes)
16. [Anexo: inventario de fuentes de csnhypodbp](#16-anexo-inventario-de-fuentes-de-csnhypodbp)

---

## 1. Resumen ejecutivo e impresiones

**La buena noticia: el port es viable y mucho más barato de lo que parece**, porque `csnhypodbp` ya está
partido en dos: de sus 34 fuentes, **solo 4 tocan GTK** y 30 son C puro testeable headless. Toda la
inteligencia del módulo (asociación, agrupación por firma de picadas, búsqueda, localización, cadena
offline del `locate`, magnitudes, perfiles, geometría de mapa, filtros, STA/LTA, estado manual) vive en
archivos que **no incluyen `gtk/gtk.h`** y que ya tienen 33 `test_*.c` escritos. Un port a Qt no
reimplementa esa lógica: la **reutiliza compilándola in situ**.

**La mala noticia: el binario no compila hoy.** No hay headers ni `moc` de Qt en la máquina (solo runtime
Qt5 suelto, sin `-dev`). Es un prerequisito bloqueante: `sudo apt install qt6-base-dev
qt6-base-dev-tools libxcb-cursor0` (decisión tomada: **Qt 6**).

**Dónde está el trabajo real.** No está en la lógica (se reutiliza) ni en las tablas (Qt las resuelve
mejor que GTK4: `QAbstractTableModel` + `QSortFilterProxyModel` sustituyen limpiamente el trío
`GListStore`/`GtkSortListModel`/`GtkSingleSelection` y sus trampas de *snapshot*). Está en tres sitios:

1. **El dibujo.** Los callbacks cairo reales viven dentro de `csnhypodbp_gui.c` (solo `on_draw_signal`
   son 315 líneas, `:4043-4357`) mezclados con globales GTK. Hay que **reexpresarlos** como funciones C
   con contexto explícito. Con la decisión "cairo sobre `QImage`" el código cairo se traslada casi
   literal (misma matemática, misma apariencia) y además queda reutilizable para export SVG/PDF/PNG.
2. **La capa de datos/glue.** `csnhypodbp_ws.c` (1810 l.) y `csnhypodbp_main.c` (2582 l.) son GTK, aunque
   buena parte de su contenido sea lógica de datos (drena de anillos, carga desde BD, fetch de ondas en
   hilos, emisión de picks/magnitudes/ARC confirmado). Eso se reescribe en C nuevo (`c/bpq_*.c`) apoyado
   en los núcleos puros. Es trabajo mecánico pero voluminoso.
3. **La configuración.** `ReadConfig()` (`csnhypodbp_main.c:321-560`) rellena ~100 globales declarados en
   `csnhypodbp.h` (header con GTK). Hay que reescribir el parseo contra un struct nuevo `BpqConfig`. Se
   puede apoyar en `ewgui_config_load` (esquema genérico por `EwKeySpec`, `common/include/ewgui/config.h:31`)
   para las escalares y un pase `kom` para las claves repetidas.

**Impresión sobre Qt vs GTK4 en este proyecto.** Qt resuelve *mejor* varias de las trampas documentadas
del entorno: los popups son ventanas nativas (`QMenu`, `QFileDialog`) y no superficies `xdg_popup`, así
que no reproduce el bug WSLg #1299 que obligó a sustituir `GtkDropDown`/`GtkMenuButton` por ventanas
modales propias; no existe el equivalente al *snapshot* del `bind` de `GtkSignalListItemFactory`; y el
`Error 71` de maximizar (mínimo mayor que el alto concedido por el compositor) es específico del camino
Wayland de GDK. A cambio, Qt trae su propio peaje: toolchain C++17, `moc`, y un conjunto de dependencias
distinto (Qt6 no usa `pkg-config` de forma garantizada; el descubrimiento fiable es `qmake6 -query`).

**Impresión sobre el riesgo estratégico.** Mantener dos GUI para el mismo módulo tiene un coste real de
duplicación de la capa de presentación (~6.000 líneas GTK frente a lo que se escriba en Qt). El valor del
prototipo es **decidir con evidencia** si Qt conviene, y para eso basta con llegar a la fase 4 (picking
operativo) comparando lado a lado con la versión GTK. Llegar a la fase 6 (paridad total) solo tiene
sentido si la decisión ya es migrar. Recomendación: **corte natural al final de la fase 4**, evaluar, y
recién entonces decidir si se completa la 5 y la 6.

**Coste estimado (orden de magnitud, no compromiso):** fase 0-1 ≈ trabajo de andamio y fontanería C;
fase 2 ≈ la primera GUI útil (eventos + mapas); fases 3-4 ≈ el picking, que es el corazón operativo;
fases 5-6 ≈ locate/confirm/magnitudes/Mww, que es donde más lógica GTK hay que reexpresar. En líneas: el
prototipo debería quedar en el orden de 1.500-2.500 líneas de C (`c/`) + 2.500-4.000 de C++ (`ui/`),
frente a las ~10.000 líneas GTK actuales de `gui.c`+`main.c`+`ws.c`+`mww.c`.

---

## 2. Decisiones cerradas con el usuario

| # | decisión | valor elegido | consecuencia |
|---|---|---|---|
| D1 | Toolchain Qt | **Qt 6** (`qt6-base-dev`, `qt6-base-dev-tools`, `libxcb-cursor0`) | C++17 obligatorio; Qt 5.15 está fuera de soporte OSS desde 2025-05-26 |
| D2 | Reutilización del código C | **in situ**: el build compila los `.c` puros de `../csnhypodbp/` y el subconjunto puro de `../common/src/`; no se copia ni se modifica nada | cero duplicación y cero deriva; a cambio el prototipo depende de archivos ajenos (mitigado por AC3/AC4) |
| D3 | Backend de dibujo | **cairo sobre `QImage`** (`CAIRO_FORMAT_ARGB32` ↔ `QImage::Format_ARGB32_Premultiplied`) | paridad de píxel con la versión GTK, reutilización de `ewgui_overlay_draw`, y export SVG/PDF/PNG casi gratis; coste: dependencia de cairo (ya instalada) |
| D4 | Nombre | carpeta `ew_gui_tools/csnhypodbpQT/`, binario `csnhypodbpQT` | coincide con `ew8_compile.sh` (el nombre de carpeta es el nombre de módulo) |
| D5 | Infraestructura Qt compartida | **`ew_gui_tools/commonqt/` → `libewguiqt.a`** (hermana de `common/`, que sigue siendo GTK): boot, tabla+proxy, selector modal, confirmación, tema, visor de log y canvas BGRA | un solo sitio para lo que repiten los módulos Qt y para la semántica de orden del GTK; no enlaza GTK/cairo/EW. Ver `docs/QT-COMMON.md` |

**Decisiones tomadas en la fase 6.5** (integración, con permiso explícito):

- Id de módulo propio: `Module MOD_CSNHYPODBPQT 172` en `earthworm.d.base` (hecho).
- `Process "csnhypodbpQT csnhypodbpqt.d"` **activo** en `startstop_unix.d` + `.d` propio `params/csnhypodbpqt.d` (hecho; coexiste con el GTK 158).
- `.gitignore`: runtime `/run_working_v8/csnhypodbpqt/` (el binario instalado ya estaba en §3b) (hecho).
- `common/tests/test_params_stays_clean.sh` §6 ampliado a la carpeta runtime nueva (hecho).

---

## 3. Diagnóstico del toolchain (evidencia)

Comprobado con `ls` sobre rutas absolutas (no se instaló nada):

| qué | rutas probadas | resultado |
|---|---|---|
| Qt6 | `/usr/lib/x86_64-linux-gnu/cmake/Qt*`, `/usr/include/*/qt6`, `/usr/lib/qt6`, `libQt6Core.so.6`, `/usr/bin/qmake6`, `/opt/Qt`, `~/Qt` | **NO existe** |
| Qt5 runtime | `libQt5Core.so.5`, `libQt5Gui.so.5`, `libQt5Widgets.so.5` | **sí existe** (`/usr/lib/x86_64-linux-gnu/`) |
| Qt5 dev | `/usr/include/*/qt5`, `libQt5Core.so`, `Qt5Core.pc`, `Qt5Widgets.pc`, `/usr/lib/qt5`, `qt5/bin`, `qt5/mkspecs` | **NO existe**; solo `/usr/lib/x86_64-linux-gnu/qt5/{plugins,qml}` |
| `qmake`/`moc` | `/usr/bin/qmake`, `/usr/bin/moc`, `/usr/lib/qt6/bin/moc`, `/usr/lib/qt5/bin/moc` | **NO existe** |
| Compiladores/herramientas | `/usr/bin/{gcc,g++,x86_64-linux-gnu-g++,make,pkg-config,cmake}` | **sí existen** todos |
| cairo dev | `/usr/include/cairo/cairo.h`, `/usr/lib/x86_64-linux-gnu/pkgconfig/cairo.pc` | **sí existe** |
| GTK4 dev | `/usr/include/gtk-4.0/gtk/gtk.h` | **sí existe** |
| libpq dev | `/usr/include/postgresql/libpq-fe.h` | **sí existe** |
| libmseed2 header | `/usr/include/libmseed2.h` | **no** (viene de `ew_8.0/include/libmseed2.h`, cubierto por `-I$(EW_HOME)/$(EW_VERSION)/include`) |

**Conclusión:** el prerequisito bloqueante es instalar el toolchain Qt6. Sin él, `make all` del prototipo
no puede funcionar; por eso el Makefile debe **fallar rápido con un mensaje que nombre los paquetes**
(§10), porque `ew8_compile.sh` solo tiene guarda para gtk4/libadwaita (`ew8_compile.sh:639-642`) y no
sabe nada de Qt.

**Datos de versiones (fuente: paquetes de Ubuntu, verificados por `@scout`):**

- `qt6-base-dev`: jammy 22.04 → 6.2.4; noble 24.04 → 6.4.2; questing 25.10 → 6.9.2. En noble el runtime
  se llama `libqt6widgets6t64` (transición time_t 64-bit).
- `qt6-base-dev-tools` trae `moc`, `uic`, `rcc`.
- Qt 6.5+ exige `libxcb-cursor.so.0` (`libxcb-cursor0`) para cargar el plugin `xcb`; sin él: `Could not
  load the Qt platform plugin "xcb"`.
- Qt 5.15: soporte estándar terminado 2025-05-26; en Ubuntu sigue empaquetado (`qtbase5-dev` 5.15.13 en
  noble) como backport de la distro.
- Qt6 **no** usa `pkg-config` como vía soportada; los `.pc` de Qt6 no están garantizados en todas las
  distros. Vía fiable: `qmake6 -query` (`QT_INSTALL_HEADERS`, `QT_INSTALL_LIBS`, `QT_INSTALL_ARCHDATA`,
  `QT_HOST_LIBEXECS` para `moc`). En Ubuntu `qt6-base-dev` sí instala `Qt6Widgets.pc`, así que se usa
  `pkg-config` con **fallback** a `qmake6 -query`.

**Licencias (resumen operativo, no asesoría legal).** Qt OSS es dual LGPLv3/GPLv3. Con **enlazado
dinámico** (el caso aquí: `libQt6Widgets.so` del sistema) la aplicación propia puede mantener su licencia
y las obligaciones LGPL se limitan esencialmente a: aviso de uso de Qt + copia de la licencia, permitir
re-link (sustituir la `.so`), y no añadir restricciones de dispositivo cerrado. Uso **interno sin
distribuir el binario** apenas activa obligaciones; si algún día se distribuye a terceros hay que
revisarlo. Qt comercial es suscripción por desarrollador (orden de magnitud: miles de USD/año;
revalidar precio el día). Módulos solo-GPL en OSS (no usados aquí): Quick 3D, gRPC, MQTT, Wayland
Compositor, Virtual Keyboard, etc.

---

## 4. Análisis del código existente

### 4.1 Separación puro/GTK en `csnhypodbp` (verificado)

**Con GTK (4 fuentes + 2 headers):**

| archivo | líneas | por qué es GTK |
|---|---|---|
| `csnhypodbp_gui.c` | 4478 | `#include "csnhypodbp.h"` (:1), `csnhypodbp_mww.h` (:3), `<adwaita.h>` (:6), `ewgui/stalta.h` (:5) |
| `csnhypodbp_main.c` | 2582 | `csnhypodbp.h` (:3), `ewgui/table_ui.h` (:6); contiene `main()`, `ReadConfig()`, `ConnectToEarthworm()`, la construcción de toda la UI y los `g_timeout_add` |
| `csnhypodbp_ws.c` | 1810 | `csnhypodbp.h` (:1), `ewgui/db.h` (:8), `ewgui/db_ids.h` (:9); contiene el drena, la carga desde BD, los hilos de fetch y la emisión a anillos |
| `csnhypodbp_mww.c` | 1161 | `<gtk/gtk.h>` directo (:19), `csnhypodbp.h` (:26), `ewgui/view.h` (:33) |
| `csnhypodbp.h` | 496 | `<gtk/gtk.h>` (:4); además declara ~100 `extern g_*` (`:90-213`) definidos en `main.c:13-266` |
| `csnhypodbp_mww.h` | 54 | `<gtk/gtk.h>` (:4) |

**Puros (30 fuentes + 28 headers), reutilizables in situ:**
`filtercfg`(557), `fixdepth`(79), `group`(467), `locate`(1341), `locator`(47), `magcfg`(449),
`magchan`(68), `magitem`(178), `magmarks`(240), `magrow`(205), `magsta`(68), `magview`(191),
`manual`(54), `mapview`(531), `master`(89), `mwwerr`(127), `mww_over`(218), `preview`(185),
`profile`(354), `row`(159), `sds`(161), `search`(193), `staitem`(140), `statable`(256), `station`(123),
`trace`(285), `traces`(39), `tt`(50), `view`(332), `locate_chain`(144, arnés).

**Ninguno de los 30 `.c` puros incluye `csnhypodbp.h`** (verificado por grep) y **ninguno llama a
funciones definidas solo en los 4 archivos GTK** (solo hay menciones en comentarios: `csnhypodbp_locate.h:61`,
`csnhypodbp_magrow.c:79`, `csnhypodbp_mww.c:853`). Esto es lo que hace posible la reutilización in situ.

**Sub-clasificación importante dentro de los "puros":**

- **Puros sin nada** (solo libc/`-lm`): `group, search, statable, preview, profile, mapview, view, trace,
  traces, station, filtercfg, locate, locator, fixdepth, magmarks, magcfg, magrow, magview, magsta,
  manual, tt, mwwerr`.
- **Puros con GObject/glib** (`<glib-object.h>`): `row.c` (`G_DEFINE_TYPE` :19), `staitem.c` (:13),
  `magitem.c` (:25), `master.c` (`GListStore`, :21). **Se excluyen del port**: son los modelos de fila de
  `GtkColumnView`; en Qt los sustituyen `QAbstractTableModel`.
- **Puros con headers EarthWorm**: `magchan.h:6` incluye `<rw_mag.h>`; `locate.c:17` y `sds.c:17` incluyen
  `<trace_buf.h>`; `sds.c:16` incluye `<libmseed2.h>`; `tt.c:9` incluye `csnloc.h`; `mww_over.h:4` incluye
  `csnwphase.h`. ⇒ **No includables desde C++** (regla §7.1); se consumen desde la capa C.
- **Puros con cairo**: ninguno de los headers puros de `csnhypodbp` declara `cairo_t*` (verificado en los 9
  headers de cálculo). El cairo de `csnhypodbp` está solo en `gui.c`/`mww.c`.

**Únicos `extern` de los `.c` puros** (verificado: no hay ninguno más):

```c
/* csnhypodbp_sds.c:23-24, comentario "Definidas en csnhypodbp_main.c (config del modulo)." */
extern int  g_sds_enabled;   /* definido en csnhypodbp_main.c:264 = 1 */
extern char g_sds_root[512]; /* definido en csnhypodbp_main.c:265 = "/mnt/sds/archive" */
```

⇒ La capa C del prototipo **debe definir ambos símbolos** (los define `bpq_config.c` al cargar el `.d`).
`test_sds_read.c:24-25` ya hace exactamente esto como stub, lo que confirma el patrón.

### 4.2 Capas de `common` (verificado)

**Con GTK (5 fuentes, la capa de vista):** `ewgui_view_gtk4.c` (`:8` vía `view.h:5`),
`ewgui_export_ui_gtk4.c` (:6), `ewgui_overlay_ui_gtk4.c` (:8), `ewgui_menu_ui_gtk4.c` (:9),
`ewgui_table_ui_gtk4.c` (:19).

**Sin GTK (reutilizables):** `ewgui_wave.c` (`-lm`), `ewgui_dsp.c` (`-lm`), `ewgui_geo.c` (`-lm`),
`ewgui_sds.c` (libc+`m`), `ewgui_sta.c` (libc), `ewgui_config.c` (**`kom.h` ⇒ EarthWorm `libew_mt.a`**),
`ewgui_cfgfile.c` (libc), `ewgui_db.c` (**libpq**, `-lm`), `ewgui_db_ids.c` (libc), `ewgui_ring.c`
(**`earthworm.h`+`transport.h` ⇒ `libew_mt.a`, `pthread`**), `ewgui_stalta.c` (libc),
`ewgui_table_cmp.c` (libc), `ewgui_overlay.c` (**cairo**, `:297`), `ewgui_version.c` (libc),
`ewgui_ctrl.c` (`transport.h`), `ewgui_actions.c` (**`gio/gio.h`**).

**La única dependencia cruzada puro→GTK** (verificada, es lo que impide reutilizar el export):

```
common/src/ewgui_export.c:6    #include "ewgui/export.h"
common/include/ewgui/export.h:6 #include "ewgui/view.h"
common/include/ewgui/view.h:4   #include <cairo.h>
common/include/ewgui/view.h:5   #include <gtk/gtk.h>          ← contagio
common/src/ewgui_export.c:107   ewgui_canvas_get_draw(...)    ← definido SOLO en ewgui_view_gtk4.c:56
common/src/ewgui_export.c:111   ewgui_canvas_get_size(...)    ← definido SOLO en ewgui_view_gtk4.c:76
```

⇒ **`ewgui_export.c` no se puede compilar sin GTK.** El export SVG/PDF/PNG se reimplementa en
`c/bpq_export.c` (~40 líneas sobre `cairo-svg/pdf/image-surface`) replicando el contrato de
`ewgui_export_render` (`export.h:40`: `fn(NULL, cr, width, height, user_data)`, ver `ewgui_export.c:78`).
En la dirección opuesta no hay problema: `ewgui_table_cmp.c:46` define `ewgui_table_cmp_values` y la
consume `ewgui_table_ui_gtk4.c:73` (puro→GTK no, GTK→puro sí).

**Invariante existente que protege esta separación:** `common/tests/check_no_gtk.sh:12-22` (falla si una
capa pura incluye `<gtk/gtk.h>`; exime `*gtk*` y `actions.h`/`view.h`, `:16`). Nota: `export.h` **no**
está exento aunque incluye `view.h` — el script solo mira `#include <gtk/gtk.h>` literal (`:18`), así que
hoy pasa de chiripa. Corrido desde `common/Makefile:58-60`.

**Ningún header de `common/include/ewgui/*.h` tiene guarda `#ifdef __cplusplus / extern "C"`** (verificado
en los 21 headers). Tampoco los de `csnhypodbp`. ⇒ Un consumidor C++ **debe** envolverlos, que es
exactamente lo que hace `bpq_capi.h` (§7.1, §9.2).

**`libewgui_gtk4.a` es 100 % C** (`common/Makefile:10-11`: `CC ?= gcc`, `AR ?= ar`; no hay objetos C++).
Enlazarla desde `g++` es técnicamente posible pero **arrastraría GTK4+libadwaita al proceso Qt**, que es
lo que se quiere evitar: el port compila directamente los `.c` puros que necesita y **no enlaza** esa `.a`.

### 4.3 Contrato runtime actual (verificado)

**CLI y orden de parseo** (`csnhypodbp_main.c:2521-2574`):

```
:2521 int main(int argc, char *argv[])
:2523 if (argc==3 && --print-config)  { ReadConfig(argv[2]); return maps_print_config(); }
:2530 if (argc==3 && --check-profiles){ ReadConfig(argv[2]); return maps_check_profiles(); }
:2537 if (argc!=2) { Uso <configfile.d>; exit(1); }
:2538 ReadConfig(argv[1]);          ← antes de GTK, log, BD y anillos
:2514 getenv("CSNHYPODBP_MAXIMIZE_MS") en dbg_maximize_hook()  ← única env de test
:2483 dbg_maximize_hook() se llama en on_activate, no en main
:2571 adw_application_new(...)      ← GTK se crea DESPUÉS de los flags headless
```

**Anillos** (`.d` en `run_working_v8/params/csnhypodbp.d:5-10`, keys en `earthworm.d.base:36-51`, módulos
en `earthworm.d.base:228-245`):

| clave del `.d` | anillo | key | dirección | tipo de mensaje |
|---|---|---|---|---|
| `InRing` | `HYPO_RING_REF` | 1016 | lectura | `TYPE_HYP2000ARC` (mod 163 hyp2000 / 164 nlloc), `TYPE_MAGNITUDE` |
| `InRingRaw` | `HYPO_RING` | 1015 | lectura | `TYPE_HYP2000ARC` (mod 162 csnloc) |
| `InRingPick` | `PICK_RING` | 1005 | lectura | `TYPE_PICK_SCNL` (captura) |
| `OutRing` | `PICK_RING` | 1005 | escritura | `TYPE_PICK_SCNL` manuales |
| `ConfRing` | `HYPO_RING_REF2` | 1017 | escritura | `TYPE_HYP2000ARC` confirmado |
| (implícito) | `InRegion` = `HYPO_RING_REF` | 1016 | escritura | `TYPE_MAGNITUDE` `CSNMan` (`ws.c:1700`) |

**Mecánica del drena** (`main.c:765-857`): `GetType(...)` para los 5 tipos, `tport_attach(&InRegion)` +
`tport_attach(&PRegion)` (**tport directo**, no `ewgui_ring_attach`), `tport_getmsg(region, GetLogo, 2, …)`
(:806) y `tport_getmsg(region, PickLogo, 1, …)` (:828); de `common` solo se usan
`ewgui_heartbeat_due` (:854) y `ewgui_ring_should_quit` (:857).

**Bucle** (`main.c:2485-2487`): `g_timeout_add(1000, ew_background_tasks, NULL)` (heartbeat + quit,
`:851-859`), `g_timeout_add(500, escuchar_anillo_earthworm, tree_global)`, `g_timeout_add(1000,
waveform_reload_timer, NULL)` (debounce de fetch, `gui.c:3660`). Además `gui.c:2370` (`locate_tick`, 1 s)
y `mww.c:826` (`mww_tick`, 1 s).

**Emisión a anillos** (todo en `ws.c`, todo `static` salvo una):

```
:1679 static void emit_mag_scnl(int imagtype, const char *type, const char *txt, int qid, int mod)
:1688   MAG_INFO m; wr_mag_scnl(&m, buf);   algorithm="CSNMan", qid "%010d", imagtype 1/5/3/4
:1700   logo.instid=MyInstId; logo.mod=MyModId; logo.type=TypeMagnitude; tport_putmsg(&InRegion,…)
:1705 static void emit_pick_scnl(sta, chan, net, loc, char phase, double t, char pref)
:1728   formato literal: "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 %c M %c\n"
:1732   logo.mod=MyModId; logo.type=TypePickSCNL; tport_putmsg(&PRegion,…)
:1737 int csnhypodbp_confirm_manual(const CsnhypodbpSolution *sol, const CsnhypodbpMagOut *mag, int pref_kind)  ← única pública
:1771   logo.mod=(unsigned char)sol->mod; logo.type=TypeHyp2000Arc; tport_putmsg(&CRegion,…)
```

**Detalle clave descubierto:** el `Confirm` usa `logo.mod = sol->mod` (el módulo de **la solución**, no el
de la GUI). Eso hace que el `origin_id` que construye `hypodb` (`hypodb_ingest.c:583-584` →
`ewgui_origin_id_build`, `common/src/ewgui_db_ids.c:48-63`: `"<event>:<mod>:<version>"`) sea **idéntico**
venga de la GUI GTK o de la Qt ⇒ dos GUIs confirmando el mismo evento+versión producen upsert/overwrite
de la misma fila (`hypodb_ingest.c:614`, `:642`), sin error y sin duplicados. Buena noticia para el
prototipo; mala si algún día se quiere distinguir el origen de la confirmación.

**Reutilizable para la emisión (PURO, en `csnhypodbp_locate.h`):**

```
:37 int csnhypodbp_picks_write(const char *path,int type,int mod,int inst,
                               const CsnhypodbpSolution *base,const CsnhypodbpManualPick *mp,int nmp);
:54 int csnhypodbp_trace_write(const char *path, const CsnhypodbpTrace *tr, int n);
:62 int csnhypodbp_arc_build(const CsnhypodbpSolution *sol,int manual,char *out,size_t outn);  /* ARC Y2K 197+114*n */
:68 int csnhypodbp_confirmation_arc(const CsnhypodbpSolution *sol,char *out,size_t outn);      /* exige ver 1..9999 y qid>0 */
```

`csnhypodbp_arc_build` ya escribe el byte manual (`locate.c:544`: `hdr[EWGUI_ARC_MANUAL_OFF]='M'/'A'`,
qid en `[136,10]`, versión en `[178,4]`). ⇒ **El ARC de confirmación se reutiliza tal cual**; solo hay que
reescribir el `tport_putmsg` y los dos formatos de texto (`emit_pick_scnl`, `emit_mag_scnl`).

**BD** (`ewgui/db.h`): `ewguidb_config_load`(:33), `open`(:40), `close`(:41), `last_error`(:44), `exec`(:47),
`upsert_event/origin/magnitude/tensor`(:123), `upsert_origin_if_absent`(:126), `replace_picks`(:133),
`replace_station_magnitudes`(:135), `set_preferred`(:139), `set_preferred_origin`(:142),
`query_recent_events`(:151), `query_map_events`(:166), `search_events`(:186),
`query_origins/picks/magnitudes/station_magnitudes`(:188), `rows_free`(:197). Filas como structs planos:
`EwGuiEventRow`(:51), `EwGuiOriginRow`(:60), `EwGuiPickRow`(:78, `residual` = columna `uncertainty`),
`EwGuiMagRow`(:89), `EwGuiTensorRow`(:99), `EwGuiStaMagRow`(:111), `EwGuiMapEvent`(:155),
`EwGuiSearchFilter`(:176). **Todo C puro, includable desde C++** (el `EwGuiDb` es opaco).

Uso actual en la GUI: `ws.c:598` (`open`), `:690` (`query_origins`), `:743` (`query_magnitudes`), `:791`
(`query_picks` → residuales), `:821` (`query_recent_events`), `:877` (`search_events`), `:1216`
(`query_station_magnitudes`); y `mww.c:942/961/994/999` (`upsert_event`, `upsert_origin_if_absent`,
`upsert_magnitude`+`upsert_tensor`, `set_preferred`).

**Ondas:** `wstore_get(WStore*, sta,net,chan,loc, t0,t1, WStoreTrace*)` (`wavestore/wstore.h:49`,
**0 datos / 1 sin datos / <0 error**), `wstore_open`(:40), `wstore_trace_free`(:77), structs
`WStoreOpts`(:20) y `WStoreTrace`(:26). C puro, sin GTK ni glib. En la GUI actual: `ws.c:1266`
(`static WStore *g_wstore`), `:1270` `store_fetch_sta` (arma `TRACE2_HEADER`+`i4`), `:1280` `wstore_get`,
`:1282` `wstore_trace_free`, `:1287` relleno del header (`snprintf` sta/net/chan/loc, `starttime`,
`samprate`, `nsamp`, `endtime`, `memcpy "i4"`, version 2/0), `:1308` `ws_fetch_thread`, `:1314`
`wstore_open` una vez por job. Fallback SDS: `csnhypodbp_sds_fetch(sta,net,chan,loc,t0,t1,&raw,&len)`
(`csnhypodbp_sds.h:12`, **1 datos / 0 sin datos / <0 error**, buffer `TRACE2+i4`), gate por globales en
`sds.c:60` (`!g_sds_enabled || !g_sds_root[0] || t1<=t0 → 0`), `csnhypodbp_sds_prefer`(`sds.h:21`),
`ewgui_sds_path`(`ewgui/sds.h:18`, traduce location nulo `--`/vacío → vacío para el SDS).

**`TRACE2_HEADER`** (`ew_8.0/include/trace_buf.h:112`): `{int pinno,nsamp; double starttime,endtime,
samprate; char sta[7],net[9],chan[4],loc[3],version[2],datatype[3],quality[2],pad[2];}`.

**Configuración:** `ReadConfig()` propio en `main.c:321` con `kom` (`k_open/k_rd/k_str/k_its/k_int/k_val`;
firmas en `ew_8.0/include/kom.h:27,32`), **no** usa `ewgui_config_load`. Claves obligatorias `:327`
(`MyModuleId, InRing, OutRing, HeartBeatInt, LogFile, StaFile, StateFile`); grupos en `:334` (anillos
opcionales), `:337` (picking/locate), `:342` (agrupación), `:345` (BD/mag/mww/store), `:361` (vista de
ondas), `:397` (búsqueda/SDS), `:404` (colores), `:429` (mapa/overlays), `:483` (perfiles), `:507`
(filtros). `maps_print_config()` en `:562` (parcial: mapa/overlays/filtros/colores/magnitud/perfiles).

`ewgui_config_load(path, spec, n)` (`config.h:31`) es genérico por tabla `EwKeySpec`
(`config.h:19`: `{name, type STR/INT/DBL/LONG/COLOR, out, size, required}`) y **ignora claves
desconocidas** (`ewgui_config.c:50`) ⇒ sirve para las escalares del mismo `.d`; las claves repetidas
(`Profile`, `Filter`, `locator`, `OverlayHide`, `StaLta`, `channelsOrder`, `componentsOrder`) necesitan un
pase `kom` propio.

### 4.4 UI actual (para replicar la paridad)

**Estructura** (constructor entre paréntesis):

```
AdwApplication (main.c:2571) → on_activate (:1559)
 AdwApplicationWindow window_global (:1637) título "CSNhypodbp - Hypocenter database picker (EW8)",
                                      default 1100x750 (:1639)
  AdwToolbarView (:1677) → AdwHeaderBar (:1672) + ☰ ewgui_menu_button_new (:1675) + vbox (:1682)
   GtkPaned[H] paned_maps (:1691-1693), reparto una sola vez w-340 (:1392-1426)
    GtkNotebook g_notebook (:1684)
     "Eventos" (:1859): GtkPaned[V] 50/50 (:1849-1858)
        arriba  "Events" (:1809) + botón "Clear search" (:1823) + sw_master → tree_global (:1724-1728)
        abajo   "Origins" (:1829) + sw_detail → tree_detail (:1761-1765)
     "Procesar" (:2473-2475): g_preview_box (:1953-2079) + GtkNotebook g_subnotebook
        "picking" (:2198-2199): waves_hbox[ list_frame→sw_list→g_sta_view (:1909-1912)
                            | waves_right→sw_ondas→canvas_global (:2083) + g_axis_canvas (:1875) ]
                            + hbox_bottom (:2112-2192): "Cancel" | filtro | A/M/Teo | depth/km/Fix | locators/locate
        "magnitudes" (:2469-2470): hbox_mag (:2356) + mag_hbox[ mag_frame→sw_mag→g_mag_view (:2257-2260)
                            | sw_magcv→g_mag_canvas (:2278) ] + GtkStack mag_stack "mag"/"Estaciones" y
                            "mww"/"Mww" (:2324-2325) + g_mag_axis_canvas (:2330) + bar (:2356-2465)
    g_maps_col (build_maps_column :1468; map_panel_new :1367-1388): GtkPaned[V] con MapSplit (:1471-1506),
        mínimos 160x140 / 160x100 (:1477-1480): g_map_plan (on_draw_map_plan) / g_map_prof (on_draw_map_profile)
```

**Columnas literales** (paridad obligatoria en los modelos Qt):

| tabla | columnas | cita |
|---|---|---|
| maestra (`tree_global`, clase `master-events`) | `Date, Lat., Lon., Dep, Res, Azm, #Stn, ID, Ml, Mwp, Mb, Ms, Mod, Ver` (+ `N`, `Est.`) | `main.c:1730-1746` |
| detalle (`tree_detail`, clase `detail-solutions`) | `Mod, Ver, Est., Created, Date, Lat., Lon., Dep, Res, #Stn, ID, Ml, Mwp, Mb, Ms` | `main.c:1773-1774`, orden default `Created DESC` `:1786-1790` |
| estaciones (`g_sta_view`, clase `sta-table`) | `station`(`[☑]`), `dist[km]`, `pick`(`P`\|`S`), `stat`(`M`\|`A`\|`Teo`), `res` | `main.c:1916-1920`, constructores `:1061-1086` |
| magnitudes (`g_mag_view`) | `station, dist[km], ancla, t0[s], t1[s], mag, res, stat` | `main.c:2261-2270` |

**Preview** (`g_preview_box`, clase `.preview-box`, candidata `.candidate`): `g_preview_title`,
`g_preview_otime` (`"Origin time: …"`), `g_preview_spinner` + `g_preview_status` (`"calculando <loc>… N s"`),
5 params + 5 mags (`csnhypodbp_preview_headers`), 5 radios `g_preview_radio[5]`, botones
`"Confirm"`/`"Discard"`; `main.c:1953-2079`. Lógica pura en `csnhypodbp_preview.{h,c}`; **un solo
render**: `csnhypodbp_preview_update()` (declarada en `csnhypodbp.h:479`, implementada en
`csnhypodbp_gui.c:2035`).

**Canvases y sus callbacks** (`EwGuiCanvas`/`EwDrawFn` en `common/include/ewgui/view.h:17-21`, API `:23-40`,
implementación única en `common/src/ewgui_view_gtk4.c:10-31,42-90`; struct interno
`{GtkWidget*, EwDrawFn draw, void *ud}` y `gtk_drawing_area_set_draw_func`):

| lienzo | registro | dibujo | líneas |
|---|---|---|---|
| trazas picking | `main.c:2083-2084` (`canvas_global`) | `on_draw_signal` `gui.c:4043` | 315 (`:4043-4357`) |
| eje UTC picking | `main.c:1875-1876` (`g_axis_canvas`) | `on_draw_axis` `gui.c:4375` | — |
| planta | `main.c:1477` (`g_map_plan`) | `on_draw_map_plan` `gui.c:3174` | — |
| perfil | `main.c:1480` (`g_map_prof`) | `on_draw_map_profile` `gui.c:3274` | — |
| magnitudes | `main.c:2278-2279` (`g_mag_canvas`) | `on_draw_mag` `gui.c:772` | — |
| eje magnitudes | `main.c:2330-2331` | `on_draw_mag_axis` `gui.c:1235` | — |
| playera Mww | `mww.c:1119` (`g_mww.ball`) | `on_draw_mww` `mww.c:345` | — |

**Qué llama el dibujo de trazas desde los núcleos puros** (es decir, qué se reutiliza al reescribirlo):
`ewgui_trace_filtered` (`gui.c:4151`), `csnhypodbp_view_use_polyline` (:4198), `csnhypodbp_view_sample_x`
(:4211), `csnhypodbp_view_min_half` (:4225), `csnhypodbp_view_band_runs` (:4226),
`draw_stalta_overlay` (:4253 → `stalta_cf_for` :3938 que usa `ewgui_trace_filtered` + `ewgui_stalta_run`),
`csnhypodbp_view_mark_x` (:4262), y el filtrado de vista (:3789 vía `ewgui_trace_filter`).
**Conclusión:** el escalado/ejes/decimaciones sí son reutilizables (`csnhypodbp_view_*`); la
envolvente min/max, el clip y las primitivas cairo están dentro del callback ⇒ se reescriben (mecánico).

**Modelo de datos en memoria** (todo puro, todo reutilizable):

```
csnhypodbp_group.h:20 CsnhypodbpPick     {sta[8],chan[8],phase,t_pick,manual,net[4],loc[4],weight,residual}
csnhypodbp_group.h:33 CsnhypodbpSolution {qid,version,mod,otime,lat,lon,depth,rms,nph,gap,manual,confirmed,
                                          pref_mag,npicks,picks[256]}
csnhypodbp_group.h:47 CsnhypodbpGroupKey · :~60 CsnhypodbpSolMeta · CsnhypodbpMagSlot/MagStore (:47-133)
csnhypodbp_group.h:55 int csnhypodbp_parse_solution(const char *msg,size_t len,int mod,CsnhypodbpSolution*)
csnhypodbp_group.h:80 csnhypodbp_group_matches / csnhypodbp_group_union
csnhypodbp_group.h:101 int csnhypodbp_preferred_index(const CsnhypodbpSolMeta*,int)  /* confirmada>manual>nph+menor RMS; -1 si n=0 */
csnhypodbp_statable.h:16 CsnhypodbpStaPick {present,origin,chan[8],net[4],loc[4],t,resid}
csnhypodbp_statable.h:27 CsnhypodbpStaRow  {sta[16],idx,dist_km,dist_deg,has_data,p,s}
csnhypodbp_statable.h:38 CsnhypodbpStaInput{sta,idx,dist_deg,has_data,chan,net,loc,caps,ncap,sol,nsol,theo_p}
csnhypodbp_statable.h:63 csnhypodbp_stapick_one   /* ranking M>A>T */
csnhypodbp_statable.h:69 csnhypodbp_resid_source  /* preview válida si no, base */
csnhypodbp_statable.h:75 csnhypodbp_statable_build(out,max,in,n,man,nman,sol)
csnhypodbp_statable.h:83 csnhypodbp_pick_allowed(origin,idx,ph,pick_on,filter)
csnhypodbp_statable.h:90 csnhypodbp_locate_picks(rows,nrows,pick_on,filter,out,max_out)
csnhypodbp_magrow.h:30  CsnhypodbpMagIn  {sta[8],chan[16],mag,amp,period,manual}
csnhypodbp_magrow.h:40  CsnhypodbpMagRow {sta,chan,dist_km,mag,res,amp,period,dist_deg,t0,t1,anchor_t,
                                          manual,used,has_data,in_range,eligible,win_src,anchor}
csnhypodbp_magrow.h:62/72/94/116 split_nslc / magrow_build / *_text / counts
csnhypodbp_preview.h:25 CsnhypodbpPreviewState {IDLE,RUNNING,CANDIDATE,ERROR}
csnhypodbp_preview.h:32 CsnhypodbpPreviewVals {candidate,state,title[48],otime[48],busy[64],err[320],val[10][24]}
csnhypodbp_preview.h:18-56 NVALS 10 (Lat,Lon,Depth,Nph,RMS,ML,Mwp,Mb,Ms,Mww) / NPARAMS 5 / MAGS 5
csnhypodbp_preview.h:47/63/70/82 is_mag / mag_col / mag_split / mag_pref_default / set_busy|set_error|clear|
                                                                            from_candidate|from_selected
csnhypodbp_mapview.h:19 CsnhypodbpMapPt {lat,lon,depth,mag,kind}  /* kind 0=sol,1=preferida,2=candidata */
csnhypodbp_mapview.h:27/42/52/78/86 map_bbox / mapfit / mapfit_at / map_screen / map_view_bounds
csnhypodbp_mapview.h:127 profile_frame · :154 CsnhypodbpFrame{x0,x1,y0,y1,px,py,pw,ph} · :175 frame_project
csnhypodbp_profile.h:26 CsnhypodbpProfile{name[32],lon1,lat1,lon2,lat2,a,b,slab_file[256],topo_file[256]}
csnhypodbp_profile.h:34 CsnhypodbpProfiles{p[64],n} · :42 CsnhypodbpProfileLine{n,truncated,*lon,*val,…}
csnhypodbp_profile.h:52 profile_add · :69 profile_nearest
csnhypodbp_view.h:20 CsnhypodbpView{def_span,t0,t1,active,have_anchor,anchor}
csnhypodbp_view.h:48/133/138 rel_to_x|x_to_rel / sample_x / ticks|tick_step (1/2/5×10^k)
csnhypodbp_search.h:35 CsnhypodbpSearch{time_min,time_max[32],have_lat/lon/depth/mag,*_min,*_max,limit}
csnhypodbp_search.h:64 csnhypodbp_search_build(fields[10],max_events,out,err,errsz)
csnhypodbp_locate.h:22 CsnhypodbpTrace{sta,net,chan,loc[16],rate,oldest,*samples,nsamp}
csnhypodbp_locate.h:29 CsnhypodbpMagOut{ml,mwp,mb,ms[32]}
csnhypodbp_station.h:32-52 DEV_STATION{szStation/Channel/NetID/Location,dLat,dLon,EwGuiTrace*,fetched,picks[],pickcap}
csnhypodbp_trace.h:20-37 CsnhypodbpTracePick{sta,chan,phase,origin,t} + CsnhypodbpPickCap
```

**No reutilizable (GTK/glib):** `EventSol` (`ws.c:130-143`, lleva `CsnhypodbpRow*`), `struct EventGroup`
(`ws.c:145-151`, `GPtrArray *sols`), `CsnhypodbpRow` (`row.c:5-17`, GObject con `cols[15]`), y las
funciones de `ws.c` que los usan: `rebuild_event_store`(:378), `apply_mag_to_solutions`(:232),
`procesar_mensaje_sismo_ex`(:923), `procesar_mensaje_mag`(:1110), `csnhypodbp_magsta_for`(:1182),
`cargar_grupos_db`(:678), `cargar_sismos_db`(:813). **La contraparte pura existe** (`CsnhypodbpSolution`,
`CsnhypodbpPick`, `CsnhypodbpGroupKey`) ⇒ el port reescribe el contenedor (un array/`std::vector` de
`CsnhypodbpSolution` + metadatos) y reutiliza el núcleo (`csnhypodbp_group_*`, `preferred_index`).

### 4.5 Overlay y geografía (reutilizables con cairo)

```
common/include/ewgui/overlay.h:47 EwOverlay *ewgui_overlay_new(void);
common/include/ewgui/overlay.h:54 int ewgui_overlay_append(EwOverlay*, const char *path);
common/include/ewgui/overlay.h:79 typedef void (*EwOverlayMapFn)(double lat,double lon,void *ud,double *sx,double *sy);
common/include/ewgui/overlay.h:82 void ewgui_overlay_draw(cairo_t*, const EwOverlay*, EwOverlayMapFn, void *ud, double width_scale);
common/src/ewgui_overlay.c:297    solo cairo_save/set_line_join/set_source_rgb/set_line_width/move_to/line_to/stroke/restore
common/include/ewgui/geo.h:11     void ewgui_geo_project(double lat,double lon,double img_w,double img_h,double *x,double *y);
```

⇒ `ewgui_overlay_draw` funciona sobre **cualquier** `cairo_t*`, sin widget GTK: es exactamente lo que
necesita el lienzo `QImage`. Contratos de mapa base (equirectangular 2:1, mundo completo, sin alpha) y
formato `.lines` siguen aplicando igual (`common/tools/check_map_image.py`, `common/tools/OVERLAYS.md`).

---

## 5. Trampas del repo que el port debe respetar

Todas documentadas en `AGENTS.md` §7; se listan aquí las que **afectan directamente** al port, con la
acción concreta.

| # | trampa | acción en csnhypodbpQT |
|---|---|---|
| T1 | `rd_chan_mag`/`rd_mag` no acotan escrituras, destruyen el mensaje y parsean in-place (costó un `SIGABRT` diferido en `hypodb_ingest`; ver `ew_gui_tools/hypodb/test_mag_lines.c`) | Pasar **copia propia NUL-terminada con holgura a ceros**; delegar solo en `csnhypodbp_magchan.c`; invariante: ninguna llamada directa a `rd_chan_mag`/`rd_mag` fuera de ese archivo. Contar `'\n'` **no** vale de proxy |
| T2 | `tport_getmsg`: `GET_MISS`/`GET_NOTRACK` **sí** traen mensaje ya copiado; solo `GET_NONE`/`GET_TOOBIG` no | Parsear y seguir drenando en MISS/NOTRACK; acotar `recsize` antes de `msg[recsize]='\0'` (códigos en `ew_8.0/include/transport.h:100`) |
| T3 | El buffer de anillo **no** viene terminado en NUL | Copiar a buffer propio con NUL antes de parsear |
| T4 | La línea-1 del ARC no admite texto en 122..135 (HYPOINVERSE lee ahí las magnitudes); el modelo de velocidades va en 182..194 | Respetar `EWGUI_ARC_*` de `common/include/ewgui/arc_layout.h:18-67` (`MANUAL_OFF 174`, `QID_OFF 136/len 10`, `QVER_OFF 178/len 4`, `VMODEL_OFF 182/len 13`, `BMTYPI_OFF 122`); no reescribir offsets a mano |
| T5 | `timeout` de SeisComP ensombrece coreutils | En todos los scripts del prototipo: **solo `/usr/bin/timeout`** (lo exige `common/tests/test_timeout_gnu.sh:89-103`, que barre `*.sh`, `Makefile`, `*.mk`) |
| T6 | GTK4/WSLg: maximizar cierra la ventana (`Error 71`) si el mínimo no cabe | En Qt el mecanismo es distinto, pero la regla de fondo aplica: **nada de mínimos inflados** (`setMinimumSize` grandes, `QSplitter::setCollapsible(false)`, tablas sin ancho acotado). Test `--live` de 5 ciclos maximizar/des-maximizar (análogo a `test_maximize.sh`) |
| T7 | `GtkDropDown`/`GtkMenuButton` dejan el popup detrás de la ventana (bug #1299) | En Qt no aplica: `QMenu`/`QFileDialog`/`QComboBox` son ventanas nativas. Sí aplica la regla Wayland de poner **parent** a los menús (`new QMenu(parentWidget)`, `setTransientParent`) y no usar `QCursor::pos()` para menús contextuales |
| T8 | El `bind` de `GtkSignalListItemFactory` es un snapshot | En Qt no aplica: `dataChanged`/`beginResetModel` sí repintan. Ojo igualmente con **índices de proxy vs fuente** y con restaurar la selección por `origin_id`, no por fila |
| T9 | Referencias prestadas de getters de modelo (`get_selected_item` es transfer none) | En Qt: `QModelIndex` es un valor, no hay refcount; pero `currentChanged` entrega índices que **se invalidan** en un reset ⇒ capturar el `origin_id` (QString) y no el índice |
| T10 | La instalación debe ser atómica y siempre ejecutada (`install_bin.sh`), y `make` a secas debe instalar | Receta `install:` con `sh ../common/tools/install_bin.sh …` (idéntica a `csnhypodbp/Makefile:37-38`) + `.DEFAULT_GOAL := all` |
| T11 | Dependencias de cabeceras: sin `-MMD -MP` la ABI se rompe en silencio | `common/tests/test_build_deps.sh:18-46` recorre **todos** los `ew_gui_tools/*/Makefile` y exige `-MMD`, `-MP`, `-MF`, `.dep`, `-include $(wildcard`, `clean-deps`. El Makefile nuevo debe cumplirlo o rompe el `check` de `common` |
| T12 | `params/` es solo configuración versionada; el runtime va a su carpeta | El estado del prototipo va a `run_working_v8/csnhypodbpqt/` (vía `--state-dir`), nunca a `params/`. `common/tests/test_params_stays_clean.sh` §1 (`:40-46`) falla si hay ficheros sin versionar a la vista en `params/` |
| T13 | Rutas absolutas en `.d` solo para claves de la lista blanca; `WorkDir`/`MwwWorkDir`/`StoreRoot` deben ser `../<algo>` | `test_params_stays_clean.sh` §3 (`:70-91`) y §4 (`:94-138`, lista `ABS_KEYS`). Si se añade `params/csnhypodbpqt.d` (fase 6) debe respetarlo |
| T14 | `startstop` resuelve `restart <nombre>` al **primer** match; la identidad es el pid | Mientras el prototipo comparta nombre/id con `csnhypodbp`, `restart csnhypodbp` puede reiniciar el equivocado. Documentar y, en fase 6, dar id y nombre propios |
| T15 | `diag.sh` es lista cerrada (`config` solo acepta `csnhypodbp\|csnrv\|csnwphase`) | No ampliarlo (toca archivo existente). Diagnóstico del prototipo: `csnhypodbpQT --print-config <d>` directo. Verificar en fase 0 si `diag.sh log csnhypodbpqt` resuelve genéricamente |
| T16 | `sniffring` sin `-n` drena el anillo | Los scripts de test del prototipo usan siempre `sniffring -n` y `/usr/bin/timeout` |
| T17 | Coherencia de canal HHZ/BHZ en 4 sitios (fuente, picks, metadata, filtro del store) | No es específica del port, pero el port **hereda** el comportamiento: reutiliza los mismos núcleos, así que la coherencia se mantiene idéntica (ni mejor ni peor). No relajar ningún `strcmp` |

---

## 6. Integración sin tocar el código existente

### 6.1 Descubrimiento automático (verificado)

`ew8_compile.sh` **no tiene lista literal de módulos**: los deriva.

```
:313        MODULES=()  # orden canonico: librerias primero, luego alfabetico
:359-376    discover_modules(): for mkf in "$ROOT"/ew_gui_tools/*/Makefile; name=${dir##*/};
            bins=$(mk_bins "$mkf"); si vacío → MOD_IS_LIB[name]=1 y va a LIBS; si no → BINS
:377-379    if grep -qE 'gtk4|libadwaita' "$mkf"; then MOD_GUI[$name]=1; fi
:381        MODULES=("${LIBS[@]}" "${BINS[@]}")
:323-337    mk_bins(): grep de la 1.ª línea 'sh … install_bin\.sh', resuelve $(TARGET)/$(MODULE)
:339-341    has_target(): grep -qE "^$2[[:space:]]*:"
:343-345    test_targets(): grep -oE '^test-[a-z-]+:' menos 'test-live'
:348-357    check_targets(): check > test > todos los test-* (sin test-live)
:740-753    phase_build(): make -C "$ROOT/$dir" -j"$JOBS" all   (JOBS=1 por defecto, :398)
:745-747    --clean ⇒ make -C "$ROOT/$dir" clean antes de all
:755-764    phase_check(): sin targets ⇒ skip_step "sin target check/test"
:780-808    phase_test(): check-offline si existe (si no SKIP); test-live solo con --live y DISPLAY
:811-822    validate_cmds(): make -C "$ROOT/$dir" -s --no-print-directory print-VALIDATE_FLAGS
:824-877    phase_validate(): check_install.sh -v y luego cada 'bin args' con cwd=params
:861-875    exige binario instalado en EW_HOME/EW_VERSION/bin y el .d en PARAMS_DIR
:926-952    --list deriva --check/--test/--live/--validate de los targets declarados
```

`check_install.sh` igual: `:13-19` inventario derivado, `:42-56` `mk_bins`, `:60-73` itera
`ew_gui_tools/*/Makefile`, `:75-83` exige `cmp -s src ins` idéntico.

⇒ **Crear la carpeta con su Makefile es suficiente** para que el módulo entre en `--list`, `all`,
`--check`, `--test`, `--live` y `--validate`. No hay que tocar `ew8_compile.sh`.

**Invariante que lo protege:** `common/tests/test_validate_flags.sh:56-60` prohíbe una tabla literal de
módulos en el script.

### 6.2 Orden de build y sub-makes

El orden es **glob alfabético con las librerías primero** (`:381`). Como `csnhypodbpQT` va alfabéticamente
**antes** que `csnloc`, `csnwphase` y `wavestore`, el Makefile nuevo **debe** traer sus propias reglas de
sub-make para las `.a` ajenas, igual que `csnhypodbp/Makefile:40-51`:

```make
../csnloc/libcsnloc_tt.a:      ; $(MAKE) -C ../csnloc libcsnloc_tt.a
../wavestore/libwavestore.a:   ; $(MAKE) -C ../wavestore libwavestore.a
../csnwphase/libcsnwphase.a:   ; $(MAKE) -C ../csnwphase libcsnwphase.a
```

(`common` es LIB —no tiene `install_bin.sh`— así que siempre se construye antes; pero **no** la
enlazamos: compilamos sus `.c` puros directamente. Aun así conviene el sub-make si algún test los usa.)
Los módulos son secuenciales a propósito (`:107-109`: dos sub-makes simultáneos a `common/` pueden
pisarse) ⇒ los sub-makes son seguros.

### 6.3 Clasificación GUI (detalle fino)

`MOD_GUI[name]=1` solo si el Makefile contiene la cadena `gtk4` o `libadwaita` (`:377`). Usos:

```
:487-490  grupo 'headless' = con binario y SIN MOD_GUI
:492-495  grupo 'gui'      = con binario y CON MOD_GUI
:511-517  is_gui_selected()
:639-642  need_build_tools(): si hay algún GUI seleccionado → pkg-config --exists gtk4 libadwaita-1 o die 4
:796-803  phase_test(): test-live corre con --live y DISPLAY, SIN consultar MOD_GUI
```

Consecuencias de que el Makefile Qt **no** mencione gtk4: el módulo cae en el grupo `headless` (lo
construye `ew8_compile.sh headless`, no `gui`), no dispara el `die 4` de GTK, y su `test-live` sí corre
con `--live`. ~~Recomendación: incluir un comentario marcador~~ **Resuelto en D5**: el Makefile
declara `MOD_KIT = qt6` y `ew8_compile.sh` clasifica por ese dato (con *fallback* al grep, que
prioriza `Qt6Widgets|qmake6` sobre `libadwaita-1`). Ya no hace falta ningún comentario-mentira:

```make
MOD_KIT = qt6
```

### 6.4 `.gitignore` (verificado)

```
:42-49  # 3a. Binarios de módulo y de test (regla general, módulos presentes/futuros)
        /ew_gui_tools/*/*            ← ignora el binario local csnhypodbpQT (sin extensión)
        !/ew_gui_tools/*/*/
        !/ew_gui_tools/*/*.*         ← NO ignora ficheros con extensión (fuentes, .d, .sh, .cxx)
        !/ew_gui_tools/*/Makefile*
:51-55  /ew_gui_tools/**/test_*  ·  !/ew_gui_tools/**/test_*.*  ·  /ew_gui_tools/**/gen_*  ·
        /ew_gui_tools/**/build/      ← nuestro BUILDDIR queda ignorado
:57-62  *.o, *.a, *.so, *.so.*, *.dylib, *.dSYM/
:64-90  # 3b ew_8.0/bin: binarios por NOMBRE literal (/ew_8.0/bin/csnloc, …, /ew_8.0/bin/wstore_prune)
:25-28  runtime ignorado por nombre: run_working_v8/{hyp2000_ring,nlloc_ring,csnhypodbp,csnwphase}/
:180-183 *.d NO se ignora (configuraciones versionadas); *.dep sí
```

**No hay patrón para `moc_*`.** Con `.cxx` como extensión de los fuentes C++ y `moc_*.cxx` generados
**dentro de `build/`**, todo queda ignorado por `:51-55`. ⇒ Regla: **ningún artefacto generado fuera de
`build/`**, y ningún `.o` escrito en carpetas ajenas (ver §10).

Quedan 2 huecos que requieren 1 línea en `.gitignore` (archivo existente ⇒ **solo con permiso**, fase 6.5):
`/ew_8.0/bin/csnhypodbpQT` y `/run_working_v8/csnhypodbpqt/`. Sin ellas el efecto es únicamente ruido en
`git status` (ningún test falla: `test_params_stays_clean.sh` §1 solo mira `params/`, §6 solo tres
carpetas literales).

### 6.5 Tests del repo que el módulo nuevo debe seguir pasando

| test | qué exige | impacto |
|---|---|---|
| `common/tests/test_build_deps.sh:18-46` | recorre **todos** los Makefiles: `-MMD`, `-MP`, `-MF`, `.dep`, `-include $(wildcard`, `clean-deps` | el Makefile debe cumplirlo (T11) |
| `common/tests/test_timeout_gnu.sh:89-103` | barre `*.sh`/`Makefile`/`*.mk`: `timeout` solo por ruta absoluta | nuestros scripts con `/usr/bin/timeout` (T5) |
| `common/tests/test_validate_flags.sh:20-24,38-52,56-60` | `print-VALIDATE_FLAGS`; contrato solo para 5 módulos; prohibida tabla literal | añadir el target no rompe nada; el test no itera módulos nuevos |
| `common/tests/test_install_check.sh:93-103` | inventario no literal en `check_install.sh` | ok |
| `common/tests/test_params_stays_clean.sh:40-173` | §1 sin ficheros sin versionar en `params/`; §2 salvedades; §3 `WorkDir\|MwwWorkDir\|StoreRoot` = `../<algo>`; §4 rutas absolutas solo lista blanca; §5 `CommandFile→.hyp→STA/CRH`; §6 tres carpetas literales | si se añade `params/csnhypodbpqt.d`, debe respetar §3/§4/§5 y estar versionado |
| `common/tests/test_ring_conf.sh:1-53` | `startstop_unix.d:Ring` + `earthworm.d.base:Ring/clave` + `csnhypodbp.d:ConfRing` + `hypodb.d:InRingConf` coinciden; `nRing` == nº de líneas `Ring`; la clave existe en `earthworm.d.base` | no añadimos anillos ⇒ no rompe. Si se añade un `Ring`, subir `nRing` |
| `common/tests/test_pick_rings.sh:74-80,131-145` | `MyModuleId` de `pickml_phasenetwc.d` tiene su `Module`; clave de `LEGACY_PICK_RING` única; `nRing` | no itera todos los `.d` ⇒ no rompe |
| `common/tests/test_ew_monitor_module.sh:49-55,68-81,242-247` | módulos salen del `.d`; prohibido nombre literal de módulo en `ew_monitor.sh`; ambigüedad de nombre ⇒ rc 2 | un módulo nuevo **no** rompe el test (no hay nombres literales); sí hereda la ambigüedad si se declara un 2.º `Process` con nombre parecido |
| `common/tests/check_no_gtk.sh` | solo mira `common/src/*.c` e `include/ewgui/*.h` | no se toca `common` ⇒ sigue pasando |
| `hypodb/test_source_invariants.sh:46-54` | invariantes de confirmación | no se toca `hypodb` ⇒ sigue pasando |

**Ningún test exige `.DEFAULT_GOAL := all` ni `CC ?= gcc` a todos los Makefiles** (verificado);
`.DEFAULT_GOAL := all` solo aparece en `pickml/Makefile:45`, `pickS/Makefile:21`, `csnmags/Makefile:31`.
Aun así se recomienda ponerlo (T10).

### 6.6 Identidad de módulo

- Fuentes EarthWorm **no están en el repo** (`ew_8.0/VENDORED.md:4-7,20,24,31`: solo headers, `.a` y
  binarios; `src/` ~811 MB no versionado). Solo se dispone de la firma
  `int GetModId(char *, unsigned char *);` (`ew_8.0/include/earthworm_simple_funcs.h:64`).
- Todos los módulos tratan `GetModId(...) != 0` como **fatal**: `csnhypodbp_main.c:764` (`exit(-1)`),
  `pickml_main.c:308-311`, `wstore_ingest_main.c:29-33`, `csnloc.c:192`, `csnmags.c:500-507`.
  El mensaje de `pickml` lo dice explícito: *"falta 'Module %s' en earthworm.d"*.
- ⇒ **Un id de módulo distinto exige una línea `Module MOD_X <id>` en `earthworm.d.base`** (archivo
  versionado, `:79-245`; `earthworm.d` lo genera `worktree_bootstrap.sh:114` con offset de keys).
- Ids usados: `MOD_PICK_FP 151`, `MOD_EW2OPENAPI 153`, `MOD_CSNMAGS 156`, `MOD_CSNTVP 157`,
  **`MOD_CSNHYPODBP 158`**, `MOD_CSNRV 159`, `MOD_CONTROL 160`, `MOD_STAEVDISP 161`, `MOD_CSNLOC 162`,
  `MOD_HYP2000_RING 163`, `MOD_NLLOC_RING 164`, `MOD_PICKS 165`, `MOD_MAGNITUDES 166`,
  `MOD_MAGNITUDES_RAW 167`, `MOD_CSNMAGS_RAW 168`, `MOD_HYPODB 169`, `MOD_WSTORE 170`,
  **`MOD_PICKML 171`** (máximo, `earthworm.d.base:245`). **Libres: 172-255** (y 58, 63, 152, 154, 155).
  Keys de anillo libres: `1061+` (excepto 2000/9999; con offset `+10000*N` por instancia).
- **No hay ningún chequeo de id duplicado** entre procesos vivos (verificado en `startstop`, tests de
  `common`, y `test_ew_monitor_module.sh` que solo prueba ambigüedad de nombre→pid).
- `hypodb_ingest_main.c:204-207`: en el anillo de confirmaciones, `mod = logo.mod ? logo.mod : 158`
  (158 hardcodeado como fallback = `MOD_CSNHYPODBP`); en los anillos vivos `:209` usa 162/163 por defecto.

**Decisión para el prototipo:** usar `MyModuleId MOD_CSNHYPODBP` (158) y **no correr junto a
`csnhypodbp`**. Id propio (`Module MOD_CSNHYPODBPQT 172`) en fase 6.5 con permiso explícito.

### 6.7 Arranque manual del prototipo

Idéntico a la receta de humano de `AGENTS.md` §6 (los módulos abren rutas relativas a `$EW_PARAMS`):

```bash
cd <raíz del repo>
source ./ew8_unix.sh
cd run_working_v8/params
csnhypodbpQT csnhypodbp.d --state-dir ../csnhypodbpqt     # o: BPQ_STATE_DIR=../csnhypodbpqt
# headless, sin DISPLAY:
csnhypodbpQT --print-config csnhypodbp.d
csnhypodbpQT --check-profiles csnhypodbp.d
# offscreen (tests):
QT_QPA_PLATFORM=offscreen BPQ_MODE=--dump csnhypodbpQT csnhypodbp.d
```

Ojo con la posición del flag: en `csnhypodbp` los flags headless van **antes** del `.d`
(`main.c:2523,2530`) y `--state-dir` es nuevo; para no romper la paridad se acepta en **ambas**
posiciones (`argv[1]` o `argv[3]`).

---

## 7. Arquitectura propuesta

### 7.1 Regla de capas (invariante duro, testeado)

```
ew_gui_tools/csnhypodbpQT/
├── Makefile                     contrato §10
├── README.md                    arranque, limitaciones, diferencias con la versión GTK
├── c/                           C. ÚNICO sitio con cairo, EarthWorm, kom, libpq, libmseed2, wstore, csnwphase
│   ├── bpq_capi.h               la única puerta C→C++ (todo dentro de extern "C")
│   ├── bpq_config.c             BpqConfig + kom/ewgui_config_load + define g_sds_enabled/g_sds_root
│   ├── bpq_rings.c              attach/drena/heartbeat/should_quit/put
│   ├── bpq_magmsg.c             parseo de TYPE_MAGNITUDE vía csnhypodbp_magchan.c (copia + holgura)
│   ├── bpq_events.c             store de grupos + fila maestra (sobre csnhypodbp_group/preferred_index)
│   ├── bpq_db.c                 fachada de ewguidb_*
│   ├── bpq_waves.c              store→SDS, TRACE2_HEADER+i4 → EwGuiTrace
│   ├── bpq_sta.c                carga de StaFile (ewgui/sta.h) + distancias
│   ├── bpq_filters.c            presets del .d (csnhypodbp_filtercfg + ewgui_trace_filter + ewgui_stalta_run)
│   ├── bpq_manual.c             fachada de csnhypodbp_manual.c (estado de picadas manuales)
│   ├── bpq_locate_req.c         armado de BpqLocateReq + fachada de csnhypodbp_locate_run
│   ├── bpq_emit.c               emit_pick / emit_mag / emit_confirm (formatos byte-idénticos)
│   ├── bpq_mww.c, bpq_mww_save.c  inversión W (libcsnwphase.a) + guardado en BD
│   ├── bpq_canvas.c             cairo_image_surface sobre buffer BGRA ajeno + bpq_scene_paint
│   ├── bpq_export.c             cairo svg/pdf/png (reemplazo de ewgui_export_render)
│   └── bpq_draw_{map,profile,traces,axis,mag,mww}.c   los callbacks cairo, con contexto explícito
├── ui/                          C++17/Qt6. SOLO <Qt…> y "bpq_capi.h"
│   ├── main.cxx                 orden exacto de flags (§8.1)
│   ├── bpq_mainwindow.{h,cxx}   QMainWindow + QTabWidget + QSplitters
│   ├── bpq_canvas_widget.{h,cxx} QWidget que blitea el QImage de bpq_scene_paint
│   ├── bpq_events_model.{h,cxx}      QAbstractTableModel (maestra, 15 cols)
│   ├── bpq_sort_proxy.{h,cxx}        QSortFilterProxyModel (lessThan por kind)
│   ├── bpq_origins_model.{h,cxx}     detalle (+Created, default Created DESC)
│   ├── bpq_sta_model.{h,cxx}         estaciones (5 cols, 2 filas/estación, checkable)
│   ├── bpq_mag_model.{h,cxx}         magnitudes (8 cols)
│   ├── bpq_map_panel.{h,cxx}  bpq_profile_panel.{h,cxx}  bpq_wave_panel.{h,cxx}  bpq_mag_panel.{h,cxx}
│   ├── bpq_mww_panel.{h,cxx}
│   ├── bpq_preview_panel.{h,cxx}     render único desde CsnhypodbpPreviewVals
│   ├── bpq_search_dialog.cxx         modal (QDateTimeEdit + QCalendarWidget)
│   ├── bpq_confirm.cxx               Confirm/Discard
│   ├── bpq_ring_worker.{h,cxx}       QTimer 500 ms / 1 s en el hilo GUI
│   ├── bpq_fetch_worker.{h,cxx}      QThread ondas
│   ├── bpq_locate_worker.{h,cxx}     QThread locate
│   └── bpq_mww_worker.{h,cxx}        QThread Mww
├── tests/
│   ├── probe_capi.cxx           se compila con g++ -fsyntax-only: prueba la C++-safety de bpq_capi.h
│   ├── test_capi_{config,rings,magmsg,events,db,waves,sta,arc,pickmsg,confirm,export,filters}.c
│   ├── test_draw_{map,profile,traces,axis,mag}.c
│   ├── test_{models,sta_model,mag_model,canvas_offscreen,fetch_worker,preview_panel,shortcuts,
│   │        search_dialog,locate_worker,canvas_smoke}.cxx        (QT_QPA_PLATFORM=offscreen)
│   └── test_{capi_purity,reuse_purity,no_touch,no_gtk_link,printconfig_parity,d_parity,
│             checkprofiles_parity,locate_chain,integration,maximize,live_events,live_confirm}.sh
└── build/                       (ignorado por .gitignore:51-55) objetos, .dep y moc_*.cxx
```

**Reglas:**

- **R1** Ningún TU C++ incluye: `gtk/*`, `glib*`, `gobject/*`, `gio/*`, `cairo*`, `earthworm.h`,
  `transport.h`, `kom.h`, `trace_buf.h`, `rw_mag.h`, `libmseed2.h`, `wstore.h`, `csnwphase.h`,
  `csnhypodbp*.h`, `ewgui/view.h`, `ewgui/export.h`, `ewgui/ring.h`, `ewgui/overlay.h`,
  `ewgui/menu_ui.h`, `ewgui/actions.h`, `ewgui/ctrl.h`. Solo `bpq_capi.h` (y Qt).
- **R2** `bpq_capi.h` envuelve **todo** en `extern "C"` y solo incluye `<stddef.h>`, `<stdint.h>`,
  `<stdio.h>` y una **lista blanca** de headers puros verificados.
- **R3** Ningún archivo fuera de `csnhypodbpQT/` se modifica ni se escribe (objetos siempre en `build/`).
- **R4** El binario no enlaza GTK/Adwaita/glib/gobject/gio.
- **R5** cairo solo en `c/`. La UI entrega un buffer de píxeles y recibe un `QImage`.
- **R6** Los workers no tocan widgets: solo señales/slots y structs C planos registrados con
  `qRegisterMetaType`.

**Lista blanca includable desde C++** (verificada pura, sin EW ni glib):
`ewgui/{wave,dsp,geo,sds,sta,db,db_ids,config,cfgfile,table_ui}.h`,
`csnhypodbp_{group,search,statable,preview,profile,mapview,view,trace,traces,station,filtercfg,locate,
magmarks,magcfg,magrow,magview,magsta,manual,fixdepth,locator,tt}.h`.
**Fuera de la lista** (solo desde C): `csnhypodbp_magchan.h` (`rw_mag.h`), `csnhypodbp_mww_over.h`
(`csnwphase.h`), `csnhypodbp_sds.h` (a reverificar: el `.c` incluye libmseed2/trace_buf; el `.h` parece
limpio pero no se arriesga), `ewgui/{view,export,overlay,menu_ui,actions,ctrl,ring}.h`,
`csnhypodbp_{row,staitem,magitem,master}.h` (glib-object).

> Nota: `ewgui/table_ui.h` está en la lista porque solo incluye `<stddef.h>` y declara
> `ewgui_table_cmp_values` (definido en el puro `ewgui_table_cmp.c:46`), útil para replicar la semántica
> de orden de las tablas. Su implementación GTK (`ewgui_table_ui_gtk4.c`) no se compila.

### 7.2 cairo ↔ QImage (el mecanismo de D3)

```
QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
bpq_scene_paint(scene, &ctx, img.bits(), w, h, img.bytesPerLine());
// en C: cairo_surface_create_for_data(bgra, CAIRO_FORMAT_ARGB32, w, h, stride)
//       cairo_create(surface) → fn(cr, w, h, ud) → cairo_destroy/surface_destroy → cairo_surface_finish
QPainter p(this); p.drawImage(0, 0, img);
```

- `CAIRO_FORMAT_ARGB32` es premultiplicado en orden nativo; en little-endian coincide byte a byte con
  `QImage::Format_ARGB32_Premultiplied`.
- `cairo_image_surface_create_for_data` acepta stride arbitrario ⇒ usar `img.bytesPerLine()` (no `4*w`).
- El buffer **no** debe reallocarse mientras cairo dibuja ⇒ el `QImage` se reserva en el widget
  (doble buffering: se reutiliza entre `paintEvent`s, se recrea solo en `resizeEvent`).
- Alto DPI: `devicePixelRatioF()` — decidir en fase 2 (por defecto render 1:1 como la versión GTK, y
  opcionalmente `scale = dpr` en `bpq_scene_paint` si se quiere nitidez; el export ya lleva su `scale`).
- Ventaja colateral: el **mismo** callback sirve para pantalla y para export (`bpq_scene_export`), igual
  que `ewgui_export_render` hacía con `EwDrawFn`.

### 7.3 Concurrencia (mapeo 1:1 con la versión GTK)

| tarea | GTK actual | Qt propuesto |
|---|---|---|
| drena de anillos | `g_timeout_add(500, escuchar_anillo_earthworm, tree_global)` `main.c:2486` | `QTimer` 500 ms en `BpqRingWorker` (hilo GUI) → `bpq_rings_drain` → señales |
| heartbeat + quit | `g_timeout_add(1000, ew_background_tasks,…)` `main.c:2485`, `:851-859` | `QTimer` 1 s → `bpq_rings_heartbeat` / `bpq_rings_should_quit` → `quitRequested()` → `QApplication::quit()` |
| debounce de fetch | `g_timeout_add(1000, waveform_reload_timer,…)` `main.c:2487`, `gui.c:3660` | `QTimer::singleShot` coalescente en `BpqFetchWorker` |
| fetch de ondas | `ws_fetch_thread` `ws.c:1308` (hilo + `FetchThreads`) | `QThread` + `BpqFetchWorker::fetch(BpqWaveBatch)` (moveToThread), señales *queued* |
| locate | job lanzado en `gui.c:1933`, tick `gui.c:2370` | `QThread` + `BpqLocateWorker::run/cancel`, señales `stage/elapsed/finished` |
| Mww | `mww_tick` `mww.c:826`, panel `mww.c:1119` | `QThread` + `BpqMwwWorker` sobre `bpq_mww_start/poll/cancel` |
| timers de UI (spinner "calculando… N s") | `g_timeout_add(1000,…)` | `QTimer` 1 s en el panel |

**Por qué el drena va en el hilo GUI y no en un worker:** `tport_getmsg` sobre memoria compartida es
barato y no bloqueante, y mantenerlo en el hilo GUI evita candados sobre `BpqEvents` (la versión GTK hace
exactamente lo mismo). Los workers solo para lo que **sí** bloquea (E/S de ondas, `popen` de la cadena de
locate, motor de fase W).

### 7.4 Menús y diálogos (lecciones de WSLg aplicadas)

- El ☰ es un `QToolButton` con `QMenu` **con parent** (no `QCursor::pos()`), o directamente `QMenuBar`.
  A diferencia de GTK4 aquí no hace falta una ventana modal propia: `QMenu` es ventana nativa. Aun así se
  respeta la regla Wayland de transient parent.
- Diálogos de búsqueda/export/colores: `QDialog` **modal** (`setWindowModality(Qt::ApplicationModal)`),
  `QFileDialog::getSaveFileName`, `QColorDialog`. Nada de popups anidados.
- Selectores de pocas opciones (A/M/Teo, locators, magnitud activa): `QCheckBox`/`QRadioButton`/`QComboBox`
  (el `QComboBox` sí usa popup, pero nativo; si se observara el problema en WSLg viejo, sustituir por
  botones en fila — mismo criterio que en GTK).

---

## 8. Contrato de comportamiento

### 8.1 CLI

```
csnhypodbpQT --print-config <archivo.d>       → stdout, exit 0.  NO crea QApplication.
csnhypodbpQT --check-profiles <archivo.d>     → stdout, exit 0 si todos los perfiles cargan, ≠0 si no.
csnhypodbpQT <archivo.d> [--state-dir <dir>]  → GUI.
csnhypodbpQT <archivo.d> --state-dir <dir>    → GUI (orden alternativo aceptado).
cualquier otra forma                          → "Uso: csnhypodbpQT <configfile.d>" por stderr, exit 1.
```

El orden de resolución es **exactamente** ese (paridad con `csnhypodbp_main.c:2521-2537`): los dos modos
headless se resuelven antes de tocar Qt, EarthWorm, la BD o los anillos ⇒ funcionan **sin `DISPLAY`**,
sin PostgreSQL y sin EarthWorm en marcha. `--print-config` con `.d` ilegible ⇒ mensaje con la ruta y
exit ≠ 0 (no un crash).

Env de test (paralela a `EWGUI_MODE` de los módulos GTK, que usa `gui_run.sh`):

```
BPQ_MODE=--dump                 → árbol de widgets con nombre/clase/visibilidad/tamaño, exit 0
BPQ_MODE=--layout               → geometría de los divisores y mínimos efectivos (para T6)
BPQ_MODE=--shot <ruta.png>      → vuelca la ventana a PNG, exit 0
BPQ_MODE=--select <origin_id>   → selecciona esa fila en Origins y vuelca el detalle (tests de selección)
QT_QPA_PLATFORM=offscreen       → sin DISPLAY
BPQ_STATE_DIR=<dir>             → equivalente a --state-dir
BPQ_EXIT_AFTER_MS=<n>           → cierra solo tras n ms (tests de humo, análogo a CSNHYPODBP_MAXIMIZE_MS)
BPQ_MAXIMIZE_MS=<n>             → n ciclos maximizar/des-maximizar y salir (test T6/AC18)
```

### 8.2 Configuración

- Lee **el mismo esquema de claves** que `csnhypodbp.d` (~50 claves, lista completa en §4.3). Por defecto
  se le pasa **`params/csnhypodbp.d`** ⇒ cero archivos nuevos en `params/` y paridad de configuración
  gratis.
- Claves obligatorias (mismas que `main.c:327`): `MyModuleId, InRing, OutRing, HeartBeatInt, LogFile,
  StaFile, StateFile`. Ausente cualquiera ⇒ error nombrando la clave, exit ≠ 0.
- Clave desconocida ⇒ **ignorada** (paridad con `ewgui_config.c:50`).
- Implementación: `ewgui_config_load` con una tabla `EwKeySpec[]` para las escalares (`STR/INT/DBL/LONG/
  COLOR`) + un pase `kom` para las repetidas/lista (`Profile`, `Filter`, `FilterMenu`, `locator`,
  `OverlayHide`, `StaLta`, `channelsOrder`, `componentsOrder`, `tracesPerScreen`).
- `--state-dir <dir>` (o `BPQ_STATE_DIR`) sobrescribe **solo** `StateFile`, `ManualStateFile`,
  `MwwWorkDir` y la raíz temporal del `locate`, resolviendo contra `$EW_PARAMS` si es relativa. Por
  defecto `../csnhypodbpqt`. El directorio se crea al arrancar (`mkdir -p`), como hacen los demás
  módulos. Motivo: no pisar el estado manual de la GUI GTK si ambas llegan a coexistir.
- Colores: `ewgui_parse_hex_color` (`ewgui/config.h:31-34`) para las 6 claves `pColor/sColor/pColorAuto/
  pColorTheo/sColorAuto/sColorTheo`; las `*Auto`/`*Theo` **opcionales** y derivadas del manual al 40 % /
  70 % **en cada dibujo** (comportamiento actual, se conserva).

### 8.3 Entradas / salidas

| | |
|---|---|
| **Entradas** | `.d`; anillos `HYPO_RING_REF` (ARC mod 163/164 + `TYPE_MAGNITUDE`), `HYPO_RING` (ARC mod 162, si `InRingRaw`), `PICK_RING` (lectura de picks + escritura de repicks); PostgreSQL vía `DbEnvFile`; ondas del `wstore` (`StoreRoot`) y, si no, del SDS (`SdsRoot`, `SdsFallbackHours`); recursos: `MapImageFile`, `OverlayFile`, `ProfileDataDir/slab*.tmp`+`topo*.tmp`, `StaFile` (`estaciones_107.txt`), `TauModel`, `.d` de csnmags (`MagConfigFile`), `.d` de csnwphase (`MwwConfigFile`), `.pz` del repo |
| **Salidas** | ventana; `TYPE_PICK_SCNL` manuales → `OutRing` (`PICK_RING`); `TYPE_MAGNITUDE` `CSNMan` → `InRegion`; ARC confirmado → `ConfRing` (`HYPO_RING_REF2`) ⇒ `hypodb` crea la fila `origin`; heartbeats; BD (solo Mww: `event`, `origin` si falta, `magnitude(Mww)`, `tensor`, `set_preferred`); log `$EW_LOG/csnhypodbpqt_*.log`; export SVG/PDF/PNG; estado manual en `ManualStateFile`; temporales del `locate` y rundirs de Mww bajo el `--state-dir` |
| **stdout** | solo los modos `--print-config`, `--check-profiles` y `BPQ_MODE` |

### 8.4 Semántica que se conserva (paridad funcional exigible)

- Agrupación: un ID por evento por `qid` **o** firma de picadas compatible (subconjunto dentro de
  `GroupPickTolSec` con ≥`GroupMinMatchPicks`); la picada no incluye canal; el ARC viaja con las fases P y S.
- Solución preferida: confirmada a mano más reciente > manual más reciente > automática con más fases y
  menor RMS (`csnhypodbp_preferred_index`, `group.h:101`).
- Marca manual en el byte 174 del ARC (`'M'`/`'A'`, `arc_layout.h:18`).
- Por estación se dibuja **una sola P y una sola S** (`M > A > Teo`); la traza del picking es **siempre
  negra** y el origen se distingue por el **color de la marca**.
- Las casillas de la tabla de estaciones **solo** filtran el envío al localizador; cada fila (estación,
  fase) es autónoma; si la fase no existe, casilla vacía y no sensible.
- `locate` manda **exactamente** las picadas de la tabla, con su canal, filtradas por los checks A/M/Teo y
  por `MaxDistKm`; el filtro de la **vista** no afecta a lo que se manda (traza cruda).
- El residual por fase viene del localizador (JSONL de `csnloc` en stdout; columnas 35-38 P / 51-54 S del
  ARC de los refinadores) y se muestra sin pulsar `locate` cuando la solución ya lo trae
  (`pick.uncertainty` vía `ewguidb_query_picks`).
- Filtros de vista: presets con atajo (`1/2/3/4/r/k/d/f/0/t`), `FilterPhase zero` por defecto ⇒
  forward+backward (fase neta cero); `fc·n/fs < 0.5` ⇒ no-op; los atajos solo actúan en la sub-pestaña
  `picking` y no se consumen si el foco está en una caja de texto.
- STA/LTA **visual** (curva CF + marca en `t_disparo + corr`); **no crea picks**.
- Mapa/perfil: los dos dibujan la preferida (destacada), **todas** las soluciones del evento y la
  candidata; el marcador **verde** es el elegido en *Origins* y sustituye al rojo mientras dure; clic sin
  arrastre sobre un marcador selecciona ese origen; reparto vertical 65/35 modificable; el perfil arranca
  encuadrado **solo a sus datos** (slab ∪ topo), no a las soluciones; `MapMinSpanDeg` acota el
  auto-encuadre; `OverlayHide` apaga capas por nombre.
- Magnitudes: ventanas y puertas leídas del `.d` de csnmags; la GUI **no lista** las estaciones que el
  motor descarta; ancla en el `t_pick` de la P efectiva (`M>A>Teo`); franja ajustable; override de la
  puerta de distancia en cajas de texto acotadas, aplicado **solo al confirmar el campo y si los cuatro
  valen**; las S no generan magnitud; `MagFetchSeconds` alarga la petición para que Ms tenga ventana.
- Preview: **un solo render** para estado, valores, color y botones; `Confirm`/`Discard` sensibles solo con
  candidata; `Discard` vuelve a mostrar la elegida (no oculta la tablita); mientras corre, spinner +
  `calculando <loc>… N s`; el error real (`timeout`/`ref.err`/ARC inválido) se ve en ese mismo renglón.
- Mww: tabla de parámetros editable (límites y parseo del núcleo `mww_field_limits`/`mww_field_parse`;
  texto inválido no se aplica y destoca el campo); banda derivada de la magnitud y recalculada; resumen y
  salida del motor lado a lado con la misma altura; **todo fallo se muestra con su texto real**, nunca un
  "sin resultado" mudo; el mecanismo focal se guarda **al confirmar** con el radio `Mww`; el centroide
  **no** se escribe en `origin`.

### 8.5 Errores y casos borde

| # | caso | comportamiento exigido |
|---|---|---|
| E1 | Qt6 ausente al compilar | `make all` falla con mensaje que nombra `qt6-base-dev`/`qt6-base-dev-tools`/`qmake6` (no un error críptico de `g++`) |
| E2 | `.d` ausente / ilegible | stderr con la ruta, exit ≠ 0, sin ventana |
| E3 | clave obligatoria ausente | stderr con el nombre de la clave, exit ≠ 0 |
| E4 | anillo inexistente | `bpq_rings_open` devuelve `NULL` + nombre del anillo en `err`; fail-fast, la GUI no arranca |
| E5 | BD caída / `db.env` ausente | la app arranca en modo **solo vivo** (anillos), error legible en la barra de estado, `bpq_db_error()` no vacío, cero crashes |
| E6 | `GET_MISS`/`GET_NOTRACK` | parsear el mensaje (ya está copiado) y **seguir** drenando |
| E7 | `GET_NONE`/`GET_TOOBIG` | no parsear; en TOOBIG acotar `recsize` antes de escribir el NUL |
| E8 | mensaje de magnitud con más entradas que `'\n'` | no desbordar (copia con holgura a ceros + `csnhypodbp_magchan.c`); buffer del llamante intacto |
| E9 | store sin datos → SDS; SDS sin ese día | `wstore_get` 1 ⇒ intentar SDS; día ausente ⇒ **no abrir el archivo** (sin ruido de libmseed2); ambos sin datos ⇒ `has_data=0`, casilla vacía y no sensible |
| E10 | `SdsEnabled 0` o `SdsRoot` vacío | `csnhypodbp_sds_fetch` devuelve 0 (gate en `sds.c:60`); solo store |
| E11 | evento más antiguo que `SdsFallbackHours` | consulta directa al SDS y se omite la retención local |
| E12 | evento sin soluciones (`n=0`) | `preferred_index = -1` ⇒ guiones en el preview, `Confirm`/`Discard` insensibles, mapa sin marcador preferido |
| E13 | `locate`: timeout / `ref.err` / ARC inválido | estado `ERROR` con el texto real en el renglón del preview; con `LocateKeepTmp 1` se conserva el temporal y su ruta viaja en el error (`[tmp=…]`) |
| E14 | `locate` cancelado | se mata el hijo y su grupo de procesos; no quedan huérfanos; el estado vuelve a la solución elegida |
| E15 | `Confirm` con `qid<=0` o `version` fuera de 1..9999 | `csnhypodbp_confirmation_arc` falla ⇒ error visible y **nada** publicado |
| E16 | dos GUIs a la vez (mismo id 158) | prohibido mientras no haya id propio: aviso al arrancar si se detecta `csnhypodbp` vivo, y README |
| E17 | maximizar en WSLg | la ventana sobrevive 5 ciclos; el mínimo efectivo cabe en la pantalla (verificar con `BPQ_MODE=--layout`) |
| E18 | rebuild del store con una fila seleccionada | la selección se restaura por **`origin_id`**, no por índice (los `QModelIndex` se invalidan en un reset) |
| E19 | orden activo + llegada de eventos nuevos | el orden se mantiene (proxy), y la fila nueva no rompe la selección |
| E20 | `resizeEvent` durante un paint | no reallocar el `QImage` en medio del dibujo (doble buffering, recreación solo en resize) |
| E21 | texto inválido en una caja del panel Mww | no se aplica, se destoca el campo, y se conserva el valor anterior |
| E22 | motor de fase W falla en un paso del `run.sh` | se muestra **la orden que falló** y el texto real del motor (los pasos van marcados `##### paso N: … #####`) |
| E23 | `--print-config` sin `DISPLAY` | funciona (no se crea `QApplication`) |
| E24 | `StaFile` con estaciones sin datos y `HideStationsWithoutData 1` | se ocultan de la tabla (y solo de la tabla) |

---

## 9. Interfaces y firmas (contrato primero)

> Son **contratos**, no implementación. Los nombres de structs de datos reutilizados
> (`CsnhypodbpSolution`, `CsnhypodbpStaRow`, `CsnhypodbpMagRow`, `CsnhypodbpPreviewVals`,
> `CsnhypodbpMapPt`, `CsnhypodbpFrame`, `EwGuiTrace`, `EwGui*Row`, `EwGuiSearchFilter`,
> `CsnhypodbpSearch`, `CsnhypodbpManualPick`, `CsnhypodbpTrace`, `CsnhypodbpMagOut`,
> `CsnhypodbpPickFilter`, `CsnhypodbpMagOvrRow`, `CsnhypodbpLocateStageFn`) son los **ya existentes** en
> los headers de la lista blanca; el port no los redefine.

### 9.1 Nomenclatura

`bpq_` = **b**p (**c**sn**h**ypo**d**bp) + **q**t. Archivos C en `c/`, C++ en `ui/` con extensión `.cxx`
(para que `moc_*.cxx` quede dentro de `build/` y el grep de R1 sea trivial).

### 9.2 `c/bpq_capi.h`

```c
#ifndef BPQ_CAPI_H
#define BPQ_CAPI_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
/* lista blanca de headers puros (R2) */
#include <ewgui/wave.h>  /* EwGuiTrace, EwFilterParams */
#include <ewgui/db.h>    /* EwGui*Row, EwGuiSearchFilter */
#include "csnhypodbp_group.h"  "csnhypodbp_statable.h"  "csnhypodbp_preview.h"
#include "csnhypodbp_locate.h" "csnhypodbp_search.h"    "csnhypodbp_trace.h"
#include "csnhypodbp_manual.h" "csnhypodbp_mapview.h"   "csnhypodbp_profile.h"
#include "csnhypodbp_view.h"   "csnhypodbp_magrow.h"

/* ---------- configuración ---------- */
typedef struct BpqConfig BpqConfig;   /* struct plano público: 1 campo por clave del .d */
BpqConfig *bpq_config_load(const char *path, const char *state_dir_or_null, char *err, size_t errn);
void       bpq_config_free(BpqConfig *c);
int        bpq_config_print(const BpqConfig *c, FILE *out);          /* --print-config  */
int        bpq_config_check_profiles(const BpqConfig *c, FILE *out); /* --check-profiles */
int        bpq_config_has_key(const BpqConfig *c, const char *key);  /* test de paridad */
const char*bpq_config_state_dir(const BpqConfig *c);

/* ---------- anillos ---------- */
typedef struct BpqRings BpqRings;
typedef struct { int qid, mod; unsigned version; double value; int nsta;
                 char type[8], algorithm[16]; } BpqMagMsg;
typedef void (*BpqArcFn) (const CsnhypodbpSolution *sol, const char *raw, size_t rawlen, void *ud);
typedef void (*BpqMagFn) (const BpqMagMsg *m, void *ud);
typedef void (*BpqPickFn)(const CsnhypodbpTracePick *p, void *ud);
BpqRings  *bpq_rings_open(const BpqConfig *c, char *err, size_t errn);
void       bpq_rings_close(BpqRings *r);
int        bpq_rings_drain(BpqRings *r, BpqArcFn arc, BpqMagFn mag, BpqPickFn pick,
                           void *ud, char *err, size_t errn);  /* nº msgs; no deja el anillo a medias */
int        bpq_rings_heartbeat(BpqRings *r, double now_epoch);
int        bpq_rings_should_quit(BpqRings *r);
int        bpq_emit_pick(BpqRings *r, const char *sta, const char *chan, const char *net,
                         const char *loc, char phase, double t, char pref);
int        bpq_emit_mag(BpqRings *r, const BpqMagMsg *m, const char *text);
int        bpq_emit_confirm(BpqRings *r, const CsnhypodbpSolution *sol, char *arc, size_t arcn);
int        bpq_magmsg_parse(const char *raw, size_t len, BpqMagMsg *out); /* T1: copia + holgura */

/* ---------- base de datos (fachada de ewguidb_*) ---------- */
typedef struct BpqDb BpqDb;
BpqDb     *bpq_db_open(const BpqConfig *c, char *err, size_t errn);
void       bpq_db_close(BpqDb *db);
const char*bpq_db_error(const BpqDb *db);
int        bpq_db_recent  (BpqDb*, int hours, EwGuiEventRow  **out, int *n);
int        bpq_db_origins (BpqDb*, const char *event_id,  EwGuiOriginRow **out, int *n);
int        bpq_db_picks   (BpqDb*, const char *origin_id, EwGuiPickRow   **out, int *n);
int        bpq_db_mags    (BpqDb*, const char *origin_id, EwGuiMagRow    **out, int *n);
int        bpq_db_sta_mags(BpqDb*, const char *mag_id,    EwGuiStaMagRow **out, int *n);
int        bpq_db_search  (BpqDb*, const EwGuiSearchFilter *f, EwGuiMapEvent **out, int *n);
int        bpq_db_tensors (BpqDb*, const char *origin_id, EwGuiTensorRow **out, int *n);
void       bpq_db_rows_free(void *rows);
int        bpq_db_save_mww(BpqDb*, const char *event_id, const char *origin_id,
                           const struct BpqMwwResult *r, char *err, size_t errn);

/* ---------- store de eventos ---------- */
typedef struct BpqEvents BpqEvents;
typedef struct BpqEvent  BpqEvent;    /* fila maestra = solución preferida del grupo */
BpqEvents *bpq_events_new(const BpqConfig *c);
void       bpq_events_free(BpqEvents*);
int        bpq_events_feed_arc(BpqEvents*, const CsnhypodbpSolution *sol);
int        bpq_events_load_db (BpqEvents*, BpqDb*, int hours, char *err, size_t errn);
int        bpq_events_search  (BpqEvents*, BpqDb*, const CsnhypodbpSearch *q, char *err, size_t errn);
void       bpq_events_clear_search(BpqEvents*);
int        bpq_events_count(const BpqEvents*);
const BpqEvent *bpq_events_at(const BpqEvents*, int i);
int        bpq_event_nsols(const BpqEvent*);
const CsnhypodbpSolution *bpq_event_sol(const BpqEvent*, int j);
int        bpq_event_preferred(const BpqEvent*);      /* == csnhypodbp_preferred_index */
void       bpq_event_id(const BpqEvent*, char *out, size_t n);   /* event_id de la BD o firma */
int        bpq_event_ncells(void);                    /* 15, como la tabla GTK */
const char*bpq_event_header(int col);                 /* "Date","Lat.",…,"Ver" */
void       bpq_event_cell(const BpqEvent*, int col, char *out, size_t n);
int        bpq_event_sort_kind(int col);              /* 0 texto, 1 numérico, 2 fecha */
double     bpq_event_sort_num(const BpqEvent*, int col);

/* ---------- ondas ---------- */
typedef struct BpqWaves BpqWaves;
typedef struct { char sta[8], net[8], chan[8], loc[4]; double t0, t1; } BpqWaveReq;
BpqWaves  *bpq_waves_new(int max_sta);
void       bpq_waves_free(BpqWaves*);
int        bpq_waves_fetch(BpqWaves*, const BpqConfig*, const BpqWaveReq *req, int nreq,
                           char *err, size_t errn);   /* store→SDS; bloqueante: desde worker */
int        bpq_waves_set_raw(BpqWaves*, int idx, const void *trace2_i4, long len);
int        bpq_waves_has_data(const BpqWaves*, int idx);
EwGuiTrace*bpq_waves_trace(BpqWaves*, int idx);
int        bpq_waves_sta_rows(const BpqWaves*, const BpqConfig*, const CsnhypodbpSolution*,
                              const CsnhypodbpManualPick *mp, int nmp,
                              CsnhypodbpStaRow *out, int max);
int        bpq_waves_fetch_span(const BpqConfig*, const CsnhypodbpSolution*, double *t0, double *t1);

/* ---------- filtros de vista y STA/LTA ---------- */
typedef struct BpqFilters BpqFilters;   /* presets del .d (Filter/FilterMenu/FilterRaw/FilterPhase/StaLta) */
BpqFilters*bpq_filters_load(const BpqConfig *c, char *err, size_t errn);
void       bpq_filters_free(BpqFilters*);
int        bpq_filters_count(const BpqFilters*);
const char*bpq_filters_name(const BpqFilters*, int i);
char       bpq_filters_key(const BpqFilters*, int i);
int        bpq_filters_apply(BpqWaves*, const BpqFilters*, int idx);  /* idx<0 = Raw */
int        bpq_filters_stalta_toggle(BpqWaves*, const BpqFilters*);
int        bpq_filters_phase_zero(const BpqFilters*);

/* ---------- estado de picadas manuales ---------- */
typedef struct BpqManual BpqManual;
BpqManual *bpq_manual_new(const BpqConfig *c);            /* carga ManualStateFile */
void       bpq_manual_free(BpqManual*);
int        bpq_manual_set(BpqManual*, const char *sta, const char *chan, char phase, double t);
int        bpq_manual_clear(BpqManual*, const char *sta, char phase);
int        bpq_manual_save(const BpqManual*, char *err, size_t errn);
int        bpq_manual_list(const BpqManual*, CsnhypodbpManualPick *out, int max);

/* ---------- lienzos: cairo vive SOLO en c/ ---------- */
typedef enum { BPQ_SCENE_MAP_PLAN, BPQ_SCENE_MAP_PROF, BPQ_SCENE_TRACES, BPQ_SCENE_TRACE_AXIS,
               BPQ_SCENE_MAG, BPQ_SCENE_MAG_AXIS, BPQ_SCENE_MWW_BALL } BpqScene;
typedef enum { BPQ_FMT_SVG, BPQ_FMT_PDF, BPQ_FMT_PNG } BpqFmt;
typedef struct BpqMapCtx     BpqMapCtx;      /* imagen, overlays, zoom, centro, paneo, puntos, selección */
typedef struct BpqProfileCtx BpqProfileCtx;  /* perfil, slab, topo, frame, soluciones, selección */
typedef struct BpqTracesCtx  BpqTracesCtx;   /* waves, nsta, view, filtros, picks, STA/LTA, 6 colores */
typedef struct BpqMagCtx     BpqMagCtx;      /* ventana, franja t0/t1, traza procesada, eje */
typedef struct BpqMwwCtx     BpqMwwCtx;      /* playera (beach ball) */
int  bpq_scene_paint (BpqScene s, const void *ctx, unsigned char *bgra, int w, int h, int stride);
int  bpq_scene_export(BpqScene s, const void *ctx, const char *path, BpqFmt fmt,
                      int w, int h, double scale);
int  bpq_scene_min_size(BpqScene s, int *w, int *h);
const char *bpq_scene_name(BpqScene s);

/* ---------- locate / magnitudes ---------- */
typedef struct {
  CsnhypodbpSolution   base;
  CsnhypodbpManualPick mp[256];   int nmp;
  CsnhypodbpTrace  trace[256]; int ntr;
  CsnhypodbpMagOvrRow  ovr[64];   int novr;
  unsigned kind_mask;  double fix_depth_km;  int timeout_s;
  char selected[32];   char tmp_root[256];   char ew_params[256], ew_home[128], ew_version[32];
} BpqLocateReq;
int bpq_locate_req_build(const BpqWaves*, const CsnhypodbpStaRow *rows, int nrows,
                         const int (*pick_on)[2], const CsnhypodbpPickFilter *f,
                         const BpqConfig*, BpqLocateReq *out);
int bpq_locate_run(const BpqConfig*, const BpqLocateReq*, CsnhypodbpLocateStageFn st, void *ud,
                   volatile int *cancel, CsnhypodbpSolution *out, CsnhypodbpMagOut *mag,
                   char *err, size_t errn);
int bpq_mag_run(const BpqConfig*, const BpqLocateReq*, CsnhypodbpMagOut *out, char *err, size_t errn);

/* ---------- Mww (fase W) ---------- */
typedef struct BpqMww BpqMww;
typedef struct BpqMwwParams BpqMwwParams;
typedef struct BpqMwwResult BpqMwwResult;
BpqMww *bpq_mww_new(const BpqConfig*);
void    bpq_mww_free(BpqMww*);
int     bpq_mww_prep (BpqMww*, const BpqMwwParams*, char *err, size_t errn);
int     bpq_mww_start(BpqMww*, char *err, size_t errn);                    /* no bloqueante */
int     bpq_mww_poll (BpqMww*, BpqMwwResult *out, char *err, size_t errn); /* 0 corriendo,1 listo,<0 err */
void    bpq_mww_cancel(BpqMww*);
const char *bpq_mww_log_tail(const BpqMww*, int nlines);
int     bpq_mww_field_limits(int field, const BpqMwwParams*, double *lo, double *hi);
int     bpq_mww_field_parse (int field, const char *text, BpqMwwParams *inout);

#ifdef __cplusplus
}
#endif
#endif
```

**Nota de diseño sobre `void *ctx`:** los `BpqScene` reciben un puntero al struct de contexto
correspondiente. Se usa `void*` para que `bpq_capi.h` no exponga cairo; los structs de contexto son
**planos** (datos, punteros a arrays propios, sin tipos de backend) y se declaran completos en
`bpq_capi.h` para que la UI pueda rellenarlos. Alternativa rechazada: un `typedef void (*BpqDrawFn)(void
*cr, …)` visible desde C++ (pierde seguridad de tipos y tenta a escribir dibujo en C++).

### 9.3 Clases Qt6 (firmas)

```cpp
// ui/bpq_canvas_widget.h
class BpqCanvasWidget : public QWidget {
  Q_OBJECT
public:
  explicit BpqCanvasWidget(BpqScene scene, QWidget *parent = nullptr);
  ~BpqCanvasWidget() override;
  void   setContext(const void *ctx);        // el ctx debe vivir >= que el widget
  void   requestRepaint();                   // update() coalescente
  QImage grabImage() const;                  // tests + export
  BpqScene scene() const;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;    // bpq_scene_min_size: no inflar el mínimo (T6)
signals:
  void clicked(double x, double y);          // clic sin arrastre
  void dragged(double dx, double y2);         // paneo
  void zoomed(double factor, double atX, double atY);
  void markerHit(int index);                 // clic sobre un marcador (mapa/perfil)
protected:
  void paintEvent(QPaintEvent*) override;         // bpq_scene_paint -> QImage -> drawImage
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void resizeEvent(QResizeEvent*) override;       // único sitio que recrea el QImage (E20)
private:
  struct Impl; Impl *d;
};

// ui/bpq_events_model.h
class BpqEventsModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit BpqEventsModel(QObject *parent = nullptr);
  void setStore(BpqEvents *store);                       // no toma propiedad
  int  rowCount(const QModelIndex & = {}) const override;      // bpq_events_count
  int  columnCount(const QModelIndex & = {}) const override;   // bpq_event_ncells() == 15
  QVariant data(const QModelIndex&, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
  const BpqEvent *eventAt(int sourceRow) const;
  QString originIdAt(int sourceRow) const;               // restaurar selección (E18)
  int  rowForEventId(const QString &eventId) const;
public slots:
  void storeChanged();                                   // beginResetModel/endResetModel
};

class BpqSortProxy : public QSortFilterProxyModel {
  Q_OBJECT
public:
  bool lessThan(const QModelIndex &l, const QModelIndex &r) const override;
  // bpq_event_sort_kind(col): 0 texto (QString::localeAwareCompare), 1 numérico, 2 fecha
  // primer clic: descendente en numéricas/fechas, alfabético en texto; segundo: invierte
};

class BpqOriginsModel : public QAbstractTableModel {   // 1 fila por solución del evento (+Created)
  Q_OBJECT
public:
  void setEvent(const BpqEvent *ev);                   // nullptr => vacío
  QString originIdAt(int row) const;
  int  rowForOriginId(const QString&) const;
  // columnas literales: Mod,Ver,Est.,Created,Date,Lat.,Lon.,Dep,Res,#Stn,ID,Ml,Mwp,Mb,Ms
  // orden default: Created DESC
};

class BpqStaModel : public QAbstractTableModel {
  Q_OBJECT
public:
  void setRows(const CsnhypodbpStaRow *rows, int n);
  // 2 filas autónomas por estación (P y S); 5 columnas: station[☑], dist[km], pick, stat, res
  bool pickEnabled(int row, int col) const;            // Qt::ItemIsUserCheckable solo si la fase existe
  int  locatePicks(int (*pick_on)[2], CsnhypodbpPick *out, int max) const;  // csnhypodbp_locate_picks
};

class BpqMagModel : public QAbstractTableModel {
  Q_OBJECT
public:
  void setRows(const CsnhypodbpMagRow *rows, int n);
  // 8 columnas: station, dist[km], ancla, t0[s], t1[s], mag, res, stat
  // celdas de texto vía csnhypodbp_magrow_mag_text/res_text/stat_text/anchor_text/vent_text
};

class BpqRingWorker : public QObject {                 // vive en el hilo GUI
  Q_OBJECT
public:
  BpqRingWorker(BpqRings *r, BpqEvents *e, QObject *parent = nullptr);
  void start(); void stop();
signals:
  void eventsChanged(); void pickArrived(); void magnitudeArrived();
  void quitRequested(); void errorOccurred(QString);
};

class BpqFetchWorker : public QObject {                // moveToThread(QThread)
  Q_OBJECT
public slots: void fetch(BpqWaveBatch req); void cancel();
signals: void finished(BpqWaveBatchResult res);
};
class BpqLocateWorker : public QObject {               // moveToThread(QThread)
  Q_OBJECT
public slots: void run(BpqLocateReq req); void cancel();
signals: void stage(QString text); void elapsed(int secs);
         void finished(int rc, CsnhypodbpSolution sol, CsnhypodbpMagOut mag, QString err);
};
class BpqMwwWorker : public QObject {                  // moveToThread(QThread)
  Q_OBJECT
public slots: void run(BpqMwwParams p); void cancel();
signals: void logLine(QString); void progress(int step, int total, QString order);
         void finished(int rc, BpqMwwResult res, QString err);
};

class BpqPreviewPanel : public QWidget {
  Q_OBJECT
public:
  void render(const CsnhypodbpPreviewVals &v);         // UN SOLO render: estado+valores+color+botones
  void setMagPreferred(int kind);                      // 0..4 = ML,Mwp,Mb,Ms,Mww
signals: void confirmRequested(int magKind); void discardRequested(); void magPreferredChanged(int);
};

class BpqMainWindow : public QMainWindow {
  Q_OBJECT
public:
  BpqMainWindow(const BpqConfig *cfg, BpqRings *r, BpqDb *db, QWidget *parent = nullptr);
  QString dumpTree() const;                            // BPQ_MODE=--dump
  QString dumpLayout() const;                          // BPQ_MODE=--layout (mínimos efectivos)
  bool    saveShot(const QString &pngPath) const;       // BPQ_MODE=--shot
  bool    selectOrigin(const QString &originId);        // BPQ_MODE=--select
};

// ui/main.cxx
int main(int argc, char **argv);   // orden exacto §8.1; QApplication solo tras los flags headless
```

`Q_DECLARE_METATYPE(BpqWaveBatch)`, `Q_DECLARE_METATYPE(BpqWaveBatchResult)`,
`Q_DECLARE_METATYPE(BpqLocateReq)`, `Q_DECLARE_METATYPE(CsnhypodbpSolution)`,
`Q_DECLARE_METATYPE(CsnhypodbpMagOut)`, `Q_DECLARE_METATYPE(BpqMwwParams)`,
`Q_DECLARE_METATYPE(BpqMwwResult)` + `qRegisterMetaType<…>()` en `main()` antes de arrancar hilos.

### 9.4 Estructura de la ventana (paridad con §4.4)

```
QMainWindow "CSNhypodbpQT - Hypocenter database picker (EW8/Qt)"  default 1100x750
├─ QToolBar / QMenuBar: ☰ (Export view…, Colours, Filters…, Search events…, Clear search, Help→Commands…, modo oscuro)
├─ QSplitter[H] (reparto inicial una sola vez, sin setCollapsible(false) que infle el mínimo)
│  ├─ QTabWidget principal
│  │  ├─ "Eventos":  QSplitter[V]
│  │  │    ├─ cabecera "Events" + botón "Clear search" + QTableView(BpqSortProxy←BpqEventsModel)
│  │  │    └─ cabecera "Origins" + QTableView(BpqOriginsModel)
│  │  └─ "Procesar": BpqPreviewPanel sobre QTabWidget
│  │       ├─ "picking":    QHBoxLayout[ QTableView(BpqStaModel) | BpqCanvasWidget(TRACES) + (TRACE_AXIS) ]
│  │       │                + barra inferior: Cancel | filtro | A/M/Teo | depth/km/Fix | locators/locate
│  │       └─ "magnitudes": QStackedWidget{ "Estaciones" | "Mww" } + BpqCanvasWidget(MAG)+(MAG_AXIS)
│  │                        + barra: A/M/Teo | distancia[°] min/max | prof[km] min/max | reset default |
│  │                                 calculate | calculate all
│  └─ QSplitter[V] 65/35: BpqCanvasWidget(MAP_PLAN) / BpqCanvasWidget(MAP_PROF)
└─ QStatusBar: estado de BD/anillos/locate + errores legibles
```

Tablas: `QTableView` cableado con `ewguiqt::wire_header_sort()` (`commonqt`), que desactiva el
`setSortingEnabled` propio de Qt (no conoce ni la dirección del primer clic ni el comparador
compartido), conecta `sectionClicked` al `SortProxy` y mantiene la flecha del header en el orden
real. `setSelectionBehavior(SelectRows)`, `setSelectionMode(SingleSelection)`, y anchos acotados
(`setMinimumWidth` pequeño + `QHeaderView::ResizeToContents`/`Interactive`) para no inflar el mínimo de la
ventana (T6). Separadores de fila/columna con `setShowGrid(true)` + `QSS` (paridad estética con las tablas
GTK).

---

## 10. Contrato del Makefile

```make
# MOD_GUI: qt6 — marcador deliberado para ew8_compile.sh:377 (grep 'gtk4|libadwaita'): este modulo ES GUI
.DEFAULT_GOAL := all
CC      ?= gcc
CXX     ?= g++
AR      ?= ar
BUILDDIR := build

# --- deteccion Qt6 con fail-fast (E1) ---
# QMAKE ?= qmake6  (si no: pkg-config --exists Qt6Widgets)
# QT_INC := $(shell $(QMAKE) -query QT_INSTALL_HEADERS)
# QT_LIB := $(shell $(QMAKE) -query QT_INSTALL_LIBS)
# QT_ARCH:= $(shell $(QMAKE) -query QT_INSTALL_ARCHDATA)   # mkspecs
# MOC    := $(QT_HOST_LIBEXECS)/moc  (fallback: /usr/lib/qt6/libexec/moc, $(shell which moc))
# si vacio: $(error Faltan herramientas Qt6. Instala: sudo apt install qt6-base-dev qt6-base-dev-tools libxcb-cursor0)

EWINC    := $(EW_HOME)/$(EW_VERSION)/include
EWLIBDIR := $(EW_HOME)/$(EW_VERSION)/lib

COMMON_PURE := ../common/src/ewgui_wave.c ewgui_dsp.c ewgui_geo.c ewgui_sds.c ewgui_sta.c \
               ewgui_config.c ewgui_cfgfile.c ewgui_db.c ewgui_db_ids.c ewgui_ring.c \
               ewgui_stalta.c ewgui_table_cmp.c ewgui_overlay.c ewgui_version.c
BP_PURE     := ../csnhypodbp/csnhypodbp_group.c … (los 24 de §4.1, SIN row/staitem/magitem/master)

# C: mismos flags que csnhypodbp/Makefile:7 MENOS gtk4/libadwaita, MAS cairo
CFLAGS   = -Wall -O2 -g -fPIC $(shell pkg-config --cflags cairo libpq) -I$(EWINC) \
           -I../common/include -I../csnhypodbp -I../csnloc -I../wavestore -I../csnwphase -Ic \
           -D_LINUX -D_REENTRANT -fno-stack-protector -U_FORTIFY_SOURCE \
           -MMD -MP -MF $(BUILDDIR)/$(@F).dep
# C++: Qt6 Widgets/Gui/Core, C++17, SIN -I a headers C/EW (R1)
CXXFLAGS = -Wall -O2 -g -fPIC -std=c++17 -fPIC \
           -I$(QT_INC) -I$(QT_INC)/QtWidgets -I$(QT_INC)/QtGui -I$(QT_INC)/QtCore \
           -I$(QT_ARCH)/mkspecs/linux-g++ -Ic -Iui \
           -MMD -MP -MF $(BUILDDIR)/$(@F).dep
LDFLAGS  = -L$(QT_LIB) -lQt6Widgets -lQt6Gui -lQt6Core $(shell pkg-config --libs cairo libpq) -lm -lpthread
EWLIBS   = ../csnwphase/libcsnwphase.a ../csnloc/libcsnloc_tt.a ../wavestore/libwavestore.a \
           $(EWLIBDIR)/libmseed2.a $(EWLIBDIR)/rw_mag.o $(EWLIBDIR)/libew_mt.a
# NOTA: NO se enlaza ../common/libewgui_gtk4.a (arrastraría GTK4+libadwaita) ⇒ R4/AC2

all: csnhypodbpQT install
install: csnhypodbpQT
	sh ../common/tools/install_bin.sh $(EW_HOME)/$(EW_VERSION)/bin csnhypodbpQT
csnhypodbpQT: $(OBJ_C) $(OBJ_CXX) $(MOC_OBJ) $(EWLIBS)
	$(CXX) -o $@ $(OBJ_C) $(OBJ_CXX) $(MOC_OBJ) $(EWLIBS) $(LDFLAGS)   # el link lo dirige g++

# objetos ajenos: NUNCA escribir .o fuera de build/ (R3)
$(BUILDDIR)/bp_%.o: ../csnhypodbp/csnhypodbp_%.c | $(BUILDDIR) ; $(CC) $(CFLAGS) -c $< -o $@
$(BUILDDIR)/cw_%.o: ../common/src/ewgui_%.c          | $(BUILDDIR) ; $(CC) $(CFLAGS) -c $< -o $@
$(BUILDDIR)/c_%.o:  c/%.c                            | $(BUILDDIR) ; $(CC) $(CFLAGS) -c $< -o $@
$(BUILDDIR)/ui_%.o: ui/%.cxx                         | $(BUILDDIR) ; $(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILDDIR)/moc_%.cxx: ui/%.h | $(BUILDDIR) ; $(MOC) $(MOCFLAGS) $< -o $@
# MOCFLAGS lleva los mismos -D/-I del CXX (moc los necesita; define Q_MOC_RUN)

# sub-make de las .a ajenas (necesario: orden alfabetico de ew8_compile.sh:381)
../csnloc/libcsnloc_tt.a:    ; $(MAKE) -C ../csnloc libcsnloc_tt.a
../wavestore/libwavestore.a: ; $(MAKE) -C ../wavestore libwavestore.a
../csnwphase/libcsnwphase.a: ; $(MAKE) -C ../csnwphase libcsnwphase.a

check: $(TESTS_C) $(TESTS_CXX) ; @for t in $^; do ./$$t || exit 1; done ; sh tests/test_scripts.sh
test: check
test-live: csnhypodbpQT ; sh tests/test_live_events.sh ; sh tests/test_maximize.sh
clean: ; rm -rf $(BUILDDIR) csnhypodbpQT $(TESTS_C) $(TESTS_CXX)
clean-deps: ; rm -f $(BUILDDIR)/*.dep
VALIDATE_FLAGS = csnhypodbpQT|--print-config csnhypodbp.d\ncsnhypodbpQT|--check-profiles csnhypodbp.d
print-VALIDATE_FLAGS: ; @printf '%s\n' '$(VALIDATE_FLAGS)'
-include $(wildcard $(BUILDDIR)/*.dep)
```

**Checklist de conformidad con los invariantes del repo:**

- [ ] `-MMD`, `-MP`, `-MF`, `.dep`, `-include $(wildcard …)`, `clean-deps` → `test_build_deps.sh` (T11)
- [ ] `timeout` solo por ruta absoluta en los `.sh` → `test_timeout_gnu.sh` (T5)
- [ ] receta `sh … install_bin.sh` reconocible por `mk_bins` (`ew8_compile.sh:325`) → `check_install.sh`
- [ ] `print-VALIDATE_FLAGS` + `.d` existente en `PARAMS_DIR` (`ew8_compile.sh:861-875`)
- [ ] `.DEFAULT_GOAL := all` (T10)
- [ ] ningún artefacto fuera de `build/` (§6.4)
- [ ] `--print-config`/`--check-profiles` no requieren `DISPLAY`

---

## 11. Criterios de aceptación

| # | criterio medible | test que lo verifica |
|---|---|---|
| **AC1** | `make -C ew_gui_tools/csnhypodbpQT all` compila e instala `ew_8.0/bin/csnhypodbpQT`; **ningún archivo fuera de `csnhypodbpQT/` cambia** | `tests/test_no_touch.sh` (`git status --porcelain` acotado a rutas permitidas) + `sh common/tools/check_install.sh -v` |
| **AC2** | El binario **no enlaza** `libgtk-4`, `libadwaita`, `libgobject`, `libgio`. (`libglib-2.0` sí aparece en `ldd`: llega por `libQt6Gui`/`QEventDispatcherGlib`, no por nuestro código ni por GTK — verificado en la fase 0) | `tests/test_no_gtk_link.sh` (`ldd` + grep) |
| **AC3** | Ningún TU C++ incluye C/EW/cairo/glib/GTK; `bpq_capi.h` es C++-safe | `tests/test_capi_purity.sh`: `g++ -std=c++17 -fsyntax-only tests/probe_capi.cxx` + grep de includes prohibidos en `ui/` |
| **AC4** | Los 24 `.c` y los headers de la lista blanca siguen sin `gtk/gtk.h` | `tests/test_reuse_purity.sh` (lista explícita de archivos, no un grep recursivo) |
| **AC5** | El Makefile cumple los invariantes del repo y no rompe el `check` de `common` | `make -C ew_gui_tools/common check` **sin modificar** `common` |
| **AC6** | `--print-config csnhypodbp.d` sale 0 **sin DISPLAY** y reporta el mismo conjunto clave/valor que `csnhypodbp --print-config csnhypodbp.d` | `tests/test_printconfig_parity.sh` (diff de salidas normalizadas y ordenadas) |
| **AC7** | `--check-profiles` lista los mismos perfiles (nombre, geometría, nº de puntos de slab/topo, truncamientos) | `tests/test_checkprofiles_parity.sh` |
| **AC8** | Mismo ARC ⇒ mismos `qid/version/mod/otime/lat/lon/depth/rms/nph/picks`, mismo agrupamiento y misma solución preferida | `tests/test_capi_events.c` (valores esperados literales, comparables con `test_parse_solution.c`/`test_group.c` existentes) |
| **AC9** | Mensajes de salida **byte-idénticos**: pick `"%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 %c M %c\n"`; ARC con byte 174 `'M'`, qid en 136, ver en 178, 122..135 limpio, vmodel en 182 | `tests/test_capi_pickmsg.c`, `tests/test_capi_arc.c` |
| **AC10** | Parseo de `TYPE_MAGNITUDE` sin desbordar (mensaje con más entradas que `'\n'`; buffer del llamante intacto) | `tests/test_capi_magmsg.c` + `check-valgrind` |
| **AC11** | Con EarthWorm en marcha, la maestra se puebla en ≤2 s desde la llegada del ARC, y `should_quit` cierra la app | `tests/test_live_events.sh` (solo `--live`; compara contra `EWGUI_MODE=--dump` de `csnhypodbp`) |
| **AC12** | Cabeceras literales idénticas a las GTK; orden por cabecera correcto (1.er clic descendente en numéricas/fechas y alfabético en texto; 2.º invierte); selección conservada por `origin_id` tras rebuild | `tests/test_models.cxx` (offscreen) |
| **AC13** | Los 7 lienzos renderizan offscreen a imagen no vacía con los píxeles esperados (traza negra, color de marca por origen `M/A/Teo`, overlay visible, marcadores preferida/candidata/elegida-verde) | `tests/test_canvas_offscreen.cxx`, `tests/test_draw_{map,profile,traces,axis,mag}.c` |
| **AC14** | El `locate` offline produce la misma solución que `csnhypodbp` para el mismo ARC+picks; timeout/cancel dejan el error real | `tests/test_locate_chain.sh` (arnés `locate_chain`) + `tests/test_locate_worker.cxx` |
| **AC15** | El fetch respeta store→SDS, `SdsEnabled`, `SdsFallbackHours`, y **no bloquea el hilo GUI** | `tests/test_capi_waves.c`, `tests/test_fetch_worker.cxx` |
| **AC16** | `ew8_compile.sh --list` incluye `csnhypodbpQT` con check/test/validate derivados; `ew8_compile.sh csnhypodbpQT --check --validate` verde | `tests/test_integration.sh` + esos comandos |
| **AC17** | **No regresión**: `make -C ew_gui_tools/csnhypodbp test` y `ew8_compile.sh --no-build --check` siguen pasando idénticos | esos dos comandos |
| **AC18** | La GUI sobrevive 5 ciclos maximizar/des-maximizar en WSLg y el mínimo efectivo cabe en la pantalla | `tests/test_maximize.sh` (`--live`, con `BPQ_MAXIMIZE_MS` y `BPQ_MODE=--layout`) |

---

## 12. Plan por fases

Cada fase deja el módulo **compilando, instalando y con sus tests verdes**. Formato de tarea:
`id | archivos objetivo | interfaz/firma | test | criterio de done`.

### Fase 0 — Andamio, build y modos headless

| id | archivos objetivo | interfaz/firma | test | done |
|---|---|---|---|---|
| 0.1 | `csnhypodbpQT/Makefile`, `README.md`, `build/` | contrato §10 | `common/tests/test_build_deps.sh`, `check_install.sh -v` | `make all` instala el binario; sin Qt6 falla nombrando `qt6-base-dev`; nada se escribe fuera de `build/`; sub-makes de las `.a` ajenas funcionan |
| 0.2 | `c/bpq_capi.h`, `c/bpq_config.c` | `BpqConfig`, `bpq_config_load/free/print/check_profiles/has_key/state_dir`; define `g_sds_enabled`/`g_sds_root` | `tests/test_capi_config.c` | parsea `params/csnhypodbp.d` completo (las ~50 claves); obligatoria ausente ⇒ error con nombre; desconocida ⇒ ignorada; `--state-dir` sobrescribe `StateFile`/`ManualStateFile`/`MwwWorkDir`/tmp |
| 0.3 | `ui/main.cxx`, `tests/probe_capi.cxx` | `int main(int,char**)` con el orden exacto de §8.1 | `tests/test_capi_purity.sh` | `g++ -fsyntax-only` pasa; cero includes prohibidos en `ui/`; los flags headless no crean `QApplication` |
| 0.4 | `ui/bpq_mainwindow.{h,cxx}`, `ui/bpq_canvas_widget.{h,cxx}`, `c/bpq_canvas.c`, `c/bpq_export.c` | `BpqMainWindow`, `BpqCanvasWidget`, `bpq_scene_paint/export/min_size` | `tests/test_canvas_smoke.cxx` (offscreen) | ventana con pestañas `Eventos`/`Procesar` visible offscreen; `bpq_scene_paint` llena el buffer (no todo ceros); export SVG/PDF/PNG válidos y no vacíos |
| 0.5 | `tests/test_{printconfig_parity,d_parity,no_touch,no_gtk_link}.sh`, `VALIDATE_FLAGS` | `VALIDATE_FLAGS = csnhypodbpQT\|--print-config csnhypodbp.d` + `\|--check-profiles csnhypodbp.d` | ellos mismos + `ew8_compile.sh csnhypodbpQT --validate` | AC1, AC2, AC6, AC7 verdes |

### Fase 1 — Capa C de datos (anillos, BD, eventos, emisión)

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 1.1 | `c/bpq_rings.c` | `bpq_rings_open/close/drain/heartbeat/should_quit` + `BpqArcFn/BpqMagFn/BpqPickFn` | `tests/test_capi_rings.c` (anillo propio del arnés, patrón `TEST_CTRL_RING` de `ew_controller`) | drena `GET_OK/MISS/NOTRACK` parseando y siguiendo; `NONE/TOOBIG` sin parsear; `recsize` acotado; heartbeat cada `HeartBeatInt`; `should_quit` con flag TERMINATE y con pid propio |
| 1.2 | `c/bpq_magmsg.c` | `bpq_magmsg_parse(raw,len,BpqMagMsg*)` sobre `csnhypodbp_magchan.c` | `tests/test_capi_magmsg.c` + valgrind | AC10; invariante: ninguna llamada directa a `rd_chan_mag`/`rd_mag` fuera de `csnhypodbp_magchan.c` |
| 1.3 | `c/bpq_events.c` | `BpqEvents`/`BpqEvent` + las 14 `bpq_event*`/`bpq_events*` | `tests/test_capi_events.c` | AC8; celdas y cabeceras idénticas a la tabla GTK; `GroupPickTolSec`/`GroupMinMatchPicks`/`GroupMaxSolutions` respetados; búsqueda reemplaza la vista y `clear_search` restaura |
| 1.4 | `c/bpq_db.c` | `bpq_db_*` (fachada `ewguidb_*`) | `tests/test_capi_db.c` (SKIP sin `EWGUI_DB_TEST_ENV=1` / `--db`) | mismas filas que `ewguidb_query_*`; residuales desde `pick.uncertainty`; BD caída ⇒ error legible y app viva |
| 1.5 | `c/bpq_emit.c` | `bpq_emit_pick/mag/confirm` | `tests/test_capi_pickmsg.c`, `tests/test_capi_arc.c` | AC9; `logo.mod = sol->mod` en el Confirm y `MyModId` en picks/magnitudes (paridad con `ws.c:1700/1732/1771`) |

### Fase 2 — Pestaña Eventos + mapa/perfil + vivo

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 2.1 | `ui/bpq_events_model.cxx`, `ui/bpq_sort_proxy.cxx` | `BpqEventsModel`, `BpqSortProxy::lessThan` | `tests/test_models.cxx` | AC12 (maestro): 15 columnas literales, orden y desempate |
| 2.2 | `ui/bpq_origins_model.cxx` | `BpqOriginsModel` (+`Created`, default `Created DESC`) | `tests/test_models.cxx` | AC12 (detalle); clic en marcador selecciona el origen y el marcador verde sustituye al rojo |
| 2.3 | `c/bpq_draw_map.c`, `ui/bpq_map_panel.cxx` | `bpq_draw_map_plan(cairo_t*,int,int,void*)` + `BpqMapCtx` | `tests/test_draw_map.c` (PNG offscreen + píxeles esperados) | reutiliza `csnhypodbp_map_bbox/mapfit/mapfit_at/map_screen/map_view_bounds`, `ewgui_geo_project`, `ewgui_overlay_draw`; zoom rueda, paneo arrastre, `MapMinSpanDeg`, `OverlayHide`, `MapSplit` 65/35 |
| 2.4 | `c/bpq_draw_profile.c`, `ui/bpq_profile_panel.cxx` | `bpq_draw_map_profile(...)` + `BpqProfileCtx` | `tests/test_draw_profile.c` | reutiliza `profile_nearest/profile_frame/frame_project` + carga de `slab*.tmp`/`topo*.tmp` (`ProfileCols`); encuadre inicial solo a datos |
| 2.5 | `ui/bpq_ring_worker.cxx` + wiring en `bpq_mainwindow.cxx` | `BpqRingWorker` (500 ms / 1 s) | `tests/gui_smoke.sh` (offscreen), `tests/test_live_events.sh` (`--live`) | AC11; offscreen arranca y no cuelga; live puebla en ≤2 s y cierra con `should_quit` |

### Fase 3 — Ondas, tabla de estaciones, búsqueda

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 3.1 | `c/bpq_waves.c` | `BpqWaves` + `fetch/set_raw/has_data/trace/sta_rows/fetch_span` | `tests/test_capi_waves.c` | AC15 (parte store/SDS): mapeo 0/1/<0 y 1/0/<0; `TRACE2_HEADER`+`i4` correcto; día ausente en SDS no abre archivo |
| 3.2 | `ui/bpq_fetch_worker.cxx` | `BpqFetchWorker` + `qRegisterMetaType<BpqWaveBatch/Result>` | `tests/test_fetch_worker.cxx` (offscreen + store sintético en tmp) | AC15 (no bloquea UI); `FetchThreads`; cancelación al cambiar de evento; debounce 1 s |
| 3.3 | `ui/bpq_sta_model.cxx` | `BpqStaModel` (5 cols, 2 filas/estación, checkable) | `tests/test_sta_model.cxx` | reutiliza `statable_build`+`stapick_one` (M>A>Teo)+`resid_source`; casilla vacía y no sensible sin fase; `pick_allowed`/`locate_picks` filtran el envío; residuales sin pulsar `locate` |
| 3.4 | `ui/bpq_search_dialog.cxx` | modal → `csnhypodbp_search_build` + `bpq_db_search` | `tests/test_search_dialog.cxx` | tiempo obligatorio; `SearchMaxEvents` acota; resultado reemplaza la vista; `Clear search` restaura; calendario conserva la hora (o `00:00:00` si no venía); error legible si falta el rango |
| 3.5 | `c/bpq_sta.c` | carga de `StaFile` (`ewgui/sta.h`) + distancias km/° | `tests/test_capi_sta.c` | misma lista y orden que la versión GTK (`csnhypodbp_station.c`); `HideStationsWithoutData` respetado |

### Fase 4 — Picking (trazas, filtros, STA/LTA, repick manual)

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 4.1 | `c/bpq_draw_traces.c` | `bpq_draw_traces(cairo_t*,int,int,void*)` + `BpqTracesCtx` | `tests/test_draw_traces.c` | AC13; traza siempre negra; marca P/S coloreada por origen; 6 claves de color y derivación 40 %/70 % cuando faltan; polilínea/muestreo con `view_use_polyline/sample_x/min_half/band_runs`; envolvente con `ewgui_trace_envelope` |
| 4.2 | `c/bpq_draw_axis.c` | `bpq_draw_trace_axis(...)` | `tests/test_draw_axis.c` | ticks 1/2/5×10^k (`view_tick_step`) y rótulos UTC (`view_fmt_utc`) idénticos para la misma ventana |
| 4.3 | `ui/bpq_wave_panel.cxx` | widget + atajos `1/2/3/4/r/k/d/f/0/t` | `tests/test_shortcuts.cxx` | solo en `picking`; no se consumen con foco en caja de texto; la tecla del filtro activo lo apaga; `f` abre `Filters…`; zoom/paneo/clic |
| 4.4 | `c/bpq_filters.c` | `bpq_filters_load/apply/stalta_toggle/phase_zero` | `tests/test_filters.c` | presets del `.d`; `FilterPhase zero` ⇒ forward+backward; `fc·n/fs<0.5` ⇒ no-op; STA/LTA visual (CF + marca en `t_disparo+corr`) sin crear picks; el filtro es solo de vista |
| 4.5 | `ui/bpq_manual_pick.cxx`, `c/bpq_manual.c` | `bpq_manual_new/set/clear/save/list` | `tests/test_manual_state.c` | clic coloca/mueve el pick de la fase activa (`p`/`S`); estado persiste en el `ManualStateFile` del prototipo; tras picar a mano el ranking pasa a `M` |

**Corte recomendado: aquí.** Con las fases 0-4 hay un prototipo operativamente comparable a la GUI GTK
(eventos vivos + historial + búsqueda + mapas + perfiles + picking + repick manual), suficiente para
decidir si se migra.

### Fase 5 — Preview, locate, Confirm/Discard, export

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 5.1 | `ui/bpq_preview_panel.cxx` | `BpqPreviewPanel::render(const CsnhypodbpPreviewVals&)` | `tests/test_preview_panel.cxx` | **un solo render**; radios ML/Mwp/Mb/Ms/Mww; fondo leve candidata / ámbar elegida / guiones sin selección; `Confirm`/`Discard` sensibles solo con candidata; `Discard` vuelve a la elegida |
| 5.2 | `ui/bpq_locate_worker.cxx` | `BpqLocateWorker::run/cancel` + señales `stage/elapsed/finished` | `tests/test_locate_worker.cxx` (offline) | spinner + `calculando <loc>… N s` (timer 1 s); `LocatorTimeoutSec`; error real (`timeout`/`ref.err`/ARC inválido) en el mismo renglón; cancelación sin hijos huérfanos |
| 5.3 | `c/bpq_locate_req.c` | `bpq_locate_req_build`, `bpq_locate_run`, `bpq_mag_run` | `tests/test_locate_req.c`, `tests/test_locate_chain.sh` | AC14; se manda exactamente lo de la tabla (con canal, filtrado por A/M/Teo y casillas, `MaxDistKm`), con traza **cruda**; `Fix`/`FixedDepthKm`; `WorkDir` propio del refinador; ARC sembrado con la solución elegida |
| 5.4 | `ui/bpq_confirm.cxx` | `Confirm`/`Discard` → `bpq_emit_confirm/mag/pick` | `tests/test_confirm.c`, `tests/test_live_confirm.sh` (`--live` + BD de prueba) | ARC publicado byte-idéntico al de `csnhypodbp_confirm_manual`; `hypodb` lo guarda como fila `origin` `<event>:<mod>:<version>`; la confirmación más reciente pasa a `preferred_origin_id`; `qver`/`qid` correctos |
| 5.5 | `c/bpq_export.c` + ítem `Export view…` | `bpq_scene_export(scene,ctx,path,fmt,w,h,scale)` | `tests/test_export.c` | SVG/PDF/PNG no vacíos para los 7 lienzos; documentado por qué no se reutiliza `ewgui_export.c` (§4.2) |

### Fase 6 — Magnitudes, Mww e integración final

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 6.1 | `ui/bpq_mag_model.cxx`, `ui/bpq_mag_panel.cxx`, `c/bpq_draw_mag.c` | `BpqMagModel` (8 cols), `bpq_draw_mag`/`bpq_draw_mag_axis`, franja arrastrable | `tests/test_mag_model.cxx`, `tests/test_draw_mag.c` | `magrow_build/_counts/_*_text`; ventana anclada en el `t_pick` de la P efectiva y franja del panel si existe; override de puerta en `QLineEdit` acotado (ancho = valor canónico + 2), aplicado solo al confirmar el campo y si los 4 valen; rótulos/cajas al 72 % y botones al 90 % (paridad estética) |
| 6.2 | `c/bpq_magcfg.c` | fachada de `csnhypodbp_magcfg.c` (`MagConfigFile`) | `tests/test_magcfg.c` | mismas ventanas y puertas (`Ml/Mwp/Mb/Ms_MinDelta/MaxDelta`, `Ml_MaxDist`, `Ml_DistType`); la GUI no lista estaciones descartadas por el motor; `MagFetchSeconds` |
| 6.3 | `c/bpq_mww.c`, `ui/bpq_mww_panel.cxx`, `ui/bpq_mww_worker.cxx` | `bpq_mww_new/prep/start/poll/cancel/log_tail/field_limits/field_parse`, `BpqMwwWorker` | `tests/test_mww.c` (+ `--slow` contra el motor real) | tabla editable con límites/parseo del núcleo (texto inválido no se aplica y destoca); banda derivada de la magnitud y recalculada; grid search; resumen y salida lado a lado con la misma altura (`MWW_ROW_H` 248 px, 1/3–2/3, fuente 134 %/91 %); `GsHdsafe`/`GsOld`/`GsTsAuto`; todo fallo con su texto real y la orden que falló |
| 6.4 | `c/bpq_mww_save.c` | `bpq_db_save_mww(event_id,origin_id,BpqMwwResult*)` | `tests/test_mww_save.c` (BD de prueba) | con el radio `Mww` + `Confirm`: `upsert_event`, `upsert_origin_if_absent`, `upsert_magnitude(Mww)`, `upsert_tensor`, `set_preferred`; el **centroide no** se escribe en `origin` |
| 6.5 | `README.md`, `tests/test_integration.sh`, `tests/test_maximize.sh` + archivos existentes | `earthworm.d.base`: `Module MOD_CSNHYPODBPQT 172`; `.gitignore`: runtime `/run_working_v8/csnhypodbpqt/`; `startstop_unix.d`: `Process "csnhypodbpQT csnhypodbpqt.d"` **activo**; nuevo `params/csnhypodbpqt.d`; `test_params_stays_clean.sh` §6 | `tests/test_integration.sh` (16 checks), `tests/test_maximize.sh` (5 ciclos), `test_params_stays_clean.sh` | **hecho**; `--validate` OK=5; `make check` verde (991 checks) |

### Fase 7 — Origen fijado (pin)

| id | archivos | interfaz | test | done |
|---|---|---|---|---|
| 7.1 | `c/bpq_pin.{h,c}` (nuevo) | `BpqPin`, `bpq_pin_init/set/clear/active/key/matches/reconcile`; `bpq_origin_key()` como **único** formato `<qid>:<mod>:<version>` | `tests/test_pin.c` | **hecho** (29 checks) |
| 7.2 | `ui/bpq_events_model.{h,cxx}` | `eventKeyAt/rowForEventKey` (identidad estable `qid` + 1.ª `otime`); `keyAt` vía `bpq_origin_key` | `tests/test_pin.sh` `refresh/feed_after` | **hecho**; una confirmación que cambia la preferida no pierde evento ni pin |
| 7.3 | `ui/bpq_mainwindow.{h,cxx}`, `ui/main.cxx` | `m_pin/m_pinEvent`, `onOriginDoubleClicked`, `pinOrigin(force, switchTab)`, `pinnedSolutionIndex`, `solutionIndexForKey`, `m_syncOrigins`, `m_curSolKey`, `m_candKey`; hooks `BPQ_TEST_FIXTURE/PIN/CLICK/REFRESH/FEED_AFTER/EVENT/PIN_AGAIN/CAND` y `BPQ_WAVE_WAIT_MS`; dump `pin key/ev/tab/fetches/sol` | `tests/test_pin.sh` (45 checks), `tests/test_pin_invariants.sh` (12) | **hecho**; doble clic fija y lleva a Procesar/picking; el clic simple no mueve; el refresh no re-descarga ondas; la candidata se compara por clave; `Discard` vuelve al pin |
| 7.4 | `README.md`, `Makefile` | `test_pin` en `TEST_C`; `test_pin*.sh` en `check-scripts` | `make check` | **hecho**; total **1077 checks** (1004 C + 16 integración + 12 invariantes + 45 e2e) |

**Decisiones de la fase 7:** el pin solo se cambia **fijando otro origen** (doble clic o
clic en un marcador de planta); el clic simple y el teclado se revierten a la fila
fijada; se suelta al cambiar de evento, al buscar/limpiar o si la solución desaparece;
la candidata del `locate` conserva la precedencia y se identifica por clave (el store
reordena); `Confirm` no mueve el pin. Estado **en memoria**, sin persistencia.

---

## 13. Riesgos y mitigaciones

| # | riesgo | probabilidad / impacto | mitigación |
|---|---|---|---|
| R-1 | **`-fPIC`/PIE al enlazar `.a` C desde `g++`**: las `.a` del repo se compilan sin `-fPIC` explícito, aunque hoy enlazan en un ejecutable PIE (`csnhypodbp/Makefile:7,54`) | media / alto (bloquea el link) | `-fPIC` en todos nuestros objetos C y C++; si aparece `relocation R_X86_64_32 against …`, **no** se parchea la `.a` ajena sin permiso: se documenta y se pide añadir `-fPIC` al Makefile del módulo dueño (archivo existente ⇒ permiso) |
| R-2 | **Headers EarthWorm no C++-safe** (`earthworm.h`, `transport.h`, `kom.h`, `rw_mag.h`, `trace_buf.h`, `libmseed2.h` podrían usar palabras clave C++ o macros hostiles) | media / alto | se elimina por construcción: R1 prohíbe incluirlos desde C++ y `test_capi_purity.sh` lo verifica en cada `check` |
| R-3 | **Deriva de la lista blanca**: si alguien añade glib/GTK a un header puro de `csnhypodbp` o `common`, el prototipo deja de compilar | media / medio | `probe_capi.cxx` falla ruidosamente en `check` (no en silencio); `test_reuse_purity.sh` da el archivo culpable |
| R-4 | **`ew8_compile.sh` no sabe de Qt**: sin Qt6 el build moriría con un error de `g++` opaco | alta (en máquina nueva) / bajo | fail-fast en el Makefile con el nombre de los paquetes apt (E1, tarea 0.1) |
| R-5 | **Duplicar un productor de anillos**: el prototipo escribe en `PICK_RING` y `HYPO_RING_REF2` | baja / alto | **resuelto en 6.5**: id propio 172; las dos GUIs pueden correr a la vez (comparten anillos; solo escriben al picar/confirmar) |
| R-6 | **`restart <nombre>` al primer match** (`startstop_unix_generic.c:2478/:2497`, documentado en `AGENTS.md`) si se declaran dos `Process` con nombre parecido | media / medio | nombre claramente distinto (`csnhypodbpQT`) y, en todo caso, las acciones por pid (ya es la política de `ew_monitor.sh`/`ew_controller`) |
| R-7 | **Cairo/`QImage`: stride y premultiplicación** mal manejados ⇒ colores raros o imagen rayada | media / medio | usar `img.bytesPerLine()` (nunca `4*w`), `Format_ARGB32_Premultiplied`, `cairo_surface_finish()` antes de blitear, y `test_canvas_offscreen.cxx` con píxeles de color esperados |
| R-8 | **Rendimiento del repintado** (trazas con miles de muestras a 60 Hz de paneo) | media / medio | doble buffering con `QImage` reutilizado, `ewgui_trace_envelope` (min/max por columna con caché) en vez de dibujar muestra a muestra, `update()` coalescente; medir con `BPQ_MODE=--shot` y tiempos en el test |
| R-9 | **Trabajo subestimado en el dibujo**: son ~1.200 líneas cairo dentro de `gui.c`/`mww.c` a reexpresar | alta / medio | fases 2/4/6 lo reparten; empezar por mapa (el más simple) y dejar Mww para el final; los tests de píxeles evitan regressions |
| R-10 | **`GetModId` exige el nombre en `earthworm.d`** ⇒ sin id propio no hay identidad separada | certeza / bajo | **resuelto en 6.5**: `MOD_CSNHYPODBPQT 172` en `earthworm.d.base`; el `.d` vivo se regenera con `worktree_bootstrap.sh --refresh` |
| R-11 | **`hypodb` sobreescribe el `origin` confirmado** si las dos GUIs confirman el mismo evento+versión (`hypodb_ingest.c:614,642`) | baja / bajo | comportamiento idéntico al actual (el `mod` del Confirm es `sol->mod`, no el de la GUI); documentado |
| R-12 | **Qt6 y WSLg**: plugin `xcb` necesita `libxcb-cursor0` desde 6.5; en Wayland los menús necesitan transient parent | media / medio | paquete en el prerequisito; `QT_QPA_PLATFORM=xcb` como escape documentado; menús con parent; `QT_QPA_PLATFORM=offscreen` para tests |
| R-13 | **Dos GUI ⇒ doble mantenimiento** de la capa de presentación | certeza / alto (estratégico) | corte recomendado al final de la fase 4: decidir con evidencia antes de invertir en 5-6 |
| R-14 | **`.d` compartido**: si alguien cambia `csnhypodbp.d`, el prototipo cambia de comportamiento | certeza / bajo | es una **ventaja** para la paridad (AC6 lo verifica); `test_d_parity.sh` detecta claves nuevas no soportadas |

---

## 14. Equivalencias GTK4 → Qt Widgets (chuleta)

| GTK4 (actual) | Qt6 (port) | notas / trampas |
|---|---|---|
| `AdwApplication` + `AdwApplicationWindow` + `AdwHeaderBar` + `AdwToolbarView` | `QApplication` + `QMainWindow` + `QToolBar`/`QMenuBar` | el modo oscuro de libadwaita ⇒ `QApplication::setStyle("Fusion")` + paleta, o `styleHints()->setColorScheme()` (Qt 6.5+) |
| `GtkNotebook` | `QTabWidget` | pestañas `Eventos`/`Procesar` y `picking`/`magnitudes` |
| `GtkPaned` (+ reparto diferido una sola vez, patrón `ec-split`) | `QSplitter` (+ `setSizes` una vez, `saveState/restoreState`) | **no** meter `QLayout` dentro; no usar `setCollapsible(false)` si infla el mínimo |
| `GtkColumnView` + `GListStore` + `GtkSortListModel` + `GtkSingleSelection` + `GtkSignalListItemFactory` | `QTableView` + `QAbstractTableModel` + `QSortFilterProxyModel` + `QItemSelectionModel` | Qt **no** tiene el problema del *snapshot* del `bind`; pero el índice de proxy ≠ índice fuente, y `QModelIndex` se invalida en un reset ⇒ identificar por `origin_id` |
| Cabecera ordenable (`ewgui/table_ui.h`, `ewgui_table_cmp_values`) | `ewguiqt::wire_header_sort()` + `SortProxy` | primer clic descendente en numéricas/fechas y alfabético en texto; segundo invierte; **un solo** comparador (`ewgui_table_cmp_values`) para GTK y Qt, y la flecha del header siempre coincide con el orden real |
| `GtkDrawingArea` + `gtk_drawing_area_set_draw_func` + `EwGuiCanvas` | `QWidget::paintEvent` + `QPainter::drawImage` sobre el `QImage` pintado por cairo | `EwDrawFn(canvas, cairo_t*, w, h, ud)` ⇒ `bpq_scene_paint(scene, ctx, bgra, w, h, stride)` |
| cairo directo | cairo sobre `QImage` (pantalla) y sobre `cairo_*_surface` (export) | paridad de píxel; `QPainter` queda solo para blitear |
| `GtkMenuButton` + `GMenuModel` (prohibido: bug #1299) ⇒ botón + ventana modal propia | `QToolButton` + `QMenu` con parent | en Qt el popup es nativo y no queda detrás; igual se pone transient parent (Wayland) |
| `GtkDropDown` (prohibido: bug #1299) ⇒ modal con lista | `QComboBox` (nativo) o `QRadioButton` en fila | si se observara el problema en WSLg viejo, botones en fila |
| `GtkFileDialog` (`ewgui_export_dialog_run`) | `QFileDialog::getSaveFileName` | ventana nativa |
| `GtkCalendar` en ventana modal (sin popups por WSLg) | `QCalendarWidget` en `QDialog` modal, o dentro de un `QDateTimeEdit` | `QDateTimeEdit::setCalendarPopup(true)` usa popup: preferir el diálogo modal (paridad con la decisión actual) |
| `GtkSpinButton` (rechazado en la UI actual por ancho) | `QLineEdit` con validador + ancho fijo (`setMaximumWidth`) | paridad con la decisión de usar cajas de texto acotadas; Qt da anchos naturales más predecibles, pero se mantiene el criterio |
| `GtkSpinner` + timer 1 s | `QProgressBar` en modo busy + `QTimer` | texto `calculando <loc>… N s` |
| `g_timeout_add(ms, fn, ud)` | `QTimer` (miembro) + `connect(timeout, …)` | la afinidad del timer decide el hilo |
| `g_idle_add` / hilo + `g_mutex` | `QThread` + worker `moveToThread` + señales *queued* | **no** poner slots en una subclase de `QThread` (corren en el hilo viejo) |
| `g_object_set` + `g_object_bind_property` | `dataChanged` / `setProperty` + `QPropertyAnimation` (no necesaria) | en Qt los cambios sí se repintan |
| `g_application_quit` | `QApplication::quit()` / `QCoreApplication::exit()` | — |
| CSS GTK (`.preview-box.candidate`, `background-image: none`) | QSS (`QWidget#preview.candidate { … }`) | en QSS no hace falta el truco `background-image: none` |
| `logit_init` + `EW_LOG` | idem (EarthWorm, desde la capa C) | prefijo de log = nombre del **módulo** (`csnhypodbpqt`), no el del `.d` |
| `EWGUI_FORCE_X11=1` | `QT_QPA_PLATFORM=xcb` | escape WSLg |
| `GSK_RENDERER=cairo` | `QT_QUICK_BACKEND` n/a; para Widgets el raster es el default | Qt Widgets no usa GPU por defecto ⇒ menos superficie de fallo en WSLg |

**Referencias externas útiles (verificadas por `@scout`):**
`doc.qt.io/qt-6/cmake-get-started.html`, `doc.qt.io/qt-6/qt-standard-project-setup.html`,
`cmake.org/cmake/help/v3.31/manual/cmake-qt.7.html` (AUTOMOC), `doc.qt.io/qt-6/moc.html` (receta GNU make
manual: `moc_%.cpp: %.h`), `doc.qt.io/qt-6/qabstracttablemodel.html`,
`doc.qt.io/qt-6/qsortfilterproxymodel.html`, `doc.qt.io/qt-6/qsplitter.html`,
`doc.qt.io/qt-6/qtabwidget.html`, `doc.qt.io/qt-6/qfiledialog.html`, `doc.qt.io/qt-6/qthread.html`
(patrón worker/`moveToThread`), `doc.qt.io/qt-6/timers.html`, `doc.qt.io/qt-6/qpa.html`
(`QT_QPA_PLATFORM`), `doc.qt.io/qt-6/linux-requirements.html`, `isocpp.org/wiki/faq/mixing-c-and-cpp`
(`extern "C"`, `main()` y link dirigidos por el compilador C++), `gcc.gnu.org/onlinedocs/.../Link-Options.html`
(`-fPIC` y PIE), `doc.qt.io/qt-6/lgpl.html` + `qt.io/development/open-source-lgpl-obligations` (LGPLv3,
enlazado dinámico), `community.kde.org/Guidelines_and_HOWTOs/Wayland_Porting_Notes` (transient parent),
`stackoverflow.com/questions/77725761` (`libxcb-cursor0` desde Qt 6.5),
`qt.io/blog/extended-security-maintenance-for-qt-5.15-begins-may-2025` (fin de soporte Qt5),
`packages.ubuntu.com/{qt6-base-dev,qtbase5-dev,libqt6widgets6}` (versiones por release).

---

## 15. Archivos existentes

### 15.1 No se toca nada

Todo el prototipo vive en `ew_gui_tools/csnhypodbpQT/`. **Ningún** archivo de `csnhypodbp/`, `common/`,
`csnloc/`, `wavestore/`, `csnwphase/`, `hypodb/`, `ew8_compile.sh`, `ew_monitor.sh`, `diag.sh`,
`startstop_unix.d`, `earthworm.d.base`, `.gitignore` ni `AGENTS.md` se modifica en las fases 0-5; la 6.5 toca **solo** lo listado en §15.2.

### 15.2 Los toques de la fase 6.5 (hechos, con permiso explícito)

| archivo | cambio | por qué |
|---|---|---|
| `run_working_v8/params/earthworm.d.base` | `Module MOD_CSNHYPODBPQT 172` | `GetModId` solo resuelve nombres declarados (`earthworm_simple_funcs.h:64`); sin esto usa el id 158 y no puede correr junto a `csnhypodbp` |
| `.gitignore` runtime | `/run_working_v8/csnhypodbpqt/` | igual que `:25-28` para los otros módulos (el binario instalado ya estaba en §3b) |
| `run_working_v8/params/startstop_unix.d` | `Process "csnhypodbpQT csnhypodbpqt.d"` + `Class/Priority` | `startstop`/`ew_monitor.sh`/`ew_controller` lo gestionan por pid |
| `run_working_v8/params/csnhypodbpqt.d` (nuevo) | copia del `.d` GTK con `MyModuleId`, `ManualStateFile` y `MwwWorkDir` propios | identidad y runtime separados; `StateFile` se deja igual (solo lectura de `csnloc`) |
| `ew_gui_tools/common/tests/test_params_stays_clean.sh` | §6 incluye `run_working_v8/csnhypodbpqt` | la carpeta runtime nueva también debe quedar fuera de git |

El `.d` nuevo respeta §3 (`ManualStateFile`/`MwwWorkDir` = `../csnhypodbpqt/…`) y §4 (no añade rutas
absolutas), y **debe versionarse**: §1 falla mientras quede sin trackear en `params/`.

### 15.3 Fuera de alcance (deliberado)

- Ampliar `diag.sh` (lista cerrada) para `config csnhypodbpqt`.
- Añadir el prototipo a `AGENTS.md` / `docs/` (lo hará quien decida adoptarlo).
- Migrar `csnrv`, `csntvp`, `csnstaevdisp`, `ew_controller` a Qt.
- ~~Tocar `common` para crear una variante `libewgui_core.a` sin GTK~~ **Superado por D5**
  (ver §17): la infraestructura Qt compartida vive en `ew_gui_tools/commonqt/`
  (`libewguiqt.a`), hermana de `common/`, sin tocarla. El port de `ew_controller`
  (`ew_gui_tools/ew_controllerQT/`) ya usa esa lib y `csnhypodbpQT` se migró a
  ella (canvas, boot y proxy de orden).

---

## 16. Anexo: inventario de fuentes de csnhypodbp

Líneas por archivo (`wc -l`), con la clasificación usada en este documento.
**P** = puro reutilizado · **Pg** = puro con GObject/glib (excluido) · **G** = GTK (se reescribe).

| archivo | l. | clase | notas |
|---|---|---|---|
| `csnhypodbp.h` | 496 | G | `<gtk/gtk.h>` :4; ~100 `extern g_*` :90-213 |
| `csnhypodbp_mww.h` | 54 | G | `<gtk/gtk.h>` :4 |
| `csnhypodbp_gui.c` | 4478 | G | todos los callbacks cairo; `on_draw_signal` :4043-4357 |
| `csnhypodbp_main.c` | 2582 | G | `main` :2521, `ReadConfig` :321, `ConnectToEarthworm` :764, UI :1559-2479, timers :2485-2487 |
| `csnhypodbp_ws.c` | 1810 | G | drena, BD, hilos de fetch, emisión :1679-1777 |
| `csnhypodbp_mww.c` | 1161 | G | panel Mww, `on_draw_mww` :345, `mww_tick` :826, guardado BD :942-999 |
| `csnhypodbp_filtercfg.c/.h` | 557/89 | P | presets de filtro |
| `csnhypodbp_locate.c/.h` | 1341/153 | P | cadena offline completa; `locate_run` :1080, `mag_run` :774, `arc_build` :544, `confirmation_arc` |
| `csnhypodbp_mapview.c/.h` | 531/197 | P | geometría de mapa y perfil (solo cálculo) |
| `csnhypodbp_group.c/.h` | 467/135 | P | parseo de ARC, agrupación, preferida, `MagStore` |
| `csnhypodbp_magcfg.c/.h` | 449/162 | P | `.d` de csnmags |
| `csnhypodbp_profile.c/.h` | 354/90 | P | perfiles slab/topo |
| `csnhypodbp_view.c/.h` | 332/191 | P | estado de vista, ticks, mapeos |
| `csnhypodbp_trace.c/.h` | 285/115 | P | captura de picadas |
| `csnhypodbp_statable.c/.h` | 256/102 | P | tabla de estaciones (2 filas/estación) |
| `csnhypodbp_magmarks.c/.h` | 240/128 | P | marcas de magnitud |
| `csnhypodbp_mww_over.c/.h` | 218/59 | P | override de parámetros Mww (incluye `csnwphase.h`) |
| `csnhypodbp_magrow.c/.h` | 205/119 | P | filas de magnitudes |
| `csnhypodbp_search.c/.h` | 193/68 | P | construcción de la consulta de búsqueda |
| `csnhypodbp_magview.c/.h` | 191/71 | P | procesado por magnitud (DSP) |
| `csnhypodbp_preview.c/.h` | 185/115 | P | estado/texto/color del preview |
| `csnhypodbp_sds.c/.h` | 161/24 | P | fallback SDS (libmseed2); `extern g_sds_*` :23-24 |
| `csnhypodbp_station.c/.h` | 123/93 | P | metadata de estación |
| `csnhypodbp_magitem.c/.h` | 178/27 | **Pg** | GObject (excluido) |
| `csnhypodbp_row.c/.h` | 159/27 | **Pg** | GObject (excluido) |
| `csnhypodbp_staitem.c/.h` | 140/25 | **Pg** | GObject (excluido) |
| `csnhypodbp_master.c/.h` | 89/36 | **Pg** | `GListStore` (excluido) |
| `csnhypodbp_fixdepth.c/.h` | 79/21 | P | profundidad fija |
| `csnhypodbp_magchan.c/.h` | 68/21 | P | `rd_chan_mag` (incluye `<rw_mag.h>` ⇒ solo desde C) |
| `csnhypodbp_magsta.c/.h` | 68/44 | P | magnitudes por estación |
| `csnhypodbp_manual.c/.h` | 54/33 | P | estado de picadas manuales |
| `csnhypodbp_tt.c/.h` | 50/21 | P | tiempos teóricos (`csnloc.h`, `libcsnloc_tt.a`) |
| `csnhypodbp_locator.c/.h` | 47/23 | P | lista de localizadores |
| `csnhypodbp_traces.c/.h` | 39/43 | P | conjunto de trazas |
| `csnhypodbp_mwwerr.c/.h` | 127/22 | P | clasificación de errores del motor W |
| `locate_chain.c` | 144 | P | arnés offline del `locate` (modelo para `test_locate_chain.sh`) |
| `Makefile` | 302 | — | referencia del contrato §10 |
| `README.md` | 484 | — | referencia funcional |
| `test_*.c` (33 archivos) | ~5.000 | P | **no se reutilizan** (son del módulo GTK) pero son la referencia de los valores esperados de nuestros tests |
| `test_ui_invariants.sh` (575), `test_config.sh` (279), `test_maximize.sh` (59), `gui_check_mww.sh` (139), `gui_check_residual.sh` (87) | — | — | referencia de los invariantes GUI a replicar en Qt |

**Totales:** 10.031 líneas en los 4 archivos GTK que hay que reexpresar; ~5.400 líneas puras que se
reutilizan tal cual (24 archivos); 4 archivos GObject que se sustituyen por modelos Qt.

---

## 17. Checklist de arranque para quien implemente (agente `build`)

1. **Pedir al usuario** que ejecute `sudo apt install qt6-base-dev qt6-base-dev-tools libxcb-cursor0`
   (o autorizarlo). Verificar: `qmake6 -query QT_INSTALL_HEADERS`, `ls /usr/lib/qt6/libexec/moc`.
2. **Pedir a `@runner`** cada compilación/test, con el comando exacto, y exigir reporte acotado
   (CMD / EXIT / SALIDA). No ejecutar `make` directamente.
3. Empezar por la **tarea 0.1** (Makefile) y no avanzar hasta que `make all` instale el binario: todo lo
   demás depende del build.
4. En cada tarea, escribir **primero el test** que la verifica (está nombrado en la tabla) y dejarlo
   fallando, luego implementar.
5. Respetar R1-R6 sin excepción: son las que impiden que el prototipo contamine el resto del sistema.
6. Al cerrar cada fase, correr `ew8_compile.sh csnhypodbpQT --check --validate` **y**
   `ew8_compile.sh --no-build --check` (AC17, no regresión) + `make -C ew_gui_tools/csnhypodbp test`.
7. No hacer `git add`/`commit`/`push`: los cambios quedan en el working tree y se reportan.
8. Si un mismo fallo se repite más de dos veces sin causa clara: parar y reportar, no cambiar cosas al azar.
9. **Antes de la fase 6.5**: pedir permiso explícito para las 3-4 líneas en archivos existentes (§15.2).

---

*Fin del documento.*
