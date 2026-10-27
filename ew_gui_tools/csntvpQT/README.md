# csntvpQT — port Qt6 de csntvp (visor de trazas en vivo + picking manual)

Prototipo de la migración de `csntvp` a Qt6. En la **etapa 1** convive con el GTK: los dos están
declarados en `startstop_unix.d` y cada uno corre con su propio `.d`.

| | |
|---|---|
| binario | `csntvpQT` (instalado en `ew_8.0/bin` con `install_bin.sh`) |
| `.d` | `run_working_v8/params/csntvpqt.d` (**propio**; copia del GTK + 3 colores) |
| id | `Module MOD_CSNTVPQT 175` en `earthworm.d.base` (generado a `earthworm.d`) |
| anillos | lee `SLINK_RING` (`TYPE_TRACEBUF2`) y `PICK_RING` (`TYPE_PICK_SCNL`); publica sus picadas manuales en `PICK_RING` |
| log | `$EW_LOG/csntvpQT_<fecha>.log` (nombre del **proceso**; sin `EW_LOG` escribible baja a consola) |
| kit | `MOD_KIT = qt6` (`ew8_compile.sh` lo clasifica solo) |

## Por qué `.d` propio

El eco de una picada se deduplica por `(mod, inst, seq)` y `seq` arranca en 0 en cada proceso
(`csntvp_data.c:371-381`). Con el mismo `MyModuleId`, una picada de este port con `seq=7` podía
sobrescribir en la ventana **GTK** una picada local con `seq=7` de otra estación: no corrompe el
anillo, corrompe la vista. Con `MOD_CSNTVPQT` los dos visores se ven las picadas como ajenas y nadie
pisa el slot del otro.

El `.d` es una copia del de `csntvp` y **sólo** puede diferir en `MyModuleId`, `zColor`, `nColor` y
`eColor`; lo gatea `tests/test_integration.sh` (y `tests/test_ui_invariants.sh` §6). Si añades una
clave al `csntvp.d`, añádela también al `csntvpqt.d`.

## Arquitectura

`c/` (C puro: EarthWorm, kom y cairo; **cero** Qt) — `c/tvq_capi.h` es la **única** frontera C→C++:

| fichero | papel |
|---|---|
| `tvq_config.c` | lee el `.d` con kom (mismas 6 claves obligatorias y defaults que el GTK); `--print-config` |
| `tvq_sta.c` | `StaFile` (`STA CHAN NET LOC escala`, con el quirk de 4 campos del GTK) |
| `tvq_trace.c` | `TYPE_TRACEBUF2` → buffer circular `int32` por (estación, canal), índice absoluto y huecos `INT_MAX` |
| `tvq_env.c` | envolvente min/max cacheada (`ewgui_trace_envelope`), filtro **causal** como el GTK |
| `tvq_pick.c` | picadas: lectura del anillo, eco propio por `seq`, `PickReplaceWindow`, publicación |
| `tvq_emit.c` | formato `TYPE_PICK_SCNL`, **byte-idéntico** a `csntvp_gui.c:326` |
| `tvq_ring.c` | `GetKey`/`GetType`/attach, latido y TERMINATE |
| `tvq_purge.c` | `clean view`: expulsa las pistas sin paquetes en N segundos |
| `tvq_paint.c` / `tvq_axis.c` | cairo → BGRA: pistas, panel, picadas y eje UTC |
| `tvq_core.c` | estado, `tick` (drena anillos), reloj de la ventana, volcado y fixture |
| `tvq_logit.c` | log del proceso con fallback a consola |

`ui/` (sólo `<Qt…>`, `ewguiqt/*` y `tvq_capi.h`): `main.cxx` (boot headless + modos instrumentados) y
`tvq_mainwindow.cxx` (menú `QMenuBar` nativo, `CanvasWidget` ×2, `QScrollBar`, timers 150 ms / 1 s).

El parser de `TYPE_PICK_SCNL` **no se copia**: el Makefile compila `../csntvp/csntvp_util.c`, así que
el formato no puede derivar del GTK. cairo vive sólo en `c/`; la UI recibe píxeles BGRA.

## Arranque

```bash
source ./ew8_unix.sh
cd run_working_v8/params
csntvpQT csntvpqt.d        # con EarthWorm arriba; Ctrl-C para parar
csntvpQT --print-config csntvpqt.d     # headless, sin DISPLAY (flag ANTES del .d)
```

`startstop` lo lanza con `Process "csntvpQT csntvpqt.d"` (junto al del GTK en la etapa 1).
`diag.sh log csntvpQT` funciona; `diag.sh run csntvpQT` **no** (busca `$PARAMS/csntvpQT.d` y el
fichero es `csntvpqt.d`: mismo desajuste de caja que los otros ports Qt).

## Comandos y menús

| | |
|---|---|
| `HOLD` (botón) | congela la ventana de tiempo; los datos siguen llenando los buffers |
| `p` / `s` (sólo en HOLD) | **arman** el modo picada: el cursor pasa a un círculo del color de la fase con su letra, la etiqueta muestra `pick: P` / `pick: S` y el lienzo dibuja una **guía vertical discontinua** bajo el puntero |
| `Esc` | desarma el modo (`pick: off`) y oculta la guía; con el modo ya apagado es no-op |
| clic izquierdo (HOLD + fase armada) | coloca la picada y la publica en `PICK_RING`; el modo **sigue armado** (persistente hasta `Esc` o soltar HOLD) |
| soltar `HOLD` | además de descongelar, desarma el modo, oculta la guía y devuelve el zoom a 1 |
| `↑` / `↓` (sólo en HOLD) | zoom vertical (0.05–100); al soltar HOLD vuelve a 1 |
| rueda sobre las trazas | desplaza la lista de pistas: **una muesca = una pista** (las de 60 uds se acumulan); no hace zoom |
| `Ctrl` + rueda | zoom vertical (sólo en HOLD), igual que `↑`/`↓`; **no** desplaza |
| `PgUp` / `PgDn` / `Home` / `End` | una página arriba/abajo, principio y final de la lista |
| *File* | `Export view...` (PNG), `Quit` |
| *View* | `Dark mode` |
| *Display* | `Stations per screen...`, `Time window...`, `Clean view...`, `Reload station list` |
| *Filter* | `Filter options...` (Raw / HP / LP / BP, orden 2 ó 4, F1/F2) |
| *Colours* | waveforms, background, font, separator, P/S y componentes Z/N/E |
| *Help* | `Commands...` |

Sin desplegables propios (WSLg #1299): los números se piden con `QInputDialog` y el color con
`QColorDialog`.

## Diferencias conocidas con el GTK

- **La pista se normaliza por su propio `env_max_abs`** de la ventana visible (`alto_pista·0.425`),
  igual que `csntvp_gui.c:378-381`. El `scale` del StaFile (columna 5, hoy 0.005 en las 141 filas)
  se parsea pero **no** entra en el dibujo: el GTK lo guarda en `dScreenScale` y no lo lee nunca.
  Multiplicarlo *después* de normalizar encogía la traza por ese factor (200×) y la dejaba en una
  línea plana; lo fija `tests/test_norm.c` (misma onda con `scale` 1.0 y 0.005 ⇒ misma altura).
- **Modo picada persistente**: a diferencia de `csnhypodbpQT` (un solo tiro), aquí el modo sigue
  armado tras picar, para encadenar P y S de muchas estaciones sin repulsar la tecla.
- **Guía vertical del puntero** (como `csnhypodbpQT`): se pinta sólo con fase armada, sigue al ratón
  dentro del área de trazas, no invade el panel izquierdo y desaparece al salir del lienzo, al pulsar
  `Esc` o al soltar `HOLD`. Es independiente del scroll: desplazar pistas no mueve su `x`.
- **Rueda = scroll, Ctrl+rueda = zoom**: `CanvasWidget` consume la rueda para `zoomed`, así que el
  port instala un `eventFilter` y la traduce a desplazamiento (una pista por muesca, acumulando las
  de 60 uds); sólo con `Ctrl` se deja pasar al canvas. La barra sigue funcionando y `tvq_core_hit`
  recibe el `scroll_y`, así que picar tras desplazarse resuelve la fila visible.
- **Lienzo del tamaño del viewport + `QScrollBar`**, en vez de un canvas de
  `n_estaciones × alto_pista` px. Con 141 pistas el del GTK son ~9300 px de alto (44 MB de BGRA
  repintados a ~2 Hz); aquí se pinta la franja visible con `scroll_y`.
- **Un solo hilo**: el timer de 150 ms drena los anillos y recalcula la envolvente, igual que el
  GTK. El *seam* para moverlo a un hilo de drenaje es `tvq_core_tick`.
- **`--print-config` es nuevo en la familia** (el GTK no lo tenía), así que `tests/test_config.sh` es
  propio en vez de reutilizar el del hermano.
- `PickReplaceWindow` es **por instancia**: la ventana que reemplaza sólo desactiva sus propias
  picadas; las del otro visor se siguen dibujando (el protocolo no tiene *retract*).
- La coloración por componente se resuelve por la **última letra del canal** (`Z`/`N`/`E`). Con el
  `stations_to_view.sta` actual (141 filas, todas `HHZ`) sólo se ve al añadir horizontales; en el
  fixture de los tests (HHZ/HHN/HHE) se valida con tres colores.

## Tests

`make -C ew_gui_tools/csntvpQT check` (alias `test`), todo headless:

| test | qué valida |
|---|---|
| `test_config` | defaults del GTK, overrides, clave desconocida ignorada, obligatoria faltante, color inválido |
| `test_sta` | formato del `StaFile`, quirk de 4 campos, fichero real de `params/` |
| `test_trace` | buffer circular, huecos, solape, `i2`, estación no listada, TRACE2 truncado |
| `test_pick` | formato byte-idéntico, ida y vuelta con el parser del GTK, eco por `seq`, `PickReplaceWindow`, guardas de HOLD |
| `test_env` | el filtro cambia la vista y **no** el crudo; clamps de tipo/orden/frecuencias |
| `test_core` | reloj (`ahora − 2 s`), HOLD, `hit`, vista, zoom, volcado |
| `test_paint` | BGRA opaco, panel, eje y un color por componente (Z/N/E) |
| `test_norm` | normalización por pista: el `scale` del StaFile no cambia la altura, amplitudes 100× distintas sí dan la misma, y el zoom multiplica esa base (clip incluido) |
| `test_guide` | guía vertical: sin fase no aparece, con P/S es discontinua en la `x` del puntero, no invade el panel ni se sale del lienzo, es independiente del scroll, escala con DPR y no tapa la picada |
| `test_clean` | `clean view` selectivo, compactado en orden y `reload` |
| `test_config.sh` | `--print-config` del `.d` real y de un fixture; códigos de salida 2/3 |
| `test_ui.sh` | `--dump` / `--layout` (mínimo ≤ 700×800) / `--shot` / `--interact` offscreen (guía y scroll incluidos) |
| `test_ui_invariants.sh` | frontera C→C++, cairo sólo en `c/`, parser reutilizado, `MOD_KIT=qt6`, whitelist del `.d`, sin desplegables |
| `test_no_gtk_link.sh` | el binario no arrastra libgtk/libadwaita |
| `test_integration.sh` | id 175 en `.base`, 174 libre, ambos `Process`, binario instalado, `earthworm.d` regenerado |

`make test-live` arranca contra el EarthWorm en marcha (necesita `DISPLAY`, `CSNTVPQT_EXIT_AFTER_MS`).

## Después de editar `earthworm.d.base`

`earthworm.d` se **genera** (está en `.gitignore`):

```bash
sh ew_gui_tools/common/tools/worktree_bootstrap.sh . --refresh
```

Sin eso `GetModId("MOD_CSNTVPQT")` falla y el módulo sale con exit 3 nombrando justo esa receta.
