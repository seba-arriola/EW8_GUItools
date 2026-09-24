#Archivo de configuracion para CSNmags_toy

MyModuleId      MOD_CSNMAGS      # Module ID
RingName        HYPO_RING        # Anillo donde llegan los TYPE_HYP2000ARC y enviamos TYPE_MAGNITUDE
LogFile         1                # 1=escribir log a disco
HeartBeatInt    30               # Segundos entre latidos

#Configuracion del Wave Server (donde sacaremos los datos)

WsIP            127.0.0.1
WsPort          16022
WsTimeout       10000            # Milisegundos de timeout para WS

#Duracion de la ventana a solicitar (segundos despues del origen)

WindowDuration  60.0
