# ew_controllerQT — port Qt6 de `ew_controller`

Panel de control de `startstop` con la misma funcionalidad que el módulo GTK4, sobre
Qt6 Widgets y `libewguiqt.a` (ver `docs/QT-COMMON.md`). Usa el mismo anillo
`CONTROL_RING` y admite las dos formas de `startstop_unix.d`:
`Process "ew_controllerQT ew_controller.d"` lo pone como panel **principal** (mismo
`MOD_CONTROL` 160, con el `Process` del GTK comentado) y `… ew_controllerqt.d` lo corre
en **paralelo** al GTK con id propio (`MOD_EW_CONTROLLERQT` 173).

## Arquitectura

```
c/                          C puro: no conoce Qt
  ecq_capi.h                frontera única C->C++ (extern "C", handles opacos)
  ecq_config.c              .d: MyModuleId/Ring/HeartBeatInt/LogFile/PollInt/LogDir
  ecq_core.c                anillo: attach/read/tick, vista plana, sensibilidad, acciones
  ecq_cfg.c                 editor del .d (.bak + rename atómico, comentarios intactos)
  ecq_log.c                 prefijo por TYPE_STATUS + fichero más reciente + tail
  ecq_logit.c               log propio: $EW_LOG/ew_controllerQT_<fecha>.log (clave LogFile)
ui/                         C++17/Qt6: sólo <Qt…>, ewguiqt/*, ecq_capi.h
  main.cxx                  ewguiqt::boot (headless sin DISPLAY) + ECQ_MODE
  ecq_mainwindow.{h,cxx}    widgets; ninguna regla de negocio
tests/                      C puro + arneses offscreen
```

La regla es la de `csnhypodbpQT` (`docs/testLibQT.md` R1/R2): ningún `.c` incluye Qt y
ningún `.cxx` incluye EarthWorm/kom/libpq/GTK/cairo. Lo verifica
`tests/test_ui_invariants.sh`.

## Compilar y ejecutar

```bash
source ./ew8_unix.sh
./ew8_compile.sh ew_controllerQT            # compila e instala en ew_8.0/bin
cd run_working_v8/params
ew_controllerQT ew_controllerqt.d           # primer plano; Ctrl-C
```

`--print-config ew_controllerqt.d` funciona **sin** `DISPLAY` (y sin EarthWorm):

```
MyModuleId      MOD_EW_CONTROLLERQT
Ring            CONTROL_RING
HeartBeatInt    30
LogFile         1
PollInt         5
LogDir          /…/run_working_v8/log
```

## Comportamiento (paridad con el GTK)

| Pieza | Regla |
|---|---|
| Tabla *Modules* | `Module/PID/Status/Details`; orden inicial `Module ASC`; clic en cabecera por `ewguiqt::wire_header_sort` (flecha del header sincronizada; numéricas DESC primero, texto ASC); `Details` = cola cruda del `TYPE_STATUS` |
| Tabla *Rings* | `Ring/Key/Size KB`; orden inicial `Ring ASC`; sin selección |
| Selección | **por pid**, nunca por nombre; sobrevive al rebuild de filas; si el pid desaparece se limpia |
| `Stop` | sólo `Alive` y nunca `startstop` |
| `Restart` | cualquier fila seleccionada que no sea `startstop` |
| `Start` | sólo `Stop`/`Dead` y nunca `startstop`; se envía como `TYPE_RESTART` (igual que el GTK) |
| `Reconfig` | incondicional sobre la selección; exige que `startstop` responda |
| Sin respuesta | `PollInt*4` sin `TYPE_STATUS` ⇒ `?` en `Status` y las cuatro acciones apagadas |
| *Configuration* | refresco **congelado** mientras la pestaña está activa; salir con cambios pide `Discard them?`; `Save` copia `.bak` y escribe atómico; `Reconfigure` guarda y reinicia (o manda `TYPE_RECONFIG` si el módulo es `startstop`) |
| *Logs* | prefijo = campo `config` del `TYPE_STATUS` (`startstop` literal), comparado **sin distinguir mayúsculas** (el log propio es `ew_controllerQT_*` y el `.d` `ew_controllerqt`); gana el `<prefijo>_*.log` de mayor mtime; últimos 32 768 B desde borde de línea |
| Log propio | `$EW_LOG/ew_controllerQT_<YYYYMMDD>.log`, con el nombre del **proceso** (no el `.d`) para que `diag.sh log ew_controllerQT` lo encuentre; `LogFile` del `.d` (0 = sólo consola); sin `EW_LOG` escribible queda en consola en vez de morir (`logit_init` haría `exit`); escribe sólo al fichero (`logit("t",…)`): anillo conectado, acciones enviadas, `startstop` sin responder/vuelto y errores de envío |
| Layout | divisores ajustables; mínimo 642×319 px (nada que impida maximizar en WSLg) |

## Tests

```bash
./ew8_compile.sh ew_controllerQT --check     # build + los 6 grupos del arnés
make -C ew_gui_tools/ew_controllerQT test-live   # opcional: xcb + xdotool
```

| Test | Qué fija |
|---|---|
| `test_config` | claves del `.d`, prioridad `EW_LOG > LogDir > "logs"`, `--print-config` |
| `test_core` | fixtures `status_alive/dead/dup_a/dup_b.txt`: filas, sensibilidad, `?`, identidad por pid, protocolo (`Start` = `TYPE_RESTART`) |
| `test_cfg_editor` | valores, `dirty`, `.bak` byte a byte, comentarios y sangría intactos |
| `test_log` | prefijo/mtime/literal `startstop`, prefijo sin distinguir mayúsculas, tail desde borde de línea |
| `test_logit` | `LogFile` consumido: sin `EW_LOG` no muere (guarda del `exit` de `logit_init`), fichero `ew_controllerQT_<fecha>.log` y `ecq_logf` escribiendo |
| `test_ui_invariants` | frontera C/C++, identidad por pid, `ecq_can()`, sin `QSettings`/`QComboBox`, arnés en `TEST_CTRL_RING` |
| `test_no_gtk_link` | el binario no enlaza GTK/Adwaita/cairo |
| `test_layout` | gap `Details→Ring` ≤ 40 px, anillos ≥ 200 px, mínimo ≤ 700×640 |
| `test_parity` | volcado `ECQ_MODE=--dump` == estado de los fixtures (filas, anillos, target y los 4 botones) |
| `test_integration` | `MOD_EW_CONTROLLERQT 173` único, `.d`, `Process` en `startstop`, inventario, `--print-config` sin DISPLAY |

El arnés es determinista y **offscreen**: publica `TYPE_STATUS` con `fake_status` en un
`TEST_CTRL_RING` propio (nunca `CONTROL_RING`, que comparte el stack real) y lee
`ECQ_MODE=--dump|--layout|--shot`. `test-live` (xcb + `xdotool`) sólo comprueba que la
ventana sobrevive a maximizar y a una selección por teclado.

## Pendientes conocidos

- `run_working_v8/params/earthworm.d` es **generado**: tras añadir el módulo a
  `earthworm.d.base` hay que refrescarlo (`worktree_bootstrap.sh --refresh`) o `GetModId`
  avisará `Invalid module name <MOD_EW_CONTROLLERQT>` y el panel quedará esperando el
  anillo.
- El arnés GTK (`tests/test_sort_and_status.sh`) sigue siendo la referencia de
  comportamiento; si cambia una regla, se actualizan los dos.
