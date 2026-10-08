#
# Archivo de configuracion para new_hypo_display (Visor de Hipocentros y Repicker)
#

MyModuleId      MOD_CSNHYPODBP   # ID del modulo
InRing          HYPO_RING_REF    # Anillo de hipocentros refinados (hyp2000_ring + nlloc_ring)
OutRing         PICK_RING        # Anillo donde inyectaremos los repicks manuales (TYPE_PICK_SCNL)
HeartBeatInt    30               
LogFile         1                

# Etiquetas de modulo origen para la columna "Mod" (id -> nombre).
ModuleLabel     162  csnloc
ModuleLabel     163  hyp2000
ModuleLabel     164  nlloc

# Configuracion del Wave Server para descargar ondas historicas al hacer clic
WsIP            127.0.0.1
WsPort          16022
WsTimeout       10000

# Archivo de estaciones a graficar (Podemos usar el mismo de csntvp)
StaFile         estaciones_107.txt         # Archivo de estaciones con formato: Estacion Canal Red Loc Escala
StateFile       csnloc.events             # Estado de eventos de csnloc (solo recuperacion al arrancar)
