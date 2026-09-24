#
#                     Configuration File for decimate
#
 MyModId         MOD_DECIMATE
 InRing            SLINK_RING    # Transport ring to find waveform data on,
 OutRing            DECI_RING    # Transport ring to write output to,
                                 # InRing and OutRing may be the same.
 HeartBeatInterval  30           # Heartbeat interval, in seconds,
 LogFile            1            # 1 -> Keep log file, 0 -> no log file
                                 # 2 -> log to file but not to stderr/stdout
#Debug		 		 # Write out debug messages (optional)

#Quiet				 # Turn off gap and overlap messages to stderr (optional)


# Specify the decimation rates as one or more integer values greater than 1, 
# all on one "DecimationRates" line, enclosed in quotes.
# These will be used in successive stages of decimation. The overall
# decimation rate is the product of all the stage decomation rates.
# The filters will be more efficient if several small decimation stages
# are used instead of one large one, and if largest rates are given first.
# Currently a maximum of 10 stages is enforced.
#--------------------------------------------------------------------------
 DecimationRates    "5"     # Decimation rates of each stage, in quotes!

 MinTraceBuf         10       # Minimum number of samples in output TRACE_BUF.

 MaxGap              3.0      # Maximum gap, in sample periods, allowed
                              # between trace data points.
                              # When exceeded, channel is restarted.

#TestMode                     # If you want Decimate to compute and log 
                              # its filter coefficients and then
                              # exit, specify "TestMode".

# Specify logo(s) of the messages to grab from the InRing.
# Up to 10 GetWavesFrom commands may be used.
# Must specify installation ID, module ID and message type.
# Installation and module may be wildcarded.
# The only valid messages types are TYPE_TRACEBUF and TYPE_TRACEBUF2
# Output message type will be the same as the input message type. 
#--------------------------------------------------------------------------
 GetWavesFrom    INST_WILDCARD MOD_WILDCARD TYPE_TRACEBUF2 

# List SCNL codes of trace messages to decimate and their output SCNL codes.
# If old GetSCN command is used, location code is set to "--".
# Wildcard characters are not allowed here.
#--------------------------------------------------------------------------
#         Input-SCNL      Output-SCNL
GetSCNL AC01 HHZ C1 -- AC01 HHZ C1 --
GetSCNL AC04 HHZ C1 -- AC04 HHZ C1 --
GetSCNL AC05 HHZ C1 -- AC05 HHZ C1 --
GetSCNL AC06 HHZ C1 -- AC06 HHZ C1 --
GetSCNL AC07 HHZ C1 -- AC07 HHZ C1 --
GetSCNL ADAL HHZ VC -- ADAL HHZ VC --
GetSCNL AF01 HHZ C1 -- AF01 HHZ C1 --
GetSCNL AHML HHZ RI -- AHML HHZ RI --
GetSCNL AP01 HHZ C1 -- AP01 HHZ C1 --
GetSCNL AP02 HHZ C1 -- AP02 HHZ C1 --
GetSCNL AVAL HHZ TC -- AVAL HHZ TC --
GetSCNL AY02 HHZ C1 -- AY02 HHZ C1 --
GetSCNL AY03 HHZ C1 -- AY03 HHZ C1 --
GetSCNL AY04 HHZ C1 -- AY04 HHZ C1 --
GetSCNL AY05 HHZ C1 -- AY05 HHZ C1 --
GetSCNL AYSN HHZ VC -- AYSN HHZ VC --
GetSCNL AZUF HHZ TC -- AZUF HHZ TC --
GetSCNL BI02 HHZ C1 -- BI02 HHZ C1 --
GetSCNL BI04 HHZ C1 -- BI04 HHZ C1 --
GetSCNL BI05 HHZ C1 -- BI05 HHZ C1 --
GetSCNL BI06 HHZ C1 -- BI06 HHZ C1 --
GetSCNL BI07 HHZ C1 -- BI07 HHZ C1 --
GetSCNL BLOQ HHZ TC -- BLOQ HHZ TC --
GetSCNL BO01 HHZ C1 -- BO01 HHZ C1 --
GetSCNL BO02 HHZ C1 -- BO02 HHZ C1 --
GetSCNL BO03 HHZ C1 -- BO03 HHZ C1 --
GetSCNL CAQ HHZ TC -- CAQ HHZ TC --
GetSCNL CARR HHZ TC -- CARR HHZ TC --
GetSCNL CAST HHZ TC -- CAST HHZ TC --
GetSCNL CHAC HHZ VC -- CHAC HHZ VC --
GetSCNL CO01 HHZ C1 -- CO01 HHZ C1 --
GetSCNL CO02 HHZ C1 -- CO02 HHZ C1 --
GetSCNL CO03 HHZ C1 -- CO03 HHZ C1 --
GetSCNL CO04 HHZ C1 -- CO04 HHZ C1 --
GetSCNL CO05 HHZ C1 -- CO05 HHZ C1 --
GetSCNL CO06 HHZ C1 -- CO06 HHZ C1 --
GetSCNL CUEV HHZ VC -- CUEV HHZ VC --
GetSCNL CYA HHZ RI -- CYA HHZ RI --
GetSCNL ELTO HHZ TC -- ELTO HHZ TC --
GetSCNL ENQU HHZ TC -- ENQU HHZ TC --
GetSCNL ESPZ HHZ AI 02 ESPZ HHZ AI --
GetSCNL FAR1 HHZ C -- FAR1 HHZ C --
GetSCNL FRIO HHZ VC -- FRIO HHZ VC --
GetSCNL HUDS HHZ VC -- HUDS HHZ VC --
GetSCNL IN43 HHZ C1 -- IN43 HHZ C1 --
GetSCNL IN44 HHZ C1 -- IN44 HHZ C1 --
GetSCNL IN47 HHZ C1 -- IN47 HHZ C1 --
GetSCNL IN48 HHZ C1 -- IN48 HHZ C1 --
GetSCNL IN49 HHZ C1 -- IN49 HHZ C1 --
GetSCNL JUBA HHZ AI 02 JUBA HHZ AI --
GetSCNL LC01 HHZ C1 -- LC01 HHZ C1 --
GetSCNL LC02 HHZ C1 -- LC02 HHZ C1 --
GetSCNL LCO HHZ IU 10 LCO HHZ IU --
GetSCNL LJUN HHZ VC -- LJUN HHZ VC --
GetSCNL LL01 HHZ C1 -- LL01 HHZ C1 --
GetSCNL LL02 HHZ C1 -- LL02 HHZ C1 --
GetSCNL LL03 HHZ C1 -- LL03 HHZ C1 --
GetSCNL LL04 HHZ C1 -- LL04 HHZ C1 --
GetSCNL LL05 HHZ C1 -- LL05 HHZ C1 --
GetSCNL LL06 HHZ C1 -- LL06 HHZ C1 --
GetSCNL LL07 HHZ C1 -- LL07 HHZ C1 --
GetSCNL LMEL HHZ C -- LMEL HHZ C --
GetSCNL LOPS HHZ VC -- LOPS HHZ VC --
GetSCNL LR04 HHZ C1 -- LR04 HHZ C1 --
GetSCNL LR05 HHZ C1 -- LR05 HHZ C1 --
GetSCNL MG01 HHZ C1 -- MG01 HHZ C1 --
GetSCNL MG02 HHZ C1 -- MG02 HHZ C1 --
GetSCNL MG03 HHZ C1 -- MG03 HHZ C1 --
GetSCNL MG04 HHZ C1 -- MG04 HHZ C1 --
GetSCNL MG05 HHZ C1 -- MG05 HHZ C1 --
GetSCNL MGDN HHZ VC -- MGDN HHZ VC --
GetSCNL ML02 HHZ C1 -- ML02 HHZ C1 --
GetSCNL MNMCX HHZ CX -- MNMCX HHZ CX -- 
GetSCNL MT01 HHZ C1 -- MT01 HHZ C1 --
GetSCNL MT02 HHZ C1 -- MT02 HHZ C1 --
GetSCNL MT03 HHZ C1 -- MT03 HHZ C1 --
GetSCNL MT05 HHZ C1 -- MT05 HHZ C1 --
GetSCNL MT07 HHZ C1 -- MT07 HHZ C1 --
GetSCNL MT08 HHZ C1 -- MT08 HHZ C1 --
GetSCNL MT09 HHZ C1 -- MT09 HHZ C1 --
GetSCNL MT10 HHZ C1 -- MT10 HHZ C1 --
GetSCNL MT12 HHZ C1 -- MT12 HHZ C1 --
GetSCNL MT13 HHZ C1 -- MT13 HHZ C1 --
GetSCNL MT14 HHZ C1 -- MT14 HHZ C1 --
GetSCNL MT15 HHZ C1 -- MT15 HHZ C1 --
GetSCNL MT16 HHZ C1 -- MT16 HHZ C1 --
GetSCNL MT18 HHZ C1 -- MT18 HHZ C1 --
GetSCNL OTAV HHZ IU 10 OTAV HHZ IU --
GetSCNL PATCX HHZ CX -- PATCX HHZ CX -- 
GetSCNL PAYG HHZ IU 00 PAYG HHZ IU --
#GetSCNL PAYG HHZ IU 10 PAYG HHZ IU 10
GetSCNL PB01 HHZ CX -- PB01 HHZ CX --
GetSCNL PB02 HHZ CX -- PB02 HHZ CX --
GetSCNL PB03 HHZ CX -- PB03 HHZ CX --
GetSCNL PB06 HHZ CX -- PB06 HHZ CX --
GetSCNL PB07 HHZ CX -- PB07 HHZ CX --
GetSCNL PB08 HHZ CX -- PB08 HHZ CX --
GetSCNL PB09 HHZ CX -- PB09 HHZ CX --
GetSCNL PB10 HHZ CX -- PB10 HHZ CX --
GetSCNL PB12 HHZ CX -- PB12 HHZ CX --
GetSCNL PB14 HHZ CX -- PB14 HHZ CX --
GetSCNL PB15 HHZ CX -- PB15 HHZ CX --
GetSCNL PB16 HHZ CX -- PB16 HHZ CX --
GetSCNL PB18 HHZ CX -- PB18 HHZ CX --
GetSCNL PB19 HHZ CX -- PB19 HHZ CX --
GetSCNL PB20 HHZ CX -- PB20 HHZ CX --
GetSCNL PB21 HHZ CX -- PB21 HHZ CX --
GetSCNL PB22 HHZ CX -- PB22 HHZ CX --
GetSCNL PB23 HHZ CX -- PB23 HHZ CX --
GetSCNL PMSA HHZ IU 10 PMSA HHZ IU --
GetSCNL PSGCX HHZ CX -- PSGCX HHZ CX -- 
GetSCNL PX06 HHZ CX -- PX06 HHZ CX --
GetSCNL QUET HHZ TC -- QUET HHZ TC --
GetSCNL QUIR HHZ TC -- QUIR HHZ TC --
GetSCNL ROC1 HHZ C  -- ROC1 HHZ C  --
GetSCNL SAML HHZ IU 00 SAML HHZ IU --
GetSCNL TA01 HHZ C1 -- TA01 HHZ C1 --
GetSCNL TA02 HHZ C1 -- TA02 HHZ C1 --
GetSCNL TA03 HHZ C1 -- TA03 HHZ C1 --
GetSCNL TCA  HHZ RI -- TCA  HHZ RI --
GetSCNL TRQA HHZ IU 00 TRQA HHZ IU --
GetSCNL TUPU HHZ TC -- TUPU HHZ TC --
GetSCNL VA01 HHZ C1 -- VA01 HHZ C1 --
GetSCNL VA02 HHZ C1 -- VA02 HHZ C1 --
GetSCNL VA03 HHZ C1 -- VA03 HHZ C1 --
GetSCNL VA04 HHZ C1 -- VA04 HHZ C1 --
GetSCNL VA05 HHZ C1 -- VA05 HHZ C1 --
GetSCNL VA06 HHZ C1 -- VA06 HHZ C1 --
GetSCNL VCA  HHZ RI -- VCA  HHZ RI --
GetSCNL VICU HHZ TC -- VICU HHZ TC --
GetSCNL YJA  HHZ RI -- YJA  HHZ RI --
GetSCNL YLTN HHZ VC -- YLTN HHZ VC --



