# Configuracion de csnstaevdisp (Visor de Estaciones y Eventos)
MyModuleId      MOD_STAEVDISP
RingName        HYPO_RING
HeartBeatInt    30
LogFile         1
StaFile         estaciones_107.txt
DbEnvFile       db.env
MapImageFile    world_map.jpg
StationSize     15

# Capa vectorial de líneas (OPCIONAL, repetible). Ruta ABSOLUTA: los datos viven en
# el disco de datos (EW_RESOURCES_ROOT = /mnt/d/resources), NO dentro del repo.
OverlayFile     /mnt/d/resources/overlays/placas.lines
OverlayFile     /mnt/d/resources/overlays/subduccion.lines
OverlayFile     /mnt/d/resources/overlays/regiones.lines
OverlayFile     /mnt/d/resources/overlays/fallas.lines
#OverlayColor    fallas 8B4513             # <capa> <RRGGBB>; repetible
#OverlayWidth    1.0                       # ancho global (px); 0 = el del archivo
#OverlayHide     fallas                    # <capa> que no se dibuja; repetible
