# live_eqt.d — EQTransformer en vivo desde SLINK_RING.
#
#   cd run_working_v8/params
#   source ../../ew8_unix.sh
#   pickml ../../ew_gui_tools/pickml/live_eqt.d --ring --stats
#
# Sólo escribe archivos: NO publica en PICK_RING. Las rutas de salida son
# relativas al cwd (run_working_v8/params), así que no atan el repo a una ruta
# absoluta y funcionan igual en un worktree.

MyModuleId      MOD_PICKML
InRing          SLINK_RING
StaFile         pickS.sta

Engine          onnx
ModelName       eqt
ModelFile       /mnt/d/resources/pickml/eqt.onnx
OrtLib          /mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0/lib/libonnxruntime.so
OrtThreads      1
OrtOptLevel     basic

WindowSamples   6000
Rate            100.0
RateTolHz       0.01
StepSec         10.0
MinPosSec       20.0
MaxPosSec       0.0
BlindSamples    500
Stacking        avg

PThreshold      0.20
SThreshold      0.20
DetThreshold    0.0
W0Prob          0.90
W1Prob          0.70
W2Prob          0.50
W3Prob          0.20

DedupSec        2.0
MinSPSec        2.0
MaxGapSec       1.0
LiveMaxAgeSec   120

PickOutFile     ../pickml_live/eqt.jsonl
PicksOutFile    ../pickml_live/eqt.picks.txt
RunMetaFile     ../pickml_live/eqt.meta.json

PReportChan     Z
SReportChan     N
