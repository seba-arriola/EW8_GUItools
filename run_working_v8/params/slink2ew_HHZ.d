#
#                     Configuration File for slink2ew
#
MyModuleId       MOD_SLINK2EW
RingName         SLINK_RING       # Transport ring to write data to.

HeartBeatInterval     30         # Heartbeat interval, in seconds.
LogFile               1          # 1 -> Keep log, 0 -> no log file
                                 # 2 -> write to module log but not stderr/stdout
Verbosity      0		 # Set level of verbosity.

#SLhost   10.54.217.9               # Host address of the SeedLink server (producción)
SLhost   10.54.217.9               # Host address of the SeedLink server
SLport         18000             # Port number of the SeedLink server

StateFile                        # If this flag is specified (uncommented) a 
                                 # file with a list of sequence numbers is
                                 # written, during a clean module shutdown,
                                 # to the parameter directory with the name
                                 # "slink<mod id>.state". During module startup 
                                 # these sequence numbers are used to resume
                                 # data streams from the last received data.
                                 # Using this functionality is highly
                                 # recommended.

StateFileInt   100              # This controls the interval (in packets 
	                         # received) at which the state is saved in
                                 # the state file.  Default is 100 packets,
                                 # 0 to disable.

#NetworkTimeout 600              # Network timeout, after this many idle
                                 # seconds the connection will be reset.
                                 # Default is 600 seconds, 0 to disable.

#NetworkDelay   30               # Network re-connect delay in seconds.

#KeepAlive      0                # Send keepalive packets (when idle) at this
                                 # interval in seconds.  Default is 0 (disabled).

#ForceTraceBuf1 0                # On systems that support TRACEBUF2
                                 # messages this flag will force the module
                                 # to create TRACEBUF messages instead.
                                 # Most people will never need this.

# Selectors and Stream's.  If any Stream lines are specified the connection
# to the SeedLink server will be configured in multi-station mode using
# Selectors, if any, as defaults.  If no Stream lines are specified the
# connection will be configured in uni-station mode using Selectors, if any.

#Selectors      "BHZ.D"          # SeedLink selectors.  These selectors are used
                                 # for a uni-station mode connection.  If one
                                 # or more 'Stream' entries are given these are
                                 # used as default selectors for multi-station
                                 # mode data streams.  See description of
                                 # SeedLink selectors below.  Multiple selectors
                                 # must be enclosed in quotes.


# List each data stream (a network and station code pair) that you
# wish to request from the server with a "Stream" command.  If one or
# more Stream commands are given the connection will be configured in
# multi-station mode (multiple station data streams over a single
# network connection).  If no Stream commands are specified the
# connection will be configured in uni-station mode, optionally using
# any specified "Selectors".  A Stream command should be followed by a
# stream key, a network code followed by a station code separated by
# an underscore (i.e. IU_KONO).  SeedLink selectors for a specific
# stream may optionally be specified after the stream key.  Multiple
# selectors must be enclosed in quotes.  Any selectors specified with
# the Selectors command above are used as defaults when no selectors
# are specified for a given stream.

#Stream  GE_DSB   "BH?.D HH?.D"
#Stream  II_KONO  00BH?.D

# Some SeedLink servers support extended selection capability and
# allow wildcars (either '*' or '?') for both the network and station
# fields, for example to request all stations from the TA network:

#Stream  KM_KMI C1?.D

Stream C1_AC01     "HH?.D" 
Stream C1_AC04     "HH?.D" 
Stream C1_AC05     "HH?.D" 
Stream C1_AC06     "HH?.D" 
Stream C1_AC07     "HH?.D" 
Stream VC_ADAL     "HH?.D" 
Stream C1_AF01     "HH?.D"
Stream RI_AHML     "HH?.D"
Stream C1_AP01     "HH?.D" 
Stream C1_AP02     "HH?.D"
Stream TC_AVAL     "HH?.D" 
Stream C1_AY02     "HH?.D" 
Stream C1_AY03     "HH?.D" 
Stream C1_AY04     "HH?.D"
Stream C1_AY05     "HH?.D" 
Stream VC_AYSN     "HH?.D" 
Stream TC_AZUF     "HH?.D" 
Stream C1_BI02     "HH?.D" 
Stream C1_BI04     "HH?.D" 
Stream C1_BI05     "HH?.D" 
Stream C1_BI06     "HH?.D" 
Stream C1_BI07     "HH?.D" 
Stream TC_BLOQ     "HH?.D" 
Stream C1_BO01     "HH?.D" 
Stream C1_BO02     "HH?.D" 
Stream C1_BO03     "HH?.D" 
Stream TC_CAQ      "HH?.D" 
Stream TC_CARR     "HH?.D" 
Stream TC_CAST     "HH?.D" 
Stream VC_CHAC     "HH?.D" 
Stream C1_CO01     "HH?.D" 
Stream C1_CO02     "HH?.D" 
Stream C1_CO03     "HH?.D" 
Stream C1_CO04     "HH?.D" 
Stream C1_CO05     "HH?.D" 
Stream C1_CO06     "HH?.D" 
Stream VC_CUEV     "HH?.D" 
Stream RI_CYA      "HH?.D" 
Stream TC_ELTO     "HH?.D" 
Stream TC_ENQU     "HH?.D" 
Stream AI_ESPZ   "02HH?.D" 
Stream C_FAR1      "HH?.D" 
Stream VC_FRIO     "HH?.D" 
Stream VC_HUDS     "HH?.D" 
Stream C1_IN43     "HH?.D" 
Stream C1_IN44     "HH?.D"
Stream C1_IN47     "HH?.D" 
Stream C1_IN48     "HH?.D" 
Stream C1_IN49     "HH?.D" 
Stream AI_JUBA   "02HH?.D" 
Stream C1_LC01     "HH?.D" 
Stream C1_LC02     "HH?.D" 
Stream IU_LCO    "10HH?.D" 
Stream VC_LJUN     "HH?.D" 
Stream C1_LL01     "HH?.D" 
Stream C1_LL02     "HH?.D" 
Stream C1_LL03     "HH?.D" 
Stream C1_LL04     "HH?.D" 
Stream C1_LL05     "HH?.D" 
Stream C1_LL06     "HH?.D" 
Stream C1_LL07     "HH?.D" 
Stream C_LMEL      "HH?.D" 
Stream VC_LOPS     "HH?.D" 
Stream C1_LR04     "HH?.D" 
Stream C1_LR05     "HH?.D" 
Stream C1_MG01     "HH?.D" 
Stream C1_MG02     "HH?.D" 
Stream C1_MG03     "HH?.D" 
Stream C1_MG04     "HH?.D" 
Stream C1_MG05     "HH?.D" 
Stream VC_MGDN     "HH?.D" 
Stream C1_ML02     "HH?.D" 
Stream CX_MNMCX    "HH?.D" 
Stream C1_MT01     "HH?.D" 
Stream C1_MT02     "HH?.D" 
Stream C1_MT03     "HH?.D" 
Stream C1_MT05     "HH?.D" 
Stream C1_MT07     "HH?.D" 
Stream C1_MT08     "HH?.D" 
Stream C1_MT09     "HH?.D" 
Stream C1_MT10     "HH?.D" 
Stream C1_MT12     "HH?.D"
Stream C1_MT13     "HH?.D" 
Stream C1_MT14     "HH?.D" 
Stream C1_MT15     "HH?.D" 
Stream C1_MT16     "HH?.D" 
Stream C1_MT18     "HH?.D" 
Stream IU_OTAV   "10HH?.D" 
Stream CX_PATCX    "HH?.D" 
Stream IU_PAYG   "00HH?.D" 
Stream CX_PB01     "HH?.D" 
Stream CX_PB02     "HH?.D" 
Stream CX_PB03     "HH?.D"
Stream CX_PB06     "HH?.D" 
Stream CX_PB07     "HH?.D" 
Stream CX_PB08     "HH?.D"
Stream CX_PB09     "HH?.D"
Stream CX_PB10     "HH?.D"
Stream CX_PB12     "HH?.D"
Stream CX_PB14     "HH?.D"
Stream CX_PB15     "HH?.D"
Stream CX_PB16     "HH?.D"
Stream CX_PB18     "HH?.D" 
Stream CX_PB19     "HH?.D"
Stream CX_PB20     "HH?.D"
Stream CX_PB21     "HH?.D"
Stream CX_PB22     "HH?.D"
Stream CX_PB23     "HH?.D"
Stream IU_PMSA   "10HH?.D"
Stream CX_PSGCX    "HH?.D"
Stream CX_PX06     "HH?.D"
Stream TC_QUET     "HH?.D"
Stream TC_QUIR     "HH?.D" 
Stream C_ROC1      "HH?.D" 
Stream IU_SAML   "00HH?.D" 
Stream C1_TA01     "HH?.D" 
Stream C1_TA02     "HH?.D" 
Stream C1_TA03     "HH?.D" 
Stream RI_TCA      "HH?.D" 
Stream IU_TRQA   "00HH?.D" 
Stream TC_TUPU     "HH?.D" 
Stream C1_VA01     "HH?.D" 
Stream C1_VA02     "HH?.D" 
Stream C1_VA03     "HH?.D" 
Stream C1_VA04     "HH?.D" 
Stream C1_VA05     "HH?.D" 
Stream C1_VA06     "HH?.D" 
Stream RI_VCA      "HH?.D" 
Stream TC_VICU     "HH?.D" 
Stream RI_YJA      "HH?.D" 
Stream VC_YLTN     "HH?.D" 
# --- Estaciones adicionales (cobertura completa, migradas desde slink2ew_BHZ.d) ---
Stream C_GO01     "HH?.D"
Stream C_GO02     "HH?.D"
Stream C_GO03     "HH?.D"
Stream C_GO04     "HH?.D"
Stream C_GO05     "HH?.D"
Stream C_GO06     "HH?.D"
Stream C_GO08     "HH?.D"
Stream C_GO09     "HH?.D"
Stream C_GO10     "HH?.D"
Stream C1_AC02    "HH?.D"
Stream C1_AF02    "HH?.D"
Stream C1_BO04    "HH?.D"
Stream C1_LR03    "HH?.D"
Stream C1_ML03    "HH?.D"
Stream C1_MT04    "HH?.D"
Stream C1_MT19    "HH?.D"
Stream G_SPB      "00HH?.D"
Stream GE_SALTA   "HH?.D"
Stream GT_PLCA    "00HH?.D"
Stream II_EFI     "00HH?.D"
Stream II_EFI     "10HH?.D"
Stream II_NNA     "00HH?.D"
Stream II_NNA     "10HH?.D"
Stream IU_CASY    "00HH?.D"
Stream IU_CASY    "10HH?.D"
Stream IU_RCBR    "10HH?.D"
Stream IU_RCBR    "00HH?.D"
Stream IU_SBA     "00HH?.D"
Stream IU_SBA     "10HH?.D"
Stream PE_GUA0    "HH?.D"
Stream PE_HYO0    "HH?.D"
Stream PE_QLK0    "HH?.D"
Stream PE_TOQ0    "HH?.D"
Stream PE_YCA0    "HH?.D"
Stream RI_USHA    "HH?.D"
Stream CX_PB04    "HH?.D"
Stream CX_PB05    "HH?.D"
Stream CX_PB11    "HH?.D"
Stream CX_HMBCX   "HH?.D"
Stream G_PEL      "HH?.D"
Stream G_PEL      "00HH?.D"
Stream G_COYC     "10HH?.D"
Stream G_COYC     "HH?.D"
Stream QT_ROC1    "HH?.D"


#(notes regarding "selectors" from a SeedLink configuration file)
#
#   The "selectors" parameter tells to request packets that match given
#   selectors. This helps to reduce network traffic. A packet is sent to
#   client if it matches any positive selector (without leading "!") and
#   doesn't match any negative selectors (with "!"). General format of
#   selectors is LLSSS.T, where LL is location, SSS is channel, and T is
#   type (one of DECOTL for data, event, calibration, blockette, timing,
#   and log records). "LL", ".T", and "LLSSS." can be omitted, meaning
#   "any". It is also possible to use "?" in place of L and S.
#
#   Some examples:
#   BH?            - BHZ, BHN, BHE (all record types)
#   00BH?          - BHZ, BHN, BHE with location code '00' (all record types)
#   BH?.D          - BHZ, BHN, BHE (data records)
#   BH? !E         - BHZ, BHN, BHE (excluding detection records)
#   BH? E          - BHZ, BHN, BHE plus detection records of all channels
#   !LCQ !LEP      - exclude LCQ and LEP channels
#   !L !T          - exclude log and timing records
#
#
# For slink2ew no record types except data records will be written to
# the ring.  In other words, requesting any records in addition to
# data records is a waste.
