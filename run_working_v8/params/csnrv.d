MyModuleId      MOD_CSNRV        # ID del modulo en Earthworm (debe estar en earthworm.d)
RingName        HYPO_RING_REF    # Anillo de hipocentros refinados + magnitudes (csnloc->hyp2000_ring->HYPO_RING_REF)
HeartBeatInt    30               # Intervalo de latidos (segundos)
LogFile         1                # 1 = Escribir log a disco, 0 = Solo consola
DbEnvFile       db.env           # Credenciales PostgreSQL (historial al arrancar)


MapImageFile    world_map_con_chile.jpg   # Mapa equirectangular Natural Earth 1:50m (10800x5400, 2:1)
InitialZoom     2.0                       # Clave OPCIONAL: zoom inicial y de reset (0.2-50.0). Default 8.0 (local); 2.0 = vista regional

# Capa vectorial de líneas (OPCIONAL, repetible). Ruta ABSOLUTA: los datos viven en
# el disco de datos (EW_RESOURCES_ROOT = /mnt/d/resources), NO dentro del repo.
OverlayFile     /mnt/d/resources/overlays/placas.lines
OverlayFile     /mnt/d/resources/overlays/subduccion.lines
OverlayFile     /mnt/d/resources/overlays/regiones.lines
OverlayFile     /mnt/d/resources/overlays/fallas.lines
#OverlayColor    fallas 8B4513             # <capa> <RRGGBB>; repetible
#OverlayWidth    1.0                       # ancho global (px); 0 = el del archivo
#OverlayHide     fallas                    # <capa> que no se dibuja; repetible
