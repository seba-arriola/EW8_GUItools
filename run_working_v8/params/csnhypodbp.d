#
# Archivo de configuracion para new_hypo_display (Visor de Hipocentros y Repicker)
#

MyModuleId      MOD_CSNHYPODBP   # ID del modulo
InRing          HYPO_RING_REF    # Anillo de hipocentros refinados (hyp2000_ring + nlloc_ring)
InRingRaw       HYPO_RING        # 2.º anillo: soluciones crudas de csnloc (opcional)
OutRing         PICK_RING        # Anillo donde inyectaremos los repicks manuales (TYPE_PICK_SCNL)
InRingPick      PICK_RING        # Anillo de picks a LEER (fase P/S + origen A/M); default OutRing
HeartBeatInt    30               
LogFile         1                
TauModel        iasp91           # Modelo de tiempos de viaje (P teorica); tablas iasp91.* en params/
PickCaptureWindowSec  600        # Ventana (s) para asociar una picada capturada al evento

# Agrupamiento de soluciones del mismo evento (qid + firma de picadas).
GroupPickTolSec     0.1          # tolerancia de tiempo de la firma de picadas (s)
GroupMinMatchPicks  3            # minimo de picadas para fusionar por firma
GroupMaxSolutions   100          # tope de versiones por evento

# Pestaña Ondas: orden de canales/componentes y localizadores offline.
# El orden es canal externo x componente interno: HHZ, HHN, HHE, BHZ, ...
channelsOrder   HH BH HN LH
componentsOrder Z N E
# locator <nombre> <binario> <config.d> (sin '/' se resuelven contra
# $EW_HOME/$EW_VERSION/bin y $EW_PARAMS).
locator         csnloc       csnloc       csnloc.d
locator         hyp2000_ring hyp2000_ring hyp2000_ring.d
locator         nlloc_ring   nlloc_ring   nlloc_ring.d
ManualStateFile csnhypodbp_manual.events   # soluciones manuales propias (locate+confirm)
LocatorTimeoutSec 180                      # timeout (s) por localizador offline
PrefetchNeighbors 2                        # estaciones vecinas a precargar

# Colores de las fases (formato RRGGBB, con o sin '#'). Aplican a las marcas de
# fase sobre la traza y al circulo del indicador de picado. Tambien se pueden
# cambiar en caliente desde el menu ☰ > Colours (el hex queda en el log para
# copiarlo aqui).
#pColor         FF0000   # P: marca + circulo del indicador
#sColor         009900   # S: marcas + circulo del indicador

# Etiquetas de modulo origen para la columna "Mod" (id -> nombre).
ModuleLabel     162  csnloc
ModuleLabel     163  hyp2000
ModuleLabel     164  nlloc

# Configuracion del Wave Server para descargar ondas historicas al hacer clic
WsIP            127.0.0.1
WsPort          16022
WsTimeout       10000

# Backend de ondas: store primero, wave_serverV como fallback (auto)
WaveBackend     auto
StoreRoot       ../wstore
DbEnvFile       db.env

# Busqueda hipocentral (pestaña Eventos): tope de eventos a entregar.
SearchMaxEvents  1000

# Backend SDS (archivo SeisComP). Los eventos mas antiguos que SdsFallbackHours
# se consultan en SdsRoot (layout <root>/<YYYY>/<NET>/<STA>/<CHAN>.D/...) en vez
# de la retencion local (store/wave_serverV).
SdsEnabled        1
SdsRoot           /mnt/sds/archive
SdsFallbackHours  168

# Archivo de estaciones a graficar (Podemos usar el mismo de csntvp)
StaFile         estaciones_107.txt         # Archivo de estaciones con formato: Estacion Canal Red Loc Escala
StateFile       csnloc.events             # Estado de eventos de csnloc (solo recuperacion al arrancar)

# ---------------------------------------------------------------------------
# Columna derecha: mapa en planta (mismo contrato que csnrv) y panel de perfil
# ---------------------------------------------------------------------------
# Mapa base: imagen equirectangular de mundo completo (lon -180..180,
# lat -90..90, ancho = 2*alto, sin canal alpha). Valida con:
#   python3 ew_gui_tools/common/tools/check_map_image.py <imagen>
MapImageFile    world_map_con_chile.jpg
# Ancho minimo (grados) del auto-encuadre del mapa de planta: con un grupo de
# soluciones apretado, la vista no se acerca mas que esto (rueda para acercar).
MapMinSpanDeg   5.0
# Capas vectoriales de lineas (OPCIONAL, repetible; formato .lines, ver
# common/tools/OVERLAYS.md). Rutas relativas al cwd (=params/).
OverlayFile     ../../resources/overlays/placas.lines
OverlayFile     ../../resources/overlays/subduccion.lines
OverlayFile     ../../resources/overlays/regiones.lines
OverlayFile     ../../resources/overlays/fallas.lines
#OverlayColor    subduccion CC3300         # <capa> <RRGGBB>; repetible
#OverlayWidth    1.2                       # ancho global (px); 0 = el del archivo
#OverlayHide     fallas                    # <capa> que no se dibuja; repetible

# Perfiles de profundidad del panel inferior. Cada linea se declara con dos
# puntos sobre la recta del perfil; se usa SIEMPRE la mas cercana a la solucion
# preferida del evento. Los valores de abajo son el ajuste por minimos cuadrados
# de la traza de cada archivo (las trazas de los scripts del repo test_db NO son
# exactamente paralelas: la pendiente va de 0.2758 en P01 a 0.2444 en P013).
# Verificacion: csnhypodbp --check-profiles <csnhypodbp.d> (compara la traza de
# cada archivo contra su recta declarada).
# Los datos de cada perfil salen de ProfileDataDir:
#   slab<name>.tmp  (columna 3 = lon, columna 7 = profundidad del slab en km)
#   topo<name>.tmp  (columna 3 = lon, columna 7 = elevacion en m)
# Alternativa: ProfileFile <archivo> declara las lineas en un archivo aparte
# (misma sintaxis "Profile ...") y reemplaza a las claves Profile.
ProfileDataDir  ../../resources/profiles
ProfileCols     3 7                       # columnas 1-based (lon, valor)
ProfileDepthMinKm 200                     # fondo del panel (margen +30 km)
Profile P01  -72 -20.011086  -66 -18.356226
Profile P02  -72 -22.012407  -66 -20.369304
Profile P03  -72 -24.019660  -66 -22.368785
Profile P04  -72 -25.021084  -66 -23.375491
Profile P05  -72 -27.030095  -66 -25.402375
Profile P06  -72 -29.037066  -66 -27.423944
Profile P07  -72 -31.055779  -66 -29.435740
Profile P08  -72 -33.066094  -66 -31.465996
Profile P09  -72 -35.094489  -66 -33.518214
Profile P010 -72 -37.131041  -66 -35.581772
Profile P011 -72 -39.151850  -66 -37.632046
Profile P012 -72 -41.174838  -66 -39.681200
Profile P013 -72 -43.237018  -66 -41.770738
