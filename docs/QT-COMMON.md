# commonqt — infraestructura Qt6 compartida (`libewguiqt.a`)

Hermana de `ew_gui_tools/common/`, **no** una variante suya. `common/` sigue siendo
GTK4 + libadwaita y su `libewgui_gtk4.a` arrastra GTK; un módulo Qt que la enlace deja
de ser un módulo Qt (lo delata `ldd`). Por eso la infraestructura Qt vive en su propio
directorio y se descubre sola: `ew8_compile.sh` la trata como librería porque su
Makefile no tiene receta `install_bin.sh`.

## Qué contiene

| Header | API | Garantía |
|---|---|---|
| `ewguiqt/boot.h` | `ewguiqt::boot(argc, argv, opts, gui)` | `LC_NUMERIC=C` **antes y después** de `QApplication`; los flags `headless` se resuelven **sin** crear la app (funcionan sin `DISPLAY`) |
| `ewguiqt/table.h` | `SortProxy` (`setColumnKinds`, `sortInitial`, `sortByHeaderClick`) | el orden delega en `ewgui_table_cmp_values` (el mismo comparador del GTK): primer clic DESC en numéricas/fecha, ASC en texto, desempate por columna |
| `ewguiqt/table.h` | `wire_header_sort(view, proxy, col)` | desactiva el orden propio de Qt, conecta `sectionClicked` al proxy y mantiene la **flecha del header** en el orden real; aplica el orden inicial con la dirección de su primer clic |
| `ewguiqt/list_select.h` | `ListSelectDialog::pick()` | ventana modal; **nunca** un desplegable (WSLg #1299 deja los popups detrás de la ventana) |
| `ewguiqt/confirm.h` | `ewguiqt::confirm()` | botón por defecto = **No**; cerrar con X/Esc devuelve `false` |
| `ewguiqt/theme.h` | `set_dark_mode()` / `dark_mode()` | paleta completa con estilo Fusion, idempotente, sin persistencia |
| `ewguiqt/log_view.h` | `LogView::loadPath()` | últimos N bytes desde un **borde de línea** (misma regla que `tail_file()` del GTK) |
| `ewguiqt/canvas.h` | `CanvasWidget::setPaintFn()` | buffer BGRA (`Format_ARGB32_Premultiplied`); el `QImage` sólo se recrea al redimensionar; 6 señales de ratón |

## Reglas duras

1. **Cero dominio**: `include/ewguiqt/` y `src/` no incluyen `<gtk/…>`, `<cairo…>`,
   `earthworm.h`, `kom.h`, `libpq` ni headers de `common/` con GTK. Lo verifica
   `tests/test_no_sci_deps.sh` (símbolos indefinidos de la `.a` incluidos).
2. **Cero cairo**: el canvas entrega un puntero a bytes; quien crea la superficie cairo
   es la capa C del módulo. Así `libewguiqt.a` sólo depende de Qt6Core/Gui/Widgets.
3. **Una sola semántica de orden**: cualquier tabla nueva usa `SortProxy`; no se
   reimplementa `lessThan` (era el modo de fallo: el port Qt ordenaba al revés que el
   GTK en el primer clic).
4. **Sin estado oculto**: nada de `QSettings`. El estado sale del `.d`/anillo.
5. **Sin `QComboBox` para elegir módulos/logs/config**: `ListSelectDialog`.

## Cómo la enlaza un módulo

```make
CXXFLAGS += -I../commonqt/include
MOCFLAGS += -I../commonqt/include      # si algún header con Q_OBJECT la incluye
EWLIBS   := ../commonqt/build/libewguiqt.a ...
LDFLAGS  := $(EWLIBS) $(QTLIBS) -lm -lpthread   # las libs DESPUÉS de los .o
../commonqt/build/libewguiqt.a:
	$(MAKE) -C ../commonqt
```

`-I../commonqt/include` también hace falta en `MOCFLAGS` cuando un header del módulo
incluye `ewguiqt/table.h` y se mocá.

## Clasificación de toolkit en `ew8_compile.sh`

Cada Makefile declara `MOD_KIT = gtk4|qt6` (una línea). El script lo lee, exige el
preflight correspondiente (`pkg-config gtk4 libadwaita-1` o `qmake6`/`Qt6Widgets`) y
clasifica `MOD_GUI` sólo con ese dato. El *fallback* para Makefiles antiguos ordena
`Qt6Widgets|qmake6` **antes** que `libadwaita-1`, porque un módulo Qt puede nombrar
`libewgui_gtk4.a` en un comentario. Invariante: `common/tests/test_compile_kits.sh`.

## Tests

`make -C ew_gui_tools/commonqt check` → 4 binarios Qt con `QT_QPA_PLATFORM=offscreen`
(tabla, boot, widgets, canvas) + `test_no_sci_deps.sh` + `test_no_gtk_link.sh`.
No necesitan `DISPLAY` ni EarthWorm.

## Lo que no está (a propósito)

- **Workers `QThread`**: el patrón `jobId` + `moveToThread` + cancelación es distinto en
  cada módulo (ondas vs. localizador vs. motor W). Se extraerá cuando haya un tercer
  consumidor, no antes.
- **Export SVG/PDF/PNG**: `csnhypodbpQT` usa `QFileDialog` + la capa C; el controlador no
  tiene lienzo, así que no aplica.
- **Migrar `common/`**: no se toca. Los módulos Qt compilan *in situ* los `.c` puros
  (`ewgui_config`, `ewgui_ctrl`, `ewgui_cfgfile`, `ewgui_ring`, `ewgui_table_cmp`, …).
