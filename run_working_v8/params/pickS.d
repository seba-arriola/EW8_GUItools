##############################################################################
#  pickS.d - Configuracion del picker automatico de ONDA S
#
#  Modulo    : MOD_PICKS (165)
#  Entrada   : SLINK_RING  (ondas TYPE_TRACEBUF2, canales HH?/BH?)
#  Guia P    : PICK_RING   (picks P de pick_FP)
#  Salida    : PICK_RING   (picks S TYPE_PICK_SCNL, 11o token de fase 'S')
#
#  Los valores de algoritmo son calibrables; los defaults son conservadores
#  para sismologia regional (banda 1-10 Hz).
##############################################################################

MyModuleId      MOD_PICKS
InRing          SLINK_RING
PickRing        PICK_RING
OutRing         PICK_RING
HeartbeatInt    30
LogFile         1
Debug           0

# Lista de estaciones 3C (PickFlag Pin Sta Net Loc ChanE ChanN ChanZ).
StaFile         pickS.sta

# --- Filtro pasa-banda (Butterworth en cascada) ---
FilterLowHz     1.0
FilterHighHz    10.0
FilterOrder     4

# --- Deteccion STA/LTA sobre la envolvente horizontal ---
StaLenSec       0.5
LtaLenSec       30.0
TriggerOn       5.0
TriggerOff      3.0
DeadTimeSec     10.0

# --- Refinamiento del onset ---
AicWinSec       3.0

# --- Buffer 3C por estacion (segundos) ---
BufferSec       60.0

# --- Guia por pick P: hybrid exige una P de la misma estacion ---
# Ventana S-P calibrada (barrido sobre 410 eventos): [2,35] s.
GuideMode       hybrid
GuideMinDtSec   2.0
GuideMaxDtSec   35.0

# --- Polarizacion 3C ---
PolWinSec       1.5

# --- Criterio de calidad -> weight 0-4 (0 = mejor) ---
MinSnr          4.0
MaxWeight       4
ReportChan      detected

# Umbrales de las metricas (entre W0 y W4 se interpola linealmente).
Q_Snr_W0        14.0
Q_Snr_W4        5.0
Q_StaLta_W0     14.0
Q_StaLta_W4     6.0
Q_Rect_W0       0.90
Q_Rect_W4       0.55
Q_IncidMinDeg   60.0
Q_IncidMaxDeg   120.0

# Compuerta dura de incidencia (grados desde la vertical; la S es ~horizontal,
# ~90). Max<=Min la deshabilita. Calibrada en [60,120].
GateIncidMinDeg 60.0
GateIncidMaxDeg 120.0

# Metricas opcionales (W0 <= 0 = deshabilitado; al habilitar entran al weight).
Q_Plan_W0       0.0
Q_Plan_W4       0.0
Q_Hv_W0         0.0
Q_Hv_W4         0.0

# Observabilidad: linea "pickS: METRICS ..." por deteccion (canal lateral,
# NO toca el pick ni el anillo). 1 = activo; pensado para calibracion.
MetricsLog      0
