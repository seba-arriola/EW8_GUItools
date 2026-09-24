/******************************************************************************
 * taulib_compat.h                                                            *
 *                                                                            *
 * Definiciones minimas que `taulib_csnloc.c` tomaba de `earlybirdlib.h`      *
 * (el header maestro WC/ATWC de ew_gui_tools/libsrc). Se mantienen aqui para *
 * que la tabla de tiempos de viaje sea autocontenida y csnloc no dependa de  *
 * libsrc. Los valores son identicos a los originales.                        *
 *                                                                            *
 * Nota: RAD/DEG/PHASE_LENGTH/PSZ/J* los aporta iasplib.h (via ttlim.h).      *
 ******************************************************************************/
#ifndef TAULIB_COMPAT_H
#define TAULIB_COMPAT_H

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#define PI      3.14159265358979323846
#define TWO_PI  6.28318530717958647692
#define TWOPI   6.28318530717958647692

#define MAX_PHASES 20

#endif /* TAULIB_COMPAT_H */
