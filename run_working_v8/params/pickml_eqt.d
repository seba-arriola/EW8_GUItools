# pickml_eqt.d — picker neuronal EQTransformer (pesos `original`) sobre 3C, en vivo.
#
# Gemelo de pickml_iquique.d: misma lista de estaciones y mismas salidas, otros
# pesos. Nace del A/B del exceso de picadas de PhaseNetWC
# (en la rama DLpickers, sobre la MISMA corrida viva de 9.3 h y 137 estaciones:
# 12 008 picks de PhaseNet vs 828 de EQT — ver docs/DLdetalles.md y
# docs/DLintegration.md).
#
# ESTADO MEDIDO (2026-10-21, tramo 21:05-21:10 UTC del store, 137 estaciones):
#   la paridad contra PyTorch es exacta (P 3.58e-07, S 1.79e-07) y el motor
#   procesa 1950 ventanas de 60 s sin un solo error, PERO en este tramo las
#   curvas de EQT estan casi muertas: maximo de P por ventana con media 0.0007 y
#   p99 0.001 (PhaseNetWC en el mismo tramo: media 0.153, p99 0.816), y solo
#   3/1950 ventanas llegan a 0.02. Con los umbrales de la otra rama (0.20/0.20)
#   el picker es MUDO: 0 picks en 5 min contra 332 de PhaseNetWC; con 0.02 emite
#   2 (24/h, solo los transitorios mas fuertes). OJO: sobre la ventana dorada
#   del fixture (test/fixtures/win1.traces) su curva P SI llega a 0.92, asi que
#   "casi no detecta" no esta probado: la comparacion honesta exige el corpus de
#   eventos con referencia (tools/eval_picks.py), que esta pendiente.
#
# CORRE EN PARALELO con pickml_iquique.d y cada uno tiene su anillo: este publica
# en LEGACY_PICK_RING (sin consumidor: no toca csnloc ni la BD) y el de iquique
# en PICK_RING (el pipeline vivo). Esa separacion es lo que evita que csnloc
# asocie dos arribos por fase/estacion; la vigila common/tests/test_pick_rings.sh.
#
#   los dos los arranca startstop:  Process "pickml pickml_iquique.d"
#                                   Process "pickml pickml_eqt.d"
#   ver este modelo:   csntvp_2               (lee LEGACY_PICK_RING)
#   ver el de iquique: csntvp / csnhypodbp    (leen PICK_RING)
#
# Validacion sin stack (no abre anillos):
#   pickml pickml_eqt.d --print-config
#   pickml pickml_eqt.d --check-model     # sesion ONNX + [B,3,6000] + estaciones
#   pickml pickml_eqt.d --store ../wstore --t0 ... --t1 ... --out /tmp/eqt.jsonl
#
# Modelo: /mnt/d/resources/pickml/eqt.onnx (slim; metadatos en eqt.onnx.json):
#   input  `waveform` float32 [batch,3,6000] ZNE, normalizacion DENTRO del grafo
#   output `psd`      float32 [batch,6000,3] = (P, S, detection)
# WindowSamples DEBE ser 6000 (ventana nativa de EQT: el motor la compara con el
# grafo al arrancar y aborta con exit 3 si no coincide). NO copiar el 3001 del
# hermano.
#
# Claves que el modulo actual todavia NO usa (estan por compatibilidad de
# formato; no creer que filtran algo):
#   DetThreshold  solo se parsea y se imprime (el canal 3 de EQT SI es la
#                 deteccion real del modelo, pero la compuerta no esta escrita)
#   BlindSamples  solo validacion/meta
#   Stacking      solo informativo

MyModuleId    MOD_PICKML
InRing        SLINK_RING
OutRing       LEGACY_PICK_RING
HeartbeatInt  30
LogFile       1

# Estaciones con 3 componentes a 100 Hz (misma lista que pickS y PhaseNetWC).
StaFile       pickS.sta

Engine        onnx
ModelName     eqt
ModelFile     /mnt/d/resources/pickml/eqt.onnx
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

# Umbrales 0.02, NO los 0.20 del barrido de la otra rama. Medido sobre 1950
# ventanas de 60 s de esta red: la curva P de EQT tiene maximo por ventana con
# media 0.0007 y p99 0.001, y solo 3/1950 ventanas llegan a 0.02 (PhaseNetWC:
# 1101/4296 por encima de 0.20). Con 0.20 este picker no emite NADA; con 0.02
# emite ~24 picks/h. El codo 0.20 sigue siendo valido como referencia de OTRO
# conjunto (docs/DLintegration.md) — aqui hay que bajarlo o no hay picadas.
PThreshold    0.02
SThreshold    0.02
DetThreshold  0.0
W0Prob        0.90
W1Prob        0.70
W2Prob        0.50
W3Prob        0.02

DedupSec      2.0
MinSPSec      2.0
MaxGapSec     1.0

# Ventana de 60 s: un pick puede emitirse hasta ~60 s despues de su tiempo (la
# referencia midio lag p50 ~34 s). El tope de atraso aceptado de SeedLink queda
# en 120 s, igual que el hermano.
LiveMaxAgeSec 120

# Publicacion al anillo vivo. PublishPhases P|S|PS, PublishMaxWeight 0..3
# (peso 0 = mejor; se publica si weight <= valor). Rollback: PublishRing 0.
PublishRing      1
PublishPhases    PS
PublishMaxWeight 3

PickOutFile   ../pickml/eqt.jsonl
PicksOutFile  ../pickml/eqt.picks.txt
RunMetaFile   ../pickml/eqt.meta.json

PReportChan   Z
SReportChan   N
