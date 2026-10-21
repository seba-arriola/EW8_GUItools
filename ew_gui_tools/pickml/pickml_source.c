/******************************************************************************
 * pickml_source.c — fuentes de trazas: un tank suelto o el store indexado.   *
 *                                                                            *
 * El store NO se modifica: se lee su formato documentado                     *
 *   <root>/<NET>.<STA>.<LOC>.<CHAN>/<YYYYMMDD>.tank                          *
 * con registros TRACE2_HEADER (64 B) + muestras int32. Se recorre por         *
 * estación y por día, con un lookahead por componente para entrelazar Z/N/E   *
 * por tiempo (sin eso, la ventana 3C nunca estaría completa).                *
 *                                                                            *
 * Diferencias deliberadas con wstore_get: aquí NO hay "huecos = 0"; cada      *
 * registro aporta su starttime y el buffer marca lo que falta (ver            *
 * pickml_buffer.c).                                                          *
 ******************************************************************************/

#include "pickml.h"
#include "pickml_sds.h"
#include <errno.h>

#define PM_MAX_SAMP 200000

struct PmSrc {
    int  kind;
    char err[PM_STR_LEN];

    /* ---- tank ---- */
    FILE         *f;
    int32_t      *x;
    long          cap;

    /* ---- store ---- */
    const char   *root;
    const PmStation *sta;
    int           nsta;
    double        t0, t1;
    int           si;
    long          day;
    long          day_end;
    FILE         *sf[PM_MAX_COMP];
    TRACE2_HEADER sh[PM_MAX_COMP];
    int32_t      *sx[PM_MAX_COMP];
    long          scap[PM_MAX_COMP];
    int           shave[PM_MAX_COMP];
    int           seof[PM_MAX_COMP];
    int           sopen;

    void         *sds;           /* backend SDS (ver pickml_sds.h) */
};

static void copy_fld(char *dst, size_t n, const char *src, size_t maxsrc)
{
    size_t len = 0;
    if (!dst || n == 0) return;
    while (len < maxsrc && src[len]) len++;
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Copia truncando al tamaño del destino (campos fijos del TRACE2_HEADER). */
static void set_fld(char *dst, size_t n, const char *src)
{
    size_t len = 0;
    if (!dst || n == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    while (len + 1 < n && src[len]) len++;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void day_ymd(long day, char out[9])
{
    time_t    t = (time_t)day;
    struct tm tm;
    gmtime_r(&t, &tm);
    if (strftime(out, 9, "%Y%m%d", &tm) == 0) out[0] = '\0';
}

/* Lee un registro (cabecera + muestras) y lo deja en *buf como int32.
 * Devuelve 1 si hay registro, 0 en EOF/truncado, <0 en error de formato. */
static int read_record(FILE *f, TRACE2_HEADER *hd, int32_t **buf, long *cap,
                       char *err, size_t errn)
{
    size_t  sz;
    size_t  got;

    if (!f || !hd) return 0;
    if (fread(hd, sizeof(TRACE2_HEADER), 1, f) != 1) return 0;

    if (hd->nsamp <= 0 || hd->nsamp > PM_MAX_SAMP) {
        if (err) snprintf(err, errn, "nsamp invalido (%d)", hd->nsamp);
        return -1;
    }
    if (!(hd->samprate > 0.0)) {
        if (err) snprintf(err, errn, "samprate invalida (%.3f)", hd->samprate);
        return -1;
    }

    sz = 4;
    if (hd->datatype[0] == 's' || hd->datatype[0] == 'i') {
        if (hd->datatype[1] == '2') sz = 2;
    }

    if (hd->nsamp > *cap) {
        int32_t *p = (int32_t *)realloc(*buf, (size_t)hd->nsamp * sizeof(int32_t));
        if (!p) {
            if (err) snprintf(err, errn, "sin memoria para %d muestras", hd->nsamp);
            return -1;
        }
        *buf = p;
        *cap = hd->nsamp;
    }

    if (sz == 4) {
        got = fread(*buf, sizeof(int32_t), (size_t)hd->nsamp, f);
        if (got != (size_t)hd->nsamp) return 0;
    } else {
        int16_t *tmp = (int16_t *)malloc((size_t)hd->nsamp * sizeof(int16_t));
        long     i;
        if (!tmp) {
            if (err) snprintf(err, errn, "sin memoria (int16 temporal)");
            return -1;
        }
        got = fread(tmp, sizeof(int16_t), (size_t)hd->nsamp, f);
        if (got != (size_t)hd->nsamp) { free(tmp); return 0; }
        for (i = 0; i < hd->nsamp; i++) (*buf)[i] = (int32_t)tmp[i];
        free(tmp);
    }
    return 1;
}

/* ============================ tank ======================================== */

int pm_src_open_tank(PmSrc **out, const char *path)
{
    PmSrc *s;

    if (!out || !path) return PM_ERR;
    *out = NULL;

    s = (PmSrc *)calloc(1, sizeof(*s));
    if (!s) return PM_ERR;
    s->kind = PM_SRC_TANK;

    s->f = fopen(path, "rb");
    if (!s->f) {
        snprintf(s->err, sizeof(s->err), "no se pudo abrir %s: %s",
                 path, strerror(errno));
        free(s);
        return PM_ERR;
    }
    *out = s;
    return PM_OK;
}

/* ============================ store ======================================= */

static void store_close_day(PmSrc *s)
{
    int c;
    for (c = 0; c < PM_MAX_COMP; c++) {
        if (s->sf[c]) { fclose(s->sf[c]); s->sf[c] = NULL; }
        s->shave[c] = 0;
        s->seof[c]  = 1;
    }
    s->sopen = 0;
}

static int store_open_day(PmSrc *s)
{
    char ymd[9];
    char path[1024];
    int  c, any = 0;

    store_close_day(s);
    day_ymd(s->day, ymd);

    for (c = 0; c < PM_MAX_COMP; c++) {
        const PmStation *st = &s->sta[s->si];
        const char *loc = st->loc[0] ? st->loc : "--";

        snprintf(path, sizeof(path), "%s/%s.%s.%s.%s/%s.tank",
                 s->root, st->net, st->sta, loc, st->chan[c], ymd);
        s->sf[c] = fopen(path, "rb");
        if (s->sf[c]) {
            any = 1;
            s->seof[c] = 0;
        } else {
            s->seof[c] = 1;
        }
        s->shave[c] = 0;
    }
    s->sopen = any;
    return any ? PM_OK : PM_NODATA;
}

static int store_fill(PmSrc *s, int c)
{
    int rc;

    if (s->shave[c] || s->seof[c] || !s->sf[c]) return 0;

    for (;;) {
        TRACE2_HEADER *hd = &s->sh[c];

        rc = read_record(s->sf[c], hd, &s->sx[c], &s->scap[c],
                         s->err, sizeof(s->err));
        if (rc <= 0) {
            s->seof[c] = 1;
            return 0;
        }
        /* Fuera de la ventana pedida: se descarta y se sigue leyendo. */
        if (hd->endtime < s->t0) continue;
        if (s->t1 > 0.0 && hd->starttime > s->t1) {
            s->seof[c] = 1;
            return 0;
        }
        s->shave[c] = 1;
        return 1;
    }
}

static void store_advance_day(PmSrc *s)
{
    store_close_day(s);
    s->day += 86400;
    s->day_end = s->day + 86400;
}

/* ============================ API ========================================= */

int pm_src_open_store(PmSrc **out, const char *root, const PmStation *sta,
                      int nsta, double t0, double t1)
{
    PmSrc *s;

    if (!out || !root || !sta || nsta <= 0) return PM_ERR;
    *out = NULL;
    if (!(t0 > 0.0) || !(t1 > t0)) return PM_ERR;

    s = (PmSrc *)calloc(1, sizeof(*s));
    if (!s) return PM_ERR;

    s->kind = PM_SRC_STORE;
    s->root = root;
    s->sta  = sta;
    s->nsta = nsta;
    s->t0   = t0;
    s->t1   = t1;
    s->si   = 0;
    s->day  = (long)floor(t0 / 86400.0) * 86400;
    s->day_end = s->day + 86400;

    *out = s;
    return PM_OK;
}

int pm_src_next(PmSrc *s, PmTrace *tr)
{
    if (!s || !tr) return PM_ERR;
    memset(tr, 0, sizeof(*tr));

    if (s->kind == PM_SRC_SDS)
        return pmsds_next(s->sds, tr);

    if (s->kind == PM_SRC_TANK) {
        TRACE2_HEADER hd;
        int           rc;

        /* El buffer se conserva entre llamadas (campo del fuente). */
        rc = read_record(s->f, &hd, &s->x, &s->cap, s->err, sizeof(s->err));
        if (rc <= 0) return rc;

        tr->t0   = hd.starttime;
        tr->rate = hd.samprate;
        tr->n    = hd.nsamp;
        copy_fld(tr->sta, sizeof(tr->sta), hd.sta, sizeof(hd.sta));
        copy_fld(tr->net, sizeof(tr->net), hd.net, sizeof(hd.net));
        copy_fld(tr->chan, sizeof(tr->chan), hd.chan, sizeof(hd.chan));
        copy_fld(tr->loc, sizeof(tr->loc), hd.loc, sizeof(hd.loc));
        tr->samples = s->x;
        return 1;
    }

    /* ---- store: estación por estación, día por día ---- */
    for (;;) {
        int  c, best = -1;
        double bt = 0.0;

        if (s->si >= s->nsta) return 0;

        if (!s->sopen) {
            if (s->day_end <= s->t0) { s->day += 86400; s->day_end = s->day + 86400; continue; }
            if (s->t1 > 0.0 && s->day > s->t1) {
                s->si++;
                if (s->si >= s->nsta) return 0;
                s->day = (long)floor(s->t0 / 86400.0) * 86400;
                s->day_end = s->day + 86400;
                continue;
            }
            store_open_day(s);
            if (!s->sopen) { store_advance_day(s); continue; }
        }

        for (c = 0; c < PM_MAX_COMP; c++) store_fill(s, c);

        for (c = 0; c < PM_MAX_COMP; c++) {
            if (!s->shave[c]) continue;
            if (best < 0 || s->sh[c].starttime < bt) {
                best = c;
                bt   = s->sh[c].starttime;
            }
        }

        if (best < 0) {
            /* Día agotado para esta estación. */
            store_advance_day(s);
            if (s->t1 > 0.0 && s->day > s->t1) {
                s->si++;
                s->day = (long)floor(s->t0 / 86400.0) * 86400;
                s->day_end = s->day + 86400;
            }
            continue;
        }

        {
            const PmStation *st = &s->sta[s->si];
            TRACE2_HEADER   *hd = &s->sh[best];

            tr->t0   = hd->starttime;
            tr->rate = hd->samprate;
            tr->n    = hd->nsamp;
            set_fld(tr->sta,  sizeof(tr->sta),  st->sta);
            set_fld(tr->net,  sizeof(tr->net),  st->net);
            set_fld(tr->chan, sizeof(tr->chan), st->chan[best]);
            set_fld(tr->loc,  sizeof(tr->loc),  st->loc[0] ? st->loc : "--");
            tr->samples = s->sx[best];
            s->shave[best] = 0;
        }
        return 1;
    }
}

/* ============================= SDS ======================================== */

int pm_src_open_sds(PmSrc **out, const char *root, const PmStation *sta,
                    int nsta, double t0, double t1, double rate)
{
    PmSrc *s;

    if (!out) return PM_ERR;
    *out = NULL;

    s = (PmSrc *)calloc(1, sizeof(*s));
    if (!s) return PM_ERR;
    s->kind = PM_SRC_SDS;

    s->sds = pmsds_open(root, sta, nsta, t0, t1, rate);
    if (!s->sds) {
        snprintf(s->err, sizeof(s->err), "no se pudo abrir el SDS %s",
                 root ? root : "");
        free(s);
        return PM_ERR;
    }
    *out = s;
    return PM_OK;
}

void pm_src_counts(const PmSrc *s, PmSrcCounts *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    if (s && s->kind == PM_SRC_SDS) pmsds_counts(s->sds, c);
}

const char *pm_src_error(const PmSrc *s)
{
    return (s && s->err[0]) ? s->err : "sin detalle";
}

void pm_src_close(PmSrc *s)
{
    int c;
    if (!s) return;
    if (s->f) fclose(s->f);
    store_close_day(s);
    pmsds_close(s->sds);
    free(s->x);
    for (c = 0; c < PM_MAX_COMP; c++) free(s->sx[c]);
    free(s);
}
