# csnrvQT — port Qt6 de `csnrv`

Visor regional / *Report Viewer* con la misma funcionalidad que el módulo GTK4, sobre
Qt6 Widgets y `libewguiqt.a` (ver `docs/QT-COMMON.md`). **Sustituye** al GTK en
`startstop_unix.d`: usa el **mismo** `csnrv.d` y el `Process` de `csnrv` está retirado.

| | valor |
|---|---|
| binario | `csnrvQT` |
| `.d` | `csnrv.d` (`MyModuleId MOD_CSNRV`, id 159) |
| anillo | `HYPO_RING_REF` |
| log | `csnrvQT_<fecha>.log` (propio; el GTK usaría `csnrv_<fecha>.log`) |

Como los dos son **lectores** del mismo anillo, se pueden comparar en vivo: basta
volver a declarar el `Process` del GTK.

## Arquitectura

```
c/                          C puro: no conoce Qt
  crv_capi.h                frontera única C->C++ (extern "C", handles opacos)
  crv_config.c              .d (kom): claves, defaults, clamps, overlays
  crv_arc.c                 parseo mínimo del ARC (offsets de csnrv.c)
  crv_magmsg.c              TYPE_MAGNITUDE con copia defensiva de rd_mag
  crv_mag.c                 estado de las 4 magnitudes + preferencia + --test-mag
  crv_fmt.c                 formatos de resumen, "Time Since Quake" e ISO->epoch
  crv_db.c                  seed del historial PostgreSQL
  crv_core.c                anillo HYPO_RING_REF, heartbeat, snapshot
  crv_map.c                 mapa equirectangular + grilla + .lines + export (cairo)
  crv_logit.c               log propio ($EW_LOG/csnrvQT_<fecha>.log)
ui/                         C++17/Qt6: sólo <Qt…>, ewguiqt/*, crv_capi.h
  main.cxx                  ewguiqt::boot; --print-config/--check-overlays/--test-mag
                            y modos offscreen --dump/--layout/--shot/--interact
  crv_mainwindow.{h,cxx}    ventana; ninguna regla de negocio
  crv_canvas_widget.h       adaptador fino sobre ewguiqt::CanvasWidget
  crv_overlay_dialog.{h,cxx} QCheckBox por capa (sin desplegables, WSLg #1299)
```

`cairo` vive **solo** en `c/`: el fondo lo decodifica la UI con `QImage` y llega como BGRA
(`crv_map_set_background`), y el export re-renderiza la misma escena a SVG/PDF/PNG.

## Arranque

```bash
source ./ew8_unix.sh
cd run_working_v8/params
csnrvQT csnrv.d
```

Sin `DISPLAY` funcionan los modos headless:

```bash
csnrvQT --print-config csnrv.d     # mismo bloque que el GTK
csnrvQT --check-overlays csnrv.d   # capas/polilíneas y overrides
csnrvQT --test-mag                 # C3..C9 (ML/Mwp/Mb/Ms)
```

## Gestos del mapa

- **Rueda**: zoom ×1.2 / ÷1.2 **anclado al cursor** (`crv_map_zoom_at`): el punto del mundo
  bajo el puntero no se mueve. Clamp 0.2–50; al topar, el paneo no deriva.
- **Arrastre con botón izquierdo**: paneo (umbral de 2 px, para no panear en un clic).
- **Botón derecho**: no panea.
- Un **qid nuevo** vuelve al `InitialZoom` y pan 0, como el GTK.

Las señales de `ewguiqt::CanvasWidget` llegan en píxeles **lógicos**; `ui/` las multiplica por
`devicePixelRatioF()` porque el mapa trabaja en píxeles físicos.

## Tests

| test | qué fija |
|---|---|
| `test_config` | defaults, clamps, claves repetibles, `--check-overlays` |
| `test_arc` | t0 UTC, S/W, depth/100, qid, versión y fallback al char 161 |
| `test_magmsg` | 5 tipos, qid ajeno y que `rd_mag` no destruye el buffer del llamante |
| `test_mag` | regla de preferencia y reset por qid |
| `test_state` | versión por qid, evento nuevo y formatos |
| `test_map` | proyección, zoom/pan/reset, **zoom anclado al cursor**, capas y export PNG/SVG/PDF |
| `test_logit` | `LogFile`, nombre del proceso y guarda sin `EW_LOG` |
| `test_headless` | el `test_config.sh` del GTK, **sin tocarlo**, contra `csnrvQT` |
| `test_ui` | `--dump`/`--layout`/`--shot`/`--interact` offscreen (4 magnitudes, tabla 4×3, mínimo ≤ 700×800, zoom ×1.2 anclado y paneo) |
| `test_ui_invariants` | la capa C no conoce Qt, la UI no conoce EarthWorm/GTK/cairo y los gestos están cableados |
| `test_no_gtk_link` | el binario no arrastra GTK/libadwaita |
| `test_integration` | `csnrv.d`/159, sin id 174, `startstop` y binario instalado |

`make check`, `make test-live` (requiere sesión gráfica y EarthWorm arriba).
