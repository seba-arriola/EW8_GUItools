/*
 * Test headless del lector SDS (criterio C6): genera un miniSEED sintetico en
 * el layout SDS con libmseed2, lo lee con csnhypodbp_sds_fetch y comprueba que
 * devuelve un buffer TRACE2_HEADER + int32 ("i4") con las muestras intactas.
 *
 *   make -C ew_gui_tools/csnhypodbp test_sds_read && ./test_sds_read
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <libmseed2.h>
#include <trace_buf.h>

#include "ewgui/sds.h"
#include "csnhypodbp_sds.h"

/* Config del modulo que consume el lector. */
int  g_sds_enabled = 1;
char g_sds_root[512] = "";

#define NSAMP 1000
#define RATE  100.0

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static double epoch(int year, int mon, int day)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = day;
    return (double)timegm(&tm);
}

static void mkdirs(const char *path)
{
    char tmp[1024];
    char *p;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int main(void)
{
    const char *root = "/tmp/opencode/sds_test";
    const char *sta = "GO01", *net = "C1", *loc = "00", *chan = "HHZ";
    double t0 = epoch(2030, 3, 5);
    double t1 = t0 + 10.0;
    char path[1024], dir[1024];
    int32_t samples[NSAMP];
    MSRecord *msr;
    char *raw = NULL;
    long len = 0;
    int i, rc;

    mkdirs(root);
    snprintf(g_sds_root, sizeof g_sds_root, "%s", root);

    if (ewgui_sds_path(path, sizeof path, root, net, sta, loc, chan, t0) != 0) {
        printf("FAIL: no se pudo construir la ruta SDS\n");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s", path);
    { char *s = strrchr(dir, '/'); if (s) *s = '\0'; }
    mkdirs(dir);

    for (i = 0; i < NSAMP; i++)
        samples[i] = i;

    msr = msr_init(NULL);
    if (!msr) { printf("FAIL: msr_init\n"); return 1; }
    snprintf(msr->network, sizeof msr->network, "%s", net);
    snprintf(msr->station, sizeof msr->station, "%s", sta);
    snprintf(msr->location, sizeof msr->location, "%s", loc);
    snprintf(msr->channel, sizeof msr->channel, "%s", chan);
    msr->starttime = (hptime_t)(MS_EPOCH2HPTIME(t0) + 0.5);
    msr->samprate = RATE;
    msr->datasamples = samples;
    msr->numsamples = NSAMP;
    msr->samplecnt = NSAMP;
    msr->sampletype = 'i';
    rc = msr_writemseed(msr, path, 1, 4096, DE_STEIM2, 1, 0);
    msr->datasamples = NULL;   /* apunta a `samples` (pila): no la libere libmseed */
    msr->numsamples = 0;
    msr->samplecnt = 0;
    msr_free(&msr);
    if (rc < 0) {
        printf("FAIL: msr_writemseed rc=%d (%s)\n", rc, path);
        return 1;
    }

    rc = csnhypodbp_sds_fetch(sta, net, chan, loc, t0, t1, &raw, &len);
    CHECK(rc == 1, "fetch devuelve datos (rc==1)");
    if (rc == 1) {
        TRACE2_HEADER *h = (TRACE2_HEADER *)raw;
        int32_t *d = (int32_t *)(raw + sizeof(TRACE2_HEADER));
        CHECK(strcmp(h->sta, sta) == 0, "sta");
        CHECK(strcmp(h->net, net) == 0, "net");
        CHECK(strcmp(h->chan, chan) == 0, "chan");
        CHECK(strcmp(h->loc, loc) == 0, "loc");
        CHECK(fabs(h->samprate - RATE) < 1e-6, "samprate");
        CHECK(memcmp(h->datatype, "i4", 2) == 0, "datatype i4");
        CHECK(h->nsamp == NSAMP, "nsamp");
        CHECK(len == (long)sizeof(TRACE2_HEADER) + NSAMP * (long)sizeof(int32_t),
              "longitud del buffer");
        CHECK(d[0] == 0 && d[NSAMP / 2] == NSAMP / 2 && d[NSAMP - 1] == NSAMP - 1,
              "muestras intactas");
        free(raw);
    }

    /* ventana fuera del archivo -> sin datos */
    raw = NULL; len = 0;
    rc = csnhypodbp_sds_fetch(sta, net, chan, loc, t0 + 3600.0, t0 + 3610.0, &raw, &len);
    CHECK(rc == 0 && raw == NULL, "ventana fuera -> sin datos");

    /* SDS deshabilitado -> sin datos */
    g_sds_enabled = 0;
    rc = csnhypodbp_sds_fetch(sta, net, chan, loc, t0, t1, &raw, &len);
    CHECK(rc == 0, "SDS deshabilitado -> sin datos");
    g_sds_enabled = 1;

    /* F1: el loc nulo de EarthWorm ("--") debe encontrar el archivo SDS escrito
       con loc vacio, y el header de salida conserva "--". */
    {
        const char *sta2 = "GO02";
        char path2[1024], dir2[1024];
        char *raw2 = NULL;
        long len2 = 0;
        int rc2;

        if (ewgui_sds_path(path2, sizeof path2, root, net, sta2, "", chan, t0) != 0) {
            printf("FAIL: no se pudo construir la ruta SDS (loc vacia)\n");
            return 1;
        }
        snprintf(dir2, sizeof dir2, "%s", path2);
        { char *s = strrchr(dir2, '/'); if (s) *s = '\0'; }
        mkdirs(dir2);

        msr = msr_init(NULL);
        if (!msr) { printf("FAIL: msr_init\n"); return 1; }
        snprintf(msr->network, sizeof msr->network, "%s", net);
        snprintf(msr->station, sizeof msr->station, "%s", sta2);
        snprintf(msr->location, sizeof msr->location, "%s", "");
        snprintf(msr->channel, sizeof msr->channel, "%s", chan);
        msr->starttime = (hptime_t)(MS_EPOCH2HPTIME(t0) + 0.5);
        msr->samprate = RATE;
        msr->datasamples = samples;
        msr->numsamples = NSAMP;
        msr->samplecnt = NSAMP;
        msr->sampletype = 'i';
        rc2 = msr_writemseed(msr, path2, 1, 4096, DE_STEIM2, 1, 0);
        msr->datasamples = NULL;
        msr->numsamples = 0;
        msr->samplecnt = 0;
        msr_free(&msr);
        if (rc2 < 0) {
            printf("FAIL: msr_writemseed rc=%d (%s)\n", rc2, path2);
            return 1;
        }

        rc2 = csnhypodbp_sds_fetch(sta2, net, chan, "--", t0, t1, &raw2, &len2);
        CHECK(rc2 == 1, "loc '--' encuentra el archivo con loc vacio (F1)");
        if (rc2 == 1) {
            TRACE2_HEADER *h2 = (TRACE2_HEADER *)raw2;
            CHECK(strcmp(h2->loc, "--") == 0, "header conserva loc '--'");
            free(raw2);
        }
    }

    /* F2: estacion/dia ausente -> rc=0 y SIN ruido de libmseed en stderr. */
    {
        char errf[] = "/tmp/opencode/sds_test_err.XXXXXX";
        int fd = mkstemp(errf);
        int saved = -1;
        long errsz = -1;

        raw = NULL; len = 0;
        if (fd >= 0) {
            fflush(stderr);
            saved = dup(2);
            dup2(fd, 2);
        }
        rc = csnhypodbp_sds_fetch("NOPE", net, chan, loc, t0, t1, &raw, &len);
        if (fd >= 0) {
            struct stat st;
            fflush(stderr);
            dup2(saved, 2);
            close(saved);
            if (fstat(fd, &st) == 0)
                errsz = (long)st.st_size;
            close(fd);
            unlink(errf);
        }
        CHECK(rc == 0 && raw == NULL, "estacion ausente -> sin datos");
        CHECK(errsz == 0, "estacion ausente -> stderr vacio (sin 'Cannot open file')");
    }

    /* F3: el ultimo registro de la ventana empieza antes de t1 y sigue mas
       alla. El buffer solo tiene cap = (t1-t0)*rate+2 muestras; `filled` debe
       quedar acotado a lo realmente copiado (antes se iba a off+rn y el
       memcpy de salida leia fuera del buffer). */
    {
        const char *sta3 = "GO03";
        char path3[1024], dir3[1024];
        char *raw3 = NULL;
        long len3 = 0;
        int rc3;
        const int long_n = 1000;
        int32_t long_samples[1000];

        for (i = 0; i < long_n; i++) long_samples[i] = 5000 + i;

        if (ewgui_sds_path(path3, sizeof path3, root, net, sta3, loc, chan,
                           t1 - 0.5) != 0) {
            printf("FAIL: no se pudo construir la ruta SDS (F3)\n");
            return 1;
        }
        snprintf(dir3, sizeof dir3, "%s", path3);
        { char *s = strrchr(dir3, '/'); if (s) *s = '\0'; }
        mkdirs(dir3);

        msr = msr_init(NULL);
        if (!msr) { printf("FAIL: msr_init\n"); return 1; }
        snprintf(msr->network, sizeof msr->network, "%s", net);
        snprintf(msr->station, sizeof msr->station, "%s", sta3);
        snprintf(msr->location, sizeof msr->location, "%s", loc);
        snprintf(msr->channel, sizeof msr->channel, "%s", chan);
        /* OJO: MS_EPOCH2HPTIME no parentiza su argumento; con `t1 - 0.5`
         * multiplicaria solo el 0.5 por 1e6. */
        msr->starttime = (hptime_t)(MS_EPOCH2HPTIME((t1 - 0.5)) + 0.5);
        msr->samprate = RATE;
        msr->datasamples = long_samples;
        msr->numsamples = long_n;
        msr->samplecnt = long_n;
        msr->sampletype = 'i';
        rc3 = msr_writemseed(msr, path3, 1, 4096, DE_STEIM2, 1, 0);
        msr->datasamples = NULL;
        msr->numsamples = 0;
        msr->samplecnt = 0;
        msr_free(&msr);
        if (rc3 < 0) {
            printf("FAIL: msr_writemseed rc=%d (%s)\n", rc3, path3);
            return 1;
        }

        rc3 = csnhypodbp_sds_fetch(sta3, net, chan, loc, t0, t1, &raw3, &len3);
        CHECK(rc3 == 1, "registro que cruza t1 -> datos (F3)");
        if (rc3 == 1) {
            TRACE2_HEADER *h3 = (TRACE2_HEADER *)raw3;
            int32_t *d3 = (int32_t *)(raw3 + sizeof(TRACE2_HEADER));
            const long cap = (long)((t1 - t0) * RATE) + 2;

            CHECK(h3->nsamp > 0 && (long)h3->nsamp <= cap,
                  "nsamp acotado a la ventana (sin leer fuera del buffer)");
            CHECK(len3 == (long)sizeof(TRACE2_HEADER) +
                       (long)h3->nsamp * (long)sizeof(int32_t),
                  "longitud coherente con nsamp");
            CHECK(h3->endtime <= t1 + 2.0 / RATE + 1e-6,
                  "endtime dentro del margen de 2 muestras");
            /* El buffer arranca en t0: el bloque que cruza t1 empieza en el
             * indice (t1 - 0.5 - t0) * RATE = 950, no en 0. */
            CHECK(d3[950] == long_samples[0] && d3[951] == long_samples[1],
                  "muestras del bloque largo intactas");
            free(raw3);
        }
    }

    /* Punto 2(a): predicado prefer_sds (evento antiguo -> SDS primero). */
    CHECK(csnhypodbp_sds_prefer(200.0, 1, "/mnt/sds", 168) == 1,
          "evento antiguo -> prefer_sds");
    CHECK(csnhypodbp_sds_prefer(100.0, 1, "/mnt/sds", 168) == 0,
          "evento reciente -> no prefer_sds");
    CHECK(csnhypodbp_sds_prefer(200.0, 0, "/mnt/sds", 168) == 0,
          "SDS deshabilitado -> no prefer_sds");
    CHECK(csnhypodbp_sds_prefer(200.0, 1, "", 168) == 0,
          "sin SdsRoot -> no prefer_sds");

    if (fails) {
        printf("FALLARON %d comprobaciones\n", fails);
        return 1;
    }
    printf("ALL SDS READ TESTS PASSED\n");
    return 0;
}
