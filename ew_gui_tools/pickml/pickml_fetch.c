/******************************************************************************
 * pickml_fetch.c — prepara un evento de la BD hipocentral para calibrar.      *
 *                                                                            *
 *   pickml_fetch <fetch.d> --event <event_id> --out <dir>                     *
 *                                                                            *
 * Deja en <dir>:                                                             *
 *   event.tank     registros TRACE2_HEADER + int32 (3 componentes/estación)   *
 *   picks_ref.csv  net,sta,loc,chan,phase,t_ref,weight,pick_status,residual_s *
 *   meta.json      evento, origen, ventana, fuente y estaciones               *
 *                                                                            *
 * Sólo toma picadas MANUALES (pick_status='manual'): las revisadas por una    *
 * persona. Las 3 componentes se DERIVAN del canal picado (HHZ -> HHE/HHN/HHZ),*
 * porque los históricos no están en el StaFile actual.                       *
 *                                                                            *
 * Fuente de ondas: store (si `StoreRoot` y el día está) y, si no hay datos,   *
 * SDS (`SdsRoot`, layout YYYY/NET/STA/CHAN.D/NET.STA.LOC.CHAN.D.YYYY.DDD`).   *
 * Es un binario aparte A PROPÓSITO: pickml no enlaza libpq ni libmseed.       *
 ******************************************************************************/

#include "pickml.h"

#include <libpq-fe.h>
#include <libmseed2.h>
#include <kom.h>
#include <errno.h>
#include <unistd.h>

typedef struct {
    char   DbEnvFile[PM_STR_LEN];
    char   StoreRoot[PM_STR_LEN];
    char   SdsRoot[PM_STR_LEN];
    int    OnlyManual;
    int    MaxStations;
    double PadPreSec, PadPostSec;
    double Rate;
} FetchCfg;

typedef struct {
    char   net[PM_SCNL_LEN], sta[PM_SCNL_LEN], loc[PM_SCNL_LEN], chan[PM_SCNL_LEN];
    char   phase;
    char   status[16];
    double t, unc;
    int    weight;
} RefPick;

/* Estación 3C derivada de las picadas: banda del canal + Z/N/E. */
typedef struct {
    char net[PM_SCNL_LEN], sta[PM_SCNL_LEN], loc[PM_SCNL_LEN];
    char chan[PM_MAX_COMP][PM_SCNL_LEN];
    int  used;
    int  got;
} FSta;

/* ------------------------------------------------------------------ config */

static void cfg_defaults(FetchCfg *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->DbEnvFile, "db.env");
    c->OnlyManual  = 1;
    c->MaxStations = 0;
    c->PadPreSec   = 60.0;
    c->PadPostSec  = 120.0;
    c->Rate        = 100.0;
}

static int cfg_load(const char *file, FetchCfg *c, char *err, size_t errn)
{
    char *com, *s;

    if (!k_open(file)) {
        snprintf(err, errn, "no se pudo abrir %s", file);
        return PM_ERR;
    }
    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if      (k_its("DbEnvFile"))   { s = k_str(); snprintf(c->DbEnvFile, sizeof(c->DbEnvFile), "%s", s ? s : ""); }
        else if (k_its("StoreRoot"))   { s = k_str(); snprintf(c->StoreRoot, sizeof(c->StoreRoot), "%s", s ? s : ""); }
        else if (k_its("SdsRoot"))     { s = k_str(); snprintf(c->SdsRoot, sizeof(c->SdsRoot), "%s", s ? s : ""); }
        else if (k_its("OnlyManual"))  c->OnlyManual = k_int();
        else if (k_its("MaxStations")) c->MaxStations = k_int();
        else if (k_its("PadPreSec"))   c->PadPreSec = k_val();
        else if (k_its("PadPostSec"))  c->PadPostSec = k_val();
        else if (k_its("Rate"))        c->Rate = k_val();
        else continue;

        if (k_err()) {
            snprintf(err, errn, "error de config en '%s'", com);
            k_close();
            return PM_ERR;
        }
    }
    k_close();

    if (c->SdsRoot[0] == '\0' && c->StoreRoot[0] == '\0') {
        snprintf(err, errn, "hace falta SdsRoot y/o StoreRoot");
        return PM_ERR;
    }
    if (c->Rate <= 0.0) {
        snprintf(err, errn, "Rate debe ser > 0");
        return PM_ERR;
    }
    return PM_OK;
}

/* Lee KEY=VALUE de db.env (comillas opcionales, # comentarios). */
static void env_get(const char *file, const char *key, char *out, size_t n)
{
    FILE *f;
    char  line[512];

    out[0] = '\0';
    f = fopen(file, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *eq, *v, *e;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || !*p) continue;
        eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        e = eq - 1;
        while (e >= p && (*e == ' ' || *e == '\t')) *e-- = '\0';
        if (strcmp(p, key) != 0) continue;
        v = eq + 1;
        while (*v == ' ' || *v == '\t' || *v == '"' || *v == '\'') v++;
        e = v + strlen(v) - 1;
        while (e >= v && (*e == '\n' || *e == '\r' || *e == '"' || *e == '\'' ||
                          *e == ' ' || *e == '\t')) *e-- = '\0';
        snprintf(out, n, "%s", v);
        break;
    }
    fclose(f);
}

/* ------------------------------------------------------------------ fechas */

static void ymd(double t, char *out, size_t n)
{
    time_t tt = (time_t)floor(t);
    struct tm tm;
    gmtime_r(&tt, &tm);
    strftime(out, n, "%Y%m%d", &tm);
}

static void sds_path(char *out, size_t n, const char *root, const char *net,
                     const char *sta, const char *loc, const char *chan, double t)
{
    time_t tt = (time_t)floor(t);
    struct tm tm;
    int y, d;

    gmtime_r(&tt, &tm);
    y = tm.tm_year + 1900;
    d = tm.tm_yday + 1;
    if (!loc || !strcmp(loc, "--") || loc[0] == '\0') loc = "";
    snprintf(out, n, "%s/%04d/%s/%s/%s.D/%s.%s.%s.%s.D.%04d.%03d",
             root, y, net, sta, chan, net, sta, loc, chan, y, d);
}

/* ------------------------------------------------------------- remuestreo */

typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Biquad;

/* Butterworth de 2.º orden (Q=1/sqrt(2)) para anti-alias antes de decimar. */
static void biquad_lp(Biquad *q, double fc, double fs)
{
    double w0 = 2.0 * M_PI * fc / fs;
    double cw = cos(w0), sw = sin(w0);
    double alpha = sw / (2.0 * 0.7071067811865476);
    double a0 = 1.0 + alpha;

    q->b0 = (1.0 - cw) * 0.5 / a0;
    q->b1 = (1.0 - cw) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0 * cw / a0;
    q->a2 = (1.0 - alpha) / a0;
    q->z1 = q->z2 = 0.0;
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
    switch (type) {
        case 'i': return (double)((const int32_t *)src)[i];
        case 'f': return (double)((const float *)src)[i];
        case 'd': return ((const double *)src)[i];
        default:  return 0.0;
    }
}

/* ------------------------------------------------------------------- datos */

static int append_samples(int32_t **buf, long *n, long *cap,
                          const int32_t *x, long k)
{
    if (*n + k > *cap) {
        long     nc = (*cap ? *cap : 4096);
        int32_t *p;
        while (nc < *n + k) nc *= 2;
        p = (int32_t *)realloc(*buf, (size_t)nc * sizeof(int32_t));
        if (!p) return PM_ERR;
        *buf = p;
        *cap = nc;
    }
    memcpy(*buf + *n, x, (size_t)k * sizeof(int32_t));
    *n += k;
    return PM_OK;
}

/* Copia [i0,i1) de un registro mseed/tank a int32. */
static __attribute__((unused)) int copy_range(const void *src, char type, long i0, long i1, int32_t *dst)
{
    long i;

    for (i = i0; i < i1; i++) {
        switch (type) {
            case 'i': dst[i - i0] = ((const int32_t *)src)[i]; break;
            case 'f': dst[i - i0] = (int32_t)lroundf(((const float *)src)[i]); break;
            case 'd': dst[i - i0] = (int32_t)llround(((const double *)src)[i]); break;
            default: return PM_ERR;
        }
    }
    return PM_OK;
}


/* Lee un tank del store — mismo formato que pickml_source. */
static int store_channel(const FetchCfg *c, const char *net, const char *sta,
                         const char *loc, const char *chan, double t0, double t1,
                         int32_t **out, long *n, double *start)
{
    double day;
    long   cap = 0;

    *out = NULL; *n = 0; *start = 0.0;
    if (!c->StoreRoot[0]) return PM_NODATA;

    for (day = floor(t0 / 86400.0) * 86400.0; day < t1; day += 86400.0) {
        char  ymdstr[16], path[1024];
        FILE *f;

        ymd(day, ymdstr, sizeof(ymdstr));
        snprintf(path, sizeof(path), "%s/%s.%s.%s.%s/%s.tank",
                 c->StoreRoot, net, sta, loc, chan, ymdstr);
        f = fopen(path, "rb");
        if (!f) continue;

        for (;;) {
            TRACE2_HEADER h;
            int32_t      *tmp;
            long          i0, i1, k;
            double        a, b;

            if (fread(&h, sizeof(h), 1, f) != 1) break;
            if (h.nsamp <= 0 || h.nsamp > 20000000) break;
            tmp = (int32_t *)malloc((size_t)h.nsamp * sizeof(int32_t));
            if (!tmp) { fclose(f); return PM_ERR; }
            if (fread(tmp, sizeof(int32_t), (size_t)h.nsamp, f) != (size_t)h.nsamp) {
                free(tmp);
                break;
            }

            a = h.starttime;
            b = a + (double)h.nsamp / h.samprate;
            if (!(h.samprate > 0.0) || b <= t0 || a >= t1) { free(tmp); continue; }
            if (fabs(h.samprate - c->Rate) > 0.01) { free(tmp); continue; }

            i0 = (t0 > a) ? (long)ceil((t0 - a) * h.samprate) : 0;
            i1 = (t1 < b) ? (long)floor((t1 - a) * h.samprate) : h.nsamp;
            if (i0 < 0) i0 = 0;
            if (i1 > h.nsamp) i1 = h.nsamp;
            if (i1 <= i0) { free(tmp); continue; }

            k = i1 - i0;
            if (*n == 0) *start = a + (double)i0 / h.samprate;
            if (append_samples(out, n, &cap, tmp + i0, k) != PM_OK) {
                free(tmp);
                fclose(f);
                return PM_ERR;
            }
            free(tmp);
        }
        fclose(f);
    }
    return (*n > 0) ? PM_OK : PM_NODATA;
}

static int sds_channel(const FetchCfg *c, const char *net, const char *sta,
                       const char *loc, const char *chan, double t0, double t1,
                       int32_t **out, long *n, double *start)
{
    double day;
    long   cap = 0;

    *out = NULL; *n = 0; *start = 0.0;
    if (!c->SdsRoot[0]) return PM_NODATA;

    for (day = floor(t0 / 86400.0) * 86400.0; day < t1; day += 86400.0) {
        char         path[1024];
        MSFileParam *msfp = NULL;
        MSRecord    *msr  = NULL;
        int          rc;
        long         dec = 1;
        Biquad       lp = {0, 0, 0, 0, 0, 0, 0};
        int          lp_on = 0;

        sds_path(path, sizeof(path), c->SdsRoot, net, sta, loc, chan, day);
        if (access(path, R_OK) != 0) {
            if (getenv("PICKML_FETCH_DEBUG"))
                fprintf(stderr, "pickml_fetch: sds SIN ACCESO %s\n", path);
            continue;
        }
        if (getenv("PICKML_FETCH_DEBUG"))
            fprintf(stderr, "pickml_fetch: sds %s\n", path);

        while ((rc = ms_readmsr_r(&msfp, &msr, path, 0, NULL, NULL, 1, 1, 0)) == MS_NOERROR) {
            double a, b;
            long   i0, i1;

            /* starttime es hptime_t (µs desde epoch), NO double: se usa el
             * accesor de libmseed, igual que csnhypodbp_sds.c. */
            a = MS_HPTIME2EPOCH(msr_starttime(msr));

            if (getenv("PICKML_FETCH_DEBUG"))
                fprintf(stderr, "  rec a=%.3f rate=%.2f n=%lld sc=%lld type=%c data=%p\n",
                        a, msr->samprate, (long long)msr->numsamples,
                        (long long)msr->samplecnt, msr->sampletype,
                        msr->datasamples);

            if (!msr->datasamples || msr->numsamples <= 0 || msr->samplecnt <= 0) continue;
            if (!(msr->samprate > 0.0)) continue;

            /* El picker trabaja a `Rate`. Si la estación corre al doble (o al
             * cuádruple) se decima con un anti-alias; si no, se omite. */
            if (fabs(msr->samprate - c->Rate) <= 0.01)                 dec = 1;
            else if (fabs(msr->samprate - 2.0 * c->Rate) <= 0.01)      dec = 2;
            else if (fabs(msr->samprate - 4.0 * c->Rate) <= 0.01)      dec = 4;
            else continue;

            if (dec > 1 && !lp_on) {
                biquad_lp(&lp, 0.35 * c->Rate, msr->samprate);
                lp_on = 1;
            }

            b = a + (double)msr->numsamples / msr->samprate;
            if (b <= t0 || a >= t1) continue;

            i0 = (t0 > a) ? (long)ceil((t0 - a) * msr->samprate) : 0;
            i1 = (t1 < b) ? (long)floor((t1 - a) * msr->samprate) : (long)msr->numsamples;
            if (i0 < 0) i0 = 0;
            if (i1 > msr->numsamples) i1 = (long)msr->numsamples;
            if (i1 <= i0) continue;

            {
                long     k0 = i0 + ((dec - (i0 % dec)) % dec);
                long     nout, j = 0, k;
                int32_t *tmp;

                if (k0 >= i1) continue;
                nout = (i1 - k0 + dec - 1) / dec;
                tmp = (int32_t *)malloc((size_t)nout * sizeof(int32_t));
                if (!tmp) {
                    ms_readmsr_r(&msfp, &msr, NULL, 0, NULL, NULL, 0, 0, 0);
                    return PM_ERR;
                }
                for (k = k0; k < i1; k++) {
                    double v = sample_at(msr->datasamples, msr->sampletype, k);
                    if (dec > 1) v = biquad_run(&lp, v);
                    if ((k - k0) % dec == 0) tmp[j++] = (int32_t)llround(v);
                }
                if (*n == 0) *start = a + (double)k0 / msr->samprate;
                if (append_samples(out, n, &cap, tmp, j) != PM_OK) {
                    free(tmp);
                    ms_readmsr_r(&msfp, &msr, NULL, 0, NULL, NULL, 0, 0, 0);
                    return PM_ERR;
                }
                free(tmp);
            }
        }
        ms_readmsr_r(&msfp, &msr, NULL, 0, NULL, NULL, 0, 0, 0);
        if (getenv("PICKML_FETCH_DEBUG"))
            fprintf(stderr, "  rc_final=%d (MS_ENDOFFILE=%d)\n", rc, MS_ENDOFFILE);
    }
    return (*n > 0) ? PM_OK : PM_NODATA;
}

static int channel_data(const FetchCfg *c, const char *net, const char *sta,
                        const char *loc, const char *chan, double t0, double t1,
                        int32_t **out, long *n, double *start)
{
    if (store_channel(c, net, sta, loc, chan, t0, t1, out, n, start) == PM_OK) return PM_OK;
    return sds_channel(c, net, sta, loc, chan, t0, t1, out, n, start);
}

static int tank_write(FILE *f, const char *sta, const char *net, const char *loc,
                      const char *chan, double t0, const int32_t *x, long n,
                      double rate)
{
    TRACE2_HEADER h;

    if (n <= 0) return PM_OK;
    memset(&h, 0, sizeof(h));
    snprintf(h.sta, sizeof(h.sta), "%s", sta);
    snprintf(h.net, sizeof(h.net), "%s", net);
    snprintf(h.loc, sizeof(h.loc), "%s", loc);
    snprintf(h.chan, sizeof(h.chan), "%s", chan);
    h.starttime = t0;
    h.endtime   = t0 + (double)(n - 1) / rate;
    h.samprate  = rate;
    h.nsamp     = (int)n;
    strcpy(h.datatype, "i4");

    if (fwrite(&h, sizeof(h), 1, f) != 1) return PM_ERR;
    if (fwrite(x, sizeof(int32_t), (size_t)n, f) != (size_t)n) return PM_ERR;
    return PM_OK;
}

/* ------------------------------------------------------------------- NSLC */

static int nslc_split(const char *s, char *net, char *sta, char *loc, char *chan)
{
    const char *p = s;
    char       *dst[4];
    int         k;

    dst[0] = net; dst[1] = sta; dst[2] = loc; dst[3] = chan;
    for (k = 0; k < 4; k++) {
        const char *dot = strchr(p, '.');
        size_t      len = dot ? (size_t)(dot - p) : strlen(p);
        if (len >= PM_SCNL_LEN) len = PM_SCNL_LEN - 1;
        memcpy(dst[k], p, len);
        dst[k][len] = '\0';
        if (!dot) { k++; break; }
        p = dot + 1;
    }
    for (; k < 4; k++) dst[k][0] = '\0';
    return 0;
}

/* Deriva las 3 componentes de la banda del canal picado (HHZ -> HHE/HHN/HHZ). */
static int derive_chans(FSta *f, const char *pickchan)
{
    char band[8];
    int  n = (int)strlen(pickchan);

    if (n < 2 || n > 7) return PM_ERR;
    snprintf(band, sizeof(band), "%.7s", pickchan);
    band[n - 1] = '\0';
    snprintf(f->chan[PM_COMP_E], PM_SCNL_LEN, "%sE", band);
    snprintf(f->chan[PM_COMP_N], PM_SCNL_LEN, "%sN", band);
    snprintf(f->chan[PM_COMP_Z], PM_SCNL_LEN, "%sZ", band);
    return PM_OK;
}

static int find_fsta(const FSta *v, int n, const char *net, const char *sta,
                     const char *loc)
{
    char l1[PM_SCNL_LEN];
    int  i;

    snprintf(l1, sizeof(l1), "%s", loc);
    if (!strcmp(l1, "--")) l1[0] = '\0';

    for (i = 0; i < n; i++) {
        char l2[PM_SCNL_LEN];
        if (strcmp(v[i].net, net) || strcmp(v[i].sta, sta)) continue;
        snprintf(l2, sizeof(l2), "%s", v[i].loc);
        if (!strcmp(l2, "--")) l2[0] = '\0';
        if (!strcmp(l1, l2)) return i;
    }
    return -1;
}

/* -------------------------------------------------------------------- main */

static void usage(void)
{
    fprintf(stderr,
        "Uso: pickml_fetch <fetch.d> --event <event_id> --out <dir>\n"
        "  fetch.d: DbEnvFile StoreRoot SdsRoot OnlyManual MaxStations\n"
        "           PadPreSec PadPostSec Rate\n");
}

int main(int argc, char **argv)
{
    FetchCfg  cfg;
    const char *d = NULL, *event = NULL, *outdir = NULL;
    char      err[PM_STR_LEN];
    char      host[128], port[16], dbn[128], user[64], pass[128], conninfo[1024];
    PGconn   *conn;
    PGresult *res;
    char      origin_id[128] = "";
    double    ot = 0.0, lat = 0.0, lon = 0.0, depth = 0.0, t0 = 0.0, t1 = 0.0;
    int       i, npk = 0, cap = 0, nf = 0, capf = 0, nskipped = 0, nwritten = 0, nofill = 0;
    RefPick  *pk = NULL;
    FSta     *fsta = NULL;
    char      tankpath[1024], csvpath[1024], metapath[1024];
    FILE     *tank = NULL, *csv = NULL, *meta = NULL;

    for (i = 1; i < argc; i++) {
        if (!d && argv[i][0] != '-') { d = argv[i]; continue; }
        if      (!strcmp(argv[i], "--event")) { if (++i < argc) event = argv[i]; }
        else if (!strcmp(argv[i], "--out"))   { if (++i < argc) outdir = argv[i]; }
        else { usage(); return 2; }
    }
    if (!d || !event || !outdir) { usage(); return 2; }

    cfg_defaults(&cfg);
    if (cfg_load(d, &cfg, err, sizeof(err)) != PM_OK) {
        fprintf(stderr, "pickml_fetch: %s\n", err);
        return 2;
    }

    /* ---- BD ---- */
    env_get(cfg.DbEnvFile, "DB_HOST", host, sizeof(host));
    env_get(cfg.DbEnvFile, "DB_PORT", port, sizeof(port));
    env_get(cfg.DbEnvFile, "DB_NAME", dbn, sizeof(dbn));
    env_get(cfg.DbEnvFile, "DB_USER", user, sizeof(user));
    env_get(cfg.DbEnvFile, "DB_PASSWORD", pass, sizeof(pass));
    if (!host[0] || !dbn[0]) {
        fprintf(stderr, "pickml_fetch: %s no define DB_HOST/DB_NAME\n", cfg.DbEnvFile);
        return 2;
    }
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s connect_timeout=5",
             host, port[0] ? port : "5432", dbn, user, pass);

    conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "pickml_fetch: BD: %s", PQerrorMessage(conn));
        PQfinish(conn);
        return 4;
    }
    fprintf(stderr, "pickml_fetch: BD %s@%s:%s user=%s | event=[%s]\n",
            dbn, host, port[0] ? port : "5432", user, event);
    if (getenv("PICKML_FETCH_DEBUG")) {
        PGresult *r = PQexec(conn, "select count(*) from origin");
        if (PQresultStatus(r) == PGRES_TUPLES_OK)
            fprintf(stderr, "pickml_fetch: origenes en la BD = %s\n",
                    PQgetvalue(r, 0, 0));
        else
            fprintf(stderr, "pickml_fetch: count(origin): %s\n",
                    PQerrorMessage(conn));
        PQclear(r);
    }

    {
        char *lit = PQescapeLiteral(conn, event, strlen(event));
        char  sql[2048];

        if (!lit) {
            fprintf(stderr, "pickml_fetch: no se pudo escapar el event_id\n");
            PQfinish(conn);
            return 4;
        }
        snprintf(sql, sizeof(sql),
            "SELECT o.origin_id, extract(epoch from o.origin_time)::float8, "
            "       coalesce(o.latitude,-999)::float8, coalesce(o.longitude,-999)::float8, "
            "       coalesce(o.depth,-1)::float8, count(p.pick_id) "
            "FROM origin o LEFT JOIN pick p ON p.origin_id = o.origin_id "
            "WHERE o.event_id = %s GROUP BY 1,2,3,4,5 "
            "ORDER BY count(p.pick_id) DESC LIMIT 1", lit);
        PQfreemem(lit);
        res = PQexec(conn, sql);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "pickml_fetch: consulta de origenes: %s\n",
                    PQerrorMessage(conn));
            PQclear(res);
            PQfinish(conn);
            return 4;
        }
        if (PQntuples(res) < 1) {
            fprintf(stderr, "pickml_fetch: evento %s sin origenes\n", event);
            PQclear(res);
            PQfinish(conn);
            return 4;
        }
        snprintf(origin_id, sizeof(origin_id), "%s", PQgetvalue(res, 0, 0));
        ot    = atof(PQgetvalue(res, 0, 1));
        lat   = atof(PQgetvalue(res, 0, 2));
        lon   = atof(PQgetvalue(res, 0, 3));
        depth = atof(PQgetvalue(res, 0, 4));
        PQclear(res);
    }

    {
        char *lit = PQescapeLiteral(conn, origin_id, strlen(origin_id));
        char  sql[2048];

        if (!lit) {
            fprintf(stderr, "pickml_fetch: no se pudo escapar el origin_id\n");
            PQfinish(conn);
            return 4;
        }
        snprintf(sql, sizeof(sql),
            "SELECT channel, phase, extract(epoch from pick_time)::float8, "
            "       coalesce(weight,-1), coalesce(pick_status,''), "
            "       coalesce(uncertainty::text,'NaN') "
            "FROM pick WHERE origin_id = %s ORDER BY pick_time", lit);
        PQfreemem(lit);
        res = PQexec(conn, sql);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "pickml_fetch: consulta de picadas: %s\n",
                    PQerrorMessage(conn));
            PQclear(res);
            PQfinish(conn);
            return 4;
        }
        for (i = 0; i < PQntuples(res); i++) {
            RefPick  r;
            char     ph = PQgetvalue(res, i, 1)[0];
            const char *st = PQgetvalue(res, i, 4);
            int      fi;

            if (ph != 'P' && ph != 'S' && ph != 'p' && ph != 's') continue;
            if (cfg.OnlyManual && strcmp(st, "manual") != 0) continue;

            memset(&r, 0, sizeof(r));
            nslc_split(PQgetvalue(res, i, 0), r.net, r.sta, r.loc, r.chan);
            r.phase  = (ph == 'p' || ph == 'P') ? 'P' : 'S';
            r.t      = atof(PQgetvalue(res, i, 2));
            r.weight = atoi(PQgetvalue(res, i, 3));
            snprintf(r.status, sizeof(r.status), "%s", st);
            r.unc    = atof(PQgetvalue(res, i, 5));

            if (npk == cap) {
                int      nc = cap ? cap * 2 : 256;
                RefPick *p = (RefPick *)realloc(pk, (size_t)nc * sizeof(RefPick));
                if (!p) { fprintf(stderr, "pickml_fetch: sin memoria\n"); return 3; }
                pk = p; cap = nc;
            }
            pk[npk++] = r;

            /* alta de la estación 3C derivada del canal picado */
            fi = find_fsta(fsta, nf, r.net, r.sta, r.loc);
            if (fi < 0) {
                FSta fs;
                if (nf == capf) {
                    int   nc = capf ? capf * 2 : 128;
                    FSta *p = (FSta *)realloc(fsta, (size_t)nc * sizeof(FSta));
                    if (!p) { fprintf(stderr, "pickml_fetch: sin memoria\n"); return 3; }
                    fsta = p; capf = nc;
                }
                memset(&fs, 0, sizeof(fs));
                snprintf(fs.net, sizeof(fs.net), "%s", r.net);
                snprintf(fs.sta, sizeof(fs.sta), "%s", r.sta);
                snprintf(fs.loc, sizeof(fs.loc), "%s", r.loc);
                if (derive_chans(&fs, r.chan) != PM_OK) { nskipped++; continue; }
                fsta[nf++] = fs;
            }
        }
        PQclear(res);
    }
    PQfinish(conn);

    if (npk == 0 || nf == 0) {
        fprintf(stderr, "pickml_fetch: el origen %s no tiene picadas P/S%s\n",
                origin_id, cfg.OnlyManual ? " manuales" : "");
        return 4;
    }

    /* ---- ventana: todas las picadas ± relleno ---- */
    t0 = pk[0].t;
    t1 = pk[0].t;
    for (i = 0; i < npk; i++) {
        if (pk[i].t < t0) t0 = pk[i].t;
        if (pk[i].t > t1) t1 = pk[i].t;
    }
    t0 -= cfg.PadPreSec;
    t1 += cfg.PadPostSec;

    snprintf(tankpath, sizeof(tankpath), "%s/event.tank", outdir);
    snprintf(csvpath,  sizeof(csvpath),  "%s/picks_ref.csv", outdir);
    snprintf(metapath, sizeof(metapath), "%s/meta.json", outdir);

    tank = fopen(tankpath, "wb");
    csv  = fopen(csvpath, "w");
    if (!tank || !csv) {
        fprintf(stderr, "pickml_fetch: no se pudo escribir en %s\n", outdir);
        return 4;
    }
    fprintf(csv, "net,sta,loc,chan,phase,t_ref,weight,pick_status,residual_s\n");

    {
        int done = 0;
        for (i = 0; i < nf; i++) {
            struct { int32_t *x; long n; double st; int ok; } c3[PM_MAX_COMP];
            int  c, okc = 1;
            double tb0 = 0.0, tb1 = 0.0, bt;
            long   blk = (long)(cfg.Rate + 0.5);   /* bloques de 1 s */

            if (cfg.MaxStations > 0 && done >= cfg.MaxStations) break;
            done++;

            for (c = 0; c < PM_MAX_COMP; c++) {
                c3[c].x = NULL; c3[c].n = 0; c3[c].st = 0.0; c3[c].ok = 0;
                if (channel_data(&cfg, fsta[i].net, fsta[i].sta, fsta[i].loc,
                                 fsta[i].chan[c], t0, t1,
                                 &c3[c].x, &c3[c].n, &c3[c].st) == PM_OK &&
                    c3[c].n > 0)
                    c3[c].ok = 1;
                else
                    okc = 0;

                if (getenv("PICKML_FETCH_DEBUG"))
                    fprintf(stderr, "fetch: %s.%s %s.%s ok=%d n=%ld st=%.3f\n",
                            fsta[i].sta, fsta[i].net, fsta[i].loc,
                            fsta[i].chan[c], c3[c].ok, c3[c].n, c3[c].st);
            }

            if (!okc) {
                for (c = 0; c < PM_MAX_COMP; c++) free(c3[c].x);
                nofill++;
                continue;
            }

            /* Sólo el tramo cubierto por las TRES componentes. */
            tb0 = c3[0].st;
            tb1 = c3[0].st + (double)c3[0].n / cfg.Rate;
            for (c = 1; c < PM_MAX_COMP; c++) {
                double a = c3[c].st;
                double b = c3[c].st + (double)c3[c].n / cfg.Rate;
                if (a > tb0) tb0 = a;
                if (b < tb1) tb1 = b;
            }

            /* El tank se escribe INTERCALADO por tiempo (1 s por componente):
             * un registro gigante por componente haría que, al llegar la
             * segunda, el buffer de la estación ya hubiera avanzado. */
            for (bt = tb0; bt < tb1; bt += (double)blk / cfg.Rate) {
                for (c = 0; c < PM_MAX_COMP; c++) {
                    long i0 = (long)llround((bt - c3[c].st) * cfg.Rate);
                    long nb = blk;

                    if (i0 < 0) { nb += i0; i0 = 0; }
                    if (i0 + nb > c3[c].n) nb = c3[c].n - i0;
                    if (nb <= 0) continue;

                    if (tank_write(tank, fsta[i].sta, fsta[i].net, fsta[i].loc,
                                   fsta[i].chan[c],
                                   c3[c].st + (double)i0 / cfg.Rate,
                                   c3[c].x + i0, nb, cfg.Rate) == PM_OK)
                        fsta[i].got++;
                }
            }

            for (c = 0; c < PM_MAX_COMP; c++) free(c3[c].x);
            if (fsta[i].got > 0) nwritten++;
            else nofill++;
        }
    }

    for (i = 0; i < npk; i++) {
        int fi = find_fsta(fsta, nf, pk[i].net, pk[i].sta, pk[i].loc);
        if (fi < 0 || fsta[fi].got == 0) continue;
        fprintf(csv, "%s,%s,%s,%s,%c,%.6f,%d,%s,%.3f\n",
                pk[i].net, pk[i].sta, pk[i].loc[0] ? pk[i].loc : "--",
                pk[i].chan, pk[i].phase, pk[i].t, pk[i].weight,
                pk[i].status, pk[i].unc);
    }
    fclose(csv);
    fclose(tank);

    /* StaFile de la corrida: pickml --tank lo consume tal cual. */
    {
        char  stapath[1024];
        FILE *sf;

        snprintf(stapath, sizeof(stapath), "%s/stations.sta", outdir);
        sf = fopen(stapath, "w");
        if (sf) {
            int k, pin = 0;
            fprintf(sf, "# PickFlag Pin Sta Net Loc ChanE ChanN ChanZ\n");
            for (k = 0; k < nf; k++) {
                if (fsta[k].got == 0) continue;
                fprintf(sf, "1 %d %s %s %s %s %s %s\n", ++pin,
                        fsta[k].sta, fsta[k].net,
                        fsta[k].loc[0] ? fsta[k].loc : "--",
                        fsta[k].chan[PM_COMP_E], fsta[k].chan[PM_COMP_N],
                        fsta[k].chan[PM_COMP_Z]);
            }
            fclose(sf);
        }
    }

    meta = fopen(metapath, "w");
    if (meta) {
        fprintf(meta,
            "{\"event_id\":\"%s\",\"origin_id\":\"%s\",\"origin_time\":%.3f,"
            "\"latitude\":%.6f,\"longitude\":%.6f,\"depth\":%.3f,"
            "\"t0\":%.3f,\"t1\":%.3f,\"pad_pre\":%.1f,\"pad_post\":%.1f,"
            "\"rate\":%.1f,\"picks\":%d,\"picks_skipped\":%d,"
            "\"stations\":%d,\"stations_written\":%d,\"stations_nodata\":%d,"
            "\"only_manual\":%d}\n",
            event, origin_id, ot, lat, lon, depth, t0, t1,
            cfg.PadPreSec, cfg.PadPostSec, cfg.Rate,
            npk, nskipped, nf, nwritten, nofill, cfg.OnlyManual);
        fclose(meta);
    }

    printf("pickml_fetch: %s origen=%s picks=%d estaciones=%d/%d -> %s/event.tank\n",
           event, origin_id, npk, nwritten, nf, outdir);
    free(pk);
    free(fsta);
    return 0;
}
