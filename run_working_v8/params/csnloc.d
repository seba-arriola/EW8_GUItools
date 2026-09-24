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
# TauTable acepta "iasp91" o "iasp91.tbl"; deben existir <modelo>.tbl y .hed
# en $EW_PARAMS (o en el cwd del proceso).
TauTable          iasp91

# --- Grillas anidadas de back-projection ---
# Cada archivo .grid declara bbox, resolucion horizontal y capas de
# profundidad (posiblemente no homogeneas). Las grillas gruesas (global/
# regional) se buscan siempre; las locales se activan por cobertura de
# estaciones o por cercania a una nucleacion de nivel mas grueso.
# (Los archivos viven en run_working_v8/params/grids/.)
GlobalGrid        grids/global.grid
RegionalGrid      grids/chile_regional.grid
LocalGrid         grids/tarapaca.grid
LocalGrid         grids/antofagasta.grid
LocalGrid         grids/atacama.grid
LocalGrid         grids/coquimbo.grid
LocalGrid         grids/centro.grid
LocalGrid         grids/biobio.grid
LocalGrid         grids/araucania.grid
LocalGrid         grids/los_lagos.grid
LocalGrid         grids/aysen.grid
LocalGrid         grids/magallanes.grid
GridActivationMinPicks  3
GridActivationMarginDeg 1.0

# --- Asociación ---
AssocWindowSec    120.0            # ventana temporal de picks
RePickWindowSec   10.0             # re-pick mismo SCNL reemplaza
PickTTLSec        300.0            # expiración de picks
T0ToleranceSec    2.0              # tolerancia del stacking de t0 (s)
DBSCAN_Eps        60.0             # epsilon de clustering (km / km/s)
DBSCAN_MinPts     3
BackProjThreshold 0.0              # 0 = automático (50% del máximo)
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
