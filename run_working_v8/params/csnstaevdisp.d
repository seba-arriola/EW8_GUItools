# Configuracion de csnstaevdisp (Visor de Estaciones y Eventos)
MyModuleId      MOD_STAEVDISP
RingName        HYPO_RING
HeartBeatInt    30
LogFile         1
StaFile         estaciones_107.txt
DbEnvFile       db.env
MapImageFile    world_map.jpg
StationSize     15

# Capa vectorial de líneas (OPCIONAL, repetible). Ruta relativa al cwd (=params/).
OverlayFile     ../../resources/overlays/placas.lines
OverlayFile     ../../resources/overlays/subduccion.lines
OverlayFile     ../../resources/overlays/regiones.lines
OverlayFile     ../../resources/overlays/fallas.lines
#OverlayColor    fallas 8B4513             # <capa> <RRGGBB>; repetible
#OverlayWidth    1.0                       # ancho global (px); 0 = el del archivo
#OverlayHide     fallas                    # <capa> que no se dibuja; repetible
