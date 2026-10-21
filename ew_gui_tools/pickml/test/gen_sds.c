/******************************************************************************
 * gen_sds.c — convierte un tank TRACE2+int32 (gen_tank) al layout SDS de      *
 * SeisComP con libmseed2, para el test hermetico de `pickml --sds`.            *
 *                                                                             *
 *   gen_sds --tank <file> --root <dir> [--factor 1|2|3|4]                     *
 *                                                                             *
 * `--factor` interpola cada muestra y multiplica la tasa (100 -> 200/300/400  *
 * Hz): sirve para ejercitar el camino de decimacion (2x/4x) y el rechazo de   *
 * tasas no soportadas (3x).                                                   *
 *                                                                             *
 * Cada registro del tank se escribe como un registro miniSEED en              *
 *   <root>/<YYYY>/<NET>/<STA>/<CHAN>.D/<NET>.<STA>.<LOC>.<CHAN>.D.<YYYY>.<DDD>*
 * (misma ruta que usa el lector: ewgui_sds_path). Se anade al archivo si ya    *
 * existe; el primer registro de cada ruta lo trunca.                          *
 ******************************************************************************/

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <libmseed2.h>
#include <trace_buf.h>

#include "ewgui/sds.h"

#define MAXSEEN 64

static char seen[MAXSEEN][1024];
static int  nseen;

static void mkdirs(const char *path)
{
    char  tmp[1024];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* 1 si es la primera vez que se escribe esta ruta (hay que truncar). */
static int first_time(const char *path)
{
    int i;

    for (i = 0; i < nseen; i++)
        if (!strcmp(seen[i], path)) return 0;
    if (nseen < MAXSEEN) {
        snprintf(seen[nseen], sizeof(seen[nseen]), "%s", path);
        nseen++;
    }
    return 1;
}

static int write_rec(const char *path, const TRACE2_HEADER *h,
                     int32_t *x, long n, double rate, int overwrite)
{
    MSRecord *msr;
    int       rc;

    msr = msr_init(NULL);
    if (!msr) return -1;

    snprintf(msr->network, sizeof(msr->network), "%s", h->net);
    snprintf(msr->station, sizeof(msr->station), "%s", h->sta);
    snprintf(msr->location, sizeof(msr->location), "%s", h->loc);
    snprintf(msr->channel, sizeof(msr->channel), "%s", h->chan);

    msr->starttime  = (hptime_t)(MS_EPOCH2HPTIME(h->starttime) + 0.5);
    msr->samprate   = rate;
    msr->datasamples = (void *)x;
    msr->numsamples  = n;
    msr->samplecnt   = n;
    msr->sampletype  = 'i';

    rc = msr_writemseed(msr, path, overwrite, 4096, DE_STEIM2, 1, 0);

    /* datasamples apunta a memoria del llamante: no la libere libmseed. */
    msr->datasamples = NULL;
    msr->numsamples  = 0;
    msr->samplecnt   = 0;
    msr_free(&msr);
    return rc < 0 ? -1 : 0;
}

int main(int argc, char **argv)
{
    const char *tank = NULL, *root = NULL;
    int         factor = 1, i, rc = 0;
    FILE       *f;

    for (i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--tank")   && i + 1 < argc) tank = argv[++i];
        else if (!strcmp(argv[i], "--root")   && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--factor") && i + 1 < argc) factor = atoi(argv[++i]);
        else {
            fprintf(stderr,
                "Uso: gen_sds --tank <file> --root <dir> [--factor N]\n");
            return 2;
        }
    }
    if (!tank || !root || factor < 1 || factor > 4) {
        fprintf(stderr, "Uso: gen_sds --tank <file> --root <dir> [--factor N]\n");
        return 2;
    }

    f = fopen(tank, "rb");
    if (!f) { perror(tank); return 1; }

    for (;;) {
        TRACE2_HEADER h;
        int32_t      *x, *y = NULL;
        long          n, k;
        double        rate;
        char          path[1024], dir[1024];
        char         *slash;
        int           l;

        if (fread(&h, sizeof(h), 1, f) != 1) break;
        if (h.nsamp <= 0 || h.nsamp > 10000000) break;

        n = h.nsamp;
        x = (int32_t *)malloc((size_t)n * sizeof(int32_t));
        if (!x) { rc = 1; break; }
        if (fread(x, sizeof(int32_t), (size_t)n, f) != (size_t)n) {
            free(x);
            break;
        }

        rate = h.samprate;
        if (factor > 1) {
            long m = n * factor;

            y = (int32_t *)malloc((size_t)m * sizeof(int32_t));
            if (!y) { free(x); rc = 1; break; }
            for (k = 0; k < n; k++) {
                double next = (k + 1 < n) ? (double)x[k + 1] : (double)x[k];
                int    j;
                for (j = 0; j < factor; j++) {
                    double v = (double)x[k] + (next - (double)x[k]) *
                               ((double)j / (double)factor);
                    y[k * factor + j] = (int32_t)llround(v);
                }
            }
            rate = h.samprate * (double)factor;
        }

        if (ewgui_sds_path(path, sizeof(path), root, h.net, h.sta, h.loc,
                           h.chan, h.starttime) == 0) {
            snprintf(dir, sizeof(dir), "%s", path);
            slash = strrchr(dir, '/');
            if (slash) *slash = '\0';
            mkdirs(dir);

            l = first_time(path);
            if (write_rec(path, &h, y ? y : x, n * (long)factor, rate, l) != 0) {
                fprintf(stderr, "gen_sds: no se pudo escribir %s\n", path);
                free(y);
                free(x);
                rc = 1;
                break;
            }
        }

        free(y);
        free(x);
    }

    fclose(f);
    return rc;
}
