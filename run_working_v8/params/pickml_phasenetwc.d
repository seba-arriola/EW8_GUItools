# pickml_phasenetwc.d — picker neuronal PhaseNetWC (pesos `jma_wc`) sobre 3C.
#
# Escribe archivos Y publica los picks como TYPE_PICK_SCNL en PICK_RING
# (PublishRing 1): es el picker automatico del pipeline vivo. Los pickers
# viejos (pick_FP/pickS) quedaron publicando en LEGACY_PICK_RING.
# Config ajustada con el primer barrido sobre el store (45 eventos,
# 254 P + 55 S de referencia): recall P 91.7 % / S 92.7 % con ~400 picks/h.
# Ver docs/DLdetalles.md §11 y docs/DL_to-do.md.
#
#   cd run_working_v8/params
#   pickml pickml_phasenetwc.d --ring --stats
#
# Un evento historico se puede picar directo del SDS de SeisComP:
#   pickml pickml_phasenetwc.d --sds /mnt/sds/archive \
#       --t0 2026-10-20T21:45:00 --t1 2026-10-20T21:49:00 --jsonl --stats
#
# El directorio ../pickml/ (salidas) lo crea el módulo al arrancar si falta.
# El .onnx se genera con:
#   tools/export_onnx.py --arch phasenet --model jma_wc --out .../phasenetwc.onnx
# (el exportador detecta el orden de clases: jma_wc es PSN, no NPS)

MyModuleId    MOD_PICKML
InRing        SLINK_RING
OutRing       PICK_RING
HeartbeatInt  30
LogFile       1

# Estaciones con 3 componentes a 100 Hz (misma lista que pickS).
StaFile       pickS.sta

Engine        onnx
ModelName     phasenetwc
ModelFile     /mnt/d/resources/pickml/phasenetwc.slim.onnx
OrtLib        /mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0/lib/libonnxruntime.so.1.31.0
OrtThreads    0

# Ventana nativa de PhaseNetWC: 3001 muestras = 30.01 s a 100 Hz.
WindowSamples 3001
Rate          100.0
RateTolHz     0.01
StepSec       5.0
MinPosSec     2.0
MaxPosSec     0.0
BlindSamples  100

# Umbrales del primer ajuste. P mas alto que S: la curva P de WC es mas
# ruidosa y 0.40 no pierde ninguna referencia (0.50 si pierde).
PThreshold    0.40
SThreshold    0.30
DetThreshold  0.0
W0Prob        0.90
W1Prob        0.70
W2Prob        0.50
W3Prob        0.30

# Dedup 3 s: mismo recall que 1-2 s con ~2 % menos de picadas; sin dedup el
# volumen se triplica (los solapes de ventana emiten el mismo pico).
DedupSec      3.0
MinSPSec      2.0
MaxGapSec     1.0

# Trazas tardias de SeedLink: se aceptan hasta 120 s (sube proc_s, no CPU).
LiveMaxAgeSec 120

# Publicacion al anillo vivo. PublishPhases P|S|PS, PublishMaxWeight 0..3
# (peso 0 = mejor; se publica si weight <= valor). Rollback: PublishRing 0 +
# `./ew_monitor.sh restart pickml`.
PublishRing      1
PublishPhases    PS
PublishMaxWeight 0

PickOutFile   ../pickml/phasenetwc.jsonl
PicksOutFile  ../pickml/phasenetwc.picks.txt
RunMetaFile   ../pickml/phasenetwc.meta.json

PReportChan   Z
SReportChan   N
