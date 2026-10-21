# live_phasenet.d — PhaseNet en vivo desde SLINK_RING.
#
#   cd run_working_v8/params
#   source ../../ew8_unix.sh
#   pickml ../../ew_gui_tools/pickml/live_phasenet.d --ring --stats
#
# Sólo escribe archivos: NO publica en PICK_RING.

MyModuleId      MOD_PICKML
InRing          SLINK_RING
StaFile         pickS.sta

Engine          onnx
ModelName       phasenet
ModelFile       /mnt/d/resources/pickml/phasenet.onnx
OrtLib          /mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0/lib/libonnxruntime.so
OrtThreads      1
OrtOptLevel     basic

WindowSamples   3001
Rate            100.0
RateTolHz       0.01
StepSec         5.0
MinPosSec       2.0
MaxPosSec       0.0
BlindSamples    100
Stacking        avg

PThreshold      0.30
SThreshold      0.30
DetThreshold    0.0
W0Prob          0.90
W1Prob          0.70
W2Prob          0.50
W3Prob          0.30

DedupSec        2.0
MinSPSec        2.0
MaxGapSec       1.0
LiveMaxAgeSec   120

PickOutFile     ../pickml_live/pn.jsonl
PicksOutFile    ../pickml_live/pn.picks.txt
RunMetaFile     ../pickml_live/pn.meta.json

PReportChan     Z
SReportChan     N
