# Arnés de validación GUI (`common/tools/gui_run.sh` + `gui_probe.py`)

Validación **externa** de los módulos GTK4 (`csnhypodbp`, `csnrv`, `csntvp`, `csnstaevdisp`,
`ew_controller`) sobre un display virtual, sin tocar el código de los módulos y sin arrancar
EarthWorm.

```
gui_run.sh --check                # dependencias (sale !=0 si falta alguna)
gui_run.sh --selftest             # prueba el arnés contra una app GTK4 mínima
gui_run.sh -- <comando> [args]    # corre <comando> en la sesión aislada
```

Dentro de la sesión quedan exportados `EWGUI_PROBE` (ruta de `gui_probe.py`) y
`EWGUI_TOOLS_DIR`, para los scripts que se pasen con `--`.

## Cómo funciona

`gui_run.sh` levanta un `Xvfb` efímero dentro de un `dbus-run-session` propio y exporta
`GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=atspi`. El bus de sesión es imprescindible:
AT-SPI publica el bus de accesibilidad a partir de él. La aplicación y la sonda comparten
esa sesión, así que la sonda ve el árbol de accesibilidad del proceso que se acaba de lanzar.

No toca `run_working_v8/params` ni los logs reales: el llamante decide qué params usar
(copia aislada vía `EW_PARAMS`).

## Dependencias

```
sudo apt-get install -y xvfb dbus-daemon dbus-x11 at-spi2-core python3-pyatspi \
                        python3-gi gir1.2-gtk-4.0 xdotool x11-utils x11-apps imagemagick
```

Obligatorias: `xvfb`, `dbus-run-session`, `python3-pyatspi`. El resto habilita el clic por
coordenadas (`xdotool`), la geometría del toplevel y las capturas.

## La sonda (`gui_probe.py`)

| opción | qué hace |
|---|---|
| `--dump [--limit N]` | vuelca el árbol accesible (rol, nombre, rect) hasta profundidad N |
| `--headers` | lista los encabezados de columna con su rect |
| `--rows [--limit N]` | una línea por fila de tabla con el texto de sus celdas, **en orden visual** |
| `--rect NAME` | imprime `x y w h` en coordenadas de pantalla |
| `--click-by-name NAME` | pulsa el widget por su **acción AT-SPI** |
| `--assert-text TEXTO` | falla si ningún objeto contiene TEXTO en su nombre |
| `--under NAME` | limita la búsqueda al subárbol de ese objeto |
| `--timeout S` | espera máxima (15 s por defecto) |

Ejemplos:

```bash
# volcar la tabla de la pestaña activa
gui_run.sh -- bash -c 'csnhypodbp /tmp/opencode/gui/params/csnhypodbp.d & sleep 8; \
  python3 "$EWGUI_PROBE" --headers; python3 "$EWGUI_PROBE" --rows --limit 20'

# pulsar una cabecera por coordenadas (ver limitación más abajo) y releer el orden
gui_run.sh -- bash -c 'csnhypodbp /tmp/opencode/gui/params/csnhypodbp.d & sleep 8; \
  R=$(python3 "$EWGUI_PROBE" --rect Ml); set -- $R; \
  xdotool mousemove --sync $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) click 1; \
  python3 "$EWGUI_PROBE" --rows --limit 20'
```

Los paneles de mapa de `csnhypodbp` **no** se pueden asertar por texto: sus etiquetas de estado están
ocultas y el `GtkDrawingArea` no aparece en el árbol de accesibilidad (ni por nombre ni por
descripción). Esa verificación se hace **por píxeles**: se captura con `import -window root`, se
convierte a PPM y se cuentan colores conocidos — solución **elegida en *Origins*** `#00C853`, preferida
`#D42E2E` y resto gris azulado. Ejemplo medido: al arrancar hay una mancha verde en **los dos** paneles
(la solución elegida por defecto) y, tras pulsar otro marcador, el verde se muda a ese punto y la
preferida vuelve a rojo. Dos ayudas del mismo montaje: un **arrastre** de 40 px desplaza los marcadores
40 px (prueba de que el gesto llega al canvas) y un recorte de 80×80 centrado en el marcador, con el
**centroide** de sus píxeles verdes, dice si el verde se movió.

La sonda sí expone `--assert-desc`/`--descs` (descripción accesible, `GtkAccessible`), por si algún
widget la publica.

## Geometría: por qué se acumula

GTK4 **no traduce** las posiciones de los widgets a coordenadas de pantalla por AT-SPI:
todos los objetos reportan `0,0` (el frame incluido). Sí reporta **tamaños correctos** y los
hijos **en orden visual**, así que la sonda acumula los tamaños de los hermanos anteriores
(en una caja vertical todos los hijos tienen el ancho del padre ⇒ desplazan en `y`; en una
fila horizontal todos tienen su alto ⇒ desplazan en `x`) y toma la posición real del
toplevel de X con `xdotool search --name <título> getwindowgeometry --shell`.

Verificado con el auto-test: `label 0,0 122x19` y `push button 0,19 122x34` ⇒ clic en `61,36`.

## Límites conocidos (no son fallos del arnés)

- **Cabeceras de `GtkColumnView`**: hay un bug abierto de GTK
  ([#8354](https://gitlab.gnome.org/GNOME/gtk/-/work_items/8354)) por el que las cabeceras
  ordenables **no exponen acción AT-SPI**, así que no se pueden pulsar semánticamente. Se
  pulsan por **coordenadas** (`--rect` + `xdotool`), que es el único punto frágil del arnés.
- **El clic por coordenadas no llegó a las ventanas de los módulos** (medido en `ew_controller` con
  marco `AdwApplicationWindow` + header bar): ni el clic en una pestaña del `GtkNotebook` cambió de
  página, aunque `--rect` da coordenadas coherentes, la ventana está en `0,0`
  (`xdotool getwindowgeometry --shell`) y `windowfocus`/`windowraise`/`windowactivate` no lo
  arreglan. El auto-test (`gui_run.sh --selftest`, app GTK4 mínima) **sí** verifica el clic por
  coordenadas, así que la diferencia está en el árbol de widgets real (sospecha: desfase del
  acumulador con la header bar), no en el arnés. Por eso el clic en cabeceras sigue siendo
  *best-effort* (AVISO) en `test_sort_and_status.sh`.
- **El teclado SÍ llega** (XTEST): con `xdotool windowfocus --sync <wid>`, `Tab`, `Down` y
  `ctrl+Page_Down` (canario de cambio de pestaña) funcionan. Es la vía para **seleccionar una fila**
  de un `GtkColumnView` y para pulsar botones (ver `test_sort_and_status.sh --select`).
- **`--focused` es la brújula para manejar widgets con el teclado**: imprime `rol|nombre x y w h`
  del objeto con el estado `FOCUSED` (exit 1 si no hay ninguno). Patrón recomendado: `Tab`/
  `shift+Tab` hasta que `--focused` nombre el widget buscado y **sólo entonces** mandar `space`
  (activa el widget enfocado) o `Down`/`Return` (navegar una lista). Mandar teclas a ciegas activa
  el botón equivocado: en `ew_controller` un `space` sobre *Reconfig* envía un `TYPE_RECONFIG` real
  a `startstop` (`startstop_unix_generic.c:426-469`: re-lee params, añade hijos nuevos y reinicia
  `statmgr`). Ejemplo: el modo `--dup` de `test_sort_and_status.sh` selecciona una fila y comprueba
  que el rebuild no la cambia de instancia.
  **OJO (medido)**: en Xvfb **sin gestor de ventanas** la ventana nunca queda activa y **ningún**
  objeto reporta `FOCUSED`, así que `--focused` no sirve ahí (informa "ningun objeto con el foco" y
  el modo que lo usa AVISA en vez de fallar). Lo mismo impide pulsar los botones de la barra de
  `ew_controller`: no exponen acción AT-SPI y no hay forma de saber cuál tiene el foco ⇒ los cuadros
  de confirmación y el selector de logs **no** son automatizables en ese entorno; su lógica queda
  cubierta por los unitarios de `common` (`test_ctrl.c`) y por `test_ui_invariants.sh`.
- **Los anillos de EarthWorm tienen UN solo puntero de lectura**: si dos clientes leen el mismo
  anillo, **se reparten los mensajes** (el que llega primero se los lleva). Por eso los tests de
  `ew_controller` que inyectan estado **no** usan el `CONTROL_RING` de producción: `make_fixture`
  crea un anillo propio (`TEST_CTRL_RING`, key 1061, con `fake_status --create`) y el `.d` aislado
  del módulo apunta ahí. Antes competían con `startstop` y con la instancia real de `ew_controller`
  (el test era intermitente) **y** metían módulos falsos en la GUI del usuario. Consecuencias
  prácticas: el módulo arranca con la tabla vacía hasta que el test inyecta (no hay quién responda
  en ese anillo) y `on_poll` no marca "sin respuesta" mientras nunca haya habido un status
  (`g_last_status == 0`). Si algún día otro test inyecta en un anillo compartido, que use copias
  repetidas (`inject3`) y reintentos, no una inyección única.
- **`--click-by-name` puede informar un falso "OK"**: en las filas/celdas de un `GtkColumnView` la
  única acción AT-SPI es `listitem.scroll-to`, así que la sonda dice que pulsó cuando en realidad
  sólo **hizo scroll** (no selecciona). Hay que asertar el **efecto**, no el mensaje: en
  `ew_controller` la barra de estado publica `target: <módulo> (<estado>)` cuando hay fila
  seleccionada, y `--assert-text` lo comprueba.
- **Estética**: la sonda no juzga si algo "se ve bien". Se pueden tomar capturas
  (`import`/`xwd`) para revisión humana o diffs de píxeles, pero no hay juicio visual.
- **WSLg**: los problemas del compositor (popups detrás de la ventana, bug #1299) **no se
  reproducen** bajo Xvfb. Esa parte se valida en la sesión real del usuario.
- **Datos**: la aplicación necesita anillos o una fuente de datos. Para los ensayos se usa
  una copia aislada de params y/o la BD de prueba (`db/docker-compose.yml`).

## Auto-test

`gui_run.sh --selftest` lanza `gui_selftest_app.py` (app GTK4 mínima con una etiqueta y un
botón) y comprueba las tres piezas: leer texto y pulsar por acción, calcular geometría, y
clicar por coordenadas con `xdotool` verificando que la etiqueta cambió.
