# DLdetalles — pickers neuronales (EQTransformer / PhaseNet) en EW8_GUItools

Documento **descriptivo y operativo**: qué son estos módulos, cómo se integran en EarthWorm, qué
recursos y dependencias necesitan, cómo se instalan/migran y cómo se evalúan. El diseño, las
decisiones y el contrato numérico están en `docs/DLintegration.md`; aquí se explica lo necesario
para **entender, instalar, operar y portar** el sistema a otra máquina.

> Estado: entorno de pruebas. `pickml` **no** publica en `PICK_RING` todavía: en vivo sólo escribe
> archivos (JSONL + SCNL) para medir y calibrar.

---

## 1. Qué hay y qué no hace

| binario | qué hace | entradas | salidas |
|---|---|---|---|
| `pickml` | pica P y S con un modelo neuronal ONNX | `SLINK_RING` (live); `--tank`, `--store`, `--sds` (offline) | JSONL de picks, líneas `TYPE_PICK_SCNL`, `meta.json` |
| `pickml_fetch` | prepara un evento para calibrar | BD hipocentral (libpq) + store/SDS (libmseed2) | `event.tank`, `picks_ref.csv`, `meta.json` |

`pickml` **no asocia, no localiza y no calcula magnitudes**. Eso sigue siendo de `csnloc` y
`csnmags`. Tampoco escribe en la BD: `hypodb_ingest` no lo conoce.

El flujo completo de calibración es:

```
BD (picadas manuales) ──pickml_fetch──> event.tank + picks_ref.csv
                                              │
                                        pickml --tank
                                              │
                                        picks.jsonl
                                              │
                                    tools/eval_picks.py  (recall vs manuales)
```

En vivo:

```
SLINK_RING ──(TYPE_TRACEBUF2)──> pickml --ring ──> pn.jsonl / eqt.jsonl (+ .picks.txt)
```

---

## 2. Cómo funciona un picker neuronal

Los dos modelos son **redes convolucionales** (no “cajas negras” de caja: tienen una interfaz
numérica muy concreta):

1. Se toman **3 componentes** de una estación (`Z`, `N`, `E`) a **100 Hz**, en una ventana de
   longitud fija.
2. La ventana se pasa **en cuentas crudas** (no se filtra ni se normaliza en C).
3. El **grafo ONNX ya lleva la normalización dentro** (la misma que usa SeisBench al entrenar).
4. La red devuelve, para **cada muestra** de la ventana, tres probabilidades: `P`, `S` y
   `detección`.
5. El C busca **picos locales** de esas curvas por encima de un umbral y los convierte en picadas
   con un peso (`weight`) según la probabilidad.

### 2.1 Las dos arquitecturas

| | **PhaseNet** | **EQTransformer** |
|---|---|---|
| ventana nativa | **3001 muestras (30.01 s)** | **6000 muestras (60 s)** |
| forma | U-Net 1D (encoder/decoder con skip connections) | encoder tipo Transformer + decoder |
| salida | 3 clases por muestra (`N`, `P`, `S`) | 3 curvas (`Detection`, `P`, `S`) |
| coste por ventana (medido, 1 hilo) | **1.3–3.2 ms** | **21–34 ms** |
| arranque de sesión ORT | 31–170 ms (1.1 MB) | **~25 s** con el grafo *slim* (4.7 MB) |
| carácter | sensible: muchos picks, más ruido | conservador: pocos picks, más fuertes |

EQTransformer tarda ~25 s **una sola vez por arranque de proceso** en crear la sesión de ONNX
Runtime (no por ventana). Por eso un reinicio de EQT tiene ese coste; PhaseNet arranca al instante.

### 2.2 Post-proceso (está en C, no en el grafo)

`pickml_post.c` + `pickml_main.c`:

- **Umbral** `PThreshold` / `SThreshold`: debajo no hay pick.
- **Picos locales**: un máximo de la curva (no cada muestra por encima del umbral).
- **Peso 0–3** (`W0Prob`…`W3Prob`): probabilidad → peso; por debajo de `W3Prob` se descarta. El
  peso viaja al SCNL y a la BD cuando se publique.
- **Blinding** de borde (`BlindSamples`, 5 s): evita picks en el borde de la ventana.
- **Dedup** (`DedupSec`): el mismo SCNL+fase dentro de N segundos es un solo pick, aunque aparezca
  en ventanas solapadas (`StepSec` 10 s).
- **Compuerta S–P** (`MinSPSec`): descarta un S a menos de 2 s de un P de la misma estación.
- **Recorte de posición** (`MinPosSec`/`MaxPosSec`): no se emiten picos en los primeros segundos de
  la ventana (en EQT, 20 s).

### 2.3 Por qué la latencia no es “un número”

Una ventana **sólo se puede evaluar cuando está completa**. Una picada que cae al principio de la
ventana espera casi toda la ventana; una que cae al final sale casi enseguida. De ahí:

- PhaseNet: `lag` de ~1 s a ~33 s (ventana 30 s).
- EQT: `lag` de ~2 s a ~40 s (ventana 60 s, `MinPosSec` 20).

Eso es **arquitectura del modelo**, no un atraso del proceso. En vivo, `pickml` escribe `lag_s` y
`proc_s` por picada para medirlo en vez de estimarlo (ver §8.3).

---

## 3. Contrato ONNX (fijo, validado al abrir)

```
entrada   "waveform"  float32  [batch, 3, T]     ZNE, cuentas crudas
salida    "psd"       float32  [batch, T, 3]     (P, S, detección)
opset     17
T         WindowSamples del modelo (EQT 6000, PhaseNet 3001)
```

- La normalización **está dentro del grafo** (`annotate_batch_pre` de SeisBench). El C no la repite.
- Al abrir el modelo se lee el tipo de entrada y se exige `[B, 3, WindowSamples]`; si no coincide,
  el módulo **falla al arrancar** en vez de publicar basura.
- Por eso un `.onnx` cualquiera no sirve: hay que exportarlo con `tools/export_onnx.py`, que fija
  el contrato y reordena la salida de cada arquitectura (`Detection,P,S` de EQT) a `(P,S,det)`.
- **PhaseNet no tiene orden de clases garantizado**: el `forward` crudo de SeisBench entrega las
  tres curvas en el orden con que se convirtieron los pesos, y cambia de un peso a otro. Medido en
  0.10.2: `original` sale `(N,P,S)` y `jma_wc` (PhaseNetWC) sale `(P,S,N)`. El exportador lo detecta
  (`--channel-order auto`) localizando la clase N por su media sobre ruido; con `--channel-order
  nps|psn` se fuerza si alguna vez falla. Un `.onnx` de PhaseNet exportado con el orden equivocado
  **no falla al abrir**: produce curvas con P y S intercambiadas y un S saturado a 1.0 (§11.1).

---

## 4. Integración con EarthWorm

- `pickml` es un binario **C** que enlaza la librería estática de EarthWorm
  (`ew_8.0/lib/libew_mt.a`: `kom`, `logit`, `transport`) igual que los demás `csn*`.
- **No hay Python en runtime.** ONNX Runtime se carga con `dlopen()` desde la ruta `OrtLib` del
  `.d`; el binario no queda ligado a `libonnxruntime.so` (`ldd` no la lista) y arranca aunque ORT
  no esté (con el motor `stub`, útil para tests).
- El motor es conmutable por configuración:
  - `Engine stub` → no usa modelo; tests deterministas.
  - `Engine onnx` → carga `ModelFile` + `OrtLib`.
- **Modos**:
  - `--tank <file>` / `--store <root> --t0 --t1` / `--sds <root> --t0 --t1`: offline,
    determinista (un JSON por pick en stdout o `--out`). El SDS de SeisComP se lee directo y se
    decima 1x/2x/4x a `Rate`; un día/estación ausente se informa en `--stats`.
  - `--ring`: live desde `SLINK_RING`; escribe archivos y `--stats` cada 60 s; **no** publica en
    `PICK_RING`.
- En live lee del anillo con cursor propio y **purga** al arrancar para partir en la cola (no
  reprocesa minutos de historial). `LiveMaxAgeSec` (120 s) descarta trazas más viejas.
- El **store** se lee con el formato documentado (`<root>/<NET>.<STA>.<LOC>.<CHAN>/<YYYYMMDD>.tank`),
  **sin** `wstore_get`: esa función rellena huecos con ceros y la red los leería como señal. Aquí
  cada muestra sabe si existe; huecos cortos se interpolan (`MaxGapSec`) y los largos descartan la
  ventana.

---

## 5. Modelos y recursos

### 5.1 Dónde viven

Los modelos **no** se instalan con `make`. Son archivos que se copian a un directorio de recursos
fuera del repo (igual que `GfDir` de `csnwphase` o `SdsRoot`), y el `.d` los referencia por ruta
**absoluta**:

```
/mnt/d/resources/pickml/
├── eqt.onnx                  4.7 MB  EQT “slim” (el que corre hoy)
├── eqt.full.onnx             5.4 MB  EQT original exportado (referencia)
├── eqt.onnx.json             metadatos: opset, pesos, hash, paridad vs torch
├── phasenet.onnx             1.1 MB  PhaseNet
├── phasenet.onnx.json
└── onnxruntime-linux-x64-1.31.0/
    ├── include/onnxruntime_c_api.h
    └── lib/libonnxruntime.so
```

`eqt.onnx` es el grafo original con los ~8 300 nodos `Constant` convertidos en *initializers*
(`tools/slim_onnx.py`): mismo resultado numérico (paridad 1.3e-07), arranque de 58–63 s → **25 s**.
El `.full.onnx` se conserva para comparar.

### 5.2 Cómo se exportan (y cómo traer otros pesos)

Sólo hace falta para **crear/cambiar** un modelo; no en la máquina que sólo ejecuta.

```sh
python3 -m venv ~/.venvs/pickml
. ~/.venvs/pickml/bin/activate
pip install "torch==2.4.*" --index-url https://download.pytorch.org/whl/cpu
pip install "seisbench==0.10.*" onnx onnxruntime numpy

# EQT (ventana nativa 6000)
python3 tools/export_onnx.py --arch eqt --model original --out eqt.onnx

# PhaseNet (ventana nativa 3001)
python3 tools/export_onnx.py --arch phasenet --model original --out phasenet.onnx

# reducir el grafo de EQT (opcional pero recomendado)
python3 tools/slim_onnx.py eqt.onnx eqt_slim.onnx
```

`--model` acepta cualquier peso de SeisBench para esa arquitectura. **Cambiar de pesos no requiere
tocar C**: mientras el `.onnx` salga de `export_onnx.py`, el contrato es el mismo; sólo se cambia
`ModelFile` (y `ModelName` para etiquetar el JSONL).

### 5.3 Pesos disponibles en SeisBench 0.10.2 (venv actual)

| arquitectura | pesos |
|---|---|
| **PhaseNet** | `original`, `instance`, `iquique`, `scedc`, `stead`, `ethz`, `geofon`, `lendb`, `neic`, `obs`, `diting`, `jma`, `jma_wc`, `phasenet_sn`, `pisdl`, `volpick`, `aq2009` |
| **EQTransformer** | `original`, `original_nonconservative`, `instance`, `iquique`, `scedc`, `stead`, `ethz`, `geofon`, `lendb`, `neic`, `obs`, `volpick` |

Relevantes para esta red (norte de Chile, redes `C1`/`CX`):

- **`iquique`** (ambas): entrenado con la secuencia de Iquique, en la misma región y con
  instrumentación similar. Es el candidato más obvio a mejorar antes de probar arquitecturas
  nuevas.
- **`original_nonconservative`** (EQT): la variante no conservadora del original; debería dar más
  detecciones (y más ruido) que el `original` que corre hoy.
- **`diting`** (PhaseNet): entrenado con el dataset chino DiTing; no es la región, pero es un
  “pesado” habitual como alternativa.

**No hay hoy una comparación medida de variantes.** El pipeline para hacerla ya está:
`pickml_fetch` (eventos con picadas manuales) → exportar el candidato → `pickml --tank` →
`eval_picks.py` (recall y extras). Es la forma correcta de decidir: no por papers, sino contra las
picadas manuales de esta red.

> Aviso: la BD de desarrollo (`db.env`, puerto 5438) **no tiene picadas manuales**. Para calibrar
> hay que apuntar `DbEnvFile` al dump histórico (puerto **5435**, 3.86 M de picadas manuales) o a
> la BD que las tenga.

---

## 6. Dependencias e instalación (migración a otra máquina)

### 6.1 Runtime (lo que necesita el binario)

| dependencia | para qué | cómo llega |
|---|---|---|
| `ew_8.0/` vendorizado | headers + `libew_mt.a` (kom/logit/anillos) | ya versionado en el repo |
| `libonnxruntime.so` 1.31 (CPU) | inferencia | tarball oficial, `OrtLib` en el `.d` |
| `eqt.onnx` / `phasenet.onnx` | modelos | copiar (o exportar) |
| `pickS.sta` (o `.sta` propio) | lista 3C por estación | `run_working_v8/params/pickS.sta` |
| `libpq` | **sólo** `pickml_fetch` | `libpq-dev` |
| `libmseed2.a` | `pickml_fetch` **y** `pickml` (modo `--sds`) | estática, viene con EarthWorm |

`pickml` en sí sólo enlaza `libm`, `libc` y `libpthread` (más `libdl` para `dlopen`). `ldd
ew_8.0/bin/pickml` no muestra ORT.

### 6.2 Herramientas Python (no runtime)

Sólo para **exportar/analizar**, en un venv aparte:

| script | necesita | para qué |
|---|---|---|
| `tools/export_onnx.py` | `torch`, `seisbench`, `onnx` (+ `onnxruntime` para verificar) | exportar pesos a ONNX |
| `tools/slim_onnx.py` | `onnx`, `numpy` | reducir nodos `Constant` de EQT |
| `tools/store_window.py` | stdlib | extraer una ventana 3C del store (fixtures) |
| `tools/eval_picks.py` | stdlib | recall vs picadas manuales |
| `tools/latency.py` | stdlib | percentiles de `lag_s`/`proc_s` |

**El venv no se activa para ejecutar los módulos.** Se creó para instalar `torch`/`seisbench`/`onnx`
y exportar los modelos; una vez que existen los `.onnx`, la operación es 100 % C.

### 6.3 Pasos de instalación en una máquina nueva

1. Clonar el repo y `source ./ew8_unix.sh` (define `EW_HOME`, `EW_VERSION`, `EW_PARAMS`, `EW_LOG`).
2. Copiar/extraer ONNX Runtime (CPU o GPU) a una ruta estable, p. ej.
   `/opt/onnxruntime-1.31` o `/mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0`.
3. Copiar los modelos (`eqt.onnx`, `phasenet.onnx`, sus `.json`) a
   `/ruta/recursos/pickml/`. **No** hace falta meterlos en el repo (`resources/` está ignorado).
4. Ajustar en los `.d` las rutas `ModelFile`, `OrtLib`, `StaFile` y las salidas (`PickOutFile`,
   `PicksOutFile`, `RunMetaFile`).
5. Compilar e instalar:

   ```sh
   source ./ew8_unix.sh
   make -C ew_gui_tools/pickml ORT_DIR=/opt/onnxruntime-1.31
   make -C ew_gui_tools/pickml check
   make -C ew_gui_tools/pickml check-offline
   ```

   `ORT_DIR` sólo aporta el **header**; la librería se carga en runtime con `dlopen`.
6. Validar sin abrir ventana ni anillo:

   ```sh
   pickml ew_gui_tools/pickml/live_phasenet.d --print-config
   ```

7. (Opcional) `make check-engine ORT_DIR=... PICKML_MODEL=... PICKML_ORT=...` exige además el
   fixture dorado; comprueba paridad numérica contra PyTorch.
8. Probar en vivo **sin publicar**: `pickml <live.d> --ring --stats`. Debe escribir el JSONL y
   sacar una línea de stats cada 60 s.

### 6.4 Qué **no** hay que instalar

- No hace falta CUDA para el modo actual (CPU).
- No hace falta Python en la máquina de producción.
- No hace falta compilar EarthWorm: se usan sus librerías vendorizadas.
- No hay que copiar `GfDir` (eso es de `csnwphase`).

---

## 7. CPU, GPU y ajuste de la máquina

### 7.1 Estado actual: CPU

El código **no** añade ningún *execution provider*: ONNX Runtime usa el proveedor CPU por defecto.
Lo único configurable es:

| clave | efecto |
|---|---|
| `OrtThreads` | hilos *intra-op* (`0` = los que decida ORT) |
| `OrtOptLevel` | `none` / `basic` / `all` (medido: no cambia el arranque de EQT) |

Medición con la configuración actual (9.3 h, 137 estaciones en `pickS.sta`, ambos modelos a la vez):

| modelo | CPU | RSS | ventanas |
|---|---|---|---|
| PhaseNet | **4.9 %** de un núcleo | **89 MB** | ~3 ms cada 10 s por estación |
| EQTransformer | **19.1 %** (media con el arranque; ~16 % en régimen) | **156 MB** | ~27 ms cada 10 s por estación |

Con 137 estaciones y `StepSec 10` son ~13.7 ventanas/s: EQT ≈ 0.37 núcleos, PhaseNet ≈ 0.04. **El
throughput no es el problema**; sobra CPU en cualquier máquina moderna.

### 7.2 Si se quiere GPU

Para usar GPU hacen falta **dos** cosas:

1. **Runtime GPU**: el paquete `onnxruntime-linux-x64-gpu-<ver>` (incluye
   `libonnxruntime_providers_cuda.so` y `libonnxruntime_providers_shared.so`), con CUDA/cuDNN
   compatibles con esa versión. Los `.so` de proveedores deben poder resolverse (mismo directorio
   que `libonnxruntime.so` o `LD_LIBRARY_PATH`).
2. **Código**: llamar al proveedor CUDA en `pickml_engine_onnx.c` antes de `CreateSession`
   (`SessionOptionsAppendExecutionProvider_CUDA_V2` en la API C de ORT; comprobar el nombre exacto
   en el header de la versión). Hoy **no** está implementado: el binario usaría CPU aunque se le
   dé la librería GPU.

Cuándo tiene sentido: si se sube mucho el número de estaciones, se baja `StepSec` (más ventanas/s)
o se quiere EQT con ventanas más cortas. Para 137 estaciones a 10 s **no compensa**: el coste por
ventana es de milisegundos y el arranque de EQT (25 s) no mejora con GPU (es construcción del
grafo).

---

## 8. Operación

### 8.1 Configuración

Referencia completa de claves en `ew_gui_tools/pickml/README.md` (§Configuración). Las que más se
tocan:

| clave | EQT | PhaseNet | nota |
|---|---|---|---|
| `WindowSamples` | 6000 | 3001 | **nativa**; se valida contra el grafo |
| `StepSec` | 10 | 10 | solape entre ventanas |
| `MinPosSec` | 20 | 2 | recorte inicial |
| `PThreshold` / `SThreshold` | 0.20 | 0.30 | barrido en 3 eventos |
| `W3Prob` | 0.20 | 0.30 | por debajo se descarta |
| `LiveMaxAgeSec` | 120 | 120 | tope de atraso aceptado en vivo |

`StaFile` tiene el formato `PickFlag Pin Sta Net Loc ChanE ChanN ChanZ` (idéntico a `pickS.sta`).
Hoy hay **137 estaciones** (C1 73, CX 23, IU 14, C 12, PE 5, TC 4, II 4, AI 1, RI 1).

### 8.2 Arranque manual (recomendado para probar)

```sh
cd /home/seba/Dev/ew8/DLpickers
source ./ew8_unix.sh
cd run_working_v8/params

pickml ../../ew_gui_tools/pickml/live_phasenet.d --ring --stats
pickml ../../ew_gui_tools/pickml/live_eqt.d       --ring --stats
```

- Salidas: `run_working_v8/pickml_live/{pn,eqt}.jsonl`, `.picks.txt`, `.meta.json`.
- Al arrancar abre la salida con `"w"`: **trunca** el archivo anterior.
- `Ctrl-C` (SIGTERM) cierra limpio. Para parar ambos: `pkill -TERM -x pickml`.

### 8.3 Latencia: qué mirar

En live, cada línea del JSONL añade:

| campo | significado |
|---|---|
| `gen_t_epoch` / `gen_t_utc` | hora de pared en que se escribió la picada |
| `lag_s` | `gen_t − t` (muestra → picada escrita) |
| `proc_s` | `gen_t − fin_ventana` (fin de ventana → escrita) |

- `lag_s` es lo que ve el consumidor; incluye la espera del algoritmo.
- `proc_s` aísla el proceso: si **crece en el tiempo**, hay backlog.
- La línea `--stats` resume `lag_ms_medio/max` y `entrada_ms_medio/max` (retraso de la traza del
  anillo).
- `tools/latency.py` da percentiles por fase y por bloque de 10 min:

  ```sh
  python3 ew_gui_tools/pickml/tools/latency.py run_working_v8/pickml_live/*.jsonl
  ```

**Ojo al interpretar `proc_s` alto:** en esta operación, los valores de 80–120 s **no** son CPU.
Son ventanas que se completan cuando llega una traza **tardía** de SeedLink (hasta
`LiveMaxAgeSec`). Se ve en que el `proc_s` mediano se queda plano (~3.5 s) y sólo hay picos
aislados (~2 % de las picadas). Un backlog real se vería como un `proc_s` mediano **creciente**.

### 8.4 Coste de arranque

- PhaseNet: sesión de ORT en 31–170 ms.
- EQT: **~25 s** (grafo slim). Es una vez por proceso; el log lo anuncia
  (`pickml: onnx arranque: ... sesion=NNNN ms`).

---

## 9. Calibración y validación

### 9.1 Preparar eventos

```sh
pickml_fetch ew_gui_tools/pickml/pickml_fetch.d \
    --event <event_id> --out /tmp/cal/<event_id>
```

Deja `event.tank` (3C por estación), `picks_ref.csv` (**sólo picadas manuales**; las revisadas por
una persona) y `meta.json`. Las componentes se derivan del canal picado (`HHZ` → `HHE/HHN/HHZ`).
Las ondas salen del store y, si el día no está, del SDS (`SdsRoot`).

### 9.2 Picar y evaluar

```sh
pickml <modelo>.d --tank /tmp/cal/<event>/event.tank --out picks.jsonl
python3 ew_gui_tools/pickml/tools/eval_picks.py \
    --picks picks.jsonl --ref /tmp/cal/<event>/picks_ref.csv --min-prob 0.3
```

Criterio: **recall** contra las manuales. Lo que el modelo detecta de más **no** se cuenta como
falso positivo: son detecciones adicionales (la referencia no es exhaustiva). Ajustar
`--min-prob`/`--min-prob-p/s` para ver el compromiso.

### 9.3 Paridad numérica

`make check-engine` compara la salida del `.onnx` contra PyTorch sobre una ventana real
(`test/fixtures/win1.tank`). Sirve para detectar un export mal hecho (diferencias ~1e-07).

---

## 10. Resultados de la corrida (corte 2026-10-21 06:50 UTC, 9.3 h)

Configuración: `live_eqt.d` (0.20/0.20) y `live_phasenet.d` (0.30/0.30), `StaFile pickS.sta`
(137 estaciones), ambos en paralelo desde 2026-10-20 21:31 UTC.

| | PhaseNet | EQTransformer |
|---|---|---|
| picks totales | **12 008** | **828** |
| P | 8 338 | 230 |
| S | 3 670 | 598 |
| estaciones con picks | 78 | 62 |
| `lag_s` P (p50 / p90 / máx) | 6.3 / 12.1 / 131.8 s | 34.4 / 41.8 / 69.8 s |
| `lag_s` S (p50 / p90 / máx) | 12.5 / 30.5 / 127.5 s | 25.9 / 40.9 / 79.8 s |
| `proc_s` p50 / máx | 3.4 / 124 s | 3.0 / 55.7 s |
| picks con `proc_s` > 10 s | 251 (2.1 %) | 19 (2.3 %) |

El `proc_s` mediano es **plano hora a hora** (3.21–3.57 s en PhaseNet, 2.7–4.2 s en EQT) durante
las 9 h, así que **no hay backlog**: los picos corresponden a trazas tardías, como se explica en
§8.3.

### 10.1 Contraste con las soluciones automáticas

Ventana 21:25–07:00, **44 eventos** con solución (NLLoc/HYPOINVERSE/csnloc) y **258 picadas P + 59
S** de referencia (deduplicadas por estación/fase/tiempo). Tolerancia: P ±2 s, S ±3 s, misma
estación.

| | PhaseNet | EQT | Unión |
|---|---|---|---|
| eventos con ≥1 P cubierta | **42/44** (95 %) | 33/44 (75 %) | 42/44 |
| eventos con ≥3 P cubiertas | **41/44** (93 %) | 23/44 (52 %) | 41/44 |
| eventos con ≥1 S cubierta | 24/44 (55 %) | 25/44 (57 %) | **26/44** |
| **recall P** | **91.1 %** (235/258) | 45.0 % (116/258) | **91.1 %** |
| **recall S** | 69.5 % (41/59) | **91.5 %** (54/59) | **93.2 %** (55/59) |
| picks “adicionales” | 97.8 % | 80.4 % | — |

Lectura:

- **PhaseNet es el picker de P**: cubre el 91 % de las P de las soluciones y 42/44 eventos.
- **EQT es el picker de S**: con 15 veces menos picadas, cubre el 91.5 % de las S.
- **Son complementarios**: la unión sube el S a 93 % y añade eventos con S que ninguno cubre solo.
- El porcentaje de “adicionales” es alto porque la BD sólo tiene las picadas de 44 eventos
  localizados; el resto son detecciones del modelo sin referencia (no se evalúan como error).
- La variación entre el corte de 5 h y el de 9 h es de pocos puntos (P de PhaseNet 94→91 %, S de
  EQT 85→92 %): no se ve deriva temporal, sólo distinta sismicidad en cada tramo.

### 10.2 Coherencia interna S–P

Sólo el 44 % (PhaseNet) y el 36 % (EQT) de las S tienen una P de la misma estación 0.2–30 s antes
(mediana S–P: 8.1 s y 14.1 s). No implica que estén mal: si el modelo no picó la P (o cayó fuera de
la ventana), la S sigue siendo válida. El contraste con la BD (§10.1) es mejor referencia que esta
métrica.

### 10.3 Detecciones adicionales

Como proxy, agrupando P de ≥3 estaciones en ventanas de 5 s: **974 clusters** en PhaseNet (~105/h)
y **28** en EQT, frente a 44 soluciones en el mismo periodo. Muchos serán sismicidad local pequeña
o ruido coherente; no se evalúan sin una referencia. La ventana de 5 s es deliberadamente estrecha:
para EQT (picks más dispersos) subestima la detección a nivel de evento.

### 10.4 Cómo se mide (reproducible)

1. Soluciones: una por evento, prefiriendo NLLoc → HYPOINVERSE → csnloc y, dentro de cada una, la
   de más estaciones:

   ```sql
   SELECT DISTINCT ON (o.event_id) o.event_id, o.origin_id, o.origin_time, o.station_count,
          o.standard_error, o.location_algorithm
   FROM origin o
   WHERE o.origin_time BETWEEN '<t0>+00' AND '<t1>+00'
   ORDER BY o.event_id,
            CASE o.location_algorithm WHEN 'NLLoc' THEN 0 WHEN 'HYPOINVERSE' THEN 1 ELSE 2 END,
            o.station_count DESC NULLS LAST;
   ```

2. Picadas de referencia: todas las de esas soluciones (incluye las S de csnloc/HYPOINVERSE, que
   NLLoc no copia), deduplicadas por `(estación, fase, tiempo)`.
3. Criterio: misma estación y fase, `|Δt| ≤ 2 s` para P y `≤ 3 s` para S. Se informa recall por fase
   y cobertura por evento; las picadas del modelo sin referencia se cuentan como **adicionales**,
   no como error.

---

## 11. PhaseNetWC: modelo único y primer ajuste

Decisión vigente: **un solo modelo**, PhaseNetWC (pesos `jma_wc`), ventana nativa 3001 (30.01 s),
100 Hz, y **sin publicar en `PICK_RING`** (el binario no tiene ese camino: no es una opción de
configuración, es código que falta). `docs/DL_to-do.md` lista lo que queda para el modo live, el
reentrenamiento y la promoción.

### 11.1 Dos bugs que este cambio destapó

**(a) El orden de clases de PhaseNet no es estable.** El exportador reordenaba la salida como
`(N, P, S)` fijo (`export_onnx.py`), que es lo que necesita `original`, pero `jma_wc` entrega
`(P, S, N)`. Con el mapeo viejo el `.onnx` **no fallaba al abrir**: producía un S saturado
(99.2 % de las muestras > 0.9, media 0.996) y una curva etiquetada “P” que en realidad era la S
(peakaba en los tiempos de S de la referencia). Se detectó midiendo la curva volcada contra las
picadas de referencia, no con `--check-model` (que sólo valida formas).

Arreglo: `export_onnx.py --channel-order auto` localiza la clase N por su media sobre ruido (N
siempre domina) y arma `(P,S)` con las otras dos en orden de índice. Medido: `jma_wc` →
`P=0 S=1 N=2` (medias 0.002 / 0.001 / 0.996); `original` → `P=1 S=2 N=0` (1.000 / 0.000 / 0.000).
El orden queda en el `.onnx.json` (`channel_order`).

**(b) `pickml_fetch` nunca leía el store con localización nula.** Construía la ruta con el `loc`
vacío de la picada (`CX.PB19..HHZ`) en vez del `--` con que el store nombra el directorio, así que
el `fopen` fallaba en silencio y caía al SDS. Con eventos del día anterior funcionaba *de casualidad*
(estaban en el SDS); con los del día en curso (`2026.294`) daba `estaciones=0/4` y un `event.tank`
de 0 bytes. Arreglo: normalizar `loc` vacío a `--` en `store_channel` (`pickml_fetch.c`).

### 11.2 Coste medido

| | PhaseNet `original` | PhaseNetWC (`jma_wc`) |
|---|---|---|
| ventana | 3001 | 3001 |
| ms por ventana (1 hilo) | 1.6 | **3.2** |
| arranque | 102 ms | **166 ms** |
| `.onnx` | 1.1 MB | **4.3 MB** |
| nodos tras `slim_onnx.py` | — | 224 → 145 |

Con 137 estaciones y `StepSec 5` el coste por ventana es irrelevante (≈33 % de un núcleo a
`StepSec 5` en el peor caso). El límite sigue siendo la ventana (30 s), no la CPU.

### 11.3 Corpus y barrido (primer ajuste)

- **Corpus**: 45 eventos de la ventana 2026-10-20 21:25 → 2026-10-21 07:00, **254 P + 55 S** de
  referencia (picadas automáticas de `csnloc`/HYPOINVERSE/NLLoc, no manuales), armado con
  `pickml_fetch` **desde el store** —el SDS no se tocó— y `tools/calib_run.sh`.
- **Herramienta**: `tools/sweep_post.py` reimplementa el post-proceso del C y lo aplica sobre las
  curvas volcadas (`--dump-curves`, con el sidecar `curve.idx`). Paridad verificada contra el
  binario: 184/184 picadas idénticas en el evento de prueba.
- **Umbrales** (recall P / S, picadas, extras): 0.10–0.30 dan lo mismo (91.7 / 92.7); P 0.40
  mantiene el recall con 13 % menos picadas; P 0.50 pierde 2 P; S 0.40 baja a 90.9 y S 0.60 a 85.5.
- **Geometría**: `MinPosSec` 0/2/4 es indistinto (6 pierde 2 picadas); `MaxPosSec 26` **baja** el
  recall (90.9 / 90.9) ⇒ no se acota.
- **Dedup**: sin dedup el volumen se **triplica** (3213 vs ~1000) con el mismo recall; 3 s quita
  ~2 % más de duplicados sin perder recall.
- **Config final** (`run_working_v8/params/pickml_phasenetwc.d`): `P 0.40`, `S 0.30`,
  `DedupSec 3.0`, `MinSPSec 2.0`, `MinPosSec 2.0`, `MaxPosSec 0`, `W3Prob 0.30`.
  Resultado con el binario real: **985 picadas, recall P 91.7 % (233/254), S 92.7 % (51/55)**,
  `|dt|` mediana 0.41 s (P90 0.88 s) y 0.41 s (P90 0.95 s), ~402 picadas/h.

### 11.4 A/B contra el `original` (mismo corpus, mismo post-proceso)

| | `original` | **`jma_wc`** |
|---|---|---|
| picadas | 672 (454 P / 218 S) | **985** (638 P / 347 S) |
| recall P | 91.3 % | **91.7 %** |
| recall S | 85.5 % | **92.7 %** |
| extras (no penalizan) | 393 | 701 |
| ms/ventana | 1.6 | 3.2 |

PhaseNetWC gana **+1.2 pp de P y +7.2 pp de S** a cambio de ~47 % más de picadas (que es lo que
el usuario quiere: más detecciones) y el doble de CPU por ventana (irrelevante). La referencia es
automática y el corpus es de una noche: es un **primer ajuste**, no la calibración final.

### 11.5 Cómo se corre

```bash
source ./ew8_unix.sh
cd run_working_v8/params
pickml pickml_phasenetwc.d --ring --stats        # sólo escribe ../pickml/phasenetwc.*

# evento histórico, directo del SDS (sin store ni tank):
pickml pickml_phasenetwc.d --sds /mnt/sds/archive \
    --t0 2026-10-20T21:45:00 --t1 2026-10-20T21:49:00 --jsonl --stats
```

Para repetir el ajuste: `tools/calib_run.sh` arma el corpus y `tools/sweep_post.py --root` barre
umbrales y geometría sin volver a correr el modelo.

---

## 12. Limitaciones y siguientes pasos

- **No publica en `PICK_RING`**: es deliberado hasta validar latencia y calidad. Publicar exige
  subir `AssocWindowSec` de `csnloc` (45 → 90 s) porque hay `lag` de hasta 40 s.
- **La latencia mínima la fija la ventana**, no el código: no es un picker de “segundos”.
- **EQT tarda 25 s en arrancar**; un reinicio por `startstop` tiene ese coste.
- **La BD de desarrollo no tiene picadas manuales**: para calibrar de verdad hay que usar el dump
  histórico (5435).
- **El ajuste está hecho contra picadas automáticas** (una noche, 45 eventos): sirve para arrancar,
  no para cerrar. La calibración real necesita el dump manual (5435) y un split por evento
  (`docs/DL_to-do.md` §3).
- **Falta comparar los otros pesos** sobre el corpus manual: `iquique`, `stead`, `phasenet_sn`
  (existe en 0.10.2) y `diting`, todos por el mismo camino (`--arch phasenet --model <nombre>`).
  `jma_wc` es el único PhaseNetWC; para más capacidad sin cambiar de arquitectura no hay más pesos.
- **GPU no implementada**: sólo tiene sentido si se sube mucho el número de estaciones o se baja
  `StepSec`.
