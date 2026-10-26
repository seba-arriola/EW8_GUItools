/******************************************************************************
 * pickml_sds.c — lectura offline de un SDS de SeisComP (miniSEED).            *
 *                                                                            *
 * El SDS de SeisComP guarda un archivo por (NET, STA, LOC, CHAN) y dia UTC:   *
 *   <root>/<YYYY>/<NET>/<STA>/<CHAN>.D/<NET>.<STA>.<LOC>.<CHAN>.D.<YYYY>.<DDD>*
 *                                                                            *
 * Se usa el mismo camino que pickml_fetch para que un evento historico se    *
 * pueda picar sin convertirlo antes a trace:                                  *
 *   - dia ausente -> se salta (NO_DAY), no es error;                          *
 *   - tasa 1x/2x/4x -> se decima a Rate con un Butterworth de 2.o orden       *
 *     (fc = 0.35 * Rate) sobre la senal original, como el fetch;             *
 *   - location nulo -> vacio en la ruta (`NET.STA..CHAN`), "--" en la traza.  *
 ******************************************************************************/

#include "pickml_sds.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libmseed2.h>

#include "ewgui/sds.h"

/* Tramo maximo que se entrega al buffer. `pickml_main` trocea por capacidad,
 * pero `advance` se llama una vez por traza: entregar el dia entero perderia
 * ventanas. 1 s es ademas la cadencia del trace que escribe pickml_fetch. */
#define PMSDS_SLICE_SEC 1.0

/* --------------------------------------------------------------- auxiliares */

typedef struct {
    double b0, b1, b2, a1, a2, z1, z2;
} Biquad;

/* Butterworth de 2.o orden (Q = 1/sqrt(2)), RBJ lowpass. */
static void biquad_lp(Biquad *q, double fc, double fs)
{
    double w0, alpha, c, a0;

    memset(q, 0, sizeof(*q));
    if (!(fc > 0.0) || !(fs > 0.0)) return;

    w0    = 2.0 * M_PI * fc / fs;
    alpha = sin(w0) / (2.0 * (1.0 / sqrt(2.0)));
    c     = cos(w0);
    a0    = 1.0 + alpha;

    q->b0 = (1.0 - c) / 2.0 / a0;
    q->b1 = (1.0 - c) / a0;
    q->b2 = q->b0;
    q->a1 = (-2.0 * c) / a0;
    q->a2 = (1.0 - alpha) / a0;
}

static double biquad_run(Biquad *q, double x)
{
    double y = q->b0 * x + q->z1;

    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static double sample_at(const void *src, char type, long i)
{
    if (!src) return 0.0;
    switch (type) {
        case 'i': return (double)((const int32_t *)src)[i];
        case 'f': return (double)((const float *)src)[i];
        case 'd': return ((const double *)src)[i];
        default:  return 0.0;
    }
}

static int append_samples(int32_t **buf, long *n, long *cap,
                          const int32_t *x, long k)
{
    long need;

    if (k <= 0) return 0;
    need = *n + k;
    if (need > *cap) {
        long     nc = *cap ? *cap : 1024;
        int32_t *p;

        while (nc < need) nc *= 2;
        p = (int32_t *)realloc(*buf, (size_t)nc * sizeof(int32_t));
        if (!p) return -1;
        *buf = p;
        *cap = nc;
    }
    memcpy(*buf + *n, x, (size_t)k * sizeof(int32_t));
    *n += k;
    return 0;
}

/* ------------------------------------------------------------ un canal ----- */

/* Diagnóstico opcional: PICKML_FETCH_DEBUG (histórico) o PICKML_SDS_DEBUG. */
static int sds_debug(void)
{
    return getenv("PICKML_SDS_DEBUG") != NULL ||
           getenv("PICKML_FETCH_DEBUG") != NULL;
}

int pmsds_channel(const char *root, const char *net, const char *sta,
                  const char *loc, const char *chan,
                  double t0, double t1, double rate,
                  int32_t **out, long *n, double *start, PmSdsWhy *why)
{
    double day;
    long   cap = 0;
    int    saw_day = 0, saw_rate = 0;

    if (out) *out = NULL;
    if (n) *n = 0;
    if (start) *start = 0.0;
    if (!root || !root[0] || !net || !sta || !chan ||
        !(rate > 0.0) || t1 <= t0) {
        if (why) *why = PMSDS_IO;
        return PMSDS_IO;
    }

    for (day = floor(t0 / 86400.0) * 86400.0; day < t1; day += 86400.0) {
        char         path[1024];
        MSFileParam *msfp = NULL;
        MSRecord    *msr  = NULL;
        Biquad       lp;
        int          lp_on = 0;

        if (ewgui_sds_path(path, sizeof(path), root, net, sta, loc, chan,
                           day) != 0)
            continue;
        /* Dia ausente: el SDS simplemente no tiene ese archivo y libmseed
         * imprimiria "Cannot open file" en stderr. */
        if (access(path, R_OK) != 0) {
            if (sds_debug()) fprintf(stderr, "pickml: sds sin acceso %s\n", path);
            continue;
        }
        saw_day = 1;

        while (ms_readmsr_r(&msfp, &msr, path, 0, NULL, NULL, 1, 1, 0)
                   == MS_NOERROR) {
            double   a, b, fs;
            long     i0, i1, k0, k, j = 0, nout;
            int      dec;
            int32_t *tmp;

            if (!msr->datasamples || msr->numsamples <= 0 ||
                msr->samplecnt <= 0)
                continue;
            fs = msr->samprate;
            if (!(fs > 0.0)) continue;

            if (fabs(fs - rate) <= 0.01)            dec = 1;
            else if (fabs(fs - 2.0 * rate) <= 0.01) dec = 2;
            else if (fabs(fs - 4.0 * rate) <= 0.01) dec = 4;
            else {
                saw_rate = 1;
                if (sds_debug())
                    fprintf(stderr, "pickml: sds tasa %.3f no soportada (%s)\n",
                            fs, path);
                continue;
            }

            if (dec > 1 && !lp_on) {
                biquad_lp(&lp, 0.35 * rate, fs);
                lp_on = 1;
            }

            a = MS_HPTIME2EPOCH(msr_starttime(msr));
            b = a + (double)msr->numsamples / fs;
            if (b <= t0 || a >= t1) continue;

            i0 = (long)ceil((t0 - a) * fs);
            i1 = (long)floor((t1 - a) * fs);
            if (i0 < 0) i0 = 0;
            if (i0 > msr->numsamples) i0 = msr->numsamples;
            if (i1 < 0) i1 = 0;
            if (i1 > msr->numsamples) i1 = msr->numsamples;
            if (i1 <= i0) continue;

            /* Primera muestra que cae en la rejilla decimada. */
            k0   = i0 + ((dec - (i0 % dec)) % dec);
            nout = (i1 - k0 + dec - 1) / dec;
            if (nout <= 0) continue;

            tmp = (int32_t *)malloc((size_t)nout * sizeof(int32_t));
            if (!tmp) {
                ms_readmsr_r(&msfp, &msr, NULL, 0, NULL, NULL, 0, 0, 0);
                free(*out);
                *out = NULL;
                if (n) *n = 0;
                if (why) *why = PMSDS_IO;
                return PMSDS_IO;
            }

            for (k = k0; k < i1; k++) {
                double v = sample_at(msr->datasamples, msr->sampletype, k);

                if (dec > 1) v = biquad_run(&lp, v);
                if ((k - k0) % dec == 0) tmp[j++] = (int32_t)llround(v);
            }
            if (*n == 0 && j > 0) *start = a + (double)k0 / fs;
            if (append_samples(out, n, &cap, tmp, j) != 0) {
                free(tmp);
                ms_readmsr_r(&msfp, &msr, NULL, 0, NULL, NULL, 0, 0, 0);
                free(*out);
                *out = NULL;
                if (n) *n = 0;
                if (why) *why = PMSDS_IO;
                return PMSDS_IO;
            }
            free(tmp);
        }
        ms_readmsr_r(&msfp, &msr, NULL, 0, NULL, NULL, 0, 0, 0);
    }

    if (*n > 0) {
        if (why) *why = PMSDS_OK;
        return PMSDS_OK;
    }
    if (why) *why = saw_rate ? PMSDS_RATE
                             : (saw_day ? PMSDS_NODATA : PMSDS_NO_DAY);
    return why ? *why : PMSDS_NODATA;
}

/* Copia truncando al tamaño del destino (los campos de PmTrace son más cortos
 * que los de PmStation: NET 9 vs 16, LOC 3 vs 16). */
static void cpy_fld(char *dst, size_t n, const char *src)
{
    size_t len = 0;

    if (!dst || n == 0) return;
    if (src) while (len < n - 1 && src[len]) len++;
    if (len) memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ------------------------------------------------------------ backend ------ */

struct PmSds {
    const char      *root;
    const PmStation *sta;
    int              nsta;
    int              si;

    double           t0, t1, rate;
    double           day, day_end;
    int              started;

    long             off[PM_MAX_COMP];
    long             sn[PM_MAX_COMP];
    double           st0[PM_MAX_COMP];
    int32_t         *sx[PM_MAX_COMP];

    PmSrcCounts      cnt;
};

PmSds *pmsds_open(const char *root, const PmStation *sta, int nsta,
                  double t0, double t1, double rate)
{
    struct stat st;
    PmSds      *s;

    if (!root || !root[0] || !sta || nsta <= 0 || t1 <= t0 || !(rate > 0.0))
        return NULL;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;

    s = (PmSds *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->root  = root;
    s->sta   = sta;
    s->nsta  = nsta;
    s->t0    = t0;
    s->t1    = t1;
    s->rate  = rate;
    s->day   = floor(t0 / 86400.0) * 86400.0;
    s->day_end = s->day + 86400.0;
    return s;
}

static void sds_free_station(PmSds *s)
{
    int c;

    for (c = 0; c < PM_MAX_COMP; c++) {
        free(s->sx[c]);
        s->sx[c] = NULL;
        s->off[c] = 0;
        s->sn[c]  = 0;
        s->st0[c] = 0.0;
    }
    s->started = 0;
    s->day     = floor(s->t0 / 86400.0) * 86400.0;
    s->day_end = s->day + 86400.0;
}

/* Carga el dia actual de la estacion actual (las 3 componentes). */
static void sds_load_day(PmSds *s)
{
    const PmStation *st = &s->sta[s->si];
    double a = s->t0 > s->day ? s->t0 : s->day;
    double b = s->t1 < s->day_end ? s->t1 : s->day_end;
    int    c;

    for (c = 0; c < PM_MAX_COMP; c++) {
        int32_t  *x = NULL;
        long      n = 0;
        double    st0 = 0.0;
        PmSdsWhy  why = PMSDS_NODATA;

        if (pmsds_channel(s->root, st->net, st->sta, st->loc, st->chan[c],
                          a, b, s->rate, &x, &n, &st0, &why) != PMSDS_OK) {
            if (why == PMSDS_NO_DAY)      s->cnt.no_day++;
            else if (why == PMSDS_RATE)   s->cnt.rate++;
            else                          s->cnt.nodata++;
            free(x);
            continue;
        }
        free(s->sx[c]);
        s->sx[c]  = x;
        s->sn[c]  = n;
        s->st0[c] = st0;
        s->off[c] = 0;
    }
    s->started = 1;
}

int pmsds_next(PmSds *s, PmTrace *tr)
{
    const PmStation *st;

    if (!s || !tr) return PM_ERR;
    memset(tr, 0, sizeof(*tr));

    while (s->si < s->nsta) {
        int    best = -1, c;
        double bt = 0.0, k;

        if (!s->started) sds_load_day(s);

        /* Componente con la muestra mas temprana pendiente. */
        for (c = 0; c < PM_MAX_COMP; c++) {
            double t;
            if (s->off[c] >= s->sn[c]) continue;
            t = s->st0[c] + (double)s->off[c] / s->rate;
            if (best < 0 || t < bt) { best = c; bt = t; }
        }
        if (best < 0) {
            /* Dia agotado: siguiente dia o siguiente estacion. */
            s->day += 86400.0;
            s->day_end = s->day + 86400.0;
            s->started = 0;
            if (s->day >= s->t1) {
                s->si++;
                sds_free_station(s);
            }
            continue;
        }

        st = &s->sta[s->si];
        k  = s->rate * PMSDS_SLICE_SEC;
        if (k < 1.0) k = 1.0;
        if ((long)k > s->sn[best] - s->off[best])
            k = (double)(s->sn[best] - s->off[best]);

        cpy_fld(tr->sta, sizeof(tr->sta), st->sta);
        cpy_fld(tr->net, sizeof(tr->net), st->net);
        cpy_fld(tr->loc, sizeof(tr->loc), st->loc[0] ? st->loc : "--");
        cpy_fld(tr->chan, sizeof(tr->chan), st->chan[best]);
        tr->t0   = s->st0[best] + (double)s->off[best] / s->rate;
        tr->rate = s->rate;
        tr->n    = (long)k;
        tr->samples = s->sx[best] + s->off[best];
        s->off[best] += (long)k;
        return 1;
    }
    return 0;
}

void pmsds_close(PmSds *s)
{
    int c;

    if (!s) return;
    for (c = 0; c < PM_MAX_COMP; c++) free(s->sx[c]);
    free(s);
}

void pmsds_counts(const PmSds *s, PmSrcCounts *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    if (s) *c = s->cnt;
}
