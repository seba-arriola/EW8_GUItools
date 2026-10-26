# Plan de entrenamiento de EQT — referencia técnica

Lo que hay que saber para entrenar un EQTransformer y servirlo con `pickml`: contrato de
entrada/salida, formato de datos, etiquetas, fine-tuning y export a ONNX.

> **Estado**: todavía **no se entrena**. El próximo paso es cerrar el filtro de la base
> (`docs/DL-dataset-autores.md`); el plan de despliegue está en `docs/DL_to-do.md` §4.
> Este documento es el "cómo", verificado contra el código de `ew_gui_tools/pickml/`.

## 1. El contrato que impone el runtime

Nada de esto es negociable: es lo que `pickml` espera del grafo.

| | |
|---|---|
| entrada | `waveform` float32 **`[batch, 3, 6000]`**, canales **Z, N, E**, 100 Hz |
| normalización | **dentro del grafo** (`annotate_batch_pre`), no en el llamador |
| salida | `psd` float32 **`[batch, 6000, 3]`** = `(P, S, detection)` |
| ventana | 6000 muestras = **60 s exactos** a 100 Hz (la nativa de EQT) |

- El motor C acepta cualquier forma cuyo producto sea `nwin*nsamp*3` y lee los canales
  por **índice fijo**: `0=P`, `1=S`, `2=det` (`pickml_engine_onnx.c:305-317`).
  ⇒ **Una red P-only de 2 salidas no se puede servir sin tocar C** y sin regenerar las
  referencias de paridad. Por eso se entrena **multi-tarea P + S + detección**, siempre.
- `WindowSamples` del `.d` debe ser 6000: el motor compara con la forma del grafo y
  aborta al arrancar si no coincide (fallar temprano es a propósito).
- El `.d` manda `Rate 100.0`; una ventana de otra tasa se descarta.

## 2. Qué es EQT en SeisBench

```python
import seisbench.models as sbm
m = sbm.EQTransformer(in_channels=3, in_samples=6000, classes=2, phases="PS")
# salidas = 1 + classes = 3  (pick por clase + detección)
```

- Paper (Mousavi et al. 2020): pérdida **BCE por rama** con
  `loss_weights = [0.2, 0.3, 0.5]` en orden **(detección, P, S)**; ventana de 60 s a
  100 Hz filtrada 1–45 Hz; entrenado en STEAD (~1.0 M sismos + 300 k ruidos), split
  85/5/10, ~89 h en 4×V100.
- El peso por rama es el mecanismo previsto para desbalancear o **apagar** una tarea
  (`S = 0` ⇒ entrenamiento sólo P).
- **SeisBench no trae loss ni freeze**: da el modelo y los generadores de datos; el
  loop de PyTorch (optimizador, loss, `requires_grad`) lo escribes tú.
- API útil: `from_pretrained(name)`, `load(path)`, `save(path)`, `list_pretrained()`.

## 3. Los datos

### 3.1 Formato

`seisbench.WaveformDataset` = una carpeta con **`metadata.csv` + `waveforms.hdf5`**.

- `metadata.csv`: una fila por traza. A nivel de clase no exige columnas; las exige el
  *labeller* que se use (`label_columns`).
- Mínimas para entrenar EQT:
  `trace_name` (clave al HDF5), `trace_sampling_rate_hz`,
  `trace_p_arrival_sample`, `trace_s_arrival_sample`.
- Útiles además: `station_network_code`, `station_code`, `trace_channel_code`,
  `source_magnitude`, `source_depth_km`, `split`.
- **Cada fila es una ventana de 60 s con las 3 componentes** (6000 × 3). En float32 son
  **72 kB** por ventana: 100 000 ventanas ≈ **7 GB**.

### 3.2 Reglas de armado

- Las tres componentes de la **misma estación**, a 100 Hz, **sin huecos** dentro de la
  ventana (`MaxGapSec`): si falta una, la ventana se descarta (no se rellena).
- La etiqueta es una **muestra**, no un tiempo:
  `sample = round((t_pick − t_inicio_ventana) · 100)`.
- El labeller probabilístico convierte el arribo en una gaussiana; EQT usa **±20
  muestras (±0.2 s)** para P y S, y para detección una **caja `P → S + 1.4·(S−P)`**
  (si no hay S, `fixed_window` o una caja corta alrededor de la P).
- Aumentos del paper (probabilidades): desplazamiento temporal, ruido gaussiano, gaps,
  *channel dropout* y ventanas aleatorias. No son opcionales con 10⁴ muestras: son la
  diferencia entre generalizar y memorizar.

### 3.3 Cómo se arma el corpus

El camino ya existe a medias en `tools/`:

1. `pickml_fetch` escribe por evento un `event.traces` 3C + `picks_ref.csv` + `stations.sta`
   (del store; el SDS como respaldo).
2. `tools/store_window.py` materializa una ventana 3C del store (grilla 100 Hz, exige
   cobertura completa).
3. **Falta** el paso explícito de volcar N ventanas a `metadata.csv` + `waveforms.hdf5`
   con máscaras de etiqueta (`tools/make_dataset.py`, pendiente en `DL_to-do.md`).

## 4. La trampa central: etiqueta ausente ≠ negativo

En `seisbench/generate/labeling.py`, el labeller hace `label_val[np.isnan] = 0` y luego
calcula ruido como `1 − suma(curvas)`.

⇒ **`trace_s_arrival_sample = NaN` no enmascara la rama S: la entrena como "aquí no hay
S"** (curva S en cero, ruido en uno).

En un catálogo donde la S se marca a veces (caso real: 1.17 M de S manuales pero no en
todas las estaciones de todos los eventos), eso enseña al modelo a **no picar S justo
donde sí hay**. Las dos salidas correctas:

1. **Máscara por rama** en el loss: la S sólo contribuye en las filas donde hay S
   etiquetada (BCE con peso por muestra, o `loss_weights` por batch).
2. **Etiquetar la S** donde exista; y donde no, no usarlo como negativo.

Lo mismo aplica al muestreo: una ventana de un evento con la P marcada y la S sin marcar
**no** es un negativo de S.

**Corolario para las métricas**: "no está en el catálogo" no significa "no existe". Medir
recall contra etiquetas incompletas mide *acuerdo*, no calidad — es exactamente lo que
pasó al medir contra las picadas automáticas de PhaseNetWC.

## 5. Fine-tuning (la vía recomendada)

No entrenar desde cero: 1.3 M de ventanas y 89 h en 4 GPU. Partir de lo que ya funciona:

1. Pesos iniciales: `EQTransformer.from_pretrained("original")` o el `.pt` de `iquique`.
2. **Etapa 1 — encoder congelado**: `requires_grad = False` en el encoder y `True` en las
   cabeceras, LR ~1e-4. Sólo cambia la calibración de salida; **no puede destruir la S**.
3. **Etapa 2 (opcional)**: descongelar con LR 10× menor, vigilando que el recall S no baje.
4. **Split por evento**, nunca por ventana: el mismo sismo en train y test infla todo.
5. Semilla fija y `meta.json` con pesos, épocas, LR, tamaño y métricas.
6. Órdenes de magnitud: fine-tune de transferencia converge con **10⁴–10⁵** ventanas
   (OBSTransformer: 36 k sismos + 25 k ruidos); desde cero hace falta ~10⁶.

## 6. Del `.pt` al ONNX

### 6.1 Export

`tools/export_onnx.py` (opciones reales, `export_onnx.py:284-299`):

```
--arch eqt|phasenet          --out <ruta.onnx>
--model <variante SeisBench> --opset 17
--window <muestras>          --channel-order auto|nps|psn
--fixture-trace/--fixture-t0/--fixture-dir/--fixture-prefix
```

- Obtiene los pesos con `cls.from_pretrained(args.model)` ⇒ **sólo variantes publicadas**.
  **No acepta un `.pt`/`.ckpt` local**: para exportar un modelo propio hay que agregar esa
  carga al script (pendiente).
- No hay flag de normalización: la inyecta en el grafo con `annotate_batch_pre` y la
  reporta en consola (`norm=peak` en los pesos chilenos; la v1 de `iquique` venía mal).
- Detecta el layout de la salida probando `(1,3,W)` y `(1,W,3)`.
- Escribe el `.onnx` **y** `<out>.json` con: `sha256`, `arch`, `opset`, versiones de
  seisbench/torch, `weights`, `channel_order`, formas de entrada
  (`waveform [batch,3,WINDOW]`) y salida (`psd [batch,WINDOW,3]`,
  `classes ["P","S","detection"]`), `normalization` y `max_abs_diff_vs_torch`.

### 6.2 Adelgazar

`tools/slim_onnx.py`: convierte cada `Constant` en initializer, deduplica las repetidas y
reescribe los inputs de los nodos. **No cambia la semántica**; baja el tamaño y sobre todo
el tiempo de creación de sesión (medido con `iquique`: **0.18 s tras slim**, contra los
~28 s que tarda la sesión del grafo `original` sin adelgazar).

### 6.3 Verificación (obligatoria)

| qué | cómo | criterio |
|---|---|---|
| paridad numérica ONNX vs torch | `test/test_engine.sh` con fixtures `win1.traces` + `win1.ref.{p,s}.f32` (float32 crudo) | `max|Δ| ≤ 1e-4` en P y en S |
| engine completo | `make -C ew_gui_tools/pickml check-engine` | SKIP si falta ORT/modelo/fixture |
| contrato del `.d` y del grafo | `pickml <d> --check-model` | abre la sesión, valida `[B,3,6000]` y las estaciones |

⚠ `--check-model` valida **formas, no semántica**: un grafo con los canales permutados
pasa el check y pica mal. Para eso están la paridad y la evaluación offline.

## 7. Cómo se sirve en el pipeline

`.d` mínimo:

```
Engine        onnx
ModelFile     /mnt/d/resources/pickml/<modelo>.onnx
OrtLib        /mnt/d/resources/pickml/onnxruntime-.../lib/libonnxruntime.so.1.31.0
WindowSamples 6000
Rate          100.0
StepSec       10.0
PThreshold    ...        SThreshold ...      W0Prob..W3Prob ...
PublishRing   1
```

- Lee ondas `TYPE_TRACEBUF2` de `SLINK_RING`, arma ventanas deslizantes de 6000 muestras y
  publica `TYPE_PICK_SCNL` (fase y origen `A`) en su `OutRing`.
- Post-proceso propio: umbral, picos, ventana de validez (`MinPosSec`), `DedupSec`,
  `MinSPSec`, peso por prob.
- **Un modelo por instancia**. Dos modelos en paralelo exigen **dos anillos**; si
  comparten anillo, `csnloc` asocia dos arribos por fase y estación.

## 8. Checklist de una corrida completa

1. Filtro de la BD → `metadata.csv` + `waveforms.hdf5` con máscaras (§3, pendiente).
2. Fine-tune con encoder congelado y split por evento (§5).
3. Export a ONNX (§6.1) — hoy el script sólo toma variantes de SeisBench.
4. `slim_onnx.py` (§6.2).
5. Paridad `≤1e-4` + `--check-model` (§6.3).
6. Offline contra el corpus de calibración (`--traces`/`--store`) y barrido de umbrales
   con `tools/sweep_post.py`; recall con `tools/eval_picks.py`.
7. Publicar primero en un anillo sin consumidor (`LEGACY_PICK_RING`); pasar a `PICK_RING`
   sólo después de comparar en vivo.

## 9. Estado

**No se ha entrenado nada todavía.** El próximo paso es cerrar el filtro de la base
(`docs/DL-dataset-autores.md`: lista blanca de autores, typos y lista negra); sin eso, el
dataset saldría con autores mezclados y pérdida del 7 % de las picadas por diferencias de
escritura. Después de eso, `make_dataset.py` y recién ahí el fine-tune.

---

### Referencias

- EQT (Mousavi et al. 2020, *Geophysical Journal International*): pérdida multi-tarea,
  etiquetas, aumentos. `https://doi.org/10.1093/gji/ggaa057`
- SeisBench: `https://seisbench.readthedocs.io` — `generate/labeling.py` (comportamiento
  del labeller con NaN) y `models/eqtransformer.py` (firma de la clase).
- Export ONNX de EQT en este repo: `ew_gui_tools/pickml/tools/export_onnx.py`,
  `slim_onnx.py`, `test/test_engine.sh`.
- Contrato del motor: `ew_gui_tools/pickml/pickml_engine_onnx.c` (`:305-317`).
- Corpus y calibración: `ew_gui_tools/pickml/tools/{calib_run.sh,sweep_post.py,eval_picks.py,store_window.py}`.
