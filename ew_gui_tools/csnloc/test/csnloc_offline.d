# Config de csnloc para el test del MODO OFFLINE (test/test_offline.sh).
# Se ejecuta desde el directorio de csnloc, con las tablas iasp91.tbl/.hed
# presentes en el cwd. No toca anillos (el modo offline no los usa).
MyModuleId        MOD_CSNLOC
InRing            PICK_RING
OutRing           HYPO_RING
HeartBeatInt      30
LogFile           0
Debug             0
DumpHypo          0

StaFile           test/test_stations.txt
TauTable          iasp91

# Grilla de busqueda (archivo .grid, relativo al cwd = directorio de csnloc).
LocalGrid         test/csnloc_offline.grid

AssocWindowSec    120.0
RePickWindowSec   10.0
PickTTLSec        300.0
T0ToleranceSec    2.0
DBSCAN_Eps        40.0
DBSCAN_MinPts     1
BackProjThreshold 0.0
MaxEventsPerWindow 5
MinPhasesPerEvent 3
MaxRMS            2.0
PhaseWeightP      1.0
PhaseWeightS      0.8

NumThreads        1
RefineIterations  4
RefineNodeKm      5.0

AgencyID          CL
Author            csnloc
EventTTLSec       300.0
