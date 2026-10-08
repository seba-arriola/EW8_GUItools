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
AssocWindowSec    45.0             # ventana temporal de picks
RePickWindowSec   10.0             # re-pick mismo SCNL reemplaza
PickTTLSec        300.0            # expiración de picks
T0ToleranceSec    2.0              # tolerancia del stacking de t0 (s)

# --- Deduplicación de eventos ---
# Claves que existen en el codigo pero NO estaban en este .d (corrian a default).
# A 100 km se fusionaban eventos cercanos: medido sobre 388 eventos, bajarlo a 50
# sube el <=25 km de 236 a 258, el <=50 de 356 a 368 y la mediana de 21.8 a 20.4 km
# (tank_tools/calib/RESULTS.md §8). A 200 km se PIERDEN eventos.
EventDedupSec     30.0
EventDedupKm      50.0
DBSCAN_Eps        60.0             # epsilon de clustering (km / km/s)
DBSCAN_MinPts     3
BackProjThreshold 0.0              # 0 = automático (50% del máximo)
MaxEventsPerWindow 5
MinPhasesPerEvent 3
MaxRMS            2.0
PhaseWeightP      1.0
PhaseWeightS      0.8

# --- Cómputo ---
NumThreads        8              # hilos de la retroproyeccion. Medido sobre 25
                                 # tanks: 1->152s, 4->86s, 8->71s, 16->65s. El
                                 # techo es 2.34x (RefineHypo y DBSCAN son
                                 # seriales), asi que 8 es el punto dulce.
RefineIterations  3
RefineNodeKm      5.0
RefineDepthKm     10.0             # semiancho de la caja de refinamiento en z (km); 0 = auto (2*RefineNodeKm)
# DepthPriorKm     0.0             # profundidad a priori (km); 0 = sin prior
# DepthPriorSigmaKm 0.0            # sigma del prior gaussiano (km)

# --- Salida ---
AgencyID          CL
Author            csnloc
EventTTLSec       300.0
