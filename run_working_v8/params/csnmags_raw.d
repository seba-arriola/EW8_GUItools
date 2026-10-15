# Configuración de csnmags (instancia CRUDA) - Motor de magnitudes ML/Mwp/Mb/Ms
#
# Segunda instancia de csnmags que lee hipocentros CRUDOS de csnloc en HYPO_RING
# y publica TYPE_MAGNITUDE en el mismo anillo. Sirve para que los eventos que
# todavía no tienen solución refinada muestren magnitudes en csnhypodbp.
# Usa exactamente los mismos algoritmos/calibración que csnmags.d; solo cambia
# MyModuleId y el anillo (HYPO_RING en vez de HYPO_RING_REF).

MyModuleId        MOD_MAGNITUDES_RAW
InRing            HYPO_RING
OutRing           HYPO_RING
HeartBeatInt      30
LogFile           1
Debug             0

# --- Fuente de ondas ---
StoreRoot         ../wstore
# store para lo reciente y SDS para lo antiguo (todo por .d).
# Archivo SDS (SeisComP). SdsFallbackHours 0 = SDS desactivado.
SdsEnabled        1
SdsRoot           /mnt/sds/archive
SdsFallbackHours  168

# --- Respuesta instrumental ---
StaFile           estaciones_107.txt
ResponseDir       /mnt/d/resources/csnmags/responses
ResponsePattern   %S_%C_%N.pz
ResponseInMeters  0

# --- Calibración regional (grillas + tablas) ---
CalibFile         /mnt/d/resources/csnmags/calib/calib_map.txt
StaCorrFile       /mnt/d/resources/csnmags/calib/station_corr.txt
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
Mb_QTable         /mnt/d/resources/csnmags/calib/mb_Q.tab
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
