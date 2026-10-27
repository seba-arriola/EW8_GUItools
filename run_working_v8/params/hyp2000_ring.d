# hyp2000_ring.d - modulo de anillo que refina hipocentros con HYPOINVERSE.
#
# Lee los ARC de csnloc (HYPO_RING) y escribe el ARC refinado en HYPO_RING_REF.
# No usa pipe ni la cadena sausage.

MyModuleId      MOD_HYP2000_RING
InRing          HYPO_RING
InPickRing      PICK_RING
OutRing         HYPO_RING_REF
# --- Locator vivo (live_usage=1 en csnhypodbpqt.d) ---
# Asociacion por tiempo de viaje: para cada P/S se predice t0 + tt(seed,sta,fase)
# y se acepta si |t_pick - t_pred| <= residual. La ventana crece con la
# distancia y la profundidad; NO es una ventana fija alrededor de t0.
#
# LivePickLookbackSec: maximo (t_pick - t0) en segundos que se mira hacia
#   adelante. Debe cubrir la S de la estacion mas lejana; 300 s cubre ~2000 km.
#   El buffer de picks se purga con este valor + 60 s.
# LivePickResidualSecP/S: tolerancia del residual de asociacion (s). Es una
#   puerta de ASOCIACION, no de calidad: el refinador decide despues que fases
#   usa. P mas estricta que S (la S tiene mas dispersion).
LivePickLookbackSec  300.0
LivePickResidualSecP 3.0
LivePickResidualSecS 5.0
# Metadatos para el tiempo de viaje: estaciones (lat/lon) y modelo tau.
# StaFile se resuelve contra $EW_PARAMS; TauTable busca <EW_PARAMS>/<modelo>.tbl.
StaFile         estaciones_107.txt
TauTable        iasp91
HeartBeatInt    30
LogFile         1
Debug           1
SourceCode      W

# Fichero de arranque de HYPOINVERSE (.hyp): modelo + estaciones. Vive en params/
# (versionado) y se referencia relativo al cwd del modulo = su WorkDir (ver abajo).
CommandFile     ../params/hyp2000_ring.hyp

# Directorio de trabajo PROPIO del modulo (hypoinverse escribe arcIn/arcOut aqui):
# relativo a $EW_PARAMS, en run_working_v8/hyp2000_ring/ (runtime, no versionado).
# Los ficheros de entrada (.hyp/.sta/.crh) NO viven aqui: son ../params/... .
WorkDir         ../hyp2000_ring

# Region opcional (mismo formato .grid que csnloc): solo se refinan eventos
# dentro del bbox. Vacio = sin filtro.
GridFile        grids/chile_regional.grid
VelocityModel   CHILE_POTIN

# Filtros de calidad de la SALIDA del refinador (0 = sin filtro).
# Una solucion con rms=99.99 (centinela de HYPOINVERSE), nph < MinPhases o
# gap > MaxGapDeg NO se publica ni contamina el seed de la siguiente corrida;
# cuenta como intento fallido para N_retry.
MinPhases       4
MaxRMS          2.0
MaxGapDeg       300
# Profundidad fija (km); <0 = libre. La GUI la pasa por corrida.
# FixedDepthKm  -1.0
