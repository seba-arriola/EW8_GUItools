#
# Archivo de configuracion para csntvp (Visor GTK y Picks Manuales)
#

MyModuleId      MOD_CSNTVP       # ID del modulo (asegurate que exista en earthworm.d)
InRing          SLINK_RING       # Anillo de entrada (donde leemos las ondas TRACEBUF2)
OutRing         PICK_RING        # Anillo de salida (donde inyectaremos picks y leemos los automaticos)
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

# Pantalla
#StationsPerScreen 12
#TimeWindow        6

# Ventana de reemplazo de picks (segundos); 0 desactiva
#PickReplaceWindow 15
