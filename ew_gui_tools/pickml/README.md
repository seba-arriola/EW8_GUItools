# pickml — picker neuronal P/S (EQTransformer sobre ONNX)

Módulo **headless** que detecta y pica ondas **P y S** con una red neuronal,
sin Python en runtime: consume un grafo **ONNX** con la API C de ONNX Runtime
(cargada con `dlopen`, así que el binario base no depende de ORT) y publica
`TYPE_PICK_SCNL` en `PICK_RING`.

No asocia ni localiza (eso es `csnloc`) ni calcula magnitudes.

## Flujo

```
tank / store (TRACE2_HEADER + int32)      SLINK_RING (TYPE_TRACEBUF2)
                 │                                 │
                 └──────────► fuente ◄─────────────┘
                                 │  (por estación y componente)
                                 ▼
                    buffer 3C 100 Hz con máscara de validez
                    (los huecos NO son ceros: se marcan)
                                 │  60 s = 6000 muestras
                                 ▼
                        motor ONNX  [1,3,6000] -> [1,6000,3]
                                 │  (P, S, detección por muestra)
                                 ▼
              post-proceso C: umbral + pico + ventana útil + dedup
                                 │
                 ┌───────────────┴───────────────┐
                 ▼                               ▼
        JSONL en stdout (offline)        TYPE_PICK_SCNL -> PICK_RING
```

## Contrato del grafo

| | |
|---|---|
| entrada | `float32[B, 3, 6000]`, canales **Z, N, E**, 100 Hz, **muestras crudas** |
| salida | `float32[B, 6000, 3]` = (P, S, detección) por muestra |
| normalización | **dentro del grafo** (`norm=std` de EQTransformer); pickml no filtra ni normaliza |
| blinding / solape / umbrales | **fuera** del grafo, en C |

El `forward` nativo de SeisBench devuelve **tres** tensores en el orden
`(Detection, P, S)`; el exportador los apila en el orden `(P, S, detección)` que
espera el C, y eso queda fijado en el wrapper (no se asume).

Se valida al arrancar leyendo la forma real del tensor de entrada: si no es
`[B,3,6000]` el módulo sale con error en vez de publicar basura.

El `.onnx` se genera una sola vez con `tools/export_onnx.py` (SeisBench +
PyTorch, entorno aparte, **no** es dependencia de runtime). El script fija el
contrato anterior, detecta el layout de entrada del modelo en vez de asumirlo,
escribe un `.json` con sha256/opset/versiones y puede emitir el fixture dorado
(`test/fixtures/win1.tank` + curvas de referencia).

## Latencia: lo que hay que saber antes de conectarlo a csnloc

Una ventana sólo se puede evaluar cuando está **completa**, así que la picada se
publica al terminar la ventana que la contiene. La latencia no es un número
único: depende de dónde caiga el arribo dentro de la ventana.

Medido en vivo (no estimado) con `lag_s` de cada picada:

| modelo | ventana | `MinPosSec` | lag p50 | lag p90 | lag máx |
|---|---|---|---|---|---|
| PhaseNet | 30.01 s | 2 s | 6–8 s | 12–27 s | ~33 s |
| EQTransformer | 60 s | 20 s | 2–34 s | 13–37 s | ~40 s |

Por eso el modo live escribe en cada línea (ver §Salida):

- `lag_s` = `gen_t` − `t`: muestra → picada escrita. Es lo que ve el consumidor.
- `proc_s` = `gen_t` − fin de ventana: fin de ventana → escrita. Aísla el coste
  del proceso (~3–5 s p50, máx ~9 s, dominado por el retraso de entrada del
  anillo y el sesgo entre componentes). **Si `proc_s` crece con el tiempo, el
  módulo no va al día**; si se queda plano, es factible en tiempo real.

`tools/latency.py` resume ambas por fase y por bloque de 10 min:

```sh
python3 tools/latency.py run_working_v8/pickml_live/*.jsonl
```

`csnloc` asocia con `PickBuffer_Snapshot(now, AssocWindowSec)` y `AssocWindowSec`
vale **45 s** en producción, procesando cada `AssocWindowSec/2+1` = **23 s**
(`csnloc.c:922-928`): un pick con más de ~22 s de retraso puede no entrar en
ningún ciclo y **descartarse en silencio**. Con `lag` de hasta ~40 s, para el
modo anillo hay que subir `AssocWindowSec` (propuesta: **90 s**) y verificarlo
con un test end-to-end; no se asume.

## Modos

```sh
pickml <pickml.d> --tank  <file> [--jsonl] [--stats] [--dump-curves <prefijo>]
pickml <pickml.d> --store <root> --t0 <t> --t1 <t> [--jsonl] [--stats]
pickml <pickml.d> --sds   <root> --t0 <t> --t1 <t> [--jsonl] [--stats]
pickml <pickml.d> --print-config
pickml <pickml.d> --check-model
```

`--dump-curves` escribe `<prefijo>.{p,s,det}.f32` con las curvas crudas del
modelo (float32, 6000 valores por ventana). Es lo que usa `check-engine` para
comparar contra PyTorch; no hace falta en operación.

- `--t0/--t1` aceptan `YYYY-MM-DDTHH:MM:SS`, `YYYY-MM-DD HH:MM:SS`,
  `YYYYMMDDhhmmss` o epoch.
- **Sin flags de fuente el modo es VIVO** (`SLINK_RING`), igual que
  `csnloc <d>`/`pickS <d>`: es el que usa `startstop`, que sólo pasa el `.d`
  (`--ring` sigue valiendo como sinónimo explícito). Dos fuentes a la vez
  (`--tank --sds`, …) siguen siendo error de uso.
- El **directorio de cada salida se crea si falta** (`PickOutFile`,
  `PicksOutFile`, `RunMetaFile`, o `--out/--picks-out/--meta`): en un worktree
  nuevo `run_working_v8/pickml/` no existe y el arranque moría con `4`.
- Códigos de salida: `0` ok · `2` configuración inválida · `3` dependencia
  ausente (ORT/modelo) o modo no implementado · `4` datos/fuente.
- La salida offline está **ordenada por (t, sta, fase)**, así que es
  byte-idéntica entre corridas (lo exige `make check-offline`).
- El **modo vivo** escribe archivos (JSONL + `.picks.txt`) y
  `--stats`, y **publica** los picks como `TYPE_PICK_SCNL` en `OutRing` cuando
  `PublishRing 1` (con `PublishPhases P|S|PS` y `PublishMaxWeight 0..3`).
  `--publish-dry-run` valida y cuenta sin escribir. `LogFile` 0/1/2 = consola /
  ambos / sólo `EW_LOG/pickml_*.log` (el nombre es literal: el `.d` puede
  llamarse distinto y `diag.sh log pickml` lo encuentra igual).

### El store no se modifica

`pickml` lee el formato documentado del store:

```
<root>/<NET>.<STA>.<LOC>.<CHAN>/<YYYYMMDD>.tank     TRACE2_HEADER(64 B) + int32
```

Recorre por estación y por día, con un lookahead por componente para
entrelazar Z/N/E por tiempo (sin eso la ventana 3C nunca se completa). **No**
usa `wstore_get` a propósito: esa función rellena los huecos con **ceros** y no
los distingue de señal, lo que envenena la normalización de la red. Aquí cada
muestra sabe si existe; los huecos cortos se interpolan (`MaxGapSec`) y los
largos descartan la ventana.

## Configuración (`pickml.d`)

Ver el ejemplo completo en `pickml.d`. Resumen:

| Clave | Default | Notas |
|---|---|---|
| `StaFile` | `pickml.sta` | `PickFlag Pin Sta Net Loc ChanE ChanN ChanZ` (igual que `pickS.sta`) |
| `ModelName` | `eqt` | etiqueta que va al JSONL y a la metadata (`eqt`/`phasenet`) |
| `Engine` | `onnx` | `stub` = tests sin modelo |
| `ModelFile`, `OrtLib` | — | relativas al cwd o absolutas |
| `OrtThreads` | 0 | 0 = auto |
| `OrtOptLevel` | `basic` | `none`/`basic`/`all`; no cambia el arranque (medido) |
| `WindowSamples` / `Rate` | 6000 / 100 | ventana **nativa** del modelo: EQT 6000, PhaseNet 3001. Se valida contra la dimensión declarada del grafo |
| `StepSec` | 10 | paso entre ventanas |
| `MinPosSec` / `MaxPosSec` | 20 / 0 | recorte de bordes; `0` = `WindowSec - 1/rate` |
| `BlindSamples` / `Stacking` | 500 / `avg` | blinding y solape |
| `PThreshold` / `SThreshold` / `DetThreshold` | 0.20 / 0.20 / 0 | EQT; PhaseNet usa 0.30/0.30 |
| `W0Prob`…`W3Prob` | 0.90/0.70/0.50/0.30 | prob → weight 0..3; debajo de `W3Prob` se descarta |
| `DedupSec` | 2.0 | mismo SCNL+fase dentro de esta ventana = un solo pick |
| `MinSPSec` | 2.0 | descarta un S a menos de esto de un P de la misma estación (0 = off); igual que `pickS` `GuideMinDtSec` |
| `MaxGapSec` | 1.0 | hueco máximo interpolable dentro de una ventana (1.7 % de 60 s) |
| `PReportChan` / `SReportChan` | Z / N | canal con el que se publica cada fase |
| `PickOutFile` | — | JSONL de picks; vacío = stdout (la CLI `--out` manda) |
| `PicksOutFile` | — | líneas `TYPE_PICK_SCNL` en texto; vacío = no se escribe |
| `RunMetaFile` | — | cabecera JSON de la corrida (modelo, ventana, umbrales) |

Validaciones al cargar (exit 2): `WindowSamples >= 8`, `Rate > 0`,
`StepSec ∈ (0, WindowSec]`, `StepSec <= WindowSec - MinPosSec` (si no, habría
muestras que no caen en ninguna ventana), `BlindSamples/rate < MinPosSec`,
umbrales y `W*Prob` coherentes, canales de reporte válidos. La forma del grafo
se valida al abrirlo: `dims == [B, 3, WindowSamples]`.

## Salida

Por defecto, un JSON por línea en **stdout**; con `--out <file>` (o
`PickOutFile`) va a un archivo. Siempre ordenado por `(t, sta, fase)`:

```json
{"sta":"PB15","net":"CX","loc":"--","chan":"HHZ","phase":"P",
 "t_epoch":1792473303.558000,"t_utc":"2026-10-20 05:15:03.558",
 "prob":0.9240,"weight":0,"win_t0":1792473299.558000,"model":"eqt"}
```

`--picks-out <file>` (o `PicksOutFile`) escribe las mismas fases en el formato
`TYPE_PICK_SCNL` (**ojo**: el SCNL va `STA.CHAN.NET.LOC`):

```
8 0 0 1 PB15.HHZ.CX.-- ?0 20261020051503.558 0 0 0 P A
```

Origen `A` (automático) para que `hypodb` no lo confunda con un re-pick manual.
El `mod`/`inst` van a 0: en el anillo los pone el módulo; aquí es un archivo de
calibración. El SCNL **no** lleva la marca de generación (el formato es el del
anillo y `putpick` no la admite); para medir latencia se usa el JSONL.

En **modo live** cada línea del JSONL añade las métricas de latencia:

```json
{"sta":"LL03",...,"phase":"P","t_epoch":1792530110.840001,
 "t_utc":"2026-10-20 21:01:50.840","prob":0.3122,"weight":3,
 "win_t0":1792530098.560001,
 "gen_t_epoch":1792530132.111487,"gen_t_utc":"2026-10-20 21:02:12.111",
 "lag_s":21.271,"proc_s":2.705,"model":"phasenet"}
```

- `gen_t_epoch`/`gen_t_utc`: hora de pared en que se escribió la picada.
- `lag_s`: `gen_t` − `t` (muestra → picada escrita).
- `proc_s`: `gen_t` − fin de ventana (fin de ventana → escrita).

En offline estos tres campos **no** se escriben, para que la salida siga siendo
determinista (`check-offline` compara corridas con `cmp`).

`--meta <file>` (o `RunMetaFile`) deja la corrida reproducible: modelo,
`model_file`, motor, ventana, paso, umbrales, `w_prob`, `dedup`, `min_sp`,
fuente (`tank`/`store`), `t0`/`t1`, estaciones, ventanas, picks y `engine_ms`.

## Compilación

```sh
source ./ew8_unix.sh
make -C ew_gui_tools/pickml            # autodetecta el ORT vendorizado (motor real)
make -C ew_gui_tools/pickml check
make -C ew_gui_tools/pickml check-offline
```

El Makefile **autodetecta** `/mnt/d/resources/pickml/onnxruntime-linux-x64-*`
(el mismo ORT que el `.d` carga por `OrtLib`) y compila el motor real; si no lo
encuentra, avisa y cae al motor `stub`. Ojo: un binario `stub` instalado hace
que `Engine onnx` falle en runtime (`exit 3`) y el módulo quede muerto bajo
`startstop`, así que en la máquina de operación el aviso del build no debe
aparecer. Para forzar cada caso:

```sh
make -C ew_gui_tools/pickml ORT_DIR=            # stub explícito (sin motor ONNX)
make -C ew_gui_tools/pickml ORT_DIR=/ruta/a/onnxruntime
make -C ew_gui_tools/pickml check-engine ORT_DIR=... \
     PICKML_MODEL=/ruta/eqt.onnx PICKML_ORT=/ruta/libonnxruntime.so
```

`ORT_DIR` sólo aporta el **header** en build (`include/onnxruntime_c_api.h`);
la librería se carga en runtime con `dlopen` desde la clave `OrtLib` del `.d`,
así que el binario **no** queda ligado a ella (`ldd` no la lista) y arranca
aunque ORT no esté.
Medido con **ORT 1.31** (la API `ORT_API2_STATUS`); versiones anteriores que
devuelven el `OrtStatus*` de otra forma no compilan (falla explícita, no
silenciosa).

**Coste medido (32 cores, sin GPU, ORT 1.31):**

| etapa | valor |
|---|---|
| `dlopen` de `libonnxruntime.so` | 5–7 ms |
| `CreateEnv` + `GetApi` | ~50 ms |
| `CreateSession` (grafo EQT slim) | **~25 s** |
| `CreateSession` (grafo PhaseNet, 1.1 MB) | **31–170 ms** |
| inferencia por ventana EQT (`OrtThreads 1`) | **21–34 ms** (de media en caliente) |
| inferencia por ventana PhaseNet (`OrtThreads 1`) | **1.3–3.2 ms** |

El arranque de EQT baja a ~25 s con el grafo slim (`tools/slim_onnx.py`: convierte los
8 386 nodos `Constant` en 76 initializers únicos). Es un coste **único por arranque de
proceso**, no por ventana.

El minuto de arranque es del **grafo EQT**: 20 838 nodos y 8 269 constantes (un
modelo de 1 nodo tarda 14 ms en la misma máquina; PhaseNet, 1.1 MB, tarda 31
ms). No depende del nivel de optimización (`all`/`basic`/`none` = 64/63/66 s)
ni de dónde esté el `.onnx` (`/mnt/d` y `/tmp` dan lo mismo). Es un coste
**único por proceso**; el módulo lo anuncia en el log
(`pickml: onnx arranque: ... sesion=NNNN ms`).

Con 70 ms/ventana, 130 estaciones a `StepSec 10` son 13 ventanas/s ≈ **0.9
núcleos** con EQT, y ~0.02 núcleos con PhaseNet: el throughput no es el
problema.

## Fixture y datos reales

El fixture dorado de `check-engine` es una **ventana real** (sismo local,
`CX.PB15`, 2026-10-20 05:15:03 UTC, 565k counts de peak-to-peak) extraída del
store. Para regenerarlo:

```sh
# 1. buscar un transitorio real (peak-to-peak de 1 s, NO max|x|: hay estaciones
#    con offset DC de millones de counts que ganarían ese ranking sin ser sismo)
python3 tools/store_window.py --root ../../run_working_v8/wstore --scan --top 10

# 2. materializar la ventana 3C (alinea por tiempo; las componentes pueden
#    empezar con segundos de diferencia)
python3 tools/store_window.py --root ../../run_working_v8/wstore \
    --scnl CX.PB15.--.HHZ --t0 1792473299.558 --dur 60 --out /tmp/win.tank

# 3. referencia PyTorch + fixture
python3 tools/export_onnx.py --out /mnt/d/resources/pickml/eqt.onnx \
    --fixture-tank /tmp/win.tank --fixture-t0 1792473299.558 \
    --fixture-dir test/fixtures
```

Sobre esa ventana el grafo normalizado da **P 0.924 en la muestra 400** y
**S 0.377 en la 2327**; el C reproduce la curva de referencia a **3.6e-07**.

**La normalización va DENTRO del grafo**, pero no está en `forward`: SeisBench la
aplica en `annotate_batch_pre` (quita la media y divide por σ **por
componente**). El exportador la mete en el wrapper. Sin ella el modelo veía
cuentas crudas: P 0.446 y el arribo S etiquetado como P (ver la evaluación).

Para exportar el segundo modelo:

```sh
python3 tools/export_onnx.py --arch phasenet --out /mnt/d/resources/pickml/phasenet.onnx
```

PhaseNet usa ventana nativa de **3001** muestras (30.01 s); el contrato de
salida se mantiene en `(P, S, det)` con `det = max(P, S)`, porque PhaseNet no
tiene curva de detección.

**El orden de clases de PhaseNet no es estable entre pesos** y `--check-model`
no lo valida (sólo mira formas). Medido en SeisBench 0.10.2: `original` sale
`(N,P,S)` y `jma_wc` (PhaseNetWC) sale `(P,S,N)`. El exportador lo detecta solo
(`--channel-order auto`, localiza N por su media sobre ruido) y lo deja en el
`.onnx.json`; si un peso nuevo saliera mal, forzar `--channel-order nps|psn`.
Un grafo con el orden equivocado no falla al abrir: entrega P y S
intercambiadas y un S saturado a 1.0.

Para el modelo actual (PhaseNetWC, pesos `jma_wc`):

```sh
python3 tools/export_onnx.py --arch phasenet --model jma_wc \
        --out /mnt/d/resources/pickml/phasenetwc.onnx
python3 tools/slim_onnx.py /mnt/d/resources/pickml/phasenetwc.onnx \
        /mnt/d/resources/pickml/phasenetwc.slim.onnx
```

## Evaluación con datos reales (2026-10-20, sismo de CX.PB15)

**El veredicto anterior era un artefacto del exportador.** El export llamaba a
`forward` sin la normalización de SeisBench (`annotate_batch_pre`), así que el
modelo veía cuentas crudas. Misma ventana, mismo modelo:

| versión del grafo | P en +4.0 s | arribo S en +23.3 s | ¿S etiquetada? |
|---|---|---|---|
| sin normalizar (export viejo) | 0.446 | 0.339 P / 0.207 S | **no** (sale P) |
| con `annotate_batch_pre` | **0.924** | 0.000 P / **0.377 S** | **sí** |

10 estaciones de la red CX, ventana 05:14:50–05:15:35, `P/S 0.30`, un solo
proceso (`--store`) con el grafo normalizado: **9 P y 8 S**, con los P formando
un moveout limpio (05:14:56.5–05:15:10.0) y los S entre 05:15:10.9 y 05:15:25.0
(más un S tardío en PB23). La P falsa en el tiempo de la S desapareció; la
compuerta `MinSPSec` se mantiene como red para el par S-P espurio de 0.3–0.6 s
que sí aparecía en el grafo sin normalizar.

PhaseNet (3001, normalización en el grafo) corre por el mismo motor: sesión
**31 ms**, inferencia **1.5 ms/ventana**. Sobre PB15 emite un P en 05:15:22.7
(0.463) — es la base de integración; su calibración es la etapa siguiente.

**Estado del gate**: `pickml` está dado de alta en `startstop_unix.d`. Hoy publica en
`PICK_RING` **sólo la S** (`pickml_iquique.d`, `PublishPhases S`): la P de ese anillo la
aporta `pick_FP`, porque el ranking de P de los pesos chilenos es el que hay que arreglar
mientras su S ya funciona. `pickS` y `pickml_eqt` quedaron en `LEGACY_PICK_RING`, sin
consumidor. Falta la calibración con picks manuales y la comparación de 24 h antes de dar
los umbrales por buenos.

## Tests

| target | qué cubre | necesita |
|---|---|---|
| `make check` | buffer 3C (cobertura/gaps/solape/tasa/deslizamiento), ventanas, post-proceso, formato de pick, config, `.sta` | nada |
| `make check-offline` | **end-to-end** tank, store y SDS sintético con motor stub: 1 P y 1 S en los tiempos esperados, determinismo, tank≡store≡sds, día ausente y tasa no soportada | nada |
| `make check-engine` | **paridad numérica** de las curvas P/S contra PyTorch (≤1e-4), determinismo de ORT y, si la referencia tiene un pico P claro, que el pick caiga en él | ORT + `.onnx` + fixture |

## Estado

Implementado y verificado:

- **Grafo EQT slim**: `tools/slim_onnx.py` convierte los 8 386 nodos `Constant` en 76
  initializers únicos → 21 165 → 12 779 nodos y arranque **58–63 s → 25.6 s**, con
  paridad idéntica (3.58e-07).
- **Primer set de umbrales** (barrido de 3 eventos, 6 estaciones): EQT 0.20/0.20
  (`W3 0.20`) y PhaseNet 0.30/0.30. EQT: P 12/16, S 10/12; PhaseNet: P 15/16, S 5/12.
- **Modo live** (`--ring`): lee `SLINK_RING`, escribe picks en streaming a JSONL/SCNL y
  **no** publica en `PICK_RING`. Dos instancias (EQT y PhaseNet) corren en paralelo; medido
  0.8 % CPU con PhaseNet y ~2–3 % en régimen con EQT.
- **`pickml_fetch`**: evento de la BD → `event.tank` + `picks_ref.csv` + `stations.sta` +
  `meta.json`, con picadas **manuales**, store primero y SDS de fallback, y decimación
  entera (200/400 → 100 Hz) con anti-alias.
- **`tools/eval_picks.py`**: recall por fase contra las manuales; lo extra no penaliza.
  Primera medición (evento `csn2025boue`, 6 estaciones): EQT 5/6 P y 5/6 S; PhaseNet
  5/6 P y 4/6 S.
- **Dos modelos por el mismo motor**: `eqt.onnx` (6000) y `phasenet.onnx`
  (3001), ambos con la normalización **dentro** del grafo
  (`annotate_batch_pre`). Paridad C↔PyTorch: EQT 3.6e-07, PhaseNet 2.6e-08.
- **`WindowSamples` configurable**: la ventana es la nativa del modelo y se
  valida contra la dimensión declarada del grafo.
- **Salidas a archivo**: JSONL (`--out`), `TYPE_PICK_SCNL` en texto
  (`--picks-out`) y cabecera JSON de la corrida (`--meta`).
- Procesamiento offline desde **tank**, desde **store** y desde un **SDS** de
  SeisComP (`--sds <root>` + `--t0/--t1`, decimando 1x/2x/4x a `Rate`), con
  troceo de registros mayores que el buffer (un tank de 60 s con ventana de
  30 s no pierde el prefijo).

Pendiente, en orden:

1. **Calibración** de umbrales/ventanas por modelo con ≥10 eventos, usando
   `pickml_fetch` + `tools/eval_picks.py` (la primera medición ya existe: §Estado).
2. **`Follow 1`** (tail de store/tank) para live sin anillo.
3. ~~`PublishRing 1` + alta en `startstop_unix.d`~~ **hecho** (publica en `PICK_RING`;
   los pickers viejos en `LEGACY_PICK_RING`). Queda la comparación de 24 h.
4. **Arranque de EQT (~58 s)**: aceptable para un módulo de larga vida; el
   grafo tiene 8 269 nodos `Constant` que ORT recorre al cargar.

## Licencias

- Código de este módulo: el del repo.
- ONNX Runtime: MIT (se enlaza/carga, no se vendoriza).
- Pesos EQTransformer: MIT (upstream `smousavi05/EQTransformer`).
- SeisBench se usa **sólo** en `tools/export_onnx.py` (GPLv3): no entra en el
  binario ni en el runtime. Si se distribuyera el entorno de exportación,
  aplican sus términos.
