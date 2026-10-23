

#                 Startstop Configuration File for Windows NT
#
#    <nRing> is the number of transport rings to create.
#    <Ring> specifies the name of a ring followed by it's size
#    in kilobytes, eg        Ring    WAVE_RING 1024
#    The maximum size of a ring is 1024 kilobytes.
#    Ring names are listed in file earthworm.h.
#
  nRing               11
  Ring   WAVE_RING   51200
  Ring    SCN_RING   500
  Ring   PICK_RING   256
  Ring LEGACY_PICK_RING 256
  Ring   HYPO_RING   128
  Ring  HYPO_RING_REF 128
  Ring HYPO_RING_REF2 128
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
# Picker de onda S (STA/LTA+AIC+polarizacion 3C). Lee ondas de SLINK_RING y
# picks P de LEGACY_PICK_RING, y publica picks S en LEGACY_PICK_RING.
# Process	  "pickS pickS.d"
# Class/Priority    OTHER 0
#
# Pickers neuronales (ONNX), DOS en paralelo con anillos SEPARADOS: dos pickers
# publicando en el mismo anillo meten dos arribos por fase/estacion y csnloc
# asocia el doble.
#   pickml_iquique.d  -> PICK_RING         (pesos `iquique`, entrenados en Chile)
#   pickml_eqt.d      -> LEGACY_PICK_RING  (pesos `original`; sin consumidor)
# PhaseNetWC queda retirado: en esta red produce miles de P donde no hay evento
# (ver docs/DLdetalles.md §9.4). Para volver, descomentar su Process y dejarlo
# como unico publicador de PICK_RING (el de iquique tendria que bajar a LEGACY).
# Process	  "pickml pickml_phasenetwc.d"
# Class/Priority    OTHER 0
#
 Process	  "pickml pickml_iquique.d"
 Class/Priority    OTHER 0
#
 Process	  "csnloc csnloc.d"
 Class/Priority    OTHER 0
#
# Refinador HYPOINVERSE por anillo: lee HYPO_RING (ARC de csnloc) y escribe
# el ARC refinado en HYPO_RING_REF. NO usa la cadena sausage (eq*/pipe).
 Process	  "hyp2000_ring hyp2000_ring.d"
 Class/Priority    OTHER 0
#
# Refinador NonLinLoc por anillo: lee HYPO_RING y escribe en HYPO_RING_REF
# (el MISMO anillo que hyp2000_ring; csnhypodbp los distingue por modulo).
# Requiere ControlFile/TtimeRoot/OutRoot y grillas precalculadas (mk_nll_grids.py).
 Process	  "nlloc_ring nlloc_ring.d"
 Class/Priority    OTHER 0
#
 Process	  "wstore_ingest wavestore.d"
 Class/Priority    OTHER 0
#
 Process	  "csnmags csnmags.d"
 Class/Priority    OTHER 0
#
 Process	  "csnmags csnmags_raw.d"
 Class/Priority    OTHER 0
#
 Process	  "hypodb_ingest hypodb.d"
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
# Process	  "ew_controller ew_controller.d"
# Class/Priority    OTHER 0
#
 Process	  "csnstaevdisp csnstaevdisp.d"
 Class/Priority    OTHER 0
#
 Process	  "csnhypodbpQT csnhypodbpqt.d"
 Class/Priority    OTHER 0
#
# Panel de control Qt (convive con ew_controller; modid distinto).
 Process	  "ew_controllerQT ew_controller.d"
 Class/Priority    OTHER 0
#
