MyModuleId        MOD_CSNLOC        # ID en earthworm.d
InRing            PICK_RING         # Anillo de picks (TYPE_PICK_SCNL)
OutRing           HYPO_RING         # Anillo de hipocentros (TYPE_HYP2000ARC)
HeartBeatInt      30
LogFile           1
Debug             0
DumpHypo          0                # 1 = volcar el HYP2000ARC crudo al log

# --- Metadata de estaciones ---
StaFile           estaciones_107.txt

# --- Modelo de tiempos de viaje ---
TauTable          iasp91

# --- Grilla equivalente a la configuracion anterior (un solo nivel) ---
RegionalGrid      grids/legacy_chile.grid
GridActivationMinPicks  3
GridActivationMarginDeg 1.0

# --- Asociación ---
AssocWindowSec    120.0
RePickWindowSec   10.0
PickTTLSec        300.0
T0ToleranceSec    2.0
DBSCAN_Eps        60.0
DBSCAN_MinPts     3
BackProjThreshold 0.0
MaxEventsPerWindow 5
MinPhasesPerEvent 3
MaxRMS            2.0
PhaseWeightP      1.0
PhaseWeightS      0.8

# --- Cómputo ---
NumThreads        4
RefineIterations  3
RefineNodeKm      5.0

# --- Salida ---
AgencyID          CL
Author            csnloc
EventTTLSec       300.0
