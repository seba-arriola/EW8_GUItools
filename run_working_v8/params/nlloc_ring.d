# nlloc_ring.d - modulo de anillo que refina hipocentros con NonLinLoc (NLLoc).
#
# Lee los ARC de csnloc (HYPO_RING) y escribe el ARC refinado en HYPO_RING_REF,
# el MISMO anillo que hyp2000_ring: csnhypodbp los distingue por modulo origen.
# No usa pipe ni la cadena sausage.
#
# Modelo de velocidades: grillas de TIEMPOS 3D por banda de latitud, derivadas
# del modelo 3D de B. Potin (ver ew_gui_tools/nlloc_ring/mk_nll_grids_3d.py).
# Para cada ARC se elige la banda cuyo bbox contiene el hipocentro de entrada;
# si cae fuera de todas se usa ModelFallback (1D GRID2D).

MyModuleId      MOD_NLLOC_RING
InRing          HYPO_RING
OutRing         HYPO_RING_REF
HeartBeatInt    30
LogFile         1
Debug           1
SourceCode      W

# Raiz de la salida (LOCFILES). El modulo reemplaza la linea LOCFILES.
OutRoot         /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/loc/ev

WorkDir         /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring

# Filtro grueso por bbox (reusa el .grid de csnloc).
GridFile        grids/chile_regional.grid

MinPhases       4
MaxRMS          2.0

# --- Bandas 3D ---
# ModelBand <nombre> <latmin> <latmax> <lonmin> <lonmax> <TtimeRoot> <ControlFile>
# Generadas por mk_nll_grids_3d.py (bbox de la cabecera del .mod de Potin).
ModelBand N18-26_1.5k -25.2192 -17.9833 -71.5456 -65.1549 /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time3d/N18-26_1.5k/time/N18-26_1.5k /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/ctrl/N18-26_1.5k.in
ModelBand N22-30_1.5k -30.0514 -21.8992 -72.1779 -65.2067 /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time3d/N22-30_1.5k/time/N22-30_1.5k /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/ctrl/N22-30_1.5k.in
ModelBand N26-34_1.5k -34.0516 -26.0611 -72.8184 -64.4555 /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time3d/N26-34_1.5k/time/N26-34_1.5k /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/ctrl/N26-34_1.5k.in
ModelBand N30-38_1.5k -38.0434 -29.9451 -74.4971 -66.8734 /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time3d/N30-38_1.5k/time/N30-38_1.5k /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/ctrl/N30-38_1.5k.in
ModelBand N34-42_1.5k -42.0155 -33.9172 -75.3937 -68.1264 /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time3d/N34-42_1.5k/time/N34-42_1.5k /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/ctrl/N34-42_1.5k.in
ModelBand N38-46_1.5k -45.8956 -38.0129 -75.8988 -70.2262 /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time3d/N38-46_1.5k/time/N38-46_1.5k /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/ctrl/N38-46_1.5k.in

# --- Red de seguridad: un solo modelo que cubre TODO Chile a 4 km ---
# Fusion de los 6 trozos 4k de Potin (ew_gui_tools/nlloc_ring/mk_mod_merge.py,
# pad de 3 grados por replicacion del borde) con 108 estaciones.
#
# POR QUE: las 6 bandas se solapan ~4 grados y un evento puede tener sus
# estaciones repartidas en mas de una, asi que la banda elegida le daba
# cobertura PARCIAL (33 % de los eventos) o NULA (1.3 %, y ~10 % de eventos sin
# refinar). Sin observaciones NLLoc no converge. select_model prefiere la banda
# que cubre MAS estaciones del evento, asi que esta (mas gruesa) solo entra
# cuando ninguna banda fina lo cubre entero: si dos empatan gana la primera de
# la lista, y por eso las finas van ARRIBA.
ModelBand CHILE_4k -48.9000 -14.9798 -78.9000 -61.4194 /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time3d/CHILE_4k/time/CHILE_4k /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/ctrl/CHILE_4k.in

# --- Fallback 1D (y modo sin bandas) ---
# Plantilla 1D GRID2D centrada en el norte de Chile. OJO: con GRID2D el volumen
# de busqueda (LOCGRID) debe caber en el alcance de distancias de la grilla de
# tiempos (NLLocLib.c IsGrid2DBigEnough); por eso el LOCGRID es pequeno y un
# evento lejos del origen puede quedar en el borde. El 3D de las bandas evita
# esto. Para desactivar el fallback, comentar la linea ModelFallback.
ControlFile     /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/nlloc.in
TtimeRoot       /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time/model
ModelFallback   /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/time/model /home/seba/Dev/EW8_GUItools/tmp/nlloc_ring/nlloc.in

# Rate-limit de NLLoc (es caro): recalcula como mucho cada NllIntervalSec por
# evento, siempre sobre la ultima version recibida. 0 = procesar cada version.
# Ademas, si la version guardada no cambio (salvo el campo `version`) no hay
# nada nuevo que calcular y se omite la corrida.
NllIntervalSec  60
NllTTLSec       1800
NllMaxPending   64

# Lado (km) del volumen de busqueda, RE-CENTRADO en el epicentro de entrada.
# NLLoc no acepta hipocentro semilla: sin esto la solucion se pega al borde de
# la banda y, con un LOCGRID gigante (el modelo fusionado de todo Chile), el
# octree NO converge. 0 = usar el LOCGRID de la plantilla tal cual.
LocGridKm       600.0
