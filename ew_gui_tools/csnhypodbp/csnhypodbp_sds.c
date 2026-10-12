/*
 * Lector de archivos miniSEED en layout SDS de SeisComP (un canal por dia).
 * Usa libmseed2 (vendorizada en EarthWorm). Nucleo de E/S, sin GTK; el worker
 * de csnhypodbp lo invoca desde el hilo de fetch (ver ws_fetch_thread).
 *
 * El buffer de salida es `TRACE2_HEADER + int32` (datatype "i4"), igual que
 * `store_fetch_sta`; `ws_fetch_finish` lo parsea sin distinguir el origen.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libmseed2.h>
#include <trace_buf.h>

#include "ewgui/sds.h"
#include "csnhypodbp_sds.h"

/* Definidas en csnhypodbp_main.c (config del modulo). */
extern int  g_sds_enabled;
extern char g_sds_root[512];

#define SDS_DAY_SECONDS 86400.0

/* Vacia/cierra el archivo abierto (la limpieza de libmseed2). */
static void sds_close(MSFileParam **msfp, MSRecord **msr)
{
    if (*msfp)
        ms_readmsr_r(msfp, msr, NULL, 0, NULL, NULL, 0, 0, 0);
}

/* 1 si el evento es tan antiguo que la retencion local (store/wave_serverV) ya
 * no lo tiene: conviene consultar el SDS primero y omitir wave_serverV. */
int csnhypodbp_sds_prefer(double age_hours, int enabled, const char *root,
                          int fallback_hours)
{
    return enabled && root && root[0] && age_hours > (double)fallback_hours;
}

int csnhypodbp_sds_fetch(const char *sta, const char *net, const char *chan,
                         const char *loc, double t0, double t1,
                         char **raw, long *len)
{
    MSFileParam *msfp = NULL;
    MSRecord *msr = NULL;
    char path[1024];
    double rate = 0.0;
    long cap = 0, filled = 0;
    int32_t *samples = NULL;
    double day;

    if (!raw || !len || !sta || !net || !chan)
        return -1;
    *raw = NULL;
    *len = 0;

    if (!g_sds_enabled || !g_sds_root[0] || t1 <= t0)
        return 0;

    for (day = floor(t0 / SDS_DAY_SECONDS) * SDS_DAY_SECONDS;
         day <= t1; day += SDS_DAY_SECONDS) {

        if (ewgui_sds_path(path, sizeof path, g_sds_root, net, sta,
                           loc ? loc : "", chan, day) != 0)
            continue;

        /* Si el dia no esta en el SDS no se abre: evita el "Cannot open file"
           que libmseed2 imprime a stderr (caso normal en estaciones sin dato). */
        if (access(path, R_OK) != 0)
            continue;

        while (ms_readmsr_r(&msfp, &msr, path, 0, NULL, NULL, 1, 1, 0) == MS_NOERROR) {
            double rstart, rend, rsrate;
            long rn, i;

            if (!msr || !msr->datasamples || msr->samplecnt <= 0)
                continue;

            rstart = MS_HPTIME2EPOCH(msr_starttime(msr));
            if (rstart > t1)
                break;          /* registros del dia en orden: fin de la ventana */
            rsrate = msr->samprate;
            rn = (long)msr->samplecnt;
            if (rsrate <= 0.0)
                continue;

            rend = rstart + (double)(rn - 1) / rsrate;
            if (rend < t0 || rstart > t1)
                continue;

            if (rate <= 0.0) {
                rate = rsrate;
                cap = (long)((t1 - t0) * rate) + 2;
                if (cap <= 0)
                    cap = 1;
                samples = calloc((size_t)cap, sizeof(int32_t));
                if (!samples) {
                    sds_close(&msfp, &msr);
                    return -1;
                }
            }
            if (fabs(rsrate - rate) > 1e-6)
                continue;   /* tasa distinta: se ignora el registro */

            {
                long off = lround((rstart - t0) * rate);
                for (i = 0; i < rn; i++) {
                    long k = off + i;
                    if (k < 0 || k >= cap)
                        continue;
                    if (msr->sampletype == 'f')
                        samples[k] = (int32_t)lround(((float *)msr->datasamples)[i]);
                    else if (msr->sampletype == 'd')
                        samples[k] = (int32_t)lround(((double *)msr->datasamples)[i]);
                    else
                        samples[k] = ((int32_t *)msr->datasamples)[i];
                }
                if (off + rn > filled)
                    filled = off + rn;
            }
        }
        sds_close(&msfp, &msr);
    }

    if (!samples || rate <= 0.0 || filled <= 0) {
        free(samples);
        return 0;
    }

    {
        long total = (long)sizeof(TRACE2_HEADER) + filled * (long)sizeof(int32_t);
        char *buf = malloc((size_t)total);
        TRACE2_HEADER *h;

        if (!buf) {
            free(samples);
            return -1;
        }
        memset(buf, 0, (size_t)total);
        h = (TRACE2_HEADER *)buf;
        snprintf(h->sta, sizeof h->sta, "%s", sta);
        snprintf(h->net, sizeof h->net, "%s", net);
        snprintf(h->chan, sizeof h->chan, "%s", chan);
        snprintf(h->loc, sizeof h->loc, "%s", loc ? loc : "");
        h->starttime = t0;
        h->samprate = rate;
        h->nsamp = (int)filled;
        h->endtime = t0 + (double)(filled - 1) / rate;
        memcpy(h->datatype, "i4", 3);
        memcpy(buf + sizeof(TRACE2_HEADER), samples,
               (size_t)filled * sizeof(int32_t));
        free(samples);

        *raw = buf;
        *len = total;
        return 1;
    }
}
