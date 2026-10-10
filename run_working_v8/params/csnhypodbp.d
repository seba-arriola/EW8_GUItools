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

# Archivo de estaciones a graficar (Podemos usar el mismo de csntvp)
StaFile         estaciones_107.txt         # Archivo de estaciones con formato: Estacion Canal Red Loc Escala
StateFile       csnloc.events             # Estado de eventos de csnloc (solo recuperacion al arrancar)
