/******************************************************************************
 * csnhypodbp_tt.h                                                            *
 *                                                                            *
 * Envoltura del motor IASP91 de csnloc (ttmodel.c + taulib_csnloc.o) para el  *
 * tiempo teorico de P en la pestana de ondas. El modelo se lee desde el cwd   *
 * (el mismo donde csnloc encuentra iasp91.tbl/.hed, es decir EW_PARAMS).      *
 ******************************************************************************/
#ifndef CSNHYPODBP_TT_H
#define CSNHYPODBP_TT_H

/* Inicializa el modelo (nombre p. ej. "iasp91"). Devuelve 0 si quedo listo. */
int csnhypodbp_tt_init(const char *model);

/* 1 si el modelo esta cargado. */
int csnhypodbp_tt_ready(void);

/* Tiempo de viaje P (s) para delta (grados) y profundidad (km).
 * Devuelve < 0 si el modelo no esta listo o el calculo falla. */
double csnhypodbp_tt_p(double delta_deg, double depth_km);

#endif /* CSNHYPODBP_TT_H */
