# csnwphase.d — inversión de fase W on-demand (motor eost/wphase vendorizado).
#
# NO es un módulo de anillos: no se declara en startstop_unix.d. Se invoca desde
# la GUI (Procesar → magnitudes → Mww) o por CLI. Ver docs/WPHASE.md.
#
# El motor sólo lee DOS variables de entorno: WPHASE_HOME (de ahí sale
# $WPHASE_HOME/P_tt_JB) y GF_PATH (= GfDir). RDSEED/ALT_RESPONSE_FILE/GMT_BIN
# son variables muertas en el código C del motor.

# --- motor -------------------------------------------------------------
WphaseHome  /home/seba/Dev/wphase/CSNop/wphase-prod
WphaseBin   /home/seba/Dev/wphase/CSNop/wphase-prod/bin
# Base de funciones de Green: ABSOLUTA y fuera del repo (~21 GB).
GfDir       /home/seba/Dev/wphase/CSNop/wphase-prod/GF_0.1d

# --- ondas y metadata --------------------------------------------------
StoreRoot   ../wstore
SdsRoot     /mnt/sds/archive
# Un evento más viejo que esto se consulta directo al SDS (el store no retiene tanto).
SdsFallbackHours 168
StaFile     estaciones_107.txt
ResponseDir /mnt/d/resources/csnmags/responses
ResponsePattern %S_%C_%N.pz
# 1 = los .pz del repo están en nm y se escalan a counts/m (convención de rdseed,
# que es la que espera el motor). Validado con el M7.4 del 2024-07-19 (Mw 7.35/7.48
# frente a 7.4 publicado): docs/WPHASE.md §7.
ResponseInNm 1

# --- parámetros de la corrida -----------------------------------------
Dmin        5.0
Dmax        50.0
MinMagnitude 6.0
WinPre      300
WinPost     900
ScreenTh    2.0
ScreenNr    2.0
GridSearch  1
# Grid search del centroide (wpinversion_gs; NO se usa wp_grid_search.py, que es
# python2). Los -ts salen de aqui (el wrapper del CSN los derivaba de la duracion)
# y se ven en la tabla del panel. Se hacen DOS pasadas: la 3D (xyz, lat/lon/prof
# con -dz/-minz, salida grid_search_xyz_out) y una xy fina que deja
# xy_WCMTSOLUTION, que es el archivo que prefiere el parser. Los GsXyz* son los
# XYZ_*/DDEP/MINDEP del wrapper.
GsTsMin     1.0
GsTsDt      4.0
GsTsMax     30.0
GsTsNit     3
GsXyDx      0.4
GsXyNx      3
GsXyNopt    5
GsXyNit     3
GsXyzDz     50.0
GsXyzMinz   11.5
GsXyzDx     0.6
GsXyzNx     1
GsXyzNopt   4
GsXyzNit    1
TimeoutSec  1800
KeepTmp     0
MaxConcurrent 1
Debug       0

# --- i_master (mismos defaults que el generador del CSN) ---------------
Evname      Chile
# SEED sólo lo consume extract.csh, que NO se ejecuta (damos SAC directamente).
SeedRef     -
CmtFile     CMTSOLUTION
GfdirRel    ./GF
FiltOrder   4
FiltPass    1
Idec2       2 280  0.1
Idec3       0.001  0.1  100  0.03
WpWin       0. 15.0 12.0
