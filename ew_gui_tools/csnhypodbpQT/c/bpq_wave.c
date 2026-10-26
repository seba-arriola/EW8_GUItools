/*
 * bpq_wave.c - Capa de ondas del prototipo: store indexado y SDS.
 *
 * Port del camino de csnhypodbp_ws.c SIN sus globales ni GTK (ese archivo no es
 * puro: incluye csnhypodbp.h, que arrastra gtk/adwaita). El recorrido es el
 * mismo:
 *
 *   1. csnhypodbp_sds_prefer(edad, SdsEnabled, SdsRoot, SdsFallbackHours) decide
 *      si el evento ya es mas viejo que la retencion del store.
 *   2. Si no lo es: wstore_get() -> WStoreTrace (int32 ya en memoria).
 *   3. Si no hay datos (o el evento es viejo): csnhypodbp_sds_fetch() -> stream
 *      TRACE2, que se coloca sobre la grilla con bpq_trace2_decode().
 *
 * El contenedor es EwGuiTrace (common, puro): aporta finish (interpola huecos
 * cortos y recorta la cola), longitud y tasa, que es lo que necesita el panel de
 * picking de la fase 4.
 */
#include "bpq_capi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "trace_buf.h"
#include "wstore.h"

#include "csnhypodbp_sds.h"
#include "ewgui/stalta.h"
#include "ewgui/wave.h"

struct BpqWave {
    WStore *store;
};

struct BpqTrace {
    EwGuiTrace *tr;
};

/* --- decodificacion TRACE2 ------------------------------------------------ */

long bpq_trace2_decode(const char *buf, long len, double t0,
                       double *rate_inout, int32_t *out, long cap)
{
    const char *ptr, *end;
    double rate = (rate_inout && *rate_inout > 0.0) ? *rate_inout : 0.0;
    long used = 0;

    if (!buf || len < (long)sizeof(TRACE2_HEADER) || !out || cap <= 0) return 0;
    ptr = buf;
    end = buf + len;

    while (ptr + (long)sizeof(TRACE2_HEADER) <= end) {
        const TRACE2_HEADER *trh = (const TRACE2_HEADER *)ptr;
        const char *dptr;
        int dsize, s;

        if (trh->nsamp < 0 || trh->nsamp > 500000 || trh->samprate <= 0.0) break;
        if (rate <= 0.0) rate = trh->samprate;
        dsize = (trh->datatype[1] == '2') ? 2 : 4;
        dptr = ptr + sizeof(TRACE2_HEADER);
        if (dptr + (long)trh->nsamp * dsize > end) break;

        for (s = 0; s < trh->nsamp; s++) {
            double x = (dsize == 4)
                           ? (double)*(const int32_t *)(dptr + (long)s * 4)
                           : (double)*(const int16_t *)(dptr + (long)s * 2);
            double ts = trh->starttime + ((rate > 0.0) ? (double)s / rate : 0.0);
            long k = (long)((ts - t0) * rate + 0.5);

            if (k >= 0 && k < cap) {
                out[k] = (int32_t)x;
                if (k + 1 > used) used = k + 1;
            }
        }
        ptr += sizeof(TRACE2_HEADER) + (long)trh->nsamp * dsize;
    }

    if (rate_inout) *rate_inout = rate;
    return used;
}

/* --- ciclo de vida -------------------------------------------------------- */

BpqWave *bpq_wave_open(const BpqConfig *cfg, char *err, size_t errn)
{
    BpqWave *w;
    WStoreOpts opts;

    if (err && errn) err[0] = '\0';
    w = (BpqWave *)calloc(1, sizeof(*w));
    if (!w) return NULL;

    /* Sin StoreRoot queda solo el SDS (y si tampoco hay, fetch dira "sin datos"). */
    if (!cfg || !cfg->store_root[0]) return w;

    memset(&opts, 0, sizeof(opts));
    if (wstore_open(&w->store, cfg->store_root, &opts) != 0) {
        w->store = NULL;
        if (err)
            snprintf(err, errn, "no se pudo abrir el store <%s>", cfg->store_root);
    }
    return w;
}

void bpq_wave_close(BpqWave *w)
{
    if (!w) return;
    if (w->store) wstore_close(w->store);
    free(w);
}

/* --- lectura de una ventana ----------------------------------------------- */

BpqTrace *bpq_wave_fetch(BpqWave *w, const BpqConfig *cfg,
                         const char *sta, const char *net, const char *chan,
                         const char *loc, double t0, double seconds,
                         char *err, size_t errn)
{
    BpqTrace *out = NULL;
    EwGuiTrace *tr = NULL;
    char *raw = NULL;
    long len = 0;
    int got = 0, prefer_sds = 0;
    double t1, age_h;

    if (err && errn) err[0] = '\0';
    if (!cfg || !sta || !sta[0] || !isfinite(t0) || !(seconds > 0.0)) {
        if (err) snprintf(err, errn, "peticion invalida");
        return NULL;
    }
    t1 = t0 + seconds;
    age_h = ((double)time(NULL) - t0) / 3600.0;
    prefer_sds = csnhypodbp_sds_prefer(age_h, cfg->sds_enabled, cfg->sds_root,
                                       cfg->sds_fallback_hours);

    /* 1. store, salvo que el evento sea mas viejo que la retencion. */
    if (w && w->store && !prefer_sds) {
        WStoreTrace st;

        memset(&st, 0, sizeof(st));
        if (wstore_get(w->store, sta, net, chan, loc, t0, t1, &st) == 0 && st.n > 0) {
            tr = ewgui_trace_new(st.n);
            if (tr) {
                int32_t *dst = ewgui_trace_raw(tr);
                const long cap = ewgui_trace_capacity(tr);
                const long n = (st.n < cap) ? st.n : cap;

                if (dst && n > 0) {
                    memcpy(dst, st.samples, (size_t)n * sizeof(int32_t));
                    ewgui_trace_set_span(tr, st.t0, st.samprate, n);
                    got = 1;
                }
            }
            wstore_trace_free(&st);
        }
    }

    /* 2. SDS: primero si el evento es viejo, o como respaldo del store. */
    if (!got && cfg->sds_enabled && cfg->sds_root[0]) {
        if (csnhypodbp_sds_fetch(sta, net, chan, loc, t0, t1, &raw, &len) > 0 &&
            raw && len > 0) {
            /* Techo de 200 Hz: el decode recorta solo contra la capacidad. */
            const long cap = (long)((seconds + 2.0) * 200.0) + 64;
            double rate = 0.0;
            long n;

            if (!tr) tr = ewgui_trace_new(cap);
            if (tr) {
                int32_t *dst = ewgui_trace_raw(tr);
                n = bpq_trace2_decode(raw, len, t0, &rate, dst,
                                      ewgui_trace_capacity(tr));
                if (n > 0 && rate > 0.0) {
                    ewgui_trace_set_span(tr, t0, rate, n);
                    got = 1;
                }
            }
        }
        if (raw) free(raw);
    }

    if (!got) {
        if (tr) ewgui_trace_free(tr);
        if (err)
            snprintf(err, errn, "sin datos %s.%s.%s.%s [%.0f..%.0f]",
                     sta, chan ? chan : "?", net ? net : "?",
                     (loc && loc[0]) ? loc : "--", t0, t1);
        return NULL;
    }

    ewgui_trace_finish(tr);
    if (ewgui_trace_length(tr) <= 0) {
        ewgui_trace_free(tr);
        if (err) snprintf(err, errn, "traza vacia tras finish");
        return NULL;
    }
    out = (BpqTrace *)calloc(1, sizeof(*out));
    if (!out) {
        ewgui_trace_free(tr);
        return NULL;
    }
    out->tr = tr;
    return out;
}

/* --- consulta ------------------------------------------------------------- */

void bpq_trace_free(BpqTrace *t)
{
    if (!t) return;
    if (t->tr) ewgui_trace_free(t->tr);
    free(t);
}

int bpq_trace_clone(const BpqTrace *src, BpqTrace **out)
{
    BpqTrace *dst;
    EwGuiTrace *tr;
    const int32_t *raw;
    int32_t *dstp;
    long cap, n;

    if (out) *out = NULL;
    if (!src || !src->tr || !out) return 0;
    cap = ewgui_trace_capacity(src->tr);
    n = ewgui_trace_length(src->tr);
    raw = ewgui_trace_raw(src->tr);      /* el getter no es const */
    if (cap <= 0 || n <= 0 || !raw) return 0;

    tr = ewgui_trace_new(cap);
    if (!tr) return 0;
    dstp = ewgui_trace_raw(tr);
    if (!dstp) {
        ewgui_trace_free(tr);
        return 0;
    }
    memcpy(dstp, raw, (size_t)n * sizeof(int32_t));
    /* `filt` nace vacio (ewgui_trace_new lo llena de INT_MAX): el que dibuja
     * aplica su filtro de vista al recibir la copia. */
    ewgui_trace_set_span(tr, ewgui_trace_oldest(src->tr),
                         ewgui_trace_rate(src->tr), n);

    dst = (BpqTrace *)calloc(1, sizeof(*dst));
    if (!dst) {
        ewgui_trace_free(tr);
        return 0;
    }
    dst->tr = tr;
    *out = dst;
    return 1;
}

size_t bpq_trace_bytes(const BpqTrace *t)
{
    long cap;

    if (!t || !t->tr) return 0;
    cap = ewgui_trace_capacity(t->tr);
    if (cap < 0) cap = 0;
    /* raw + filt (lo que asigna ewgui_trace_new, ewgui_wave.c:21) mas una
     * holgura fija por el struct EwGuiTrace, que es OPACO fuera de common. */
    return (size_t)cap * 2u * sizeof(int32_t) + 64u;
}

long bpq_trace_n(const BpqTrace *t)
{
    return (t && t->tr) ? ewgui_trace_length(t->tr) : 0;
}

double bpq_trace_rate(const BpqTrace *t)
{
    return (t && t->tr) ? ewgui_trace_rate(t->tr) : 0.0;
}

double bpq_trace_t0(const BpqTrace *t)
{
    return (t && t->tr) ? ewgui_trace_oldest(t->tr) : 0.0;
}

BpqTrace *bpq_trace_new(double t0, double rate, const int32_t *samples, long n)
{
    BpqTrace *out;
    EwGuiTrace *tr;

    if (!samples || !(n > 0) || !(rate > 0.0) || !isfinite(t0)) return NULL;
    tr = ewgui_trace_new(n);
    if (!tr) return NULL;
    {
        int32_t *dst = ewgui_trace_raw(tr);
        const long cap = ewgui_trace_capacity(tr);

        if (!dst || cap < n) {
            ewgui_trace_free(tr);
            return NULL;
        }
        memcpy(dst, samples, (size_t)n * sizeof(int32_t));
        ewgui_trace_set_span(tr, t0, rate, n);
    }
    out = (BpqTrace *)calloc(1, sizeof(*out));
    if (!out) {
        ewgui_trace_free(tr);
        return NULL;
    }
    out->tr = tr;
    return out;
}

int bpq_trace_data(const BpqTrace *t, const int32_t **samples, long *n,
                   double *t0, double *rate)
{
    if (!t || !t->tr || !samples) return 0;
    *samples = ewgui_trace_raw((EwGuiTrace *)t->tr);
    if (n) *n = ewgui_trace_length(t->tr);
    if (t0) *t0 = ewgui_trace_oldest(t->tr);
    if (rate) *rate = ewgui_trace_rate(t->tr);
    return (*samples != NULL);
}

int bpq_trace_range(const BpqTrace *t, double *lo, double *hi)
{
    const int32_t *raw;
    double mn, mx;
    long n, i;

    if (!t || !t->tr || !lo || !hi) return 0;
    n = ewgui_trace_length(t->tr);
    raw = ewgui_trace_raw((EwGuiTrace *)t->tr);   /* el getter no es const */
    if (!raw || n <= 0) return 0;

    mn = mx = (double)raw[0];
    for (i = 1; i < n; i++) {
        const double v = (double)raw[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    *lo = mn;
    *hi = mx;
    return 1;
}

/* --- filtro de vista y STA/LTA ------------------------------------------- */

int bpq_trace_filter(BpqTrace *t, int type, double f1, double f2, int order,
                     int zerophase)
{
    EwFilterParams p;

    if (!t || !t->tr) return -1;
    memset(&p, 0, sizeof(p));
    p.filter_type = type;
    p.f1 = f1;
    p.f2 = f2;
    p.order = order;
    p.zerophase = zerophase;
    ewgui_trace_filter(t->tr, &p);
    return 0;
}

const int32_t *bpq_trace_filtered(const BpqTrace *t, long *n)
{
    if (!t || !t->tr) return NULL;
    if (n) *n = ewgui_trace_length(t->tr);
    return ewgui_trace_filtered((EwGuiTrace *)t->tr);
}

/* La traza cruda que piden los nucleos que procesan muestras (magview). */
void *bpq_trace_ew(const BpqTrace *t)
{
    return (t && t->tr) ? (void *)t->tr : NULL;
}

/* Trace de csnmags/refinador: muestras CRUDAS (nunca el buffer filtrado) y el
 * SCNL de la fila. Apunta al buffer de la traza, que debe vivir hasta que
 * termine la corrida. */
int bpq_trace_copy(const BpqTrace *t, const char *sta, const char *net,
                   const char *chan, const char *loc,
                   CsnhypodbpTrace *out)
{
    const int32_t *s = NULL;
    double t0 = 0.0, rate = 0.0;
    long n = 0;

    if (!out) return 0;
    if (!t || !bpq_trace_data(t, &s, &n, &t0, &rate)) return 0;
    if (!s || n <= 0 || !(rate > 0.0)) return 0;

    memset(out, 0, sizeof(*out));
    snprintf(out->sta, sizeof(out->sta), "%s", sta ? sta : "");
    snprintf(out->net, sizeof(out->net), "%s", net ? net : "");
    snprintf(out->chan, sizeof(out->chan), "%s", chan ? chan : "");
    snprintf(out->loc, sizeof(out->loc), "%s", loc ? loc : "");
    out->rate = rate;
    out->oldest = t0;
    out->samples = s;
    out->nsamp = n;
    return 1;
}

struct BpqStaLta {
    double *cf;
    long    n;
    double  trigger_t;
    double  peak;
};

BpqStaLta *bpq_stalta_new(const BpqTrace *t, double sta_s, double lta_s,
                          double on_thr, double off_thr, double corr_s)
{
    const int32_t *x = NULL;
    double t0 = 0.0, fs = 0.0;
    BpqStaLta *s;
    size_t idx[8];
    long n = 0, i;

    if (!bpq_trace_data(t, &x, &n, &t0, &fs) || !x || n <= 0) return NULL;
    if (!(fs > 0.0) || !(sta_s > 0.0) || !(lta_s >= sta_s)) return NULL;

    /* CF sobre la traza DIBUJADA (filtrada, fase cero), como el GTK
     * (csnhypodbp_gui.c:3913-3960): si se calcula sobre el crudo, un drift de
     * baja frecuencia domina la LTA y el disparo se va respecto al modulo GTK.
     * El buffer filtrado nace en INT_MAX; se usa el crudo solo si la vista no
     * llego a filtrar (p.ej. un test que llame aqui sin `bpq_trace_filter`). */
    {
        const int32_t *xf = bpq_trace_filtered(t, NULL);
        long k;

        if (xf)
            for (k = 0; k < n; k++)
                if (xf[k] < INT32_MAX - 1) {
                    x = xf;
                    break;
                }
    }

    s = (BpqStaLta *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->cf = (double *)calloc((size_t)n, sizeof(double));
    if (!s->cf) {
        free(s);
        return NULL;
    }
    s->n = n;

    ewgui_stalta_run(x, (size_t)n, fs, sta_s, lta_s, s->cf);
    for (i = 0; i < n; i++)
        if (s->cf[i] > s->peak) s->peak = s->cf[i];

    if (ewgui_stalta_triggers(s->cf, (size_t)n, on_thr, off_thr, idx, 8) > 0) {
        /* el cruce del umbral llega DESPUES del arribo: el llamador pasa la
         * correccion estatica (en el .d, -sta/2) */
        s->trigger_t = t0 + (double)idx[0] / fs + corr_s;
        if (s->trigger_t < t0) s->trigger_t = t0;
    }
    return s;
}

void bpq_stalta_free(BpqStaLta *s)
{
    if (!s) return;
    free(s->cf);
    free(s);
}

const double *bpq_stalta_cf(const BpqStaLta *s, long *n)
{
    if (!s) return NULL;
    if (n) *n = s->n;
    return s->cf;
}

double bpq_stalta_trigger_t(const BpqStaLta *s)
{
    return s ? s->trigger_t : 0.0;
}

double bpq_stalta_peak(const BpqStaLta *s)
{
    return s ? s->peak : 0.0;
}

/* --- diagnostico ---------------------------------------------------------- */

int bpq_wave_probe(BpqWave *w, const BpqConfig *cfg, const char *spec,
                   char *out, size_t outn)
{
    char buf[256], *tok, *save = NULL;
    char *f[6];
    int nf = 0;
    double t0, secs;
    BpqTrace *t;
    char err[256] = "";

    if (out && outn) out[0] = '\0';
    if (!spec || !spec[0] || !out || outn == 0) return -1;

    snprintf(buf, sizeof(buf), "%s", spec);
    for (tok = strtok_r(buf, ",", &save); tok && nf < 6;
         tok = strtok_r(NULL, ",", &save))
        f[nf++] = tok;
    if (nf < 6) {
        snprintf(out, outn, "probe: se esperan 6 campos STA,NET,CHAN,LOC,t0,secs");
        return -1;
    }
    t0 = strtod(f[4], NULL);
    secs = strtod(f[5], NULL);

    t = bpq_wave_fetch(w, cfg, f[0], f[1], f[2], f[3], t0, secs, err, sizeof(err));
    if (!t) {
        snprintf(out, outn, "wave %s.%s.%s.%s: %s", f[0], f[2], f[1], f[3], err);
        return -1;
    }
    {
        double lo = 0.0, hi = 0.0;
        (void)bpq_trace_range(t, &lo, &hi);
        snprintf(out, outn,
                 "wave %s.%s.%s.%s n=%ld rate=%.2f t0=%.3f rango=[%.0f,%.0f]",
                 f[0], f[2], f[1], f[3], bpq_trace_n(t), bpq_trace_rate(t),
                 bpq_trace_t0(t), lo, hi);
    }
    bpq_trace_free(t);
    return 0;
}
