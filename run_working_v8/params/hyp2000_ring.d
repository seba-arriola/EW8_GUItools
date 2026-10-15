# hyp2000_ring.d - modulo de anillo que refina hipocentros con HYPOINVERSE.
#
# Lee los ARC de csnloc (HYPO_RING) y escribe el ARC refinado en HYPO_RING_REF.
# No usa pipe ni la cadena sausage.

MyModuleId      MOD_HYP2000_RING
InRing          HYPO_RING
OutRing         HYPO_RING_REF
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

# Filtros de calidad (0 = sin filtro).
MinPhases       4
MaxRMS          2.0
