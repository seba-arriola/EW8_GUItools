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

/* Vuelca los overrides sobre la corrida (config + hipocentro semilla). */
void mww_over_apply(const MwwOverrides *ov, CsnwphaseInput *in, CsnwphaseCfg *cfg);

#endif /* CSNHYPODBP_MWW_OVER_H */
