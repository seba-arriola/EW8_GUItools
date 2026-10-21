# phasenet.d — configuración de EJEMPLO para PhaseNet (SeisBench).
#
# Mismo binario que EQT: lo único que cambia es el grafo y su ventana nativa.
# PhaseNet se exporta con `tools/export_onnx.py --arch phasenet` y usa 3001
# muestras (30.01 s a 100 Hz). El motor valida la forma contra WindowSamples.
#
# Ventaja práctica: su CreateSession tarda ~31 ms (EQT, ~58 s) y la inferencia
# ~1.5 ms por ventana.

MyModuleId      MOD_PICKML
InRing          SLINK_RING
OutRing         PICK_RING
HeartbeatInt    30
LogFile         1
Debug           0

# Una línea por estación 3C: PickFlag Pin Sta Net Loc ChanE ChanN ChanZ
StaFile         pickml.sta

# ---- motor ----
Engine          onnx
ModelName       phasenet
ModelFile       /mnt/d/resources/pickml/phasenet.onnx
OrtLib          /mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0/lib/libonnxruntime.so
OrtThreads      0
OrtOptLevel     basic

# ---- geometría: ventana NATIVA de PhaseNet (3001 = 30.01 s a 100 Hz) ----
WindowSamples   3001
Rate            100.0
RateTolHz       0.01

# Paso y bordes. Con ventana de 30 s la latencia de emisión es ~10-30 s, que
# SÍ cabe en csnloc AssocWindowSec 45 (EQT necesita ~50-60 s y pide 90).
StepSec         5.0
MinPosSec       2.0
MaxPosSec       0.0
BlindSamples    100          # 1 s: debe caber en MinPosSec
Stacking        avg

# ---- umbrales y calidad ----
PThreshold      0.30
SThreshold      0.30
DetThreshold    0.0          # 0 = puerta apagada (PhaseNet: det = max(P,S))
W0Prob          0.90
W1Prob          0.70
W2Prob          0.50
W3Prob          0.30

DedupSec        2.0
MinSPSec        2.0
MaxGapSec       1.0

# ---- salidas (offline / calibración) ----
# Vacías = stdout / apagadas. La CLI --out/--picks-out/--meta las sobreescribe,
# que es lo cómodo para correr N eventos con la misma configuración.
# (kom no admite una clave sin valor: se dejan comentadas.)
# PickOutFile   /tmp/picks.jsonl
# PicksOutFile  /tmp/picks.txt
# RunMetaFile   /tmp/picks.meta.json

PReportChan     Z
SReportChan     N

# ---- live (--ring): sólo archivos ----
LiveMaxAgeSec   120
