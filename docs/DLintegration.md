# Integración de pickers deep learning (EQTransformer / PhaseNet)

Estado: 2026-10-20. Módulo `ew_gui_tools/pickml/` (binario `pickml`, headless, C puro).

Objetivo de esta etapa: **plomería**. Conectar la entrada (EarthWorm: tank/store) y la
salida (P y S con sus parámetros) de dos modelos deep learning, con configuración
estándar, para que la calibración se haga después con datos y sin tocar el pipeline.

`pickml` **está declarado en `startstop`** (`Process "pickml pickml_iquique.d"`) y publica sus
picks como `TYPE_PICK_SCNL` en `PICK_RING`: es el picker automático del pipeline vivo. La regla del
anillo es **un productor por fase**: `pick_FP` aporta la **P** y `pickml_iquique` **sólo la S**
(`PublishPhases S`); `pickS` y `pickml_eqt` siguen en `LEGACY_PICK_RING`, sin consumidor, para
comparar.
El modo live sigue escribiendo además los archivos (JSONL + SCNL). La política de publicación es
`PublishRing` / `PublishPhases` / `PublishMaxWeight`, con `--publish-dry-run` para ensayar sin
escribir. El detalle operativo (cómo se integran los modelos, recursos, dependencias, CPU/GPU,
migración y resultados medidos) está en **`docs/DLdetalles.md`**.

---

## 1. Qué está operativo

| pieza | estado |
|---|---|
| EQTransformer 6000 (ONNX) | **operativo** offline (tank y store) |
| PhaseNet 3001 (ONNX) | **operativo** offline (tank y store), mismo binario |
| Exportador (`tools/export_onnx.py --arch eqt\|phasenet`) | operativo |
| `WindowSamples` configurable (ventana nativa por modelo) | operativo |
| Salidas: JSONL, `TYPE_PICK_SCNL` texto, cabecera JSON | operativo |
| Modo live desde anillo (`SLINK_RING`) | **operativo** (archivos + publicación en `PICK_RING`) |
| Modo live siguiendo store/tank (`Follow`) | **no implementado** |
| `pickml_fetch` (evento de la BD → tank + referencias) | **operativo** (BD + store + SDS, con decimación) |
| `tools/eval_picks.py` (recall) | **operativo** |
| Publicación en `PICK_RING` | **activa** (`PublishRing 1`; los viejos van a `LEGACY_PICK_RING`) |
| Espectro / filtros / magnitudes | fuera de alcance (es un picker) |

---

## 2. Contrato ONNX (idéntico para los dos modelos)

```
entrada : float32 [B, 3, T]     canales Z, N, E · 100 Hz · cuentas CRUDAS
salida  : float32 [B, T, 3]     (P, S, detección) por muestra
T       : WindowSamples (nativo de cada modelo)
```

- La **normalización va dentro del grafo** (`annotate_batch_pre` de SeisBench: quita la
  media y divide por σ por componente). El C no filtra ni normaliza.
- PhaseNet no tiene curva de detección: el exportador fija `det = max(P, S)`.
- El C valida al abrir el grafo que `dims == [B, 3, WindowSamples]`; si no, falla al
  arrancar (no publica basura).

### 2.1 Normalización: el detalle que cambió el resultado

El exportador llamaba a `forward`, que **no** normaliza. Consecuencia medida en la misma
ventana real (sismo `CX.PB15`, 2026-10-20 05:15:03 UTC):

| grafo | P en +4.0 s | arribo S en +23.3 s | ¿S etiquetada? |
|---|---|---|---|
| sin normalizar | 0.446 | 0.339 P / 0.207 S | **no** (salía P) |
| con `annotate_batch_pre` | **0.924** | 0.000 P / **0.377 S** | **sí** |

Con el grafo corregido, la evaluación multiestación (10 estaciones CX, un proceso
`--store`) da **9 P y 8 S** coherentes (P con moveout entre 05:14:56.5 y 05:15:10.0),
sin la P falsa en el tiempo de la S. PhaseNet, por la misma vía, da 22 picks con
P y S separadas (S-P ≈ 14 s).

### 2.2 Artefactos

| modelo | ruta | tamaño | ventana | sesión ORT | inferencia |
|---|---|---|---|---|---|
| EQTransformer `original` (slim) | `/mnt/d/resources/pickml/eqt.onnx` | 4.9 MB | 6000 (60 s) | **~25 s** | 21–34 ms/ventana |
| PhaseNet `original` | `/mnt/d/resources/pickml/phasenet.onnx` | 1.1 MB | 3001 (30.01 s) | **31–170 ms** | 1.3–3.2 ms/ventana |

El grafo completo original quedó en `eqt.full.onnx` (5.6 MB, ~58 s) por si hay que
volver atrás. La reducción se hizo con `tools/slim_onnx.py` (ver §2.3).

Junto a cada `.onnx` hay un `.onnx.json` con `arch`, `sha256`, `opset`, versiones de
SeisBench/torch, forma de entrada/salida, ventana y `normalization`.

ONNX Runtime 1.31: `/mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0/lib/libonnxruntime.so`.
El binario lo carga con `dlopen` desde la clave `OrtLib`: **no** queda ligado a ORT
(`ldd` limpio) y funciona sin él con el motor `stub`.

El minuto de arranque de EQT es del grafo (20 838 nodos, 8 269 constantes); no es disco ni
optimización (`all`/`basic`/`none` = 64/63/66 s) y es un coste **único por proceso**.

### 2.3 Arranque: qué pasa al reducir los nodos `Constant`

El export de torch deja los pesos y los parámetros de `Slice`/`Clip` como **nodos
`Constant`** (8 386), no como initializers. ORT los recorre al crear la sesión.

`tools/slim_onnx.py` los convierte a initializers y **deduplica** por dtype/forma/bytes:

| métrica | antes | después |
|---|---|---|
| nodos del grafo | 21 165 | **12 779** |
| nodos `Constant` | 8 386 | **0** |
| initializers | 184 | 260 (sólo **76 únicos**) |
| tamaño | 5.6 MB | 4.9 MB |
| `CreateSession` | 58–63 s | **25.6 s** |
| paridad C↔PyTorch | 3.58e-07 | **3.58e-07** (idéntica) |

No cambia la semántica (mismos bytes, mismos picos: 0.924 en la muestra 400) y el arranque
se reduce ~55 %. Es el grafo que quedó instalado. Lo que **no** cambia: sigue siendo un
coste por **arranque de proceso**, no por ventana; una vez vivo, cada ventana son ~33 ms.

Alternativas no aplicadas: serializar el grafo optimizado de ORT o convertirlo a formato
`.ort` (ataría el artefacto a la versión de ORT).

---

## 3. Configuración

Dos `.d` de ejemplo en el módulo, mismo esquema: `pickml.d` (EQT) y `phasenet.d`.

| clave | EQT | PhaseNet | notas |
|---|---|---|---|
| `Engine` | `onnx` | `onnx` | `stub` = tests sin modelo |
| `ModelName` | `eqt` | `phasenet` | va al JSONL y a la metadata |
| `ModelFile` / `OrtLib` | `/mnt/d/...` | `/mnt/d/...` | absolutas recomendadas |
| `WindowSamples` | 6000 | **3001** | nativa del modelo; se valida contra el grafo |
| `Rate` / `RateTolHz` | 100 / 0.01 | 100 / 0.01 | rechaza estaciones con otra tasa |
| `StepSec` | 10 | 5 | ventanas solapadas |
| `MinPosSec` | 20 | **2** | no emite picos antes de esta posición |
| `MaxPosSec` | 0 | 0 | 0 = `WindowSec − 1/rate` |
| `BlindSamples` | 500 | 100 | debe caber en `MinPosSec` |
| `Stacking` | `avg` | `avg` | `avg`/`max` entre ventanas |
| `PThreshold` / `SThreshold` | 0.30 / 0.30 | 0.30 / 0.30 | pico por fase |
| `DetThreshold` | 0 | 0 | 0 = puerta apagada |
| `W0..W3Prob` | 0.90/0.70/0.50/0.30 | igual | prob → weight 0..3 |
| `DedupSec` | 2.0 | 2.0 | mismo SCNL+fase |
| `MinSPSec` | 2.0 | 2.0 | descarta S pegada a un P |
| `MaxGapSec` | 1.0 | 1.0 | hueco máximo interpolable |
| `PReportChan` / `SReportChan` | Z / N | Z / N | canal con que se reporta |
| `PickOutFile` / `PicksOutFile` / `RunMetaFile` | vacío | vacío | la CLI los sobreescribe |

Latencia de emisión (una ventana se publica al completarse): **~50–60 s** con EQT y
**~10–30 s** con PhaseNetWC. `csnloc` asocia con `AssocWindowSec 45`: con EQT habría que subirlo
a ~90; con PhaseNetWC se mantiene 45 y se mide la cola real antes de decidir (el máximo de
131.8 s de la corrida de 9.3 h es una cola de trazas tardías, ver §8.2).

Fichero de estaciones (idéntico a `pickS.sta`):

```
# PickFlag Pin Sta Net Loc ChanE ChanN ChanZ
1 1 PB15 CX -- HHE HHN HHZ
```

---

## 4. Entradas y salidas

### 4.1 Modos implementados

```sh
# tanque (un registro TRACE2_HEADER + int32 por bloque)
pickml pickml.d --tank evento.tank

# store indexado <root>/NET.STA.LOC.CHAN/YYYYMMDD.tank
pickml pickml.d --store ../wstore --t0 1792473290 --t1 1792473375

# SDS de SeisComP (histórico): YYYY/NET/STA/CHAN.D
pickml pickml.d --sds /mnt/sds/archive --t0 ... --t1 ...

# anillo vivo (SLINK_RING -> archivos + PICK_RING si PublishRing 1).
# Es el MODO POR DEFECTO: sin flags de fuente, como csnloc/pickS. --ring = sinónimo.
pickml pickml.d
```

Formatos de tiempo: `YYYY-MM-DD[T ]HH:MM:SS[.mmm]`, `YYYYMMDDhhmmss`, epoch.

Un registro mayor que el buffer se trocea: un tank de 60 s con ventana de 30 s no pierde el
prefijo (importante para no perder P tempranas).

### 4.2 Salidas a archivo

```sh
pickml eqt.d --tank evento.tank \
    --out       evento.eqt.jsonl \
    --picks-out evento.eqt.picks.txt \
    --meta      evento.eqt.meta.json
```

- `--out` (o `PickOutFile`): JSONL, una línea por pick, orden `(t, sta, fase)`.
- `--picks-out` (o `PicksOutFile`): `TYPE_PICK_SCNL` en texto, mismo formato que el anillo
  (SCNL va `STA.CHAN.NET.LOC`; origen `A`).
- `--meta` (o `RunMetaFile`): una línea JSON con modelo, grafo, motor, ventana, paso,
  umbrales, `w_prob`, dedup, `min_sp`, fuente, `t0/t1`, estaciones, ventanas, picks y
  `engine_ms_total`.
- `--stats`: resumen a stderr (trazas, ventanas, picks, descartes, `motor_ms_ventana`).

JSONL:

```json
{"sta":"PB15","net":"CX","loc":"--","chan":"HHZ","phase":"P",
 "t_epoch":1792473303.558000,"t_utc":"2026-10-20 05:15:03.558",
 "prob":0.9240,"weight":0,"win_t0":1792473299.558000,"model":"eqt"}
```

SCNL:

```
8 0 0 1 PB15.HHZ.CX.-- ?0 20261020051503.558 0 0 0 P A
```

(`mod`/`inst` = 0: en el anillo los pone el módulo; este archivo es de calibración.)

### 4.3 Códigos de salida

| exit | significado |
|---|---|
| 0 | ok / help / print-config / check-model |
| 2 | uso o configuración |
| 3 | dependencia o modo no implementado |
| 4 | datos (no abre tank/store, fuente abortada) |

---

## 5. Post-proceso (en C, no en el grafo)

1. picos por umbral dentro de `[MinPosSec, MaxPosSec]`;
2. `DetThreshold` si > 0;
3. `prob → weight` (`W0..W3Prob`); por debajo de `W3Prob`, se descarta;
4. compuerta S-P (`MinSPSec`), en la ventana y contra el último P aceptado por estación;
5. dedup por SCNL+fase (`DedupSec`), gana la primera emisión;
6. orden y salida.

---

## 6. `pickml_fetch` (BD + SDS → tank) — implementado

Objetivo: dado un evento histórico, dejar todo lo necesario para evaluar, usando **picadas
manuales revisadas** como referencia y ondas del store o del SDS.

```sh
pickml_fetch fetch.d --event <event_id> --out /tmp/cal/<evento>
```

Salidas: `event.tank` (3C), `picks_ref.csv`
(`net,sta,loc,chan,phase,t_ref,weight,pick_status,residual_s`), `meta.json`
(origen, hora, lat/lon/prof, algoritmo, y `source: store|sds` por estación) y,
opcionalmente, `stations.csv` con lat/lon.

Piezas ya existentes que se reutilizan:

| pieza | API |
|---|---|
| conexión BD | `ewguidb_config_load`, `ewguidb_open` |
| evento → orígenes | `ewguidb_query_origins(db, event_id, …)` |
| origen → picadas | `ewguidb_query_picks(db, origin_id, …)` |
| búsqueda por tiempo | `ewguidb_search_events` |
| ruta SDS | `ewgui_sds_path` → `<root>/YYYY/NET/STA/CHAN.D/NET.STA.LOC.CHAN.D.YYYY.DDD` |
| escritura tank | `TRACE2_HEADER` + `int32` (`wstore_io.c` es el precedente) |

- `pick.channel` es `NET.STA.LOC.CHAN`; `pick_status` distingue `manual`/`automatic`.
- SDS verificado en `/mnt/sds/archive` (años 2010–2026).
- `csnhypodbp_sds_fetch` no es reutilizable directo (vive en `csnhypodbp`, depende de
  globales y de libmseed2): el tool nuevo lee el SDS con `ms_readmsr_r` + `ewgui_sds_path`.
- `pickml` **no** enlaza libpq ni libmseed: el fetch es un binario aparte.

### 6.1 Lo que hace

- `--event <event_id>` → el origen con **más picadas** de ese evento
  (`ewguidb_query_origins` no existe aquí: se consulta por SQL con `libpq`).
- Se queda sólo con `pick_status='manual'` y fases P/S.
- **Deriva las 3 componentes del canal picado** (`HHZ` → `HHE/HHN/HHZ`, `HNZ` →
  `HNE/HNN/HNZ`): los históricos no están en `pickS.sta`.
- Ventana = `[min(t_pick) − PadPreSec, max(t_pick) + PadPostSec]`.
- Ondas: **store primero** (`StoreRoot`) y **SDS como fallback**
  (`/mnt/sds/archive`, layout `YYYY/NET/STA/CHAN.D/...`).
- **Decimación**: si la estación corre a 200/400 Hz, se filtra con un Butterworth de
  2.º orden (anti-alias a 0.35·Rate) y se diezma a `Rate`. Sin esto se perdían las
  estaciones densas (C21O, V08A…).
- El tank se escribe **intercalado por tiempo en bloques de 1 s** (E,N,Z por bloque): un
  registro gigante por componente hacía que, al llegar la segunda, el buffer de la
  estación ya hubiera avanzado y todo se descartara como "tardío".
- Genera además `stations.sta` en el formato de `pickml`, para usarlo tal cual.

### 6.2 Trampas medidas

- **`db.env` puede apuntar a otra instancia** que la del dump histórico. En este entorno
  `db.env` dice `5438` (143 orígenes) y el dump de 500 758 está en `5435`. Para calibrar
  hay que usar un `DbEnvFile` con el puerto correcto.
- `MSRecord.starttime` es **`hptime_t` (µs)**, no `double`: se convierte con
  `MS_HPTIME2EPOCH(msr_starttime(msr))`. Confundirlo desplaza todos los campos.
- `numsamples`/`samplecnt` y `datasamples` son los campos válidos (no `starttime` como
  `double`, que era el supuesto inicial).

### 6.3 Salida

```
event.tank      TRACE2_HEADER + int32, 1 s por componente, 3C por estación
picks_ref.csv   net,sta,loc,chan,phase,t_ref,weight,pick_status,residual_s
stations.sta    PickFlag Pin Sta Net Loc ChanE ChanN ChanZ (listo para pickml)
meta.json       evento, origen, hora, lat/lon/prof, ventana, picks, estaciones escritas
```

---

## 7. Evaluación con criterio de recall — implementado

`tools/eval_picks.py` compara `picks_ref.csv` (solo manuales) contra los JSONL de cada
modelo.

Criterio acordado: **¿el modelo encontró algo en la misma estación y fase que ya estaba
picado a mano, y coincide en tiempo dentro de tolerancia?** Lo que el modelo encuentre de
más **no** es falso positivo y **no** penaliza.

```
recall_P = P manuales con un P del modelo en |Δt| ≤ tol_P  /  P manuales
recall_S = ídem para S
reporte  = recall por fase, mediana/P90 de |Δt|, y lista de extras (solo informativa)
```

Emparejamiento por `(net, sta, loc, phase)` **ignorando canal** (el modelo reporta S en
`HHN`; la referencia puede ser `HHZ`). Tolerancias iniciales: P 2.0 s, S 3.0 s.

Las picadas manuales existen solo para algunas estaciones: el denominador es "cuántas
picadas manuales hay", no "cuántas estaciones tiene la red".

Implementado en `tools/eval_picks.py` (Python, sólo dev-time):

```sh
python3 tools/eval_picks.py --ref picks_ref.csv \
    --pick eqt.jsonl --pick phasenet.jsonl --tol-p 2 --tol-s 3
```

### 7.1 Primera medición real (evento `csn2025boue`, 2025-01-23, 6 estaciones)

Picadas manuales de referencia: 12 (6 P, 6 S). Tolerancias P ±2 s, S ±3 s.

| modelo | recall P | mediana P | recall S | mediana S | extras |
|---|---|---|---|---|---|
| EQTransformer 6000 | **5/6 (83 %)** | 0.69 s | **5/6 (83 %)** | 0.96 s | 4 |
| PhaseNet 3001 | **5/6 (83 %)** | 0.70 s | **4/6 (67 %)** | 1.31 s | 10 |

Los "extras" (picks del modelo sin referencia manual) **no** se cuentan como fallo: es el
criterio acordado. Sirve como línea base; la calibración de umbrales es la etapa siguiente.

### 7.2 Primer ajuste de umbrales (3 eventos, 6 estaciones cada uno)

Se corrieron los dos modelos con umbral 0.05 y `W3Prob 0.05` (para capturar todos los
picos) y se evaluó filtrando por probabilidad en post-proceso. Agregado de los 3 eventos
(`csn2025boue`, `csn2024ylug`, `csn2024xfjc`), tolerancias P ±2 s / S ±3 s:

| modelo | umbral | P | S | mediana P | mediana S | extras |
|---|---|---|---|---|---|---|
| EQT | 0.15 | 13/16 | 11/12 | 0.66 s | 0.68 s | 10 |
| EQT | **0.20** | **12/16** | **10/12** | 0.69 s | 0.68 s | 9 |
| EQT | 0.25 | 12/16 | 8/12 | 0.69 s | 0.79 s | 8 |
| EQT | 0.30 | 12/16 | 8/12 | 0.69 s | 0.79 s | 6 |
| PN | 0.15 | 15/16 | 6/12 | 0.65 s | 0.92 s | 54 |
| PN | 0.20 | 15/16 | 5/12 | 0.65 s | 0.70 s | 36 |
| PN | **0.30** | **15/16** | **5/12** | 0.65 s | 0.70 s | 19 |
| PN | 0.35 | 15/16 | 5/12 | 0.65 s | 0.70 s | 16 |

**Set inicial para deploy:**

| modelo | P | S | W0..W3 | por qué |
|---|---|---|---|---|
| EQT | 0.20 | 0.20 | 0.90/0.70/0.50/0.20 | codo: +2 S y +2 P sobre 0.25 por 1 extra |
| PhaseNet | 0.30 | 0.30 | 0.90/0.70/0.50/0.30 | su S no mejora bajando; 0.30 recorta extras 54→19 |

Observaciones: PhaseNet es mejor para P (15/16) y claramente peor para S (5/12); EQT es el
más equilibrado. `MinSPSec 2.0` se aplicó también en la corrida de barrido, así que puede
haber recortado alguna S real cerca de un P espurio de baja probabilidad: el S de EQT a
0.15/0.20 es una cota inferior.

---

## 8. Modo live (archivos + publicación) — implementado

- **Anillo**: leer `SLINK_RING` (`TYPE_TRACEBUF2`), ensamblar 3C por estación con el buffer
  actual y emitir al completarse cada ventana.
- En live, los picks se escriben **en streaming** (JSONL y `TYPE_PICK_SCNL`) con `fflush`
  por pick; no se acumulan en memoria. Además, con `PublishRing 1`, se envían a `OutRing`
  con `tport_putmsg` (mismo SCNL, `mod/inst` reales) según `PublishPhases`/`PublishMaxWeight`;
  `--publish-dry-run` cuenta sin escribir.
- Se purga el anillo al arrancar y se descartan las trazas más viejas que
  `LiveMaxAgeSec` (120 s): arrancamos en vivo, no re-procesando historial.
- `--ring` es excluyente con `--tank` y `--store`; `Ctrl-C`/SIGTERM cierra ordenadamente y
  reescribe `RunMetaFile` con los contadores finales.

**Cómo se lanzan** (una terminal por modelo; `Ctrl-C` para parar):

```sh
cd /home/seba/Dev/ew8/DLpickers
source ./ew8_unix.sh
cd run_working_v8/params

# terminal 1
pickml ../../ew_gui_tools/pickml/live_phasenet.d --ring --stats

# terminal 2
pickml ../../ew_gui_tools/pickml/live_eqt.d --ring --stats
```

- `source ./ew8_unix.sh` es obligatorio: resuelve `EW_PARAMS`/`earthworm.d` y el `timeout`
  de GNU. Sin él, `GetKey("SLINK_RING")` no encuentra el anillo.
- Los `.d` (`live_eqt.d`, `live_phasenet.d`) llevan `StaFile pickS.sta` y las rutas de
  salida **relativas al cwd** (`../pickml_live/...`), así que funcionan en cualquier
  worktree. Las salidas quedan en `run_working_v8/pickml_live/` (ignorado por git):
  `eqt.jsonl`, `eqt.picks.txt`, `eqt.meta.json`, `pn.*` y los logs si se redirigen.
- Para dejarlos en segundo plano:
  `setsid pickml ../../ew_gui_tools/pickml/live_eqt.d --ring --stats > ../pickml_live/eqt.log 2>&1 < /dev/null &`

### 8.1 Medición en vivo (10 estaciones CX, ambos modelos en paralelo, 4 min)

| modelo | CPU | ventanas | ms/ventana | picks |
|---|---|---|---|---|
| PhaseNet 3001 | 0.8 % | 540 | 3.2 | 9 |
| EQTransformer 6000 | ~2–3 % en régimen (27 % promedio por los 63 s de `CreateSession`) | 120 | 34 | 1 |

Ambos escribieron sus archivos en tiempo real sin atrasos. `sin_estacion` es alto porque el
anillo trae ~85 estaciones y cada instancia filtra las suyas: no es un error.

**EQT es mucho más conservador que PhaseNet.** Medido sobre el mismo anillo con
`--dump-curves` (367 ventanas de 60 s): el máximo de P por ventana tiene **media 0.018**
(picos de 0.82) y el de S **media 0.0065** (picos de 0.59). PhaseNet, en cambio, produce
muchos picks en 0.30–0.50. Consecuencia práctica: EQT da pocos picks y muy fuertes;
PhaseNet da muchos y más ruidosos. Para el análisis conviene guardar ambos y filtrar
después, no subir el umbral de PhaseNet a ciegas.

Con `StaFile pickS.sta` (~100 estaciones) se ven ambas cosas: en 12 min PhaseNet generó
326 picks (27 clusters con ≥3 estaciones, p. ej. 31 estaciones en un mismo arribo) y EQT
un puñado de detecciones fuertes.

### 8.2 Latencia real: ¿es factible en tiempo real?

Cada línea del JSONL en vivo lleva ahora (offline no, para no romper el determinismo de
`check-offline`):

- `gen_t_epoch` / `gen_t_utc`: hora de pared en que se escribió la picada.
- `lag_s` = `gen_t − t`: muestra → picada escrita (lo que ve el consumidor).
- `proc_s` = `gen_t − fin_ventana`: fin de ventana → escrita. Aísla el coste del
  proceso de la espera del algoritmo.

Medido el 2026-10-20 con las dos instancias solas en el anillo (137 estaciones en
`pickS.sta`):

| modelo | `lag_s` p50 | `lag_s` p90 | `lag_s` máx | `proc_s` p50 | `proc_s` máx | entrada media |
|---|---|---|---|---|---|---|
| PhaseNet (3001) | 6.2–8.0 s | 12–27 s | ~33 s | 2.8–3.8 s | ~9 s | ~1.1 s |
| EQTransformer (6000) | 2.3–34 s | 13–37 s | ~40 s | 2.3–5.3 s | ~5 s | ~1.1 s |

Lectura:

- `proc_s` **no crece** con el tiempo y el motor tarda 3 ms (PhaseNet) / 27 ms (EQT) por
  ventana: el proceso va al día. La CPU es 4.6 % (PhaseNet) y ~22 % de un núcleo
  (EQT, promedio con el arranque; en régimen ~16 %).
- El grueso de `lag_s` es **algorítmico** (la picada espera a que su ventana se
  complete), no del proceso. Ese es el número que hay que comparar con el
  `AssocWindowSec` de `csnloc` (45 s, ciclos de 23 s): un pick con `lag` mayor que
  ~21 s puede quedar fuera del snapshot. La decisión vigente es **medir la cola real**
  (fracción con `lag_s >= 22 s` en el JSONL) durante la corrida de 24 h antes de
  subir la ventana; subirla a 90 s sólo corresponde si se activa EQT (ventana 60 s).
- `entrada` media ~1.1 s es el retraso del anillo; `entrada` máxima aparece esporádica
  en 30–35 s por retransmisiones tardías de SeedLink (y antes del arreglo de abajo
  llegaba al tope de `LiveMaxAgeSec`, 120 s, en el arranque).

Arreglo de arranque: la purga del anillo se cortaba en `GET_MISS_LAPPED` /
`GET_MISS_SEQGAP`, que **sí** traen mensaje (`transport.h`), así que el primer ciclo
procesaba minutos de historial y emitía picks viejos con `lag` enorme; lo mismo hacía
`pm_ring_next`, que además **descartaba** esas trazas (huecos en la ventana). Ahora el
lector arranca limpio en la cola del anillo y esos códigos se procesan.

Resumen por fase y por bloque de 10 min (detecta backlog creciente):

```sh
python3 ew_gui_tools/pickml/tools/latency.py run_working_v8/pickml_live/*.jsonl
```

---

## 9. Verificación

```sh
source ./ew8_unix.sh

# build con motor real (autodetecta el ORT vendorizado; ORT_DIR= fuerza el stub)
make -C ew_gui_tools/pickml

# unitarios + end-to-end con stub
make -C ew_gui_tools/pickml check check-offline

# paridad C↔PyTorch sobre la ventana real (≤1e-4; medido 3.6e-07)
make -C ew_gui_tools/pickml check-engine \
     ORT_DIR=/mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0 \
     PICKML_MODEL=/mnt/d/resources/pickml/eqt.onnx \
     PICKML_ORT=/mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0/lib/libonnxruntime.so

# validar un grafo sin correr
pickml phasenet.d --check-model
pickml phasenet.d --print-config
```

Regenerar los grafos y el fixture:

```sh
python3 ew_gui_tools/pickml/tools/export_onnx.py --arch eqt \
    --out /mnt/d/resources/pickml/eqt.onnx \
    --fixture-tank ew_gui_tools/pickml/test/fixtures/win1.tank \
    --fixture-t0 1792473299.558 --fixture-dir ew_gui_tools/pickml/test/fixtures

python3 ew_gui_tools/pickml/tools/export_onnx.py --arch phasenet \
    --out /mnt/d/resources/pickml/phasenet.onnx
```

---

## 10. Orden de trabajo restante

| # | tarea | estado |
|---|---|---|
| 1 | `pickml_fetch` (BD manuales + store/SDS → tank + CSV + meta) | **hecho**: 6/6 estaciones, 200 Hz decimado a 100 |
| 2 | `tools/eval_picks.py` (recall, sin FP) | **hecho**: 5/6 P y 5/6 S (EQT) en el primer evento |
| 3 | `Follow 1` (tail de store/tank) | pendiente |
| 4 | fuente `SLINK_RING` | **hecho**: ambos modelos en paralelo, sólo archivos |
| 5 | calibración de umbrales/ventanas por modelo | pendiente (≥10 eventos) |
| 6 | `PublishRing 1` + alta en `startstop_unix.d` | **hecho**: `pick_FP` (P) + `pickml_iquique` (S) en `PICK_RING`; `pickS`/`pickml_eqt` en `LEGACY_PICK_RING` |
| 7 | arranque de EQT (~58 s) | opcional: reducir nodos `Constant` |

`AssocWindowSec` de `csnloc` pasa de 45 a ~90 únicamente si se activa EQT en el anillo;
PhaseNet no lo necesita.
