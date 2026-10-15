#include "csnwphase.h"

#include <libmseed2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ewgui/sds.h>
#include <wstore.h>

/*
 * Capa de ondas: store indexado (wstore) + SDS (libmseed2).
 *
 * La política es la del resto del repo: para eventos más viejos que
 * SdsFallbackHours se consulta primero el SDS y, si no hay datos, la otra
 * fuente. El SDS se lee por día con el layout de ewgui_sds_path:
 *   <root>/<YYYY>/<NET>/<STA>/<CHAN>.D/<NET>.<STA>.<LOC>.<CHAN>.D.<YYYY>.<DDD>
 * con el location nulo de EarthWorm ("--") traducido a vacío (el SDS escribe
 * NET.STA..CHAN).
 */

/* Copia truncando de forma explícita (silencia el aviso de truncamiento). */
static void cpystr(char *dst, size_t n, const char *src)
{
    size_t len;

    if (!dst || n == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    len = strlen(src);
    if (len >= n)
        len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void csnwphase_trace_free(CsnwphaseTrace *t){
    if (!t)
        return;
    free(t->samples);
    t->samples = NULL;
    t->n = 0;
}

int csnwphase_wave_open(CsnwphaseWave *w, const CsnwphaseCfg *cfg)
{
    if (!w || !cfg)
        return -1;

    memset(w, 0, sizeof(*w));
    snprintf(w->store_root, sizeof(w->store_root), "%s", cfg->store_root);
    snprintf(w->sds_root, sizeof(w->sds_root), "%s", cfg->sds_root);
    w->sds_enabled = (cfg->sds_root[0] != '\0');
    w->sds_fallback_hours = cfg->sds_fallback_hours;

    if (cfg->store_root[0]) {
        WStore *s = NULL;

        if (wstore_open(&s, cfg->store_root, NULL) == 0) {
            w->store = s;
            w->have_store = 1;
        } else {
            fprintf(stderr, "csnwphase: store '%s' no disponible\n",
                    cfg->store_root);
        }
    }

    if (!w->have_store && !w->sds_enabled) {
        fprintf(stderr, "csnwphase: sin fuentes de ondas (StoreRoot/SdsRoot)\n");
        return -1;
    }

    return 0;
}

void csnwphase_wave_close(CsnwphaseWave *w)
{
    if (!w)
        return;
    if (w->store)
        wstore_close((WStore *)w->store);
    w->store = NULL;
    w->have_store = 0;
}

/* El motor nombra los GF como `<STA>.<NET>.<CHAN>.<LOC>` y `wpinversion` los
 * busca con el MISMO location que trae la traza: para el location nulo usa `--`,
 * NO vacio. La lista de estaciones normaliza `--` -> "" para pedir al SDS (que
 * escribe `NET.STA..CHAN`), asi que aqui hay que devolver `--` al escribir la
 * traza; si no, la inversion muere con
 * `ERROR opening ./GF/gf_rr/STA.NET.CHAN..SAC.sac.bp` (visto 2026-10-13 con
 * CX.PB16, la estacion que solo aparece con el hipocentro de la BD). */
static void set_loc(char *dst, size_t n, const char *loc)
{
    snprintf(dst, n, "%s", (loc && loc[0]) ? loc : "--");
}

static int store_get(CsnwphaseWave *w, const char *net, const char *sta,                     const char *chan, const char *loc, double t0, double t1,
                     CsnwphaseTrace *out)
{
    WStoreTrace st;
    long i;

    if (!w->have_store)
        return 0;
    if (wstore_get((WStore *)w->store, sta, net, chan, loc, t0, t1, &st) != 0)
        return 0;
    if (st.n <= 0) {
        wstore_trace_free(&st);
        return 0;
    }

    out->samples = malloc((size_t)st.n * sizeof(int32_t));
    if (!out->samples) {
        wstore_trace_free(&st);
        return -1;
    }
    for (i = 0; i < st.n; i++)
        out->samples[i] = st.samples[i];

    snprintf(out->sta, sizeof(out->sta), "%s", st.sta);
    snprintf(out->net, sizeof(out->net), "%s", st.net);
    snprintf(out->chan, sizeof(out->chan), "%s", st.chan);
    set_loc(out->loc, sizeof(out->loc), st.loc);
    out->t0 = st.t0;
    out->samprate = st.samprate;
    out->n = st.n;

    wstore_trace_free(&st);
    return 1;
}

/* Lee la ventana [t0,t1] del SDS por día. Devuelve 1/0/<0. */
static int sds_get(CsnwphaseWave *w, const char *net, const char *sta,
                   const char *chan, const char *loc, double t0, double t1,
                   CsnwphaseTrace *out)
{
    MSFileParam *msfp = NULL;
    MSRecord *msr = NULL;
    int32_t *buf = NULL;
    char path[1024];
    double rate = 0.0;
    long cap = 0, filled = 0;
    double day;

    if (!w->sds_enabled)
        return 0;

    for (day = floor(t0 / 86400.0) * 86400.0; day <= t1; day += 86400.0) {
        if (ewgui_sds_path(path, sizeof(path), w->sds_root, net, sta, loc, chan,
                           day) != 0)
            continue;
        /* Día ausente: no se abre (evita el ruido "Cannot open file"). */
        if (access(path, R_OK) != 0)
            continue;

        while (ms_readmsr_r(&msfp, &msr, path, 0, NULL, NULL, 1, 1, 0) ==
               MS_NOERROR) {
            double rstart = MS_HPTIME2EPOCH(msr_starttime(msr));
            double rsrate = msr->samprate;
            long i, rn = (long)msr->numsamples;
            double rend;

            if (rn <= 0 || rsrate <= 0.0)
                continue;
            rend = rstart + (double)(rn - 1) / rsrate;
            if (rstart > t1)
                break;
            if (rend < t0)
                continue;

            if (rate == 0.0) {
                rate = rsrate;
                cap = (long)((t1 - t0) * rate) + 2;
                buf = calloc((size_t)cap, sizeof(int32_t));
                if (!buf) {
                    ms_readmsr_r(&msfp, &msr, NULL, 0, NULL, NULL, 0, 0, 0);
                    return -1;
                }
            }
            /* Tasa distinta: se ignora (regla del resto del repo). */
            if (fabs(rsrate - rate) > 1e-6)
                continue;

            {
                long off = (long)lround((rstart - t0) * rate);

                for (i = 0; i < rn; i++) {
                    long k = off + i;
                    int32_t v;

                    if (k < 0 || k >= cap)
                        continue;
                    if (msr->sampletype == 'f')
                        v = (int32_t)lround((double)((float *)msr->datasamples)[i]);
                    else if (msr->sampletype == 'd')
                        v = (int32_t)lround(((double *)msr->datasamples)[i]);
                    else
                        v = ((int32_t *)msr->datasamples)[i];
                    buf[k] = v;
                    if (k + 1 > filled)
                        filled = k + 1;
                }
            }
        }
        ms_readmsr_r(&msfp, &msr, NULL, 0, NULL, NULL, 0, 0, 0);
    }

    if (!buf || filled <= 0) {
        free(buf);
        return 0;
    }

    snprintf(out->sta, sizeof(out->sta), "%s", sta);
    snprintf(out->net, sizeof(out->net), "%s", net);
    snprintf(out->chan, sizeof(out->chan), "%s", chan);
    set_loc(out->loc, sizeof(out->loc), loc);
    out->t0 = t0;
    out->samprate = rate;
    out->n = filled;
    out->samples = buf;
    return 1;
}

int csnwphase_wave_get(CsnwphaseWave *w, const char *net, const char *sta,
                       const char *chan, const char *loc, double t0, double t1,
                       CsnwphaseTrace *out)
{
    int prefer_sds, rc;

    if (!w || !out)
        return -1;

    memset(out, 0, sizeof(*out));
    prefer_sds = (time(NULL) - t0) / 3600.0 > (double)w->sds_fallback_hours;

    if (prefer_sds) {
        rc = sds_get(w, net, sta, chan, loc, t0, t1, out);
        if (rc > 0) {
            w->n_sds++;
            return 1;
        }
        rc = store_get(w, net, sta, chan, loc, t0, t1, out);
        if (rc > 0) {
            w->n_store++;
            return 1;
        }
    } else {
        rc = store_get(w, net, sta, chan, loc, t0, t1, out);
        if (rc > 0) {
            w->n_store++;
            return 1;
        }
        rc = sds_get(w, net, sta, chan, loc, t0, t1, out);
        if (rc > 0) {
            w->n_sds++;
            return 1;
        }
    }

    return rc;
}

/* ------------------------------------------------------------------ */
/* Selección de estaciones                                              */
/* ------------------------------------------------------------------ */

void csnwphase_chan_variants(const char *chan_in, char out[3][16], int *n)
{
    int i;

    *n = 0;
    if (!chan_in || strlen(chan_in) < 1)
        return;

    for (i = 0; i < 3; i++) {
        char c;
        const char *suffix = (i == 0) ? "Z" : (i == 1) ? "N" : "E";

        snprintf(out[*n], 16, "%s", chan_in);
        c = out[*n][strlen(out[*n]) - 1];
        /* Sólo se expanden canales cortos de banda ancha/LP (?H?); el resto
           se deja tal cual (una sola variante). */
        if ((c == 'Z' || c == 'N' || c == 'E') &&
            strlen(out[*n]) >= 3 && out[*n][strlen(out[*n]) - 2] == 'H') {
            out[*n][strlen(out[*n]) - 1] = suffix[0];
            (*n)++;
        } else {
            (*n)++;
            return;
        }
    }
}

int csnwphase_sta_select(const CsnwphaseCfg *cfg, double evla, double evlo,
                         double dmin_deg, double dmax_deg,
                         CsnwphaseStation *out, int max)
{
    FILE *f;
    char line[512];
    int n = 0;

    if (!cfg || !out || max <= 0)
        return -1;

    f = fopen(cfg->sta_file, "r");
    if (!f) {
        fprintf(stderr, "csnwphase: no se pudo abrir StaFile '%s'\n",
                cfg->sta_file);
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char sta[32], net[32], chan[32], loc[32];
        double lat, lon, elev = 0.0, sens = 1.0;
        CsnwphaseStation *s;
        double dist, az, baz, gcarc;

        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%31s %31s %31s %31s %lf %lf %lf %lf", sta, net, chan,
                   loc, &lat, &lon, &elev, &sens) < 6)
            continue;

        csnwphase_distaz(evla, evlo, lat, lon, &dist, &az, &baz, &gcarc);
        if (gcarc < dmin_deg || gcarc > dmax_deg)
            continue;
        if (n >= max)
            break;

        s = &out[n];
        memset(s, 0, sizeof(*s));
        cpystr(s->sta, sizeof(s->sta), sta);
        cpystr(s->net, sizeof(s->net), net);
        cpystr(s->chan, sizeof(s->chan), chan);
        /* El location `--` se CONSERVA: `ewgui_sds_path` ya lo traduce a vacio para el
       SDS y el store lo quiere tal cual (`--`). Ademas, asi la traza sale con `--`
       y el motor nombra los GF `<STA>.<NET>.<CHAN>.--` igual que los busca
       `wpinversion`; con vacio, la inversion muere con
       `ERROR opening ./GF/gf_rr/STA.NET.CHAN..SAC.sac.bp` (visto 2026-10-13). */
    cpystr(s->loc, sizeof(s->loc), loc);
        s->lat = lat;
        s->lon = lon;
        s->elev_m = elev;
        s->sens = sens;
        s->dist_km = dist;
        s->gcarc_deg = gcarc;
        s->az = az;
        s->baz = baz;
        n++;
    }

    fclose(f);

    /* Orden por distancia (selección simple; el motor no exige orden). */
    {
        int i, j;

        for (i = 1; i < n; i++) {
            CsnwphaseStation key = out[i];

            for (j = i - 1; j >= 0 && out[j].dist_km > key.dist_km; j--)
                out[j + 1] = out[j];
            out[j + 1] = key;
        }
    }

    return n;
}
