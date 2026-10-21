/******************************************************************************
 * pickml_main.c — CLI y bucle offline de pickml.                             *
 *                                                                            *
 * Modos:                                                                     *
 *   pickml <pickml.d> --tank <file> [--jsonl] [--stats]                     *
 *   pickml <pickml.d> --store <root> --t0 <t> --t1 <t> [--jsonl] [--stats]  *
 *   pickml <pickml.d> --print-config                                        *
 *   pickml <pickml.d> --check-model                                         *
 *                                                                            *
 * Salida: un JSON por pick, ordenado por (t, sta, fase) => determinista.     *
 * El modo anillo (SLINK_RING -> PICK_RING) es la Fase 4: no se activa hasta  *
 * pasar el gate de calidad offline.                                          *
 ******************************************************************************/

#include "pickml.h"
#include <errno.h>
#include <signal.h>
#include <unistd.h>

typedef struct {
    char   sta[PM_SCNL_LEN], net[PM_SCNL_LEN], loc[PM_SCNL_LEN], chan[PM_SCNL_LEN];
    char   phase;
    double t;
    double win_t0;
    double gen_t;               /* hora de pared en que se emitió (sólo live) */
    float  prob;
    int    weight;
} OutPick;

typedef struct {
    OutPick *v;
    long     n, cap;
} PickList;

typedef struct {
    long windows, gaps, dropped_thr, dropped_dup, late, unmatched, rate_rej, traces;
    long gated;                     /* S descartadas por la compuerta S-P */
    long emitted;                   /* picks aceptados (live no acumula lista) */
    double engine_ms;               /* tiempo acumulado dentro del motor */
    /* Latencia en modo live: `gen_lag` = emisión - tiempo de la picada y
     * `in_lag` = ahora - fin de la traza recibida (retraso de entrada). */
    double gen_lag_sum, gen_lag_max;
    long   gen_lag_n;
    double in_lag_sum, in_lag_max;
    long   in_lag_n;
} Stats;

typedef struct {
    PmBuf  buf;
    PmDedup dedup;
    PmWin  win;
    int    inited, win_inited, disabled, warned_rate;
    double last_p_t;                /* último P aceptado (compuerta S-P) */
    int    have_p;
} PmRun;

static PmConfig   cfg;
static PmStation  sta[PM_MAX_STA];
static int        nsta;
static PmEngine  *engine;
static PmRun     *run;
static PickList   out;
static Stats      stats;

/* --dump-curves: escribe las curvas crudas del modelo (float32) para el gate
 * de paridad numérica de check-engine. */
static const char *g_dump_prefix;
static FILE       *g_dump[3];
static FILE       *g_dump_idx;   /* sidecar <prefijo>.idx, una línea por ventana */
static const char *g_dump_suffix[3] = { "p", "s", "det" };

/* ---- modo live (--ring): escritura en streaming, sin acumular ---- */
static int   g_live;
static FILE *g_live_jsonl;
static FILE *g_live_picks;
static long  g_seq;
static volatile sig_atomic_t g_stop;

/* ---------------------------------------------------------------- utilidades */

static double win_sec(void)
{
    return (double)cfg.WindowSamples / cfg.Rate;
}

int pm_parse_time(const char *s, double *t)
{
    struct tm tm;
    int  y, mo, d, h, mi;
    double se;
    char sep;

    if (!s || !t) return PM_ERR;
    memset(&tm, 0, sizeof(tm));

    /* YYYY-MM-DD[T ]HH:MM:SS[.mmm] */
    if (sscanf(s, "%d-%d-%d%c%d:%d:%lf", &y, &mo, &d, &sep, &h, &mi, &se) == 7) {
        tm.tm_year = y - 1900;
        tm.tm_mon  = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min  = mi;
        tm.tm_sec  = (int)se;
        *t = (double)timegm(&tm) + (se - (double)(int)se);
        return PM_OK;
    }

    /* YYYYMMDDhhmmss */
    if (strlen(s) >= 14 && !strchr(s, '-') && !strchr(s, ':') && !strchr(s, '.')) {
        int ss = 0;
        if (sscanf(s, "%4d%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi, &ss) == 6) {
            tm.tm_year = y - 1900;
            tm.tm_mon  = mo - 1;
            tm.tm_mday = d;
            tm.tm_hour = h;
            tm.tm_min  = mi;
            tm.tm_sec  = ss;
            *t = (double)timegm(&tm);
            return PM_OK;
        }
    }

    /* epoch */
    {
        char *end = NULL;
        double v = strtod(s, &end);
        if (end && end != s && *end == '\0' && v > 0.0) { *t = v; return PM_OK; }
    }
    return PM_ERR;
}

void pm_fmt_utc(char *buf, size_t n, double t_epoch)
{
    time_t    tt = (time_t)floor(t_epoch);
    struct tm tmv;
    int       ms = (int)floor((t_epoch - (double)tt) * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; tt += 1; }
    if (ms < 0) ms = 0;
    gmtime_r(&tt, &tmv);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
}

/* Hora de pared con resolución de microsegundos. La usan las métricas de
 * latencia del modo live (la picada se fecha con el reloj de la muestra, no
 * con el de pared: la diferencia es justo lo que se quiere medir). */
static double now_epoch(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void picks_push(const OutPick *p)
{
    if (out.n == out.cap) {
        long    ncap = out.cap ? out.cap * 2 : 1024;
        OutPick *v = (OutPick *)realloc(out.v, (size_t)ncap * sizeof(OutPick));
        if (!v) {
            fprintf(stderr, "pickml: sin memoria para los picks\n");
            exit(3);
        }
        out.v = v;
        out.cap = ncap;
    }
    out.v[out.n++] = *p;
}

static int cmp_pick(const void *a, const void *b)
{
    const OutPick *x = (const OutPick *)a;
    const OutPick *y = (const OutPick *)b;
    int c;

    if (x->t < y->t) return -1;
    if (x->t > y->t) return 1;
    c = strcmp(x->sta, y->sta);
    if (c) return c;
    return (int)x->phase - (int)y->phase;
}

static void write_pick_json(FILE *f, const OutPick *p)
{
    char utc[64];

    pm_fmt_utc(utc, sizeof(utc), p->t);

    /* En offline la salida debe seguir siendo determinista (test_offline hace
     * cmp entre corridas y entre tank y store), así que la marca de generación
     * sólo viaja en modo live, donde sí tiene significado. */
    if (!g_live) {
        fprintf(f, "{\"sta\":\"%s\",\"net\":\"%s\",\"loc\":\"%s\",\"chan\":\"%s\","
                   "\"phase\":\"%c\",\"t_epoch\":%.6f,\"t_utc\":\"%s\","
                   "\"prob\":%.4f,\"weight\":%d,\"win_t0\":%.6f,\"model\":\"%s\"}\n",
                p->sta, p->net, p->loc, p->chan, p->phase, p->t, utc,
                (double)p->prob, p->weight, p->win_t0, cfg.ModelName);
        return;
    }

    {
        char   gen[64];
        double win_end = p->win_t0 + ((double)cfg.WindowSamples - 1.0) / cfg.Rate;

        pm_fmt_utc(gen, sizeof(gen), p->gen_t);
        fprintf(f, "{\"sta\":\"%s\",\"net\":\"%s\",\"loc\":\"%s\",\"chan\":\"%s\","
                   "\"phase\":\"%c\",\"t_epoch\":%.6f,\"t_utc\":\"%s\","
                   "\"prob\":%.4f,\"weight\":%d,\"win_t0\":%.6f,"
                   "\"gen_t_epoch\":%.6f,\"gen_t_utc\":\"%s\",\"lag_s\":%.3f,"
                   "\"proc_s\":%.3f,\"model\":\"%s\"}\n",
                p->sta, p->net, p->loc, p->chan, p->phase, p->t, utc,
                (double)p->prob, p->weight, p->win_t0,
                p->gen_t, gen, p->gen_t - p->t, p->gen_t - win_end,
                cfg.ModelName);
    }
}

static void write_pick_scnl(FILE *f, const OutPick *p, long seq)
{
    char line[512];

    if (pm_report_format(line, sizeof(line), 0, 0, (int)seq,
                         p->sta, p->chan, p->net, p->loc,
                         p->t, p->weight, p->phase) > 0)
        fputs(line, f);
}

static void emit_pick(int si, char phase, double t, float prob, double win_t0)
{
    int          weight = 4;
    int          comp;
    OutPick      p;

    if (!pm_prob_weight((double)prob, cfg.WProb, &weight)) {
        stats.dropped_thr++;
        return;
    }

    /* Misma regla que pm_gate_s_vs_p, pero contra el último P ACEPTADO: las
     * ventanas solapadas reparten un mismo arribo en ventanas contiguas, así
     * que el filtro intra-ventana no basta (medido: PB20 P 10.33 / S 10.80). */
    if (phase == 'S' && cfg.MinSPSec > 0.0 && run[si].have_p &&
        t >= run[si].last_p_t && (t - run[si].last_p_t) < cfg.MinSPSec) {
        stats.gated++;
        return;
    }

    if (!pm_dedup_accept(&run[si].dedup, t, phase, cfg.DedupSec)) {
        stats.dropped_dup++;
        return;
    }

    if (phase == 'P') {
        run[si].last_p_t = t;
        run[si].have_p   = 1;
    }

    comp = pm_sta_chan_for_phase(&sta[si], phase, &cfg);

    memset(&p, 0, sizeof(p));
    snprintf(p.sta, sizeof(p.sta), "%s", sta[si].sta);
    snprintf(p.net, sizeof(p.net), "%s", sta[si].net);
    snprintf(p.loc, sizeof(p.loc), "%s", sta[si].loc);
    snprintf(p.chan, sizeof(p.chan), "%s", sta[si].chan[comp]);
    p.phase  = phase;
    p.t      = t;
    p.win_t0 = win_t0;
    p.prob   = prob;
    p.weight = weight;
    p.gen_t  = now_epoch();

    if (g_live) {
        double lag = p.gen_t - p.t;

        stats.gen_lag_sum += lag;
        stats.gen_lag_n++;
        if (lag > stats.gen_lag_max) stats.gen_lag_max = lag;
    }

    stats.emitted++;

    if (g_live) {
        if (g_live_jsonl) {
            write_pick_json(g_live_jsonl, &p);
            fflush(g_live_jsonl);
        }
        if (g_live_picks) {
            write_pick_scnl(g_live_picks, &p, ++g_seq);
            fflush(g_live_picks);
        }
        return;
    }
    picks_push(&p);
}

/* Volcado crudo de las curvas del modelo (--dump-curves).
 *
 * Los tres streams de float32 no llevan ninguna marca temporal, asi que por si
 * solos no se pueden atribuir a una ventana. El sidecar `<prefijo>.idx` guarda
 * una linea por ventana, en el mismo orden de append, para que el ajuste del
 * post-proceso se haga offline (`tools/sweep_post.py`) sin volver a correr el
 * modelo. */
static void dump_curves(const float *p, const float *s, const float *d,
                        int si, double t0)
{
    int c;

    if (!g_dump[0]) {
        char path[PM_STR_LEN + 32];
        for (c = 0; c < 3; c++) {
            snprintf(path, sizeof(path), "%s.%s.f32",
                     g_dump_prefix, g_dump_suffix[c]);
            g_dump[c] = fopen(path, "wb");
            if (!g_dump[c]) {
                fprintf(stderr, "pickml: no se pudo escribir %s\n", path);
                g_dump_prefix = NULL;
                return;
            }
        }
        snprintf(path, sizeof(path), "%s.idx", g_dump_prefix);
        g_dump_idx = fopen(path, "w");
        if (!g_dump_idx) {
            fprintf(stderr, "pickml: no se pudo escribir %s\n", path);
            g_dump_prefix = NULL;
            return;
        }
        fprintf(g_dump_idx, "# t0_epoch rate win_samples sta net loc chan\n");
    }
    fwrite(p, sizeof(float), (size_t)cfg.WindowSamples, g_dump[0]);
    fwrite(s, sizeof(float), (size_t)cfg.WindowSamples, g_dump[1]);
    fwrite(d, sizeof(float), (size_t)cfg.WindowSamples, g_dump[2]);
    fprintf(g_dump_idx, "%.6f %.2f %ld %s %s %s %s\n", t0, cfg.Rate,
            cfg.WindowSamples, sta[si].sta, sta[si].net, sta[si].loc,
            sta[si].chan[0]);
}

static void advance(int si)
{
    static float *in, *cp, *cs, *cd;
    double t0, maxpos;

    if (!in) {
        in = (float *)malloc(sizeof(float) * 3 * (size_t)cfg.WindowSamples);
        cp = (float *)malloc(sizeof(float) * (size_t)cfg.WindowSamples);
        cs = (float *)malloc(sizeof(float) * (size_t)cfg.WindowSamples);
        cd = (float *)malloc(sizeof(float) * (size_t)cfg.WindowSamples);
        if (!in || !cp || !cs || !cd) {
            fprintf(stderr, "pickml: sin memoria para las ventanas\n");
            exit(3);
        }
    }
    if (!run[si].inited) return;

    /* La rejilla de ventanas arranca cuando las TRES componentes tienen datos
     * (no cuando llega la primera) y en el instante más tardío de sus primeros
     * datos: en un stream real HHE puede empezar segundos antes que HHN/HHZ y
     * si no, la primera ventana nace con un hueco de borde. */
    {
        double ready0 = pm_buf_ready(&run[si].buf);
        int    c;

        if (ready0 < 0.0) return;

        if (!run[si].win_inited) {
            double t_start = run[si].buf.t0;
            for (c = 0; c < PM_MAX_COMP; c++)
                if (run[si].buf.first_t[c] > t_start)
                    t_start = run[si].buf.first_t[c];
            pm_win_init(&run[si].win, t_start, cfg.StepSec, win_sec());
            run[si].win_inited = 1;
        }
    }

    maxpos = (cfg.MaxPosSec > 0.0) ? cfg.MaxPosSec : win_sec() - 1.0 / cfg.Rate;

    for (;;) {
        double ready = pm_buf_ready(&run[si].buf);
        long   interp = 0;
        int    rc;
        PmPick pkp[PM_MAX_EMIT], pks[PM_MAX_EMIT];
        int    np, ns, i;

        /* `ready` es el tiempo de la ULTIMA muestra recibida; una ventana de N
         * muestras abarca (N-1)/rate, no N/rate. Sin este término un tank con
         * exactamente 60 s (o el borde de un stream) nunca completaría la
         * primera ventana. */
        if (!pm_win_next(&run[si].win, ready + 1.0 / cfg.Rate, &t0)) break;

        rc = pm_buf_window(&run[si].buf, t0, cfg.WindowSamples, cfg.MaxGapSec,
                           in, &interp);
        if (rc != PM_OK) {
            stats.gaps++;
            continue;
        }
        stats.windows++;

        {
            struct timespec ta, tb;
            int erc;

            clock_gettime(CLOCK_MONOTONIC, &ta);
            erc = pm_engine_run(engine, in, 1, cfg.WindowSamples, cp, cs, cd);
            clock_gettime(CLOCK_MONOTONIC, &tb);
            stats.engine_ms += (double)(tb.tv_sec - ta.tv_sec) * 1e3 +
                               (double)(tb.tv_nsec - ta.tv_nsec) / 1e6;

            if (erc != PM_OK) {
                fprintf(stderr, "pickml: fallo del motor en la ventana %.3f\n", t0);
                continue;
            }
        }
        if (g_dump_prefix) dump_curves(cp, cs, cd, si, t0);

        np = pm_post_peaks(cp, cfg.WindowSamples, t0, cfg.Rate, cfg.PThreshold,
                           cfg.MinPosSec, maxpos, 'P', pkp, PM_MAX_EMIT);
        ns = pm_post_peaks(cs, cfg.WindowSamples, t0, cfg.Rate, cfg.SThreshold,
                           cfg.MinPosSec, maxpos, 'S', pks, PM_MAX_EMIT);

        /* Un arribo no puede ser P y S a la vez: se descarta la S que cae
         * pegada a un P de la misma estación (misma ventana). */
        ns = pm_gate_s_vs_p(pks, ns, pkp, np, cfg.MinSPSec);

        for (i = 0; i < np; i++)
            emit_pick(si, 'P', pkp[i].t, pkp[i].prob, t0);
        for (i = 0; i < ns; i++)
            emit_pick(si, 'S', pks[i].t, pks[i].prob, t0);
    }
}

/* -------------------------------------------------------------- traza a traza */

static void route_trace(const PmTrace *tr)
{
    int si, comp, rc;

    stats.traces++;

    si = pm_sta_find(sta, nsta, tr->sta, tr->chan, tr->net, tr->loc, &comp);
    if (si < 0) {
        stats.unmatched++;
        return;
    }
    if (run[si].disabled) return;

    if (fabs(tr->rate - cfg.Rate) > cfg.RateTolHz) {
        run[si].disabled = 1;
        stats.rate_rej++;
        fprintf(stderr,
                "pickml: %s.%s: tasa %.3f Hz no soportada (se exige %.3f +/- %.3f); "
                "estación deshabilitada\n",
                tr->sta, tr->chan, tr->rate, cfg.Rate, cfg.RateTolHz);
        return;
    }

    if (!run[si].inited) {
        long cap = (long)ceil((win_sec() + 10.0) * cfg.Rate);
        if (pm_buf_init(&run[si].buf, cfg.Rate, cap) != PM_OK) {
            run[si].disabled = 1;
            fprintf(stderr, "pickml: %s: no se pudo crear el buffer\n", tr->sta);
            return;
        }
        run[si].inited = 1;
    }

    /* Retraso de entrada: cuánto tiempo ha pasado desde el fin de la traza
     * hasta que el módulo la procesa. Mide si el consumo va al día con el
     * anillo (no depende de la ventana del modelo). */
    if (g_live) {
        double d = now_epoch() - (tr->t0 + (double)tr->n / tr->rate);

        stats.in_lag_sum += d;
        stats.in_lag_n++;
        if (d > stats.in_lag_max) stats.in_lag_max = d;
    }

    /* Un registro puede ser mayor que el buffer (un tank con un bloque de
     * 60 s y una ventana nativa de 30 s). Se trocea en trozos que quepan:
     * rechazarlo o truncarlo perdería arribos tempranos. */
    {
        long cap  = run[si].buf.cap;
        long done = 0;

        while (done < tr->n) {
            long chunk = tr->n - done;
            if (chunk > cap) chunk = cap;

            rc = pm_buf_add(&run[si].buf, comp,
                            tr->t0 + (double)done / tr->rate,
                            tr->rate, tr->samples + done, chunk);
            if (rc == PM_ERR) {
                run[si].disabled = 1;
                fprintf(stderr, "pickml: %s.%s: paquete rechazado por el buffer\n",
                        tr->sta, tr->chan);
                return;
            }
            if (rc == PM_NODATA) stats.late++;

            done += chunk;
            advance(si);
        }
    }
}

/* ------------------------------------------------------------------ salida */

static void print_jsonl(FILE *f)
{
    long i;

    for (i = 0; i < out.n; i++)
        write_pick_json(f, &out.v[i]);
}

/* TYPE_PICK_SCNL en texto: el mismo formato que el mensaje del anillo, para
 * poder comparar el archivo con lo que publicará el modo vivo. mod/inst van a
 * 0: en el anillo los pone el módulo, aquí es sólo un archivo de calibración. */
static void write_picks_text(FILE *f)
{
    long i;

    for (i = 0; i < out.n; i++)
        write_pick_scnl(f, &out.v[i], i + 1);
}

/* Cabecera de la corrida: sin esto un JSONL no es reproducible. */
static void write_meta(FILE *f, const char *source, double t0, double t1)
{
    fprintf(f,
        "{\"model\":\"%s\",\"model_file\":\"%s\",\"engine\":\"%s\","
        "\"window_samples\":%ld,\"rate\":%.3f,\"step_sec\":%.3f,"
        "\"min_pos_sec\":%.3f,\"blind_samples\":%d,"
        "\"p_threshold\":%.3f,\"s_threshold\":%.3f,"
        "\"w_prob\":[%.3f,%.3f,%.3f,%.3f],\"dedup_sec\":%.3f,"
        "\"min_sp_sec\":%.3f,\"source\":\"%s\",\"t0\":%.3f,\"t1\":%.3f,"
        "\"stations\":%d,\"windows\":%ld,\"picks\":%ld,"
        "\"engine_ms_total\":%.1f,"
        "\"lag_ms_medio\":%.1f,\"lag_ms_max\":%.1f,"
        "\"entrada_ms_medio\":%.1f,\"entrada_ms_max\":%.1f}\n",
        cfg.ModelName, cfg.ModelFile, pm_engine_name(engine),
        cfg.WindowSamples, cfg.Rate, cfg.StepSec,
        cfg.MinPosSec, cfg.BlindSamples,
        cfg.PThreshold, cfg.SThreshold,
        cfg.WProb[0], cfg.WProb[1], cfg.WProb[2], cfg.WProb[3],
        cfg.DedupSec, cfg.MinSPSec, source, t0, t1,
        nsta, stats.windows, g_live ? stats.emitted : out.n, stats.engine_ms,
        stats.gen_lag_n ? 1e3 * stats.gen_lag_sum / stats.gen_lag_n : 0.0,
        1e3 * stats.gen_lag_max,
        stats.in_lag_n ? 1e3 * stats.in_lag_sum / stats.in_lag_n : 0.0,
        1e3 * stats.in_lag_max);
}

static int write_outputs(const char *jsonl, const char *picks, const char *meta,
                         const char *source, double t0, double t1)
{
    FILE *f;

    if (jsonl && jsonl[0]) {
        f = fopen(jsonl, "w");
        if (!f) { fprintf(stderr, "pickml: no se pudo escribir %s\n", jsonl); return PM_ERR; }
        print_jsonl(f);
        fclose(f);
    } else {
        print_jsonl(stdout);
    }

    if (picks && picks[0]) {
        f = fopen(picks, "w");
        if (!f) { fprintf(stderr, "pickml: no se pudo escribir %s\n", picks); return PM_ERR; }
        write_picks_text(f);
        fclose(f);
    }

    if (meta && meta[0]) {
        f = fopen(meta, "w");
        if (!f) { fprintf(stderr, "pickml: no se pudo escribir %s\n", meta); return PM_ERR; }
        write_meta(f, source, t0, t1);
        fclose(f);
    }
    return PM_OK;
}

static void print_stats(FILE *f)
{
    double gen_ms = stats.gen_lag_n ? 1e3 * stats.gen_lag_sum / stats.gen_lag_n : 0.0;
    double in_ms  = stats.in_lag_n  ? 1e3 * stats.in_lag_sum  / stats.in_lag_n  : 0.0;

    fprintf(f,
        "pickml: trazas=%ld ventanas=%ld picks=%ld umbral=%ld dup=%ld "
        "huecos=%ld tardios=%ld sin_estacion=%ld tasa=%ld compuerta=%ld "
        "motor_ms_total=%.1f motor_ms_ventana=%.1f "
        "lag_ms_medio=%.0f lag_ms_max=%.0f entrada_ms_medio=%.0f "
        "entrada_ms_max=%.0f\n",
        stats.traces, stats.windows, stats.emitted, stats.dropped_thr, stats.dropped_dup,
        stats.gaps, stats.late, stats.unmatched, stats.rate_rej, stats.gated,
        stats.engine_ms,
        stats.windows > 0 ? stats.engine_ms / (double)stats.windows : 0.0,
        gen_ms, 1e3 * stats.gen_lag_max, in_ms, 1e3 * stats.in_lag_max);
}

/* ------------------------------------------------------------------ live */

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int run_live(const char *out_path, const char *picks_path,
                    const char *meta_path)
{
    PmRing *ring = NULL;
    PmTrace tr;
    char    err[PM_STR_LEN];
    time_t  now, last;
    int     rc;

    g_live = 1;
    g_live_jsonl = (out_path && out_path[0]) ? fopen(out_path, "w") : stdout;
    if (!g_live_jsonl) {
        fprintf(stderr, "pickml: no se pudo escribir %s\n", out_path);
        return 4;
    }
    if (picks_path && picks_path[0]) {
        g_live_picks = fopen(picks_path, "w");
        if (!g_live_picks) {
            fprintf(stderr, "pickml: no se pudo escribir %s\n", picks_path);
            return 4;
        }
    }

    if (pm_ring_open(&ring, cfg.InRingName, 500, err, sizeof(err)) != PM_OK) {
        fprintf(stderr, "pickml: %s\n", err);
        return 4;
    }

    if (meta_path && meta_path[0]) {
        FILE *f = fopen(meta_path, "w");
        if (f) { write_meta(f, "ring", 0.0, 0.0); fclose(f); }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr,
        "pickml: live en %s | %s | ventana %ld (%.2f s) paso %.1f s\n",
        cfg.InRingName, cfg.ModelName, cfg.WindowSamples, win_sec(), cfg.StepSec);

    time(&last);
    while (!g_stop) {
        rc = pm_ring_next(ring, &tr);
        if (rc < 0) break;
        if (rc == 0) { usleep(20000); continue; }

        time(&now);
        /* Arrancamos en vivo: una traza más vieja que LiveMaxAgeSec es historial
         * que quedó en el anillo, no dato actual (y llenaría el buffer de atrás). */
        if (cfg.LiveMaxAgeSec > 0 &&
            tr.t0 + (double)tr.n / tr.rate < (double)now - (double)cfg.LiveMaxAgeSec)
            continue;

        route_trace(&tr);

        if (now - last >= 60) { print_stats(stderr); last = now; }
    }

    pm_ring_close(ring);
    if (g_live_jsonl && g_live_jsonl != stdout) fclose(g_live_jsonl);
    if (g_live_picks) fclose(g_live_picks);

    /* La primera cabecera se escribe al arrancar (si el proceso muere de golpe
     * deja constancia de la config); al salir se reescribe con los contadores. */
    if (meta_path && meta_path[0]) {
        FILE *f = fopen(meta_path, "w");
        if (f) { write_meta(f, "ring", 0.0, 0.0); fclose(f); }
    }

    print_stats(stderr);
    return 0;
}

/* ---------------------------------------------------------------------- main */

static void usage(void)
{
    fprintf(stderr,
        "Uso: pickml <pickml.d> --tank <file> [--jsonl] [--stats]\n"
        "     pickml <pickml.d> --store <root> --t0 <t> --t1 <t> [--jsonl] [--stats]\n"
        "     pickml <pickml.d> --sds <root> --t0 <t> --t1 <t> [--jsonl] [--stats]\n"
        "     pickml <pickml.d> --ring                 (live: escribe archivos)\n"
        "     pickml <pickml.d> --print-config\n"
        "     pickml <pickml.d> --check-model\n"
        "     --out <file>        JSONL de picks (vacío = stdout)\n"
        "     --picks-out <file>  líneas TYPE_PICK_SCNL en texto\n"
        "     --meta <file>       cabecera JSON de la corrida\n"
        "     --dump-curves <prefijo>   escribe <prefijo>.{p,s,det}.f32 (paridad)\n"
        "Las claves PickOutFile/PicksOutFile/RunMetaFile del .d hacen lo mismo.\n"
        "En --ring la salida es en streaming y se corta con Ctrl-C.\n");
}

int main(int argc, char **argv)
{
    const char *d = NULL, *tank = NULL, *store = NULL, *sds = NULL;
    const char *t0s = NULL, *t1s = NULL, *dump_prefix = NULL;
    const char *out_path = NULL, *picks_path = NULL, *meta_path = NULL;
    int print_cfg = 0, check_model = 0, want_stats = 0, use_ring = 0;
    char err[PM_STR_LEN];
    double t0 = 0.0, t1 = 0.0;
    PmSrc *src = NULL;
    PmTrace tr;
    int i, rc;

    for (i = 1; i < argc; i++) {
        if (!d && argv[i][0] != '-') { d = argv[i]; continue; }
        if      (!strcmp(argv[i], "--tank"))         { if (++i < argc) tank = argv[i]; }
        else if (!strcmp(argv[i], "--store"))        { if (++i < argc) store = argv[i]; }
        else if (!strcmp(argv[i], "--sds"))          { if (++i < argc) sds = argv[i]; }
        else if (!strcmp(argv[i], "--ring"))         { use_ring = 1; }
        else if (!strcmp(argv[i], "--t0"))           { if (++i < argc) t0s = argv[i]; }
        else if (!strcmp(argv[i], "--t1"))           { if (++i < argc) t1s = argv[i]; }
        else if (!strcmp(argv[i], "--jsonl"))        { /* salida por defecto */ }
        else if (!strcmp(argv[i], "--stats"))        { want_stats = 1; }
        else if (!strcmp(argv[i], "--print-config")) { print_cfg = 1; }
        else if (!strcmp(argv[i], "--check-model"))  { check_model = 1; }
        else if (!strcmp(argv[i], "--dump-curves"))  { if (++i < argc) dump_prefix = argv[i]; }
        else if (!strcmp(argv[i], "--out"))          { if (++i < argc) out_path = argv[i]; }
        else if (!strcmp(argv[i], "--picks-out"))    { if (++i < argc) picks_path = argv[i]; }
        else if (!strcmp(argv[i], "--meta"))         { if (++i < argc) meta_path = argv[i]; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else { fprintf(stderr, "pickml: opción desconocida %s\n", argv[i]); usage(); return 2; }
    }

    if (!d) { usage(); return 2; }

    pm_config_defaults(&cfg);
    if (pm_config_load(d, &cfg, err, sizeof(err)) != PM_OK) {
        fprintf(stderr, "pickml: %s\n", err);
        return 2;
    }
    if (print_cfg) {
        pm_config_print(&cfg, stdout);
        return 0;
    }

    /* La CLI manda sobre el .d: la misma configuración sirve para N eventos. */
    if (!out_path && cfg.PickOutFile[0])    out_path   = cfg.PickOutFile;
    if (!picks_path && cfg.PicksOutFile[0]) picks_path = cfg.PicksOutFile;
    if (!meta_path && cfg.RunMetaFile[0])   meta_path  = cfg.RunMetaFile;

    nsta = pm_sta_read(cfg.StaFile, sta, PM_MAX_STA);
    if (nsta < 0) return 2;
    if (nsta == 0) {
        fprintf(stderr, "pickml: %s no tiene estaciones activas\n", cfg.StaFile);
        return 2;
    }

    run = (PmRun *)calloc((size_t)nsta, sizeof(PmRun));
    if (!run) { fprintf(stderr, "pickml: sin memoria\n"); return 3; }

    /* El modo se valida ANTES de abrir el motor: si no, un modo no
     * implementado se reportaría como "falta ONNX Runtime" (mentira útil sólo
     * para el que ya sabe el orden interno). */
    if (check_model) {
        if (pm_engine_open(&engine, &cfg, err, sizeof(err)) != PM_OK) {
            fprintf(stderr, "pickml: %s\n", err);
            return 3;
        }
        printf("pickml: motor %s listo (%d estaciones en %s)\n",
               pm_engine_name(engine), nsta, cfg.StaFile);
        pm_engine_close(engine);
        free(run);
        return 0;
    }

    if ((tank != NULL) + (store != NULL) + (sds != NULL) + (use_ring != 0) != 1) {
        fprintf(stderr,
            "pickml: elegir exactamente un modo: --tank, --store, --sds o --ring\n");
        return 2;
    }

    if (pm_engine_open(&engine, &cfg, err, sizeof(err)) != PM_OK) {
        fprintf(stderr, "pickml: %s\n", err);
        return 3;
    }

    g_dump_prefix = dump_prefix;

    if (use_ring) {
        int lrc = run_live(out_path, picks_path, meta_path);
        for (i = 0; i < nsta; i++)
            if (run[i].inited) pm_buf_free(&run[i].buf);
        pm_engine_close(engine);
        free(run);
        free(out.v);
        return lrc;
    }

    if (tank) {
        if (pm_src_open_tank(&src, tank) != PM_OK) {
            fprintf(stderr, "pickml: no se pudo abrir el tank %s\n", tank);
            return 4;
        }
    } else if (store) {
        if (!t0s || !t1s ||
            pm_parse_time(t0s, &t0) != PM_OK || pm_parse_time(t1s, &t1) != PM_OK ||
            t1 <= t0) {
            fprintf(stderr, "pickml: --store exige --t0 y --t1 válidos (t1 > t0)\n");
            return 2;
        }
        if (pm_src_open_store(&src, store, sta, nsta, t0, t1) != PM_OK) {
            fprintf(stderr, "pickml: no se pudo abrir el store %s\n", store);
            return 4;
        }
    } else if (sds) {
        if (!t0s || !t1s ||
            pm_parse_time(t0s, &t0) != PM_OK || pm_parse_time(t1s, &t1) != PM_OK ||
            t1 <= t0) {
            fprintf(stderr, "pickml: --sds exige --t0 y --t1 válidos (t1 > t0)\n");
            return 2;
        }
        if (pm_src_open_sds(&src, sds, sta, nsta, t0, t1, cfg.Rate) != PM_OK) {
            fprintf(stderr, "pickml: no se pudo abrir el SDS %s\n", sds);
            return 4;
        }
    } else {
        /* No alcanzable: la validación de modo exige exactamente uno. */
        fprintf(stderr, "pickml: modo invalido\n");
        return 2;
    }

    while ((rc = pm_src_next(src, &tr)) > 0)
        route_trace(&tr);
    if (rc < 0) {
        fprintf(stderr, "pickml: fuente abortada: %s\n", pm_src_error(src));
        pm_src_close(src);
        return 4;
    }
    if (want_stats && sds && src) {
        PmSrcCounts sc;
        pm_src_counts(src, &sc);
        fprintf(stderr, "pickml: sds sin_dia=%ld sin_datos=%ld tasa=%ld\n",
                sc.no_day, sc.nodata, sc.rate);
    }
    pm_src_close(src);

    qsort(out.v, (size_t)out.n, sizeof(OutPick), cmp_pick);
    if (write_outputs(out_path, picks_path, meta_path,
                      tank ? "tank" : (store ? "store" : "sds"),
                      t0, t1) != PM_OK) {
        for (i = 0; i < nsta; i++)
            if (run[i].inited) pm_buf_free(&run[i].buf);
        pm_engine_close(engine);
        free(run);
        free(out.v);
        return 4;
    }
    if (want_stats) print_stats(stderr);

    for (i = 0; i < 3; i++)
        if (g_dump[i]) { fclose(g_dump[i]); g_dump[i] = NULL; }
    if (g_dump_idx) { fclose(g_dump_idx); g_dump_idx = NULL; }
    for (i = 0; i < nsta; i++)
        if (run[i].inited) pm_buf_free(&run[i].buf);
    pm_engine_close(engine);
    free(run);
    free(out.v);
    return 0;
}
