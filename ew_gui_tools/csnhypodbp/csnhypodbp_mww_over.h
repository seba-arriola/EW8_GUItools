#ifndef CSNHYPODBP_MWW_OVER_H
#define CSNHYPODBP_MWW_OVER_H

#include "csnwphase.h"

/*
 * Overrides editados a mano en la tabla del panel `Mww`.
 *
 * La tabla del panel es lo que se ejecuta: `mww_over_apply()` vuelca estos
 * valores sobre el `CsnwphaseInput`/`CsnwphaseCfg` que salen de la solución
 * elegida (globals `selected_*`) y del `.d`, así que el i_master, el `run.sh` y
 * la propia tabla siguen teniendo UNA sola fuente de verdad.
 *
 * Solo los campos de `CSNWPHASE_PF_*` son editables; la banda de inversión
 * (derivada de la magnitud) y el resto de filas no se tocan desde aquí.
 */
typedef struct {
    unsigned touched;             /* bitmask de campos tocados (CSNWPHASE_PF_*) */
    int    year, month, day, hour, minute;
    double second;
    double lat, lon, depth_km, magnitude;
    int    win_pre_sec, win_post_sec;
    double screen_th, screen_nr;
    double dmin_deg, dmax_deg;    /* cribado de estaciones del motor */
} MwwOverrides;

/* Estado inicial: el de la solución y el .d, sin ningún campo tocado. */
void mww_over_init(const CsnwphaseInput *in, const CsnwphaseCfg *cfg,
                   MwwOverrides *ov);

/* Fija el campo `field` (CSNWPHASE_PF_*) con `value`. Devuelve 0 si el campo es
 * editable y -1 si no lo es (en ese caso no toca nada). */
int mww_over_set(MwwOverrides *ov, int field, double value);

/* Fija el t0 desde el texto de la celda (mismo formato que muestra la tabla).
 * Devuelve 0 si OK, -1 si el texto no es una fecha válida. */
int mww_over_set_time(MwwOverrides *ov, const char *txt);

/* Quita el bit de `field`: esa corrida vuelve al valor de la solucion/`.d` para
 * ese campo. Lo usa la celda cuando el texto no es valido, para que un texto a
 * medias (el "9" de "95") no deje aplicado un prefijo que el usuario no quiso.
 * Devuelve 0 si el campo es editable (numerico) y -1 si no lo es. */
int mww_over_unset(MwwOverrides *ov, int field);

/* Rango [lo,hi] de un campo numerico editable de la tabla: exactamente los que
 * acepta `mww_over_set`. 0 = si; -1 = no es un campo numerico editable.
 * `lo`/`hi` pueden ser NULL. */
int mww_field_limits(int field, double *lo, double *hi);

/* Texto de una celda numerica -> valor. 0 = numero finito y dentro del rango de
 * su campo; -1 = vacio, no numerico, con basura detras ("12abc"), fuera de rango
 * o inf/nan. Acepta espacios alrededor y ',' como separador decimal (la misma
 * regla que la barra de magnitudes). `out` puede ser NULL para solo validar. */
int mww_field_parse(int field, const char *txt, double *out);

/* Vuelca los overrides sobre la corrida (config + hipocentro semilla). */
void mww_over_apply(const MwwOverrides *ov, CsnwphaseInput *in, CsnwphaseCfg *cfg);

#endif /* CSNHYPODBP_MWW_OVER_H */
