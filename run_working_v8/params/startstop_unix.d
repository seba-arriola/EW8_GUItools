

#                 Startstop Configuration File for Windows NT
#
#    <nRing> is the number of transport rings to create.
#    <Ring> specifies the name of a ring followed by it's size
#    in kilobytes, eg        Ring    WAVE_RING 1024
#    The maximum size of a ring is 1024 kilobytes.
#    Ring names are listed in file earthworm.h.
#
  nRing               8
  Ring   WAVE_RING   51200
  Ring    SCN_RING   500
  Ring   PICK_RING   256
  Ring   HYPO_RING   128
  Ring  SLINK_RING   51200
  Ring   DECI_RING   500
  Ring  ALARM_RING   256
  Ring  CONTROL_RING  512
  
 MyModuleId       MOD_STARTSTOP  # Module Id for this program
 HeartbeatInt     15             # Heartbeat interval in seconds
# the next is for windows only
# MyPriorityClass  Normal         # For startstop
# these next 2  are for unix
 MyClassName   OTHER             # For this program
 MyPriority     0             # For this program
 LogFile           1             # 1=write a log file to disk, 0=don't, 
				         # 2=write to module log but not stderr/stdout
 KillDelay        5             # number of seconds to wait on shutdown
                                 #  for a child process to self-terminate
                                 #  before killing it
 HardKillDelay    5             # wait this many more secs for procs to die before really killing them

 # statmgrDelay		2        # Uncomment to specify the number of seconds
					   # to wait after starting statmgr 
					   # default is 1 second

#
#    PriorityClass values:
#       Idle            4
#       Normal          9 forground, 7 background
#       High            13
#       RealTime        24
#
#    ThreadPriority values:
#       Lowest          PriorityClass - 2
#       BelowNormal     PriorityClass - 1
#       Normal          PriorityClass
#       AboveNormal     PriorityClass + 1
#       Highest         PriorityClass + 2
#       TimeCritical    31 if PriorityClass is RealTime; 15 otherwise
#       Idle            16 if PriorityClass is RealTime; 1 otherwise
#
#    Display can be either NewConsole, NoNewConsole, or MinimizedConsole.
#
#    If the command string required to start a process contains
#    embedded blanks, it must be enclosed in double-quotes.
#    Processes may be disabled by commenting them out.
#    To comment out a line, preceed the line by #.
#
#
 Process	  "slink2ew slink2ew_HHZ.d"
 Class/Priority    OTHER 0
#
 Process	  "pick_FP pick_FP.d"
 Class/Priority    OTHER 0
#
# Puentes Kafka/GLASS3 (LEGACY, deshabilitados): la asociacion+localizacion la
# hace csnloc localmente. Para volver al flujo GLASS3: descomentar estas dos
# entradas y COMENTAR la de csnloc (no pueden convivir: ambos escriben
# HYPO_RING). Requiere Kafka+broker arriba (kafka_monitor.sh, glass_monitor.sh)
# y las variables GLASS_*/KAFKA_* de ew8_unix.sh. Ver AGENTS.md seccion 8.
# Process	  "ew2glass ew2glass.d"
# Class/Priority    OTHER 0
#
# Process	  "glass2ew glass2ew.d"
# Class/Priority    OTHER 0
#
 Process	  "csnloc csnloc.d"
 Class/Priority    OTHER 0
#
 Process	  "wave_serverV wave_serverV.d"
 Class/Priority    OTHER 0
#
 Process	  "csnmags_toy csnmags_toy.d"
 Class/Priority    OTHER 0
#
 Process	  "csntvp csntvp.d"
 Class/Priority    OTHER 0
#
 Process	  "csnhypodbp csnhypodbp.d"
 Class/Priority    OTHER 0
#
 Process	  "csnrv csnrv.d"
 Class/Priority    OTHER 0
#
 Process	  "ew_controller ew_controller.d"
 Class/Priority    OTHER 0
#
 Process	  "csnstaevdisp csnstaevdisp.d"
 Class/Priority    OTHER 0
#
