#
# Archivo de configuracion para csntvpQT (visor de trazas en vivo + picking)
#
# Copia de csntvp.d con DOS diferencias declaradas (las gatea
# csntvpQT/tests/test_integration.sh):
#   1. MyModuleId propio (MOD_CSNTVPQT, id 175): el eco de una picada se
#      deduplica por (mod,inst,seq), asi que compartir id haria que este port
#      pisara los slots locales del GTK y viceversa.
#   2. zColor/nColor/eColor: color por componente (Z/N/E). Solo este port las
#      lee; el GTK ignora las claves desconocidas.
#

MyModuleId      MOD_CSNTVPQT     # ID del modulo (port Qt, id propio 175)
InRing          SLINK_RING       # Anillo de entrada (donde leemos las ondas TRACEBUF2)
OutRing         PICK_RING        # Anillo de salida (donde inyectamos picks y leemos los automaticos)
HeartBeatInt    30               # Segundos entre latidos para statmgr
LogFile         1                # 1 = log a disco y consola, 0 = sin log, 2 = solo disco

# Archivo de estaciones a graficar
StaFile         stations_to_view.sta       # Archivo de estaciones con formato: Estacion Canal Red Loc Escala

# ------------------------------------------------------------------
# Parametros opcionales (si se omiten se usan los valores por defecto)
# ------------------------------------------------------------------
# Colores (formato RRGGBB, con o sin '#')
#waveformsColor   000000
#backgroundColor  FFFFFF
#fontColor        FF0000
#separatorColor   D9D9D9
#pColor           FF0000    # picks P (automaticos y manuales)
#sColor           000000    # picks S (automaticos y manuales)

# Colores por componente (solo port Qt; sin declarar heredan waveformsColor)
zColor           000000    # vertical (HHZ/BHZ)
nColor           1F6FE5    # norte (HHN/BHN)
eColor           1B8A3A    # este  (HHE/BHE)

# Pantalla
#StationsPerScreen 12
#TimeWindow        6

# Ventana de reemplazo de picks (segundos); 0 desactiva
#PickReplaceWindow 15
