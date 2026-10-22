# Pendientes — PhaseNetWC (`pickml`)

Estado: 2026-10-21. Este documento lista **sólo lo que falta**. Lo hecho está en
`docs/DLdetalles.md` (cómo funcionan los modelos, dependencias, pipeline, resultados) y
`docs/DLintegration.md` (diseño y contrato ONNX).

## 0. Decisión vigente

- Modelo único: **PhaseNetWC**, pesos `jma_wc`, ventana nativa **3001** (30.01 s), 100 Hz, ZNE crudo.
- **Ya publica en `PICK_RING`** (modo live, `PublishRing 1` en `pickml_phasenetwc.d`) y sigue
  escribiendo los archivos de siempre. Los pickers viejos (`pick_FP`, `pickS`) se movieron a
  `LEGACY_PICK_RING`, que no tiene consumidor: sirven para comparar con `sniffring -n`.
  La política es pura y testeable (`pickml_publish.c`): `PublishPhases P|S|PS` y
  `PublishMaxWeight 0..3`; `--publish-dry-run` cuenta sin escribir.
- `pickml` **no lee del SDS en modo vivo**: el modo vivo lee `SLINK_RING`; el offline admite
  `--tank`, `--store` (disco local, 7 días de retención) y `--sds <root>` (SDS de SeisComP, para
  eventos históricos; decima 1x/2x/4x a `Rate`). El SDS también lo usan `pickml_fetch`
  (calibración) y `csnwphase`.
- Prioridad declarada: **recall primero**. Las picadas sin referencia se reportan como adicionales,
  nunca como error, pero con **presupuesto de publicación** (§2.4).
- **Primer ajuste hecho** (ver `docs/DLdetalles.md` §11): `run_working_v8/params/pickml_phasenetwc.d`
  con `P 0.40 / S 0.30 / DedupSec 3.0`, recall **P 91.7 % / S 92.7 %** sobre 45 eventos con
  referencia automática. Es lo que hay que superar, no el punto de partida.

## 1. Línea base a superar

Medición de 9.3 h con PhaseNet `original` (config 0.30/0.30, `StepSec 5`, ventana 3001):

| | PhaseNet `original` |
|---|---|
| picks | 12 008 (8 338 P / 3 670 S) |
| estaciones con picks | 78 |
| recall P / S | 91.1 % / 69.5 % |
| eventos con >=1 P / >=1 S | 42/44 / 24/44 |
| `lag_s` P p50/p90/max | 6.3 / 12.1 / 131.8 s |
| `proc_s` p50/max | 3.4 / 124 s |
| CPU / RSS | 4.9 % / 89 MB |

La referencia usada son **258 P + 59 S automáticas** (pick_FP + pickS + refinadores), no manuales:
sirve para el primer ajuste, no para la decisión final.

Primer ajuste con PhaseNetWC sobre 45 eventos y **254 P + 55 S** automáticas (mismo post-proceso):
**P 91.7 % / S 92.7 %**, 985 picadas (~402/h). Contra el `original` en el mismo corpus: 91.3 % /
85.5 % con 672 picadas. Detalle y trampas en `docs/DLdetalles.md` §11.

## 2. Pendiente A — pasar a modo live (HECHO: código + alta en el stack)

El código de publicación y el alta en `startstop` están hechos y cubiertos por tests
(`pickml check`, `check-offline`, `common/tests/test_pick_rings.sh`). Lo que **falta** es la
medición de 24 h (§2.5.4) y la decisión sobre `AssocWindowSec` (§2.2).

### 2.1 Código (implementado)

| qué | dónde | nota |
|---|---|---|
| `PublishRing 0/1` | `pickml_config.c`, `pickml.h` | default 0; con 1 y anillo inexistente, fail-fast `exit 1` |
| `PublishPhases P\|S\|PS` | idem | fase no habilitada ⇒ `pub_skip_phase` |
| `PublishMaxWeight 0..3` | idem | peso 0 = mejor; publica si `weight <= valor` |
| envío `tport_putmsg` | `pickml_main.c` (`pub_open`/`pub_one`) | mensaje de `pickml_report.c:29-37` con `mod/inst` reales |
| política pura | `pickml_publish.{h,c}` | `pm_publish_decide`/`pm_publish_phases_parse`, test headless |
| `--publish-dry-run` | `pickml_main.c` | valida el anillo y cuenta, no escribe |
| contadores | `--stats` y el log | `pub_ok`, `pub_skip_phase`, `pub_skip_weight`, `pub_err`, `pub_dry` |

Formato del mensaje (no tocar): `TYPE_PICK_SCNL` =
`8 mod inst seq STA.CHAN.NET.LOC ?weight YYYYMMDDhhmmss.mmm 0 0 0 P|S A`, origen `A`.

### 2.2 Configuración que falta

- `earthworm.d`: `Ring LEGACY_PICK_RING 1006` (clave) y `Module MOD_PICKML 171`; el anillo nuevo
  también se declara en `startstop_unix.d` (`nRing 11`).
- `startstop_unix.d`: `Process "pickml pickml_phasenetwc.d"` con `MyModuleId` propio
  (la identidad de un módulo es el pid; el id del anillo debe ser único).
- `csnloc.d`: `AssocWindowSec` **se queda en 45**. La recomendación de 90 s era para
  EQTransformer (ventana de 60 s); el `lag_s` de PhaseNetWC no está medido en vivo (ver §8). Antes
  de subirlo hay que medir la fracción de picks con `lag_s >= 22 s` en el JSONL (los que el ciclo de
  `AssocWindowSec/2` puede descartar) durante la corrida de 24 h.
- `StaFile`: `pickS.sta` (137). Recordar la coherencia HHZ/BHZ con `slink2ew_*.d`,
  `pick_FP.sta` y `estaciones_107.txt`; un canal que no coincida se descarta **en silencio**.
- `LiveMaxAgeSec` (120): subirlo da más margen a trazas tardías pero **sube `proc_s`** (los picos
  de 80–124 s son esperar una traza, no CPU).

### 2.3 Riesgos y trampas ya conocidas

- **No hay lock de instancia**: si `startstop` lo corre, no lances `pickml` a mano (duplica picks).
- Al arrancar, los JSONL se abren con `"w"`: **truncan** lo anterior. Si querés histórico, hay que
  rotar el nombre o copiar antes de reiniciar (mejora pendiente).
- `sniffring` **sin** `-n` drena el anillo: para inspeccionar, `sniffring -n PICK_RING`.
- `tport_getmsg`: `GET_MISS`/`GET_NOTRACK` **sí** traen mensaje (ya se manejan y se sigue drenando).
- Anillo lleno o productor lento: medir `pub_err` antes de dar por buena la corrida.
- `Rate` 100 con `RateTolHz` 0.01: una estación a 200 Hz queda **deshabilitada** (sin error visible
  más allá de `stats`).
- El log va a `EW_LOG/pickml_*.log` (`logit_init` con el literal `pickml`, así que
  `diag.sh log pickml` lo encuentra aunque el `.d` se llame `pickml_phasenetwc.d`), y la línea de
  `--stats` se escribe **también** ahí, no sólo en la terminal.
- `MaxGapSec 1.0`: con ventana de 30 s es más frágil que con los 60 s de EQT; si hay huecos de
  SeedLink, subir `MaxGapSec` es más barato que bajar `MaxPosSec`.
- **El orden de clases de PhaseNet NO es estable entre pesos**: `original` es `(N,P,S)` y `jma_wc`
  es `(P,S,N)`. Un `.onnx` exportado con el orden equivocado **no falla al abrir**: da curvas
  intercambiadas y un S saturado. `export_onnx.py --channel-order auto` lo detecta y lo deja en
  `.onnx.json`; si algún peso nuevo sale mal, forzar `nps`/`psn` y volver a medir. `--check-model`
  sólo valida formas, **no** semántica.
- **`pickml_fetch` escribe un tank vacío sin error** cuando no encuentra ondas (hoy ya no le pasa
  con el store: se corregía la ruta del `loc` nulo). Antes de dar un corpus por bueno, verificar
  `stations_written > 0` en cada `meta.json`.

### 2.4 Presupuesto de publicación

Sin tope, "más picks es mejor" degenera en umbral → 0 y `csnloc` se llena de asociaciones falsas.
Antes de publicar, medir la tasa actual de `pick_FP` + `pickS` y fijar
`picks/h publicables <= 2x` esa tasa. El barrido se reporta como curva recall vs picks/h y se elige
el codo, no el extremo.

### 2.5 Aceptación del modo live

1. `PublishRing 0` ⇒ cero `tport_putmsg` y anillo intacto.
2. SCNL publicado **byte-exacto** al contrato (`pickml_report.c:29-37`).
3. Guardas sin anillo: `PublishRing 1` offline ⇒ exit 2, anillo inexistente ⇒ exit 1, y
   `--publish-dry-run` no escribe (cubierto por `test/test_publish_offline.sh`). La 1 h de prueba
   ya no usa un anillo aparte: el usuario pidió publicar directo en `PICK_RING`, así que el
   ensayo es `PublishMaxWeight` + `--publish-dry-run` y la comparación con `LEGACY_PICK_RING`.
4. 24 h en `PICK_RING` y comparar contra las 24 h previas: nº de soluciones, Nph mediano, RMS
   mediano y eventos con >=4 P **no deben empeorar**. Si empeoran, bajar `PublishMaxWeight`.
5. Rollback: `PublishRing 0` + `restart` del módulo, < 1 min, sin tocar el resto del stack.

## 3. Pendiente B — calibración con picadas manuales

El primer ajuste se hizo contra picadas automáticas. La decisión final necesita referencia manual.

- **Fuente**: dump histórico en **5435** (3.86 M picadas manuales). Ojo: `run_working_v8/params/db.env`
  apunta a **5438** (dev, 0 manuales). `pickml_fetch` lee `DbEnvFile` de su `.d`, así que se cambia ahí.
- **Tamaño**: 300–350 S (IC95 ±5 pp) partidas **por evento** en `tune` (~200) y `test` (~120).
  Con 200 S sola la recall absoluta da ±6.4 pp, y si se ajusta sobre el mismo set queda optimista.
- **Volumen**: `MaxStations 20` y `PadPreSec`/`PadPostSec` (60/120 s) ya existen; la ventana se toma
  alrededor de las picadas, no del origen (`pickml_fetch.c:673`).
- **Herramientas que faltan**: `tools/calib_run.sh` (un `pickml_fetch` por evento + concatenar refs),
  `tools/make_dataset.py` (tank → ventanas 3001 con etiquetas gaussianas).
- **Riesgo**: los eventos manuales pueden ser de una época con otros canales. `pickml_fetch` escribe
  `stations.sta` por evento: usarlo como `StaFile` evita el desajuste de canales.
- **Criterio de cierre**: recall en `test` reportada **una sola vez**, con IC, y comparación pareada
  contra la config del primer ajuste.

## 4. Pendiente C — re-entrenamiento

- **Arquitectura**: PhaseNetWC (`filter_factor=2`), 3001 muestras, 3 canales, normalización
  **dentro del grafo** (`annotate_batch_pre`). El contrato ONNX no cambia ⇒ no se toca C.
- **Datos**: `jma_wc` está entrenado en Japón; el fine-tuning con datos de Chile es lo que aporta.
  Empezar por **sólo la cabeza** (encoder congelado) y comparar contra la config del primer ajuste
  en el mismo `test`. Full fine-tune exige corpus ampliado (300–1000 eventos extra) y split propio.
- **Reglas**: un evento no puede estar en `tune` y `test`; nunca mirar `test` durante el ajuste;
  fijar semilla; guardar `meta.json` con pesos, épocas, LR y métricas.
- **Alternativa de capacidad sin entrenar**: exportar con `--window 6001` (60 s de contexto).
  Cuesta ~2× CPU y sube latencia a la de EQT; medirlo antes de adoptarlo.
- **Verificación obligatoria tras exportar**: `--check-model` (I/O `(1,3,T)→(1,T,3)`) y
  `make check-engine` con referencias regeneradas, `max|Δ| <= 1e-4`.
- **Licencias**: SeisBench es GPLv3; PhaseNet original MIT; la licencia de los `.pt` re-entrenados
  por SeisBench **no está declarada**. Antes de distribuir el ONNX, confirmarlo.

## 5. Pendiente D — promoción

1. `make install` de `pickml` (instalación atómica, funciona con el módulo corriendo).
2. Alta en `startstop_unix.d` + `earthworm.d`; arranque por `ew_monitor.sh start`.
3. Ensayo 24 h con los criterios de §2.5 y monitoreo de `proc_s`/`lag_s` por bloques de 10 min
   (`tools/latency.py --bucket-min 10`).
4. Si se adopta, actualizar `docs/DLdetalles.md` §10–§11 y `docs/DLintegration.md`, y fijar el
   procedimiento de rollback en `docs/`.

## 6. Rutas y datos

| qué | dónde |
|---|---|
| binario | `ew_8.0/bin/pickml` (instalado), `ew_gui_tools/pickml/pickml` (local) |
| modelos + ORT 1.31 | `/mnt/d/resources/pickml/` (`phasenet.onnx`, `eqt.onnx`, `phasenetwc.onnx`) |
| venv Python | `~/.venvs/pickml` (seisbench 0.10.2, torch 2.4). **No se usa en runtime** |
| salidas operativas | `run_working_v8/pickml/` (la crea `pickml` al arrancar si falta; está en `.gitignore`) |
| corpus de calibración | `run_working_v8/pickml/calib/<event>/` (tank + refs + `curve.*` + `curve.idx`) |
| configuración | `run_working_v8/params/pickml_phasenetwc.d` |
| config del fetch | `run_working_v8/params/pickml_fetch_calib.d` (usa `db.env`; `OnlyManual 0` en dev) |
| herramientas | `tools/calib_run.sh`, `tools/sweep_post.py`, `tools/eval_picks.py`, `tools/latency.py` |
| BD dev (sin manuales) | 5438 vía `run_working_v8/params/db.env` |
| BD con manuales (3.86 M) | 5435 |
| store | `RetentionDays` 7 días; alineado con `SdsFallbackHours` 168 h |
| SDS de SeisComP | `--sds <root>` (layout `YYYY/NET/STA/CHAN.D`), para eventos históricos |

## 7. Comandos de referencia

```bash
source ./ew8_unix.sh
# El Makefile autodetecta el ORT vendorizado y compila el motor real; si el
# aviso "motor ONNX no compilado (stub)" aparece, pickml no arrancara con
# `Engine onnx` (exit 3). Para forzarlo: ORT_DIR= / ORT_DIR=<dir>.
make -C ew_gui_tools/pickml && make -C ew_gui_tools/pickml install

# exportar (venv, no runtime)
~/.venvs/pickml/bin/python ew_gui_tools/pickml/tools/export_onnx.py \
    --arch phasenet --model jma_wc --out /mnt/d/resources/pickml/phasenetwc.onnx

# validar el grafo sin GUI ni datos
pickml ew_gui_tools/pickml/pickml.d --check-model

# operativo (archivos + publicacion en PICK_RING), desde params
cd run_working_v8/params && pickml pickml_phasenetwc.d --ring --stats

# ensayo sin escribir en el anillo (cuenta y valida el OutRing)
cd run_working_v8/params && pickml pickml_phasenetwc.d --ring --publish-dry-run --stats

# rollback de la publicacion sin tocar el resto del stack
#   PublishRing 0 en pickml_phasenetwc.d  +  ./ew_monitor.sh restart pickml

# evento histórico directo del SDS (sin store ni tank)
cd run_working_v8/params && pickml pickml_phasenetwc.d --sds /mnt/sds/archive \
    --t0 2026-10-20T21:45:00 --t1 2026-10-20T21:49:00 --jsonl --stats

# corpus de calibración + barrido del post-proceso (sin volver a correr el modelo)
cd run_working_v8/params && sh ../../ew_gui_tools/pickml/tools/calib_run.sh \
    pickml_fetch_calib.d events.list ../pickml/calib <pickml.d>
python3 ew_gui_tools/pickml/tools/sweep_post.py --root run_working_v8/pickml/calib \
    --p-thrs 0.2,0.3,0.4,0.5 --s-thrs 0.2,0.3,0.4,0.5 --min-pos 2 --grid /tmp/grid.csv

# replay desde el store
pickml pickml_phasenetwc.d --store <root> --t0 <t0> --t1 <t1> \
    --dump-curves /tmp/opencode/dump/pn --out /tmp/opencode/picks.jsonl --stats
```

## 8. Supuestos a verificar antes de dar nada por bueno

- ~~`jma_wc` existe y fija `filter_factor=2`~~ **verificado**: `from_pretrained("jma_wc")` da
  `filter_factor=2`, `in_samples=3001`, `labels PSN`, y el orden real de salida es `(P,S,N)`.
- ~~Coste de PhaseNetWC ≈ 4× PhaseNet~~ **medido**: 3.2 ms/ventana y 166 ms de arranque (2× y 1.6×).
- ~~El store cubre la ventana~~ **verificado**, pero con dos matices: el store rota a
  `YYYYMMDD.tank` a medianoche **UTC** y `pickml_fetch` necesitaba `loc="--"` (corregido).
- La referencia del corpus es **automática**: no confundirla con la calibración manual (§3).
- Al publicar, el `lag` de PhaseNetWC **no está medido en vivo**. Ojo con la atribución: el
  máximo de 131.8 s de la corrida de 9.3 h es de PhaseNet `original` (`docs/DLdetalles.md:445`),
  pero la recomendación de `AssocWindowSec 90` nació de **EQTransformer** (ventana 60 s,
  `docs/DLintegration.md:135,509`); con PhaseNet se decidió **mantener 45** y medir la cola real
  (§2.2) antes de tocarlo.
