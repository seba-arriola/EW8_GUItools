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
# EventPickTolSec: tolerancia (s) para considerar que dos picadas son la MISMA
#   (misma estación + fase y |Δt| <= esto).
# EventMatchSharedPicks: segundo criterio de identidad, en OR con el geométrico.
#   Dos soluciones que comparten >= N picadas son el MISMO sismo aunque sus
#   hipocentros disten más de EventMatchDistKm. Es el criterio de SeisComP
#   `scevent` (>= 3). Evita que un ancla preliminar mala (pocas fases, gap
#   grande) cree un ID nuevo para el mismo sismo: medido el 2026-10-27 12:24,
#   dos hipótesis del mismo evento a 199 km con 8 picadas comunes generaban
#   dos orígenes de csnloc. 0 = off (solo geometría).
EventMatchTimeSec     60.0
EventMatchDistKm      100.0
EventMatchVelocityKmS 0.0
EventPickTolSec       0.1
EventMatchSharedPicks 3

# Gatillo de csnloc (solo asociación; los localizadores reciben después todas
# las picadas asociadas). 0 desactiva cada puerta.
#
# MinPPhases: P mínimas de un candidato. 0 = off; 1 = exige al menos una P.
#   Evita que un evento se gatille solo con S (telesismos, ruido).
MinPPhases            1
# MaxAssocDistKm: no usar estaciones más lejanas que esto para el gatillo.
#   0 = off; 1000 = red local/regional. No limita lo que recibe el locator.
MaxAssocDistKm        1000
# MaxGapDeg: no gatillar si el gap azimutal empeora esto. 0 = off.
#   Medido sobre 359 036 orígenes con gap en la BD (2026-10-27): el RMS mediano
#   NO sube al cruzar 300 (0.30 s en 280-360, igual que por debajo) y sí salta a
#   0.82 s a partir de 360, donde además el conteo se desploma. La red chilena
#   es LINEAL (de Arica a Magallanes), así que un sismo offshore deja todas las
#   estaciones a un lado y su gap es 300-350 por geometría, no por mala calidad:
#   7,7 % del corpus histórico tiene gap > 300 con nph>=6 y RMS ~0.30 s.
#   360 es el límite geométrico (semicírculo); más allá la solución es degenerada.
MaxGapDeg             360

# --- Emisión ---
# El evento se crea siempre que dispara el gatillo (para el dedup y el TTL),
# pero NO viaja al anillo hasta ser publicable. Así la primera y única solución
# de csnloc es la buena en vez de la primera detección pobre.
#
# MinEmitPhases: fases mínimas para publicar. 5 = 1 grado de libertad (t0, lat,
#   lon y profundidad son 4 incógnitas; con 4 fases el ajuste es exacto y el RMS
#   no informa). 0 = off.
MinEmitPhases         5
# EmitImproveMinPicks: si ya se publicó y llega un candidato del MISMO evento con
#   al menos N fases más, se emite una corrección con el mismo qid (v2). Es el
#   único caso en que csnloc versiona. 0 = nunca corregir.
EmitImproveMinPicks   3

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
