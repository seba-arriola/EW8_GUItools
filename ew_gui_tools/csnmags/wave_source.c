#include "csnmags.h"

#include <string.h>

#include <wstore.h>
#include <libmseed2.h>
#include <ewgui/sds.h>

#include <math.h>
#include <time.h>
#include <unistd.h>

/*
 * wave_source.c - Abstracción de la fuente de onda.
 *
 *   WSRC_TANK  : lectura directa del tank (modo offline), patrón de pickS.
 *   WSRC_STORE : store indexado (wstore); si no tiene la traza, va al SDS.
 *
 * QUE fuentes se usan es CONFIGURACION (SdsEnabled/SdsRoot/SdsFallbackHours del
 * .d), no codigo: el store cubre lo reciente y el archivo SDS lo mas antiguo que
 * la retencion local.
 *
 * En offline se construye un índice (SCNL + offset) recorriendo el tank una sola
 * vez; wave_get lee sólo los paquetes del SCNL pedido, sin volver a barrer todo.
 */

static void scnl_copy(char *dst, size_t dsz, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, dsz, "%s", src);
}

/* ------------------------------ offline ------------------------------ */

static int tank_index(WaveSource *w)
{
    TRACE2_HEADER head;
    long offset;
    int  cap = 1024;

    w->idx = (MagTankIndex *)malloc((size_t)cap * sizeof(MagTankIndex));
    w->nidx = 0;
    if (!w->idx) return -1;

    rewind(w->tank);
    for (;;) {
        offset = ftell(w->tank);
        if (fread(&head, sizeof(TRACE2_HEADER), 1, w->tank) != 1) break;
        if (head.nsamp <= 0 || head.nsamp > MAG_MAX_TRACE) break;
        offset += (long)sizeof(TRACE2_HEADER);
        /* skip samples */
        if (fseek(w->tank, (long)head.nsamp * (long)sizeof(int32_t), SEEK_CUR) != 0) break;

        if (w->nidx >= cap) {
            MagTankIndex *ni;
            cap *= 2;
            ni = (MagTankIndex *)realloc(w->idx, (size_t)cap * sizeof(MagTankIndex));
            if (!ni) return -1;
            w->idx = ni;
        }
        {
            MagTankIndex *e = &w->idx[w->nidx++];
            scnl_copy(e->sta, sizeof(e->sta), head.sta);
            scnl_copy(e->net, sizeof(e->net), head.net);
            scnl_copy(e->chan, sizeof(e->chan), head.chan);
            scnl_copy(e->loc, sizeof(e->loc), head.loc);
            e->starttime = head.starttime;
            e->nsamp     = head.nsamp;
            e->offset    = offset;
            e->samprate  = head.samprate;
        }
    }
    return 0;
}

static int scnl_match(const char *a, const char *b)
{
    /* vacío en el filtro => comodín */
    if (!b || !b[0]) return 1;
    if (strcmp(b, "--") == 0 && (a[0] == '\0' || strcmp(a, "--") == 0)) return 1;
    return strcmp(a, b) == 0;
}

static int tank_get(WaveSource *w, const char *sta, const char *net,
                    const char *chan, const char *loc, double t0, double t1,
                    MagTrace *out)
{
    long   total = 0, i;
    int32_t *buf = NULL;
    double dt = 0.0;
    long   maxsamp = 0;

    out->x = NULL; out->n = 0; out->t0 = 0; out->dt = 0; out->samprate = 0;

    for (i = 0; i < w->nidx; i++) {
        MagTankIndex *e = &w->idx[i];
        double endt;
        if (!scnl_match(e->sta, sta)) continue;
        if (!scnl_match(e->net, net)) continue;
        if (!scnl_match(e->chan, chan)) continue;
        if (!scnl_match(e->loc, loc)) continue;
        endt = e->starttime + (double)(e->nsamp - 1) / e->samprate;
        if (endt < t0 || e->starttime > t1) continue;
        total += e->nsamp;
        if (e->nsamp > maxsamp) maxsamp = e->nsamp;
        if (dt == 0.0) dt = 1.0 / e->samprate;
    }
    if (total == 0) return -1;

    out->x = (double *)malloc((size_t)total * sizeof(double));
    if (!out->x) return -1;
    out->dt = dt; out->samprate = 1.0 / dt;

    buf = (int32_t *)malloc((size_t)maxsamp * sizeof(int32_t));
    if (!buf) { free(out->x); out->x = NULL; return -1; }

    out->n = 0;
    for (i = 0; i < w->nidx; i++) {
        MagTankIndex *e = &w->idx[i];
        double endt;
        long   j;
        if (!scnl_match(e->sta, sta)) continue;
        if (!scnl_match(e->net, net)) continue;
        if (!scnl_match(e->chan, chan)) continue;
        if (!scnl_match(e->loc, loc)) continue;
        endt = e->starttime + (double)(e->nsamp - 1) / e->samprate;
        if (endt < t0 || e->starttime > t1) continue;
        if (fseek(w->tank, e->offset, SEEK_SET) != 0) break;
        if (fread(buf, sizeof(int32_t), (size_t)e->nsamp, w->tank) != (size_t)e->nsamp) break;
        if (out->n == 0) out->t0 = e->starttime;
        for (j = 0; j < e->nsamp; j++) out->x[out->n++] = (double)buf[j];
    }
    free(buf);
    return (out->n > 0) ? 0 : -1;
}

/* ------------------------------ store ------------------------------ */

static int store_get(WaveSource *w, const char *sta, const char *net, const char *chan,
                     const char *loc, double t0, double t1, MagTrace *out)
{
    WStoreTrace st;

    out->x = NULL; out->n = 0; out->t0 = 0; out->dt = 0; out->samprate = 0;
    if (!w->store) return -1;
    memset(&st, 0, sizeof(st));
    if (wstore_get((WStore *)w->store, sta, net, chan, loc, t0, t1, &st) != 0) return -1;

    out->x = (double *)malloc((size_t)st.n * sizeof(double));
    if (!out->x) { wstore_trace_free(&st); return -1; }
    for (long i = 0; i < st.n; i++) out->x[i] = (double)st.samples[i];
    out->n = st.n;
    out->t0 = st.t0;
    out->samprate = st.samprate;
    out->dt = 1.0 / st.samprate;
    wstore_trace_free(&st);
    return 0;
}

/* ------------------------------ SDS ------------------------------ */

#define SDS_DAY_SECONDS 86400.0

static void sds_close(MSFileParam **msfp, MSRecord **msr)
{
    if (*msfp || *msr)
        ms_readmsr_r(msfp, msr, NULL, 0, NULL, NULL, 0, 0, 0);
    *msr = NULL;
}

/* Lo mas antiguo que SdsFallbackHours se pide al SDS (mismo criterio que
 * csnhypodbp_sds_prefer). Sin SdsRoot o con horas 0, el SDS no se usa. */
static int sds_prefer(WaveSource *w, double t0)
{
    double age_hours;

    if (!w->sds_enabled || !w->sds_root[0] || w->sds_fallback_hours <= 0)
        return 0;
    age_hours = ((double)time(NULL) - t0) / 3600.0;
    return age_hours > (double)w->sds_fallback_hours;
}

/* Lee la ventana [t0,t1] del archivo SDS (layout SeisComP) con libmseed2 y la
 * convierte a MagTrace. Devuelve 0 con datos, -1 si no hay (dia ausente o
 * ventana fuera): el llamante decide la siguiente fuente. */
static int sds_get(WaveSource *w, const char *sta, const char *net, const char *chan,
                   const char *loc, double t0, double t1, MagTrace *out)
{
    MSFileParam *msfp = NULL;
    MSRecord *msr = NULL;
    char path[MAG_STR * 4];
    double rate = 0.0, day;
    long cap = 0, filled = 0, i;
    double *samples = NULL;

    out->x = NULL; out->n = 0; out->t0 = 0; out->dt = 0; out->samprate = 0;
    if (!w->sds_enabled || !w->sds_root[0] || t1 <= t0)
        return -1;

    for (day = floor(t0 / SDS_DAY_SECONDS) * SDS_DAY_SECONDS;
         day <= t1; day += SDS_DAY_SECONDS) {

        if (ewgui_sds_path(path, sizeof path, w->sds_root, net, sta,
                           loc ? loc : "", chan, day) != 0)
            continue;
        /* Dia ausente: no se abre el fichero (evita el "Cannot open file" de
         * libmseed2 en estaciones sin dato). */
        if (access(path, R_OK) != 0)
            continue;

        while (ms_readmsr_r(&msfp, &msr, path, 0, NULL, NULL, 1, 1, 0) == MS_NOERROR) {
            double rstart, rend, rsrate;
            long rn;

            if (!msr || !msr->datasamples || msr->samplecnt <= 0)
                continue;

            rstart = MS_HPTIME2EPOCH(msr_starttime(msr));
            if (rstart > t1)
                break;              /* registros en orden: fin de la ventana */
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
                samples = calloc((size_t)cap, sizeof(double));
                if (!samples) {
                    sds_close(&msfp, &msr);
                    return -1;
                }
            }
            if (fabs(rsrate - rate) > 1e-6)
                continue;           /* tasa distinta: se ignora el registro */

            {
                long off = lround((rstart - t0) * rate);
                for (i = 0; i < rn; i++) {
                    long k = off + i;
                    if (k < 0 || k >= cap)
                        continue;
                    if (msr->sampletype == 'f')
                        samples[k] = (double)((float *)msr->datasamples)[i];
                    else if (msr->sampletype == 'd')
                        samples[k] = ((double *)msr->datasamples)[i];
                    else
                        samples[k] = (double)((int32_t *)msr->datasamples)[i];
                }
                if (off + rn > filled)
                    filled = off + rn;
            }
        }
        sds_close(&msfp, &msr);
    }

    if (!samples || rate <= 0.0 || filled <= 0) {
        free(samples);
        return -1;
    }
    out->x = samples;
    out->n = filled;
    out->t0 = t0;
    out->samprate = rate;
    out->dt = 1.0 / rate;
    return 0;
}

/* ------------------------------ API ------------------------------ */

int wave_open(WaveSource *w, const MagConfig *cfg, const char *tank_or_null)
{
    memset(w, 0, sizeof(*w));
    w->sds_enabled = cfg->sds_enabled;
    w->sds_fallback_hours = cfg->sds_fallback_hours;
    scnl_copy(w->sds_root, sizeof(w->sds_root), cfg->sds_root);

    if (!tank_or_null && cfg->store_root[0]) {
        /* store + SDS segun la ventana. */
        w->mode = WSRC_STORE;
        if (wstore_open((WStore **)&w->store, cfg->store_root, NULL) != 0) return -1;
        return 0;
    }

    if (tank_or_null) {
        w->mode = WSRC_TANK;
        w->tank = fopen(tank_or_null, "rb");
        if (!w->tank) { perror(tank_or_null); return -1; }
        if (tank_index(w) != 0) { fclose(w->tank); w->tank = NULL; return -1; }
        return 0;
    }

    /* Sin tank y sin store no hay fuente de ondas. */
    return -1;
}

void wave_close(WaveSource *w)
{
    if (!w) return;
    if (w->mode == WSRC_TANK) {
        if (w->tank) fclose(w->tank);
    } else if (w->mode == WSRC_STORE) {
        if (w->store) wstore_close((WStore *)w->store);
    }
    free(w->idx);
    memset(w, 0, sizeof(*w));
}

int wave_get(WaveSource *w, const char *sta, const char *net, const char *chan,
             const char *loc, double t0, double t1, MagTrace *out)
{
    if (!w) return -1;
    if (w->mode == WSRC_TANK) return tank_get(w, sta, net, chan, loc, t0, t1, out);
    if (w->mode == WSRC_STORE) {
        /* Politica de fuentes (toda por .d): lo reciente del store y lo mas
         * antiguo que SdsFallbackHours del archivo SDS. */
        int pref = sds_prefer(w, t0);
        if (pref && sds_get(w, sta, net, chan, loc, t0, t1, out) == 0) return 0;
        if (store_get(w, sta, net, chan, loc, t0, t1, out) == 0) return 0;
        if (!pref && sds_get(w, sta, net, chan, loc, t0, t1, out) == 0) return 0;
    }
    return -1;
}
