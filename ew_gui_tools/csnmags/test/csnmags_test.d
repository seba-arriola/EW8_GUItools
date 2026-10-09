# Config de test offline (sin anillos reales).
MyModuleId MOD_MAGNITUDES
InRing HYPO_RING_REF
OutRing HYPO_RING_REF
HeartBeatInt 30
LogFile 0
Debug 1

StaFile test/test_sta.txt
ResponseDir test/responses
ResponsePattern %S_%C_%N.pz
ResponseInMeters 0

WoodAndersonCoefs 0.8 0.7 2080
Ml_DistType 1
Ml_AmpMode 1
Ml_SlideLen 0.8
Ml_Coeffs 1.11 0.00189 -2.09
Ml_MaxDelta 8.0

Mwp_Enable 1
Mb_Enable 1
Ms_Enable 1
Ml_Enable 1
