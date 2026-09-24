#
# Archivo de configuracion para new_hypo_display (Visor de Hipocentros y Repicker)
#

MyModuleId      MOD_CSNHYPODBP   # ID del modulo
InRing          HYPO_RING        # Anillo donde Glass3 envia TYPE_HYP2000ARC
OutRing         PICK_RING        # Anillo donde inyectaremos los repicks manuales (TYPE_PICK_SCNL)
HeartBeatInt    30               
LogFile         1                

# Configuracion del Wave Server para descargar ondas historicas al hacer clic
WsIP            127.0.0.1
WsPort          16022
WsTimeout       10000

# Archivo de estaciones a graficar (Podemos usar el mismo de csntvp)
StaFile         estaciones_107.txt         # Archivo de estaciones con formato: Estacion Canal Red Loc Escala
HistoryFile     csnhypodbp_hist.txt        # Archivo donde se guardan los últimos sismos
