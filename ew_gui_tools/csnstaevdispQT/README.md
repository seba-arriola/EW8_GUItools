# csnstaevdispQT — port Qt6 de `csnstaevdisp`

Visor de estaciones y sismos (triángulos coloreados por elevación, círculos
escalados por magnitud) sobre mapa equirectangular, con zoom/paneo, capas
`.lines`, leyenda, export y modo oscuro. Usa Qt6 Widgets y `libewguiqt.a`
(ver `docs/QT-COMMON.md`).

**Sustituye** al GTK en `startstop_unix.d`: corre con el **mismo**
`csnstaevdisp.d` y el `Process` de `csnstaevdisp` está retirado.

| | valor |
|---|---|
| binario | `csnstaevdispQT` |
| `.d` | `csnstaevdisp.d` (`MyModuleId MOD_STAEVDISP`, id 161) |
| anillo | `HYPO_RING` (sólo heartbeat; no lee mensajes) |
| historial | PostgreSQL (`DbEnvFile` / `DbHistoryHours`), cada 2 s |
| log | `csnstaevdispQT_<fecha>.log` (propio; el GTK usa `csnstaevdisp_<fecha>.log`) |

## Arquitectura

```
c/                          C puro: no conoce Qt
  sev_capi.h                frontera única C->C++ (extern "C", handles opacos)
  sev_config.c              .d (kom): 8 claves obligatorias, overlays, reporte
  sev_sta.c                 estaciones + rango de elevación (gradiente)
  sev_db.c                  historial (lon normalizada, qid desde event_id)
  sev_core.c                BD + anillo + heartbeat + snapshot
  sev_map.c                 cairo: proyección, zoom anclado, escena y export
  sev_logit.c               log propio con el literal del proceso
ui/                         sólo <Qt...>, ewguiqt/* y sev_capi.h
  main.cxx                  ewguiqt::boot; flags headless y modos offscreen
  sev_mainwindow.{h,cxx}    ventana, menú, gestos, export y diálogos
  sev_canvas_widget.h       adaptador fino sobre ewguiqt::CanvasWidget
  sev_overlay_dialog.{h,cxx} QDialog + QCheckBox (WSLg #1299)
tests/                      tests C + scripts headless
```

**cairo vive sólo en `c/`**: el fondo lo decodifica la UI con `QImage` y llega
como BGRA (`sev_map_set_background`); el export re-renderiza la misma escena a
SVG/PDF/PNG. El menú es `QMenuBar` nativo y el diálogo de capas usa
`QCheckBox` (nada de desplegables: WSLg #1299).

## Arranque

```bash
source ./ew8_unix.sh
cd run_working_v8/params
csnstaevdispQT csnstaevdisp.d
```

Sin `DISPLAY` funcionan los modos headless (el flag va **antes** del `.d`):

```bash
csnstaevdispQT --print-config csnstaevdisp.d
csnstaevdispQT --check-overlays csnstaevdisp.d   # formato idéntico al GTK
```

## Gestos del mapa

- **Rueda**: zoom ×1.2 / ÷1.2 **anclado al cursor** (`sev_map_zoom_at`): el
  punto del mundo bajo el puntero no se mueve. Clamp 0.2–100; al topar, el
  paneo no deriva.
- **Arrastre con botón izquierdo**: paneo (umbral de 2 px de `CanvasWidget`).
- **Botón derecho**: no panea.
- El **refresco de datos (2 s) no resetea** zoom ni pan.
- El centro inicial es fijo en `lat -30, lon -70`, con zoom 2.0 (sin
  auto-encuadre), igual que el GTK.

Las señales de `ewguiqt::CanvasWidget` llegan en píxeles **lógicos**; `ui/` las
multiplica por `devicePixelRatioF()` porque el mapa trabaja en píxeles físicos.

## Diferencias conocidas con el GTK

- El zoom del GTK era **al centro**; aquí es **anclado al cursor** (misma UX
  que `csnrvQT`/`csnhypodbpQT`).
- El GTK paneaba con cualquier `motion`; `CanvasWidget` exige 2 px, así que un
  clic sin mover no desplaza el mapa.
- `--print-config` es un añadido del port (diagnóstico headless).

## Tests

| test | qué valida |
|---|---|
| `test_config` | 8 claves, defaults, overlays y reporte `--check-overlays` |
| `test_logit` | `LogFile`, nombre del proceso y guarda sin `EW_LOG` |
| `test_sta` | parser de estaciones, tope 512 y rango de elevación |
| `test_db` | `lon>180`, `qid` desde `E<qid>-…`, tope y BD ausente |
| `test_core` | estaciones, BD ausente, anillo inválido y snapshot |
| `test_map` | proyección, zoom anclado, escena, capas y export PNG/SVG/PDF |
| `test_headless` | el `test_config.sh` del GTK, **sin tocarlo**, contra este binario |
| `test_ui` | `--dump`/`--layout`/`--shot`/`--interact` offscreen (5 estaciones, 3 sismos, 2 capas; mínimo ≤ 700×800; zoom ×1.2 anclado; arrastre; el refresco no resetea) |
| `test_ui_invariants` | frontera C/C++, cairo sólo en `c/`, log literal y gestos cableados |
| `test_no_gtk_link` | el binario no arrastra GTK/libadwaita |
| `test_integration` | `csnstaevdisp.d`/161, `startstop` y binario instalado |
| `test_live` | arranque real de 3 s (SKIP sin `DISPLAY`) |

```bash
make -C ew_gui_tools/csnstaevdispQT check
make -C ew_gui_tools/csnstaevdispQT test-live
```
