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

Stream C1_AC01     "HHZ.D" 
Stream C1_AC04     "HHZ.D" 
Stream C1_AC05     "HHZ.D" 
Stream C1_AC06     "HHZ.D" 
Stream C1_AC07     "HHZ.D" 
Stream VC_ADAL     "HHZ.D" 
Stream C1_AF01     "HHZ.D"
Stream RI_AHML     "HHZ.D"
Stream C1_AP01     "HHZ.D" 
Stream C1_AP02     "HHZ.D"
Stream TC_AVAL     "HHZ.D" 
Stream C1_AY02     "HHZ.D" 
Stream C1_AY03     "HHZ.D" 
Stream C1_AY04     "HHZ.D"
Stream C1_AY05     "HHZ.D" 
Stream VC_AYSN     "HHZ.D" 
Stream TC_AZUF     "HHZ.D" 
Stream C1_BI02     "HHZ.D" 
Stream C1_BI04     "HHZ.D" 
Stream C1_BI05     "HHZ.D" 
Stream C1_BI06     "HHZ.D" 
Stream C1_BI07     "HHZ.D" 
Stream TC_BLOQ     "HHZ.D" 
Stream C1_BO01     "HHZ.D" 
Stream C1_BO02     "HHZ.D" 
Stream C1_BO03     "HHZ.D" 
Stream TC_CAQ      "HHZ.D" 
Stream TC_CARR     "HHZ.D" 
Stream TC_CAST     "HHZ.D" 
Stream VC_CHAC     "HHZ.D" 
Stream C1_CO01     "HHZ.D" 
Stream C1_CO02     "HHZ.D" 
Stream C1_CO03     "HHZ.D" 
Stream C1_CO04     "HHZ.D" 
Stream C1_CO05     "HHZ.D" 
Stream C1_CO06     "HHZ.D" 
Stream VC_CUEV     "HHZ.D" 
Stream RI_CYA      "HHZ.D" 
Stream TC_ELTO     "HHZ.D" 
Stream TC_ENQU     "HHZ.D" 
Stream AI_ESPZ   "02HHZ.D" 
Stream C_FAR1      "HHZ.D" 
Stream VC_FRIO     "HHZ.D" 
Stream VC_HUDS     "HHZ.D" 
Stream C1_IN43     "HHZ.D" 
Stream C1_IN44     "HHZ.D"
Stream C1_IN47     "HHZ.D" 
Stream C1_IN48     "HHZ.D" 
Stream C1_IN49     "HHZ.D" 
Stream AI_JUBA   "02HHZ.D" 
Stream C1_LC01     "HHZ.D" 
Stream C1_LC02     "HHZ.D" 
Stream IU_LCO    "10HHZ.D" 
Stream VC_LJUN     "HHZ.D" 
Stream C1_LL01     "HHZ.D" 
Stream C1_LL02     "HHZ.D" 
Stream C1_LL03     "HHZ.D" 
Stream C1_LL04     "HHZ.D" 
Stream C1_LL05     "HHZ.D" 
Stream C1_LL06     "HHZ.D" 
Stream C1_LL07     "HHZ.D" 
Stream C_LMEL      "HHZ.D" 
Stream VC_LOPS     "HHZ.D" 
Stream C1_LR04     "HHZ.D" 
Stream C1_LR05     "HHZ.D" 
Stream C1_MG01     "HHZ.D" 
Stream C1_MG02     "HHZ.D" 
Stream C1_MG03     "HHZ.D" 
Stream C1_MG04     "HHZ.D" 
Stream C1_MG05     "HHZ.D" 
Stream VC_MGDN     "HHZ.D" 
Stream C1_ML02     "HHZ.D" 
Stream CX_MNMCX    "HHZ.D" 
Stream C1_MT01     "HHZ.D" 
Stream C1_MT02     "HHZ.D" 
Stream C1_MT03     "HHZ.D" 
Stream C1_MT05     "HHZ.D" 
Stream C1_MT07     "HHZ.D" 
Stream C1_MT08     "HHZ.D" 
Stream C1_MT09     "HHZ.D" 
Stream C1_MT10     "HHZ.D" 
Stream C1_MT12     "HHZ.D"
Stream C1_MT13     "HHZ.D" 
Stream C1_MT14     "HHZ.D" 
Stream C1_MT15     "HHZ.D" 
Stream C1_MT16     "HHZ.D" 
Stream C1_MT18     "HHZ.D" 
Stream IU_OTAV   "10HHZ.D" 
Stream CX_PATCX    "HHZ.D" 
Stream IU_PAYG   "00HHZ.D" 
Stream CX_PB01     "HHZ.D" 
Stream CX_PB02     "HHZ.D" 
Stream CX_PB03     "HHZ.D"
Stream CX_PB06     "HHZ.D" 
Stream CX_PB07     "HHZ.D" 
Stream CX_PB08     "HHZ.D"
Stream CX_PB09     "HHZ.D"
Stream CX_PB10     "HHZ.D"
Stream CX_PB12     "HHZ.D"
Stream CX_PB14     "HHZ.D"
Stream CX_PB15     "HHZ.D"
Stream CX_PB16     "HHZ.D"
Stream CX_PB18     "HHZ.D" 
Stream CX_PB19     "HHZ.D"
Stream CX_PB20     "HHZ.D"
Stream CX_PB21     "HHZ.D"
Stream CX_PB22     "HHZ.D"
Stream CX_PB23     "HHZ.D"
Stream IU_PMSA   "10HHZ.D"
Stream CX_PSGCX    "HHZ.D"
Stream CX_PX06     "HHZ.D"
Stream TC_QUET     "HHZ.D"
Stream TC_QUIR     "HHZ.D" 
Stream C_ROC1      "HHZ.D" 
Stream IU_SAML   "00HHZ.D" 
Stream C1_TA01     "HHZ.D" 
Stream C1_TA02     "HHZ.D" 
Stream C1_TA03     "HHZ.D" 
Stream RI_TCA      "HHZ.D" 
Stream IU_TRQA   "00HHZ.D" 
Stream TC_TUPU     "HHZ.D" 
Stream C1_VA01     "HHZ.D" 
Stream C1_VA02     "HHZ.D" 
Stream C1_VA03     "HHZ.D" 
Stream C1_VA04     "HHZ.D" 
Stream C1_VA05     "HHZ.D" 
Stream C1_VA06     "HHZ.D" 
Stream RI_VCA      "HHZ.D" 
Stream TC_VICU     "HHZ.D" 
Stream RI_YJA      "HHZ.D" 
Stream VC_YLTN     "HHZ.D" 
# --- Estaciones adicionales (cobertura completa, migradas desde slink2ew_BHZ.d) ---
Stream C_GO01     "HHZ.D"
Stream C_GO02     "HHZ.D"
Stream C_GO03     "HHZ.D"
Stream C_GO04     "HHZ.D"
Stream C_GO05     "HHZ.D"
Stream C_GO06     "HHZ.D"
Stream C_GO08     "HHZ.D"
Stream C_GO09     "HHZ.D"
Stream C_GO10     "HHZ.D"
Stream C1_AC02    "HHZ.D"
Stream C1_AF02    "HHZ.D"
Stream C1_BO04    "HHZ.D"
Stream C1_LR03    "HHZ.D"
Stream C1_ML03    "HHZ.D"
Stream C1_MT04    "HHZ.D"
Stream C1_MT19    "HHZ.D"
Stream G_SPB      "00HHZ.D"
Stream GE_SALTA   "HHZ.D"
Stream GT_PLCA    "00HHZ.D"
Stream II_EFI     "00HHZ.D"
Stream II_EFI     "10HHZ.D"
Stream II_NNA     "00HHZ.D"
Stream II_NNA     "10HHZ.D"
Stream IU_CASY    "00HHZ.D"
Stream IU_CASY    "10HHZ.D"
Stream IU_RCBR    "10HHZ.D"
Stream IU_RCBR    "00HHZ.D"
Stream IU_SBA     "00HHZ.D"
Stream IU_SBA     "10HHZ.D"
Stream PE_GUA0    "HHZ.D"
Stream PE_HYO0    "HHZ.D"
Stream PE_QLK0    "HHZ.D"
Stream PE_TOQ0    "HHZ.D"
Stream PE_YCA0    "HHZ.D"
Stream RI_USHA    "HHZ.D"
Stream CX_PB04    "HHZ.D"
Stream CX_PB05    "HHZ.D"
Stream CX_PB11    "HHZ.D"
Stream CX_HMBCX   "HHZ.D"
Stream G_PEL      "HHZ.D"
Stream G_PEL      "00HHZ.D"
Stream G_COYC     "10HHZ.D"
Stream G_COYC     "HHZ.D"
Stream QT_ROC1    "HHZ.D"


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
