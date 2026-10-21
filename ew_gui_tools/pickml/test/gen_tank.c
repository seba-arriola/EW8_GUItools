/******************************************************************************
 * gen_tank.c — genera un tank 3C determinista (o el mismo dato en layout de   *
 * store) para los tests offline.                                             *
 *                                                                            *
 * Una estación TEST (XX, --) con HHE/HHN/HHZ, 100 Hz, un estallido en         *
 * TB = t0 + 95 s sobre Z (y menor en las horizontales). El pico de |Z| cae    *
 * exactamente en TB, así que el motor stub (argmax de |Z|) debe picar en TB   *
 * y 12 s después.                                                            *
 *                                                                            *
 * Uso: gen_tank --tank <file> [--epoch <t0>] [--dur <s>]                     *
 *      gen_tank --store <root> [--epoch <t0>] [--dur <s>]                   *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <trace_buf.h>

#define RATE   100
#define NSAMP  100
#define TB     95.0
#define AMP_Z  5000.0
#define AMP_H  1200.0

static const char *CHAN[3] = { "HHZ", "HHN", "HHE" };   /* Z, N, E */

static double g_tb = TB;    /* epoch absoluto del estallido (t0 + TB) */

static int32_t make_sample(int comp, double t)
{
    double  dt = t - g_tb;
    double  a  = 0.0;
    long    k  = (long)llround(t * RATE);
    int32_t noise = (int32_t)(((unsigned long)k * 2654435761UL) % 13) - 6;

    if (dt >= 0.0) {
        double amp = (comp == 0) ? AMP_Z : AMP_H;
        a = amp * exp(-dt / 0.6) * cos(2.0 * M_PI * 5.0 * dt);
    }
    return (int32_t)lround(a) + noise;
}

static void fill_header(TRACE2_HEADER *h, int comp, double t)
{
    memset(h, 0, sizeof(*h));
    h->nsamp     = NSAMP;
    h->starttime = t;
    h->endtime   = t + (double)(NSAMP - 1) / (double)RATE;
    h->samprate  = (double)RATE;
    snprintf(h->sta,  sizeof(h->sta),  "TEST");
    snprintf(h->net,  sizeof(h->net),  "XX");
    snprintf(h->chan, sizeof(h->chan), "%s", CHAN[comp]);
    snprintf(h->loc,  sizeof(h->loc),  "--");
    h->version[0]  = '2';
    h->version[1]  = '0';
    snprintf(h->datatype, sizeof(h->datatype), "i4");
}

static int mkdir_p(const char *path)
{
    char tmp[512];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static void ymd(double t, char out[9])
{
    time_t    tt = (time_t)floor(t);
    struct tm tm;
    gmtime_r(&tt, &tm);
    if (strftime(out, 9, "%Y%m%d", &tm) == 0) out[0] = '\0';
}

int main(int argc, char **argv)
{
    const char *tank = NULL, *store = NULL;
    double t0 = 1700000000.0;
    int    dur = 180;
    int    i, s, c;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tank") && i + 1 < argc)       tank = argv[++i];
        else if (!strcmp(argv[i], "--store") && i + 1 < argc) store = argv[++i];
        else if (!strcmp(argv[i], "--epoch") && i + 1 < argc) t0 = atof(argv[++i]);
        else if (!strcmp(argv[i], "--dur") && i + 1 < argc)   dur = atoi(argv[++i]);
        else { fprintf(stderr, "gen_tank: opcion desconocida %s\n", argv[i]); return 2; }
    }
    if (!tank && !store) {
        fprintf(stderr, "Uso: gen_tank --tank <file> | --store <root> "
                        "[--epoch <t0>] [--dur <s>]\n");
        return 2;
    }

    g_tb = t0 + TB;

    if (tank) {
        FILE *f = fopen(tank, "wb");
        if (!f) { perror(tank); return 1; }
        for (s = 0; s < dur; s++) {
            double t = t0 + (double)s;
            for (c = 0; c < 3; c++) {
                TRACE2_HEADER h;
                int32_t x[NSAMP];
                int k;
                fill_header(&h, c, t);
                for (k = 0; k < NSAMP; k++)
                    x[k] = make_sample(c, t + (double)k / (double)RATE);
                if (fwrite(&h, sizeof(h), 1, f) != 1 ||
                    fwrite(x, sizeof(int32_t), NSAMP, f) != NSAMP) {
                    fprintf(stderr, "gen_tank: escritura truncada\n");
                    fclose(f);
                    return 1;
                }
            }
        }
        fclose(f);
    } else {
        char ymdstr[9];
        ymd(t0, ymdstr);

        for (c = 0; c < 3; c++) {
            char dir[512], path[600];
            FILE *f;

            snprintf(dir, sizeof(dir), "%s/XX.TEST.--.%s", store, CHAN[c]);
            if (mkdir_p(dir) != 0) { perror(dir); return 1; }
            snprintf(path, sizeof(path), "%s/%s.tank", dir, ymdstr);

            f = fopen(path, "wb");
            if (!f) { perror(path); return 1; }
            for (s = 0; s < dur; s++) {
                TRACE2_HEADER h;
                int32_t x[NSAMP];
                int k;
                double t = t0 + (double)s;
                fill_header(&h, c, t);
                for (k = 0; k < NSAMP; k++)
                    x[k] = make_sample(c, t + (double)k / (double)RATE);
                if (fwrite(&h, sizeof(h), 1, f) != 1 ||
                    fwrite(x, sizeof(int32_t), NSAMP, f) != NSAMP) {
                    fprintf(stderr, "gen_tank: escritura truncada\n");
                    fclose(f);
                    return 1;
                }
            }
            fclose(f);
        }
    }

    printf("gen_tank: t0=%.0f tb=%.0f dur=%d\n", t0, t0 + TB, dur);
    return 0;
}
