# pickml.d — configuración de EJEMPLO.
#
# Este fichero vive en el módulo para no ensuciar run_working_v8/params/ (que
# sólo lleva configuración versionada y el invariante
# common/tests/test_params_stays_clean.sh vigila lo no versionado).
# Para activar el modo anillo, copiarlo a run_working_v8/params/pickml.d
# (las rutas relativas de abajo ya suponen ese cwd).

MyModuleId      MOD_PICKML
InRing          SLINK_RING
OutRing         PICK_RING
HeartbeatInt    30
LogFile         1
Debug           0

# Una línea por estación 3C: PickFlag Pin Sta Net Loc ChanE ChanN ChanZ
StaFile         pickml.sta

# ---- motor ----
# stub = sólo tests (no usa modelo). onnx = producción.
Engine          onnx
ModelName       eqt
# Recursos pesados fuera del repo (igual que GfDir/SdsRoot). Ajustar versiones.
ModelFile       /mnt/d/resources/pickml/eqt.onnx
OrtLib          /mnt/d/resources/pickml/onnxruntime-linux-x64-1.31.0/lib/libonnxruntime.so
OrtThreads      0            # 0 = el que decida ONNX Runtime
OrtOptLevel     basic        # none | basic | all (medido: no cambia el arranque)

# ---- geometría (EQTransformer: 60 s a 100 Hz) ----
WindowSamples   6000
Rate            100.0
RateTolHz       0.01

# Paso entre ventanas y recorte de bordes.
# LATENCIA: un arribo se publica al completarse la ventana que lo contiene, es
# decir ~WindowSec despues de su tiempo. Con 60 s son ~50-60 s. csnloc asocia
# con AssocWindowSec (45 s por defecto): para que estos picks entren hay que
# subirlo (~90 s) o se descartaran por viejos. Ver README.
StepSec         10.0
MinPosSec       20.0         # no se emiten picos antes de esta posicion
MaxPosSec       0.0          # 0 = WindowSec - 1/rate
BlindSamples    500          # 5 s de blinding en el borde
Stacking        avg          # avg | max (entre ventanas solapadas)

# ---- umbrales y calidad ----
PThreshold      0.20
SThreshold      0.20
DetThreshold    0.0          # 0 = puerta de deteccion apagada
W0Prob          0.90
W1Prob          0.70
W2Prob          0.50
W3Prob          0.20         # por debajo: el pick se descarta

DedupSec        2.0
MinSPSec        2.0          # descarta S a menos de esto de un P (0 = off)
MaxGapSec       1.0          # hueco máximo interpolable por ventana (1.7 % de 60 s)

# Canal con el que se reporta cada fase (debe existir en el .sta).
PReportChan     Z
SReportChan     N

# ---- live (--ring) ----
# Sólo escribe archivos (no publica en PICK_RING). Con --out/--picks-out/--meta
# se eligen las rutas; sin ellas, el JSONL va a stdout.
LiveMaxAgeSec   120          # descarta trazas con más atraso (0 = off)
