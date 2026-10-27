#ifndef CSNLOC_LIVE_ASSOC_H
#define CSNLOC_LIVE_ASSOC_H

#include <stddef.h>
#include <stdint.h>

#include "csnloc.h"

/*
 * live_assoc.c - asociacion de picadas al hipocentro de un evento vivo.
 *
 * El locator vivo NO usa una ventana fija alrededor del tiempo de origen:
 * predice el tiempo de llegada de cada P/S desde el seed (t0 + tt) y acepta
 * la picada si cae dentro de LiveAssocCfg::res_p_sec / res_s_sec. La ventana
 * crece con la distancia y la profundidad por construccion.
 *
 * El seed solo aporta el hipocentro y el qid; las fases del ARC se construyen
 * con las picadas asociadas, no con las fases viejas del seed.
 */

typedef struct {
    int      qid;
    unsigned version;
    double   t0, lat, lon, depth;
} LiveSeed;

typedef struct {
    double lookback_sec;   /* maximo t_pick - t0 (s); 0 = sin tope         */
    double res_p_sec;      /* |residual| maximo para P (s)                */
    double res_s_sec;      /* |residual| maximo para S (s)                */
} LiveAssocCfg;

typedef struct {
    uint64_t hash;         /* (sta, phase, t_pick) de las aceptadas       */
    int      n;
} LivePickSet;

/* 1 = aceptada; 0 = fuera de residuo/distancia/ventana; -1 = argumentos. */
int live_assoc_pick(const LiveSeed *s, const Pick *p, TTModel *tt,
                    const StationList *st, const LiveAssocCfg *cfg,
                    double *resid_out);

/* Construye el ARC con TODAS las P/S asociadas. No copia las fases viejas
 * del seed. Devuelve 0 y llena arc/nph_out/set_out; -1 si no hay fases. */
int live_build_arc(const LiveSeed *s, const Pick *picks, int np,
                   TTModel *tt, const StationList *st,
                   const LiveAssocCfg *cfg, char *arc, size_t arcn,
                   LivePickSet *set_out, int *nph_out);

typedef struct {
    int    ok;
    double rms;
    int    nph;
    double gap;
    char   why[64];
} LiveLocCheck;

typedef struct {
    int    min_phases;    /* 0 = off */
    double max_rms;       /* 0 = off */
    double max_gap_deg;   /* 0 = off */
} LiveLocCheckCfg;

/* Valida la cabecera del ARC de salida (nph/gap/rms). 0 = parseado (mira
 * out->ok); -1 = ARC ilegible. */
int live_check_arc(const char *arc, int len, const LiveLocCheckCfg *cfg,
                   LiveLocCheck *out);

/* 1 = N_retry agotado (n_retry<=0 => se detiene al primer intento fallido). */
int live_retry_exhausted(int no_new, int n_retry);

#endif /* CSNLOC_LIVE_ASSOC_H */
