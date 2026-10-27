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

# --- Identidad de evento y gatillo ---
# El ancla es la PRIMERA solución del evento (t0/lat/lon) y NO se mueve con
# las detecciones posteriores. Si un candidato cae dentro de esta ventana se
# considera el MISMO evento: no se emite otra solución ni se versiona. Si cae
# fuera, es un evento nuevo (aunque comparta alguna picada: se registra
# `overlap` en el log para diagnosticar el gatillo).
#
# EventMatchTimeSec: ventana temporal base (s) respecto del ancla.
# EventMatchDistKm:  distancia máxima (km) entre candidato y ancla.
# EventMatchVelocityKmS: 0 = ventana temporal fija. >0 = la ventana crece con
#   la distancia: tmax = EventMatchTimeSec + d[km]/EventMatchVelocityKmS.
#   (Equivale a una ventana de tiempo de viaje; 6.0 km/s es una Vp típica.)
# EventPickTolSec: tolerancia (s) para considerar que una picada del candidato
#   es la misma picada de un evento ya emitido (solo diagnóstico de overlap).
EventMatchTimeSec     60.0
EventMatchDistKm      100.0
EventMatchVelocityKmS 0.0
EventPickTolSec       0.1

# Gatillo de csnloc (solo asociación; los localizadores reciben después todas
# las picadas asociadas). 0 desactiva cada puerta.
#
# MinPPhases: P mínimas de un candidato. 0 = off; 1 = exige al menos una P.
#   Evita que un evento se gatille solo con S (telesismos, ruido).
MinPPhases            1
# MaxAssocDistKm: no usar estaciones más lejanas que esto para el gatillo.
#   0 = off; 1000 = red local/regional. No limita lo que recibe el locator.
MaxAssocDistKm        1000
# MaxGapDeg: no gatillar si el gap azimutal empeora esto. 0 = off; 300 = red
#   razonablemente distribuida. Los eventos offshore pueden necesitar más.
MaxGapDeg             300

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
                                 # hilos: 1->152s, 4->86s, 8->71s, 16->65s. El
                                 # techo es 2.34x (RefineHypo y DBSCAN son
                                 # seriales), asi que 8 es el punto dulce.
RefineIterations  3
RefineNodeKm      5.0
RefineDepthKm     10.0             # semiancho de la caja de refinamiento en z (km); 0 = auto (2*RefineNodeKm)
# FixedDepthKm    -1.0             # profundidad fija (km); <0 = busqueda libre. La GUI la pasa por corrida
# DepthPriorKm     0.0             # profundidad a priori (km); 0 = sin prior
# DepthPriorSigmaKm 0.0            # sigma del prior gaussiano (km)

# --- Salida ---
AgencyID          CL
Author            csnloc
EventTTLSec       300.0
