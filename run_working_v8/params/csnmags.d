# Configuración de csnmags - Motor de magnitudes ML/Mwp/Mb/Ms
#
# Módulo: ew_gui_tools/csnmags. Lee hipocentros refinados de InRing y publica
# TYPE_MAGNITUDE (ML/Mb/Ms/Mwp). La respuesta instrumental se lee de SAC PZ en
# ResponseDir (generados por tools/fetch_responses.py desde el FDSNWS de
# metadata). Ver docs/MAGNITUDES.md para el detalle de cada parámetro.

MyModuleId        MOD_MAGNITUDES
InRing            HYPO_RING_REF
OutRing           HYPO_RING_REF
HeartBeatInt      30
LogFile           1
Debug             0

# --- Wave server (modo anillo) ---
WsIP              127.0.0.1
WsPort            16022
WsTimeout         10000

# --- Respuesta instrumental ---
StaFile           estaciones_107.txt
ResponseDir       responses
ResponsePattern   %S_%C_%N.pz
ResponseInMeters  0

# --- Calibración regional (grillas + tablas) ---
CalibFile         calib/calib_map.txt
StaCorrFile       calib/station_corr.txt
TauModel          iasp91

# --- ML (Magnitud local) ---
Ml_Enable         1
WoodAndersonCoefs 0.8 0.7 2080
Ml_DistType       1
Ml_AmpMode        1
Ml_SlideLen       0.8
Ml_Coeffs         1.11 0.00189 -2.09
Ml_MaxDelta       8.0
Ml_MaxDist        600.0
Ml_SgSpeed        3.98
Ml_TA             10.0
Ml_TB             45.0
Ml_MinSta         1

# --- Mwp ---
Mwp_Enable        1
Mwp_T0            95.0
Mwp_StartOff      0.0
Mwp_HighPass      0.01
Mwp_Rho           2700.0
Mwp_Alpha         6000.0
Mwp_Fp            0.6
Mwp_MinDelta      3.0
Mwp_MaxDelta      90.0
Mwp_MinSta        1

# --- Mb ---
Mb_Enable         1
Mb_Period         1.0
Mb_Window         30.0
Mb_QTable         calib/mb_Q.tab
Mb_Band           0.5 2.0
Mb_MinDelta       5.0
Mb_MaxDelta       105.0
Mb_MinSta         1

# --- Ms ---
Ms_Enable         1
Ms_Variant        0
Ms_T              20.0
Ms_Band           0.045 0.056
Ms_Coeffs         1.66 0.3
Ms_DeepCorr       0
Ms_MinDelta       20.0
Ms_MaxDelta       160.0
Ms_MinSta         1

# --- Red ---
UseMedian         0
TruncK            2.0
