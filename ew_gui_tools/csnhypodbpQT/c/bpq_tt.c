/*
 * bpq_tt.c - Inicializacion de los modelos de tiempos de viaje (P teorica).
 *
 * Dos capas:
 *   1. `bpq_init_tt`: la tabla IASP91 de `csnloc` (`TauModel`), que es el
 *      respaldo y lo unico que hay mas alla del dominio regional.
 *   2. `bpq_tt_theo_*`: el modelo de CAPAS con el que `hyp2000_ring` localiza
 *      (bandas Potin del `.crh` que referencia su `.hyp`). Es plano, sin
 *      curvatura: vale en el dominio regional (`TheoMaxDeltaKm`, 500 km) y
 *      fuera de ahi se vuelve a IASP91. Se elige la banda por epicentro con la
 *      misma regla que el refinador (NOD/RAD1/DRAD), una vez por solucion.
 *
 * Sin el modelo de capas todo sigue funcionando como antes (IASP91) y solo se
 * avisa por `err`; nunca aborta. La capa C no imprime: el mensaje lo manda a
 * stderr `ui/main.cxx`.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"
#include "bpq_tt1d.h"

#include "csnhypodbp_tt.h"

#define BPQ_TT_DEG2KM 111.195

static BpqTt1dSet *g_set = NULL;
static int         g_mode = 1;            /* 1 = capas, 0 = iasp91 */
static double      g_max_km = 500.0;

int bpq_init_tt(const BpqConfig *cfg, char *err, size_t errn)
{
    const char *model = (cfg && cfg->tau_model[0]) ? cfg->tau_model : "iasp91";

    if (csnhypodbp_tt_init(model) == 0) return 1;
    if (err && errn)
        snprintf(err, errn,
                 "sin modelo de tiempos de viaje <%s> en el cwd "
                 "(%s.hed/.tbl); P teorica deshabilitada", model, model);
    return 0;
}

/* Primer valor de una clave en un .d (linea "Clave valor [# comentario]"). */
static int cfg_key(const char *path, const char *key, char *out, size_t n)
{
    FILE *fp;
    char line[1024];

    if (!path || !path[0]) return 0;
    fp = fopen(path, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        char k[64] = "", v[512] = "";
        char *hash = strchr(line, '#');

        if (hash) *hash = '\0';
        if (sscanf(line, "%63s %511s", k, v) < 1) continue;
        if (strcmp(k, key) != 0) continue;
        snprintf(out, n, "%s", v);
        fclose(fp);
        return v[0] ? 1 : 0;
    }
    fclose(fp);
    return 0;
}

/* .hyp del localizador hyp2000: su .d -> `CommandFile`. */
static int find_hyp(const BpqConfig *cfg, char *out, size_t n)
{
    const char *env = getenv("BPQ_THEO_HYP");
    int i;

    if (env && env[0]) {
        snprintf(out, n, "%s", env);
        return 1;
    }
    if (!cfg) return 0;
    for (i = 0; i < cfg->n_locators; i++) {
        char hyp[512] = "";

        if (!strstr(cfg->locators[i].bin, "hyp2000")) continue;
        if (!cfg_key(cfg->locators[i].cfg, "CommandFile", hyp, sizeof(hyp)))
            continue;
        snprintf(out, n, "%s", hyp);
        return 1;
    }
    return 0;
}

int bpq_tt_theo_init(const BpqConfig *cfg, char *err, size_t errn)
{
    char path[512] = "", cerr[256] = "";
    const char *env;

    if (err && errn) err[0] = '\0';
    g_mode = (cfg ? cfg->theo_model : 1) != 0;
    g_max_km = (cfg && cfg->theo_max_delta_km > 0.0) ? cfg->theo_max_delta_km
                                                     : 500.0;
    env = getenv("BPQ_THEO");
    if (env && env[0])
        g_mode = (strcmp(env, "off") != 0 && strcmp(env, "iasp91") != 0);
    env = getenv("BPQ_THEO_MAX_KM");
    if (env && env[0]) {
        const double v = atof(env);

        if (v > 0.0) g_max_km = v;
    }
    if (g_set) {
        bpq_tt1d_set_free(g_set);
        g_set = NULL;
    }
    if (!g_mode) return 1;

    if (!find_hyp(cfg, path, sizeof(path))) {
        g_mode = 0;
        if (err && errn)
            snprintf(err, errn, "sin .hyp de hyp2000 para la P teorica; "
                     "se queda iasp91");
        return 0;
    }
    g_set = bpq_tt1d_hyp_load(path, ".", cerr, sizeof(cerr));
    if (!g_set) {
        g_mode = 0;
        if (err && errn)
            snprintf(err, errn, "P teorica en capas deshabilitada (%s)",
                     cerr[0] ? cerr : path);
        return 0;
    }
    return 1;
}

int bpq_tt_theo_prepare(BpqTtTheo *t, double ev_lat, double ev_lon,
                        double depth_km)
{
    if (!t) return 0;
    memset(t, 0, sizeof(*t));
    if (!g_set || !g_mode) return 0;
    if (!isfinite(ev_lat) || !isfinite(ev_lon) || !isfinite(depth_km)) return 0;
    t->ready = bpq_tt1d_select(g_set, ev_lat, ev_lon, &t->sel);
    return t->ready;
}

double bpq_tt_theo_p(const BpqTtTheo *t, double dist_deg, double depth_km)
{
    double km;

    if (!t || !t->ready || !g_set) return NAN;
    if (!isfinite(dist_deg) || dist_deg < 0.0) return NAN;
    km = dist_deg * BPQ_TT_DEG2KM;
    if (km > g_max_km) return NAN;
    return bpq_tt1d_eval(g_set, &t->sel, km, depth_km);
}

const char *bpq_tt_theo_model(const BpqTtTheo *t)
{
    int i, bi = 0;
    double bw = 0.0;

    if (!t || !t->ready || !g_set || t->sel.n <= 0) return NULL;
    for (i = 0; i < t->sel.n; i++) {
        if (t->sel.wt[i] > bw) {
            bw = t->sel.wt[i];
            bi = t->sel.mod[i];
        }
    }
    return (bi > 0) ? bpq_tt1d_set_model_name(g_set, bi) : NULL;
}

double bpq_tt_theo_max_km(void)
{
    return g_max_km;
}

double bpq_tt_iasp_p(double dist_deg, double depth_km)
{
    if (!csnhypodbp_tt_ready()) return NAN;
    if (!isfinite(dist_deg) || !isfinite(depth_km)) return NAN;
    return csnhypodbp_tt_p(dist_deg, depth_km);
}
