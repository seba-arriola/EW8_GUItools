# pickml_iquique.d — picker neuronal EQTransformer con pesos `iquique`, en vivo.
#
# Gemelo de pickml_eqt.d: misma arquitectura y misma ventana nativa (6000), otros
# pesos. `iquique` esta entrenado con la secuencia de replicas del Mw 8.1 de
# Iquique 2014, redes CX/DG/C1/7K a 100 Hz — o sea la MISMA red y la misma
# instrumentacion, que es justo lo que `original` no tiene (y por eso sus curvas
# se caen a ~0.0007 en esta red; ver pickml_eqt.d).
#
# ESTE es el picker vivo del pipeline, y hoy publica SOLO S (`PublishPhases S`)
# en PICK_RING: la P la aporta pick_FP en el MISMO anillo, porque `iquique`
# rankea mal la P (ver docs/DLdetalles.md §9.5: a presupuesto igualado, P 51 %
# vs 90 % de PhaseNetWC) mientras su S supera a la de PhaseNetWC. csnloc lee
# PICK_RING y asocia las dos fases.
#
# La P se sigue DETECTANDO y queda en los JSONL / PicksOutFile (sirve para
# medir), pero `pm_publish_decide` la descarta al publicar: no llega al anillo.
# Tambien se sigue usando la P interna para la compuerta S-vs-P (MinSPSec) y el
# dedup, igual que antes.
#
# Corre EN PARALELO con pickml_eqt.d, que publica en LEGACY_PICK_RING (sin
# consumidor). Regla: UN productor por fase en cada anillo que alguien lea; dos
# productores de la MISMA fase meterian dos arribos y csnloc asociaria el doble
# (lo vigila common/tests/test_pick_rings.sh).
#
#   ver este modelo:  csntvp / csnhypodbp  (leen PICK_RING)
#   ver el otro:      csntvp_2             (lee LEGACY_PICK_RING)
#
# OJO: los dos pickml escriben EW_LOG/pickml_<fecha>.log (el prefijo de logit es
# literal). Atribuir lineas por `motor_ms_ventana` o, mejor, por los JSONL:
# ../pickml/iquique.jsonl vs ../pickml/eqt.jsonl.
#
# Modelo: /mnt/d/resources/pickml/eqt_iquique.onnx (slim, 1.6 MB; metadatos en
# eqt_iquique.full.onnx.json). Regenerarlo si se quiere:
#   ~/.venvs/pickml/bin/python ew_gui_tools/pickml/tools/export_onnx.py \
#       --arch eqt --model iquique --out /mnt/d/resources/pickml/eqt_iquique.full.onnx
#   ~/.venvs/pickml/bin/python ew_gui_tools/pickml/tools/slim_onnx.py \
#       /mnt/d/resources/pickml/eqt_iquique.full.onnx /mnt/d/resources/pickml/eqt_iquique.onnx
# Controles del export: imprime `norm=peak` (v2; la v1 de estos pesos tenia la
# normalizacion mal) y `max|onnx - torch| = 6.855e-07`.
#
# Contrato del grafo (verificado en el .json): input `waveform` float32
# [batch,3,6000] ZNE con normalizacion DENTRO del grafo; output `psd` float32
# [batch,6000,3] = (P, S, detection). WindowSamples DEBE ser 6000.

MyModuleId    MOD_PICKML
InRing        SLINK_RING
OutRing       PICK_RING
HeartbeatInt  30
LogFile       1

# Estaciones con 3 componentes a 100 Hz (misma lista que pickS y pickml_eqt.d).
StaFile       pickS.sta

Engine        onnx
ModelName     iquique
ModelFile     /mnt/d/resources/pickml/eqt_iquique.onnx
OrtLib        /mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0/lib/libonnxruntime.so.1.31.0
OrtThreads    0
OrtOptLevel   basic

# Ventana nativa de EQT: 6000 muestras = 60.00 s exactos a 100 Hz.
WindowSamples 6000
Rate          100.0
RateTolHz     0.01
StepSec       10.0
MinPosSec     20.0
MaxPosSec     0.0
BlindSamples  500
Stacking      avg

# Umbrales MEDIDOS sobre el tramo 21:05-21:10 UTC del 2026-10-21 (2000 ventanas,
# curvas volcadas y barridas con tools/sweep_post.py). A diferencia de `original`,
# las curvas de iquique SI viven: maximo por ventana P media 0.034 (p99 0.372),
# S media 0.036 (p99 0.393), det media 0.427 (p99 0.854).
#
#   p_thr  s_thr  picks/h
#   0.10   0.10   777     <- mas sensible
#   0.20   0.20   412     <- ESTE (equilibrado)
#   0.20   0.30   177     <- conservador
#   0.30   0.30   153
#
# La tasa la manda sobre todo SThreshold: entre P 0.10 y 0.30 hay ~3 % de
# diferencia, y la S se lleva el 80-90 % de las picadas. Referencia: PhaseNetWC
# daba ~4000 picks/h en el mismo tramo, casi todos falsos.
PThreshold    0.1
SThreshold    0.20
# El canal de deteccion de iquique SI tiene senal (mediana 0.362, p99 0.854),
# pero el modulo todavia NO lo usa como compuerta (solo se parsea). Cuando se
# implemente, 0.30-0.50 es el rango a probar.
DetThreshold  0.0
W0Prob        0.90
W1Prob        0.70
W2Prob        0.50
W3Prob        0.20

DedupSec      2.0
MinSPSec      2.0
MaxGapSec     1.0

# Ventana de 60 s: el pick puede emitirse hasta ~60 s despues de su tiempo.
LiveMaxAgeSec 120

# Publicacion al anillo vivo. PublishPhases P|S|PS, PublishMaxWeight 0..3
# (peso 0 = mejor; se publica si weight <= valor). Rollback: PublishRing 0.
# SOLO S: la P de PICK_RING la pone pick_FP.d (ver cabecera). Para volver a
# publicar las dos fases, `PublishPhases PS` + parar pick_FP (si no, dos P).
PublishRing      1
PublishPhases    S
PublishMaxWeight 3

PickOutFile   ../pickml/iquique.jsonl
PicksOutFile  ../pickml/iquique.picks.txt
RunMetaFile   ../pickml/iquique.meta.json

PReportChan   Z
SReportChan   N
