#
# Archivo de configuracion para new_hypo_display (Visor de Hipocentros y Repicker)
#

MyModuleId      MOD_CSNHYPODBPQT # ID del modulo (port Qt, id propio 172)
InRing          HYPO_RING_REF    # Anillo de hipocentros refinados (hyp2000_ring + nlloc_ring)
InRingRaw       HYPO_RING        # 2.º anillo: soluciones crudas de csnloc (opcional)
OutRing         PICK_RING        # Anillo donde inyectaremos los repicks manuales (TYPE_PICK_SCNL)
InRingPick      PICK_RING        # Anillo de picks a LEER (fase P/S + origen A/M); default OutRing
ConfRing        HYPO_RING_REF2   # Anillo de CONFIRMACIONES: el ARC confirmado que hypodb guarda como origen propio
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
# Trazas por pantalla en la pestaña picking: 1, 2 o 3 (solo esos valores; el
# resto de canales de la estación queda debajo y se alcanza con la rueda).
tracesPerScreen 1
# Tabla UNICA de localizadores. El nombre es el basename del binario.
# <bin>/<cfg> sin '/' se resuelven contra $EW_HOME/$EW_VERSION/bin y $EW_PARAMS.
# live_usage: 0 = sólo manual/offline; 1 = además locator en vivo.
# time_recalc_live: segundos entre recálculos del locator vivo.
# N_retry: intentos sin picadas nuevas antes de dejar de localizar el evento.
# key     nombre_binario  archivo_config  live_usage  time_recalc_live  N_retry
locator   csnloc          csnloc.d        0           0                 0
locator   hyp2000_ring    hyp2000_ring.d  1           20                5
locator   nlloc_ring      nlloc_ring.d    0           0                 5
# Estado propio de la GUI (soluciones manuales de locate+confirm): relativo a
# $EW_PARAMS y dentro de SU carpeta — runtime, no versionado
# (run_working_v8/csnhypodbpqt/); el modulo la crea si falta.
ManualStateFile ../csnhypodbpqt/csnhypodbp_manual.events
LocatorTimeoutSec 600                      # timeout (s) por localizador offline (NLLoc es lento)
PrefetchNeighbors 2                        # estaciones vecinas a precargar

# ---------------------------------------------------------------------------
# Filtros de la pestaña `picking` y sus atajos de teclado
# ---------------------------------------------------------------------------
# El filtro es SOLO de la vista: `locate` manda las muestras crudas y el pick
# guardado es el instante del clic, asi que el filtro no desplaza el tiempo.
# FilterPhase zero  = forward+backward, fase neta CERO (default; el orden de la
#                     respuesta en amplitud se duplica: orden 2 ≡ Butterworth 4).
# FilterPhase causal = una pasada (como csntvp): retardo de grupo de decenas a
#                     cientos de ms cerca del corte. Solo para comparar.
FilterPhase     zero

# Teclas especiales.
FilterMenu      f            # abre la ventana Filters…
FilterRaw       0            # vuelve a la traza cruda (Raw)

# Filter [<nombre>] <HP|LP|BP> <tecla> <f1> [<f2>] <orden>
#   - tecla: un caracter; 'p' y 's' estan reservadas por el picado de fases.
#   - orden: 2 o 4. Con fase cero, 2 ya da la selectividad de un 4 causal.
#   - el nombre es opcional y solo se usa para el HUD y la ventana Filters.
Filter HP  r 1.0   2         # high-pass desde 1.0 Hz
Filter LP  k 0.1   2         # low-pass  hasta 0.1 Hz
Filter BP  d 0.001 12 4      # band-pass 0.001-12 Hz

# Filtros RECOMENDADOS por distancia (cada uno con SU tecla: el analista elige;
# no hay seleccion automatica). Evidencia y bandas: docs/FILTERS.md.
#   P local (<200 km):        HP 1 Hz (NMSOP IS 11.4) + contenido 1-20 Hz
#   P regional (200-1000 km): default operativo SeisComP BW(4,0.7,2)
#   P telesismo (>1000 km):   detector 0.5-2 Hz (Murdock & Hutt 1983)
#   S local (<200 km):        S en la coda de P, mas baja en frecuencia
Filter P_local       BP 1 1.0 20.0 2
Filter P_regional    BP 2 0.7 10.0 2
Filter P_teleseismic BP 3 0.5  2.0 2
Filter S_local       BP 4 0.5  8.0 2

# STA/LTA (solo visual: curva CF + marca de disparo; NO crea picks).
# StaLta <tecla> <sta_s> <lta_s> <on> <off> [<corr_s>]
# La marca se dibuja en t_disparo + corr (default -sta/2): es una ESTIMACION;
# el retardo real depende del SNR, por eso no se usa como pick.
StaLta          t 0.5 10.0 3.0 1.5

# Colores de las picadas (formato RRGGBB, con o sin '#'). La TRAZA se dibuja
# siempre negra: lo que cambia segun el origen es el color de la marca del pick.
#   pColor/sColor           -> marca MANUAL (y circulo del indicador de picado)
#   pColorAuto/sColorAuto   -> marca AUTOMATICA; por defecto el manual aclarado 40%
#   pColorTheo/sColorTheo   -> marca TEORICA;    por defecto el manual aclarado 70%
# Las claves Auto/Theo son opcionales: si no se declaran se calculan del manual
# EN CADA DIBUJO, asi que cambiar el manual (aqui o en el menu ☰ > Colours)
# recolorea tambien a automaticas y teoricas. Elegir una auto/teo en el menu la
# vuelve explicita (deja de heredar); para volver a heredar, comenta su clave.
# El hex actual queda en el log para copiarlo aqui.
#pColor         FF0000   # P manual (marca + circulo del indicador)
#pColorAuto     FF6666   # P automatica (default: pColor aclarado 40%)
#pColorTheo     FFB3B3   # P teorica    (default: pColor aclarado 70%)
#sColor         009900   # S manual
#sColorAuto     66C266   # S automatica (default: sColor aclarado 40%)
#sColorTheo     B3E0B3   # S teorica    (default: sColor aclarado 70%)

# Fuente de ondas: el store indexado (wstore_ingest -> StoreRoot) para lo
# reciente y el archivo SDS para lo antiguo (ver Sds* abajo).
StoreRoot       ../wstore
DbEnvFile       db.env

# Busqueda hipocentral (pestaña Eventos): tope de eventos a entregar.
SearchMaxEvents  1000

# Archivo SDS (SeisComP). Los eventos mas antiguos que SdsFallbackHours se
# consultan en SdsRoot (layout <root>/<YYYY>/<NET>/<STA>/<CHAN>.D/...) en vez de
# la retencion local del store. SdsFallbackHours 0 = SDS desactivado.
SdsEnabled        1
SdsRoot           /mnt/sds/archive
SdsFallbackHours  168

# Archivo de estaciones a graficar (Podemos usar el mismo de csntvp)
StaFile         estaciones_107.txt         # Archivo de estaciones con formato: Estacion Canal Red Loc Escala
StateFile       csnloc.events             # Estado de eventos de csnloc (solo recuperacion al arrancar)
# Estaciones sin forma de onda disponible: 1 = ocultas al arrancar, 0 = visibles.
# Este valor solo fija el estado INICIAL de la sesion: el item "Hide stations
# without data" del menu lo alterna (con su casilla) y nunca reescribe el .d.
HideStationsWithoutData  1

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
# Zoom por defecto del panel de planta: factor sobre el auto-encuadre (1.0 = el
# que sale del bbox; 2.0 = el doble de cerca). Con cualquier valor, el mapa queda
# centrado en la solucion preferida, o en la elegida en Origins (la verde).
#MapZoom         1.0
# Reparto vertical planta/perfil: proporcion `arriba/abajo` (7/3 = 70% planta).
MapSplit        7/3
# Capas vectoriales de lineas (OPCIONAL, repetible; formato .lines, ver
# common/tools/OVERLAYS.md). Rutas ABSOLUTAS: los datos viven en el disco de datos
# (EW_RESOURCES_ROOT = /mnt/d/resources), NO dentro del repo.
OverlayFile     /mnt/d/resources/overlays/placas.lines
OverlayFile     /mnt/d/resources/overlays/subduccion.lines
OverlayFile     /mnt/d/resources/overlays/regiones.lines
OverlayFile     /mnt/d/resources/overlays/fallas.lines
#OverlayColor    subduccion CC3300         # <capa> <RRGGBB>; repetible
#OverlayWidth    1.2                       # ancho global (px); 0 = el del archivo
#OverlayHide     fallas                    # <capa> que no se dibuja; repetible

# --- Magnitudes (sub-pestana `magnitudes`) ---
# El .d de csnmags que la GUI lee para dibujar: coeficientes Wood-Anderson,
# bandas de Mb/Ms y las ventanas de ML/Mwp/Mb/Ms. No se duplican valores: la GUI
# dibuja y csnmags mide con el MISMO archivo.
MagConfigFile   csnmags.d
# Panel `Mww` (inversión de fase W, on-demand): el mismo .d que el CLI csnwphase y
# el directorio de sus corridas: relativo a $EW_PARAMS, dentro de SU carpeta
# (runtime, no versionado: run_working_v8/csnhypodbpqt/mww). Se crea solo, con padres.
MwwConfigFile   csnwphase.d
MwwWorkDir      ../csnhypodbpqt/mww
# Ventana temporal pedida por solucion (s). Es larga a proposito: Ms mide su
# ventana (P + Ms_Window) y la sub-pestana `magnitudes` necesita el dato ya
# descargado para no re-pedirlo al cambiar de magnitud. Tope: 870 s (el buffer de
# la traza son 15 min).
#MagFetchSeconds 700

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
ProfileDataDir  /mnt/d/resources/profiles
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
