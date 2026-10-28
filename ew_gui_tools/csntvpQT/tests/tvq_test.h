#ifndef TVQ_TEST_H
#define TVQ_TEST_H

/* Utilidades comunes de los tests de la capa C de csntvpQT.
 * Todo se construye en un directorio temporal propio: no tocan params/ ni
 * necesitan EarthWorm corriendo. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "tvq_capi.h"
#include "tvq_internal.h"

static int tvq_t_fail = 0;

#if defined(__GNUC__)
#define TVQ_TEST_UNUSED __attribute__((unused))
#else
#define TVQ_TEST_UNUSED
#endif

#define T_CHECK(cond, what)                                                  \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL: %s\n", what);                                      \
            tvq_t_fail++;                                                    \
        } else {                                                             \
            printf("ok  : %s\n", what);                                      \
        }                                                                    \
    } while (0)

static TVQ_TEST_UNUSED const char *tvq_t_dir(void)
{
    static char dir[256];

    if (!dir[0]) {
        snprintf(dir, sizeof dir, "/tmp/tvqtest_%d", (int)getpid());
        (void)mkdir(dir, 0700);
    }
    return dir;
}

static TVQ_TEST_UNUSED const char *tvq_t_path(const char *name)
{
    static char p[512];

    snprintf(p, sizeof p, "%s/%s", tvq_t_dir(), name);
    return p;
}

static TVQ_TEST_UNUSED void tvq_t_write(const char *name, const char *content)
{
    FILE *f = fopen(tvq_t_path(name), "w");

    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static TVQ_TEST_UNUSED void tvq_t_colour(double rgb[3], double r, double g, double b)
{
    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;
}

/* Config valida SIN leer ningun .d: la capa C solo necesita estos campos. */
static TVQ_TEST_UNUSED TvqConfig *tvq_t_cfg(const char *sta_name, const char *sta_lines)
{
    TvqConfig *cfg = (TvqConfig *)calloc(1, sizeof(TvqConfig));

    if (!cfg)
        return NULL;

    snprintf(cfg->my_module, sizeof cfg->my_module, "MOD_CSNTVPQT");
    cfg->module_id = -1;
    snprintf(cfg->in_ring, sizeof cfg->in_ring, "SLINK_RING");
    snprintf(cfg->out_ring, sizeof cfg->out_ring, "PICK_RING");
    cfg->heartbeat_int = 30;
    cfg->log_file = 0;
    snprintf(cfg->sta_file, sizeof cfg->sta_file, "%s", tvq_t_path(sta_name));
    cfg->stations_per_screen = 12;
    cfg->time_window_min = 6;
    cfg->pick_replace_secs = 15.0;

    tvq_t_colour(cfg->colour[TVQ_COLOUR_WAVE], 0, 0, 0);
    tvq_t_colour(cfg->colour[TVQ_COLOUR_BG], 1, 1, 1);
    tvq_t_colour(cfg->colour[TVQ_COLOUR_FONT], 1, 0, 0);
    tvq_t_colour(cfg->colour[TVQ_COLOUR_SEP], 0.85, 0.85, 0.85);
    tvq_t_colour(cfg->colour[TVQ_COLOUR_P], 1, 0, 0);
    tvq_t_colour(cfg->colour[TVQ_COLOUR_S], 0, 0, 0);

    if (sta_lines)
        tvq_t_write(sta_name, sta_lines);
    return cfg;
}

/* Construye un TYPE_TRACEBUF2 en `buf`. Devuelve el largo o -1. */
static TVQ_TEST_UNUSED int tvq_t_trace(char *buf, size_t cap, const char *sta, const char *chan,
                       const char *net, const char *loc, const char *dtype,
                       double rate, double t0, int nsamp,
                       const int32_t *samples)
{
    TRACE2_HEADER *h;
    size_t bytes;
    int bps = (strcmp(dtype, "i2") == 0) ? 2 : 4;

    bytes = sizeof(TRACE2_HEADER) + (size_t)nsamp * (size_t)bps;
    if (bytes > cap)
        return -1;

    memset(buf, 0, bytes);
    h = (TRACE2_HEADER *)buf;
    snprintf(h->sta, sizeof h->sta, "%s", sta);
    snprintf(h->chan, sizeof h->chan, "%s", chan);
    snprintf(h->net, sizeof h->net, "%s", net);
    snprintf(h->loc, sizeof h->loc, "%s", loc);
    snprintf(h->datatype, sizeof h->datatype, "%s", dtype);
    h->samprate = rate;
    h->starttime = t0;
    h->endtime = t0 + (double)(nsamp - 1) / rate;
    h->nsamp = nsamp;

    if (bps == 4) {
        memcpy(buf + sizeof(TRACE2_HEADER), samples, (size_t)nsamp * 4);
    } else {
        int16_t *p = (int16_t *)(buf + sizeof(TRACE2_HEADER));

        for (int k = 0; k < nsamp; k++)
            p[k] = (int16_t)samples[k];
    }
    return (int)bytes;
}

static TVQ_TEST_UNUSED void tvq_t_sine(int32_t *out, int n, double amp)
{
    for (int k = 0; k < n; k++)
        out[k] = (int32_t)(amp * (double)((k % 20) - 10) / 10.0);
}

#endif /* TVQ_TEST_H */
