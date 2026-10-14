#include "csnwphase.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * Escribe un SAC sintético (senoide de 0.05 Hz a 20 sps, 300 s) con la misma
 * geometría del evento de referencia (Illapel 2015) y una estación regional.
 * El binario REAL del motor (`saclst`) verifica después la cabecera:
 * tests/test_sac_engine.sh.
 *
 * Uso: test_sac <dir>
 */

#define NPTS 6000          /* 300 s a 20 sps */
#define RATE 20.0

static void mkdir_p(const char *path)
{
    char tmp[1024];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "/tmp/csnwphase_sac";
    CsnwphaseSAC s;
    float *data;
    char path[1024];
    double dist, az, baz, gcarc;
    long i;

    mkdir_p(dir);

    data = malloc(NPTS * sizeof(float));
    assert(data);
    for (i = 0; i < NPTS; i++)
        data[i] = (float)(1000.0 * sin(2.0 * M_PI * 0.05 * (double)i / RATE));

    memset(&s, 0, sizeof(s));
    snprintf(s.sta, sizeof(s.sta), "AF02");
    snprintf(s.net, sizeof(s.net), "C1");
    snprintf(s.chan, sizeof(s.chan), "BHZ");
    s.loc[0] = '\0';
    s.stla = -23.0;      /* estación regional (NO la del evento) */
    s.stlo = -68.0;
    s.stel_m = 2400.0;
    s.stdp_m = 0.0;
    s.evla = -31.573;
    s.evlo = -71.674;
    s.evel_m = 0.0;
    s.evdp_km = 25.0;
    s.mag = 8.3;
    s.rate = RATE;
    s.start_epoch = 1442444000.0;   /* 2015-09-16 22:53:20 UTC */
    s.origin_epoch = s.start_epoch + 60.0;   /* o = +60 s */
    s.npts = NPTS;
    s.data = data;

    csnwphase_distaz(s.evla, s.evlo, s.stla, s.stlo, &dist, &az, &baz, &gcarc);
    printf("geometria esperada: dist=%.3f km az=%.2f baz=%.2f gcarc=%.4f\n",
           dist, az, baz, gcarc);

    snprintf(path, sizeof(path), "%s/%s.%s.%s.SAC", dir, s.net, s.sta, s.chan);
    assert(csnwphase_sac_write(path, &s) == 0);
    printf("escrito: %s\n", path);

    free(data);
    printf("ALL CSNWPHASE SAC PREP OK\n");
    return 0;
}
