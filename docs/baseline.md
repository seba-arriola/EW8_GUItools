# Baseline de rendimiento y comportamiento (Fase 0)

Documento de referencia del estado **antes** del refactor. Sirve para demostrar
que el refactor es *behavior-preserving* y que la migración a GTK4 no degrada el
rendimiento.

> Estado: **pendiente de completar** (T0.1). Los valores numéricos los rellena
> quien ejecute la medición.

---

## 1. Golden de comportamiento (T0.2) — automático

Script: `ew_gui_tools/common/tests/baseline.sh`.

```bash
source ./ew8_unix.sh
ew_gui_tools/common/tests/baseline.sh            # -> ew_gui_tools/common/build/baseline/
```

Corre los tests unitarios actuales y guarda salida (`.out`) + hash (`.sha`):

| Test | Fuente | Qué cubre |
|---|---|---|
| `csntvp_filter` | `csntvp/test_filter.c` | filtro IIR (`aplicar_filtro_iir`) |
| `csnhypodbp_filter` | `csnhypodbp/test_filter.c` | IIR + gaps + demean + find_data_end |
| `csnrv_test_mag` | `csnrv/csnrv.c --test-mag` | parseo de `TYPE_MAGNITUDE` (headless) |

Tras cualquier cambio, re-correr el script y comparar `.out`/`.sha`. Diferencia =
cambio real de comportamiento.

---

## 2. Medición de UI (T0.1) — manual, requiere DISPLAY

Herramientas: **sysprof** y **GTK Inspector**. Objetivo: ms/frame y nº de
repintados del canvas de `csntvp` y `csnhypodbp` con datos reales.

### Procedimiento

1. Arranca EarthWorm con datos (`source ./ew8_unix.sh`; `./ew_monitor.sh start`).
2. Lanza el módulo con Inspector:
   ```bash
   GTK_DEBUG=interactive ew_gui_tools/csntvp/csntvp run_working_v8/params/csntvp.d
   ```
   En Inspector: pestaña **Recorder** (render nodes) y **Statistics** (frames).
3. Perfila con sysprof mientras interactúas (zoom, HOLD, cambio de ventana):
   ```bash
   sysprof-cli --gtk /tmp/opencode/csntvp.syscap
   ```
4. Anota: fps sostenido, ms/frame, nº de frames con repintado, y si el hilo
   principal se bloquea (pico de latencia al seleccionar sismo en `csnhypodbp`).

### Tabla a completar

| Módulo | Escenario | ms/frame | fps | Repintados/s | Bloqueos del hilo principal |
|---|---|---|---|---|---|
| csntvp | 12 estaciones, 6 min, en vivo | | | | |
| csntvp | HOLD + zoom | | | | |
| csnhypodbp | selección de sismo (fetch del store) | | | | — (medir el cuelgue) |
| csnhypodbp | dibujo de trazas alineadas a P | | | | |

---

## 3. Bench headless de envolvente (C6) — medido

`make -C ew_gui_tools/common bench` (`common/tests/bench_wave.c`).

- **64 trazas × 30 s a 250 sps → envolvente en 10.33 ms** (objetivo < 16 ms). OK.
- Conclusión: el cómputo de la envolvente cabe en el presupuesto; no requiere worker
  ni dirty-rect al ritmo actual (ver Fase 5 en `docs/GTK4-MIGRATION.md`).

---

## 4. Cómo interpretar

- Si el refactor no cambia `csnrv_test_mag` ni los filtros → golden intacto (C5).
- Si en Fase 6 el bench de envolvente empeora o el nº de repintados sube →
  el port a GTK4 se hizo mal (cairo-por-frame en `DrawingArea`).
