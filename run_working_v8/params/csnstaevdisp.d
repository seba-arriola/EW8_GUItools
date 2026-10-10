# Configuracion de csnstaevdisp (Visor de Estaciones y Eventos)
MyModuleId      MOD_STAEVDISP
RingName        HYPO_RING
HeartBeatInt    30
LogFile         1
StaFile         estaciones_107.txt
QuakeFile       csnhypodbp_hist.txt
MapImageFile    world_map.jpg
StationSize     15

# Capa vectorial de líneas (OPCIONAL, repetible). Ruta relativa al cwd (=params/).
OverlayFile     ../../resources/overlays/subduccion.lines
OverlayFile     ../../resources/overlays/regiones.lines
OverlayFile     ../../resources/overlays/fallas.lines
