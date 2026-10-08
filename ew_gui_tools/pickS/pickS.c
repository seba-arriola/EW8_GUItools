/******************************************************************************
 * pickS.c — picker automático de onda S (Earthworm nativo).                  *
 *                                                                            *
 *   Uso: pickS <pickS.d>                    (modo anillo, producción)         *
 *        pickS <pickS.d> <tankfile> [ppicks] (modo offline, stdout)          *
 *                                                                            *
 * En modo offline lee un tank (TRACEBUF2 + int32) secuencialmente y emite    *
 * líneas TYPE_PICK_SCNL (fase S) a stdout. `ppicks` es opcional y aporta los *
 * picks P de guía (formato .picks de pick_FP). En modo anillo lee las ondas  *
 * de InRing y los picks P de PickRing, y publica los S en OutRing.           *
 ******************************************************************************/

#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "pickS.h"
#include <kom.h>
#include <earthworm.h>
#include <transport.h>

#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif
#ifndef TERMINATE
#define TERMINATE 0
#endif

typedef struct {
    PickS_Params cfg;
    unsigned char MyInstId, MyModId;
    unsigned char TypeHeartBeat, TypeError, TypePickSCNL, TypeTracebuf2;
    PickS_Station stations[PICKS_MAX_STA];
    int          nsta;
    PickS_Ring3C *rings;            /* paralelo a stations */
    PickS_PickRef *refs;
    int          nrefs;
    int          seq;
    int          offline;
    SHM_INFO     OutRegion;
    unsigned char OutInst;
} PickS_Ctx;

static PickS_Ctx ctx;

/*--------------------------------- utilidades -------------------------------*/

static long lmin3(long a, long b, long c) { long m = a < b ? a : b; return m < c ? m : c; }
static long lmax3(long a, long b, long c) { long m = a > b ? a : b; return m > c ? m : c; }

static double rms_comp(const PickS_Ring3C *r, int comp, long a, long b)
{
    double s = 0.0;
    long   i;
    if (b <= a) return 0.0;
    for (i = a; i < b; i++) { double v = r->buf[comp][i]; s += v * v; }
    return sqrt(s / (double)(b - a));
}

static double peak_amp(const PickS_Ring3C *r, int comp, long a, long b)
{
    double m = 0.0;
    long   i;
    if (b <= a) return 0.0;
    for (i = a; i < b; i++) { double v = fabs(r->buf[comp][i]); if (v > m) m = v; }
    return m;
}

/*----------------------------- reporte --------------------------------------*/

static void emit_pick(int sta_idx, int comp, double t, int weight, long amp)
{
    PickS_Station *st = &ctx.stations[sta_idx];
    const char *chan = st->chan[comp];
    char line[256];
    int  len;

    len = PickS_Report_Format(line, sizeof(line),
                              (int)ctx.MyModId, (int)ctx.MyInstId, ctx.seq++,
                              st->sta, chan, st->net, st->loc,
                              t, '?', weight, amp, 'S');

    if (ctx.offline) {
        printf("%s", line);
        fflush(stdout);
    } else {
        MSG_LOGO logo;
        logo.instid = ctx.MyInstId;
        logo.mod    = ctx.MyModId;
        logo.type   = ctx.TypePickSCNL;
        if (tport_putmsg(&ctx.OutRegion, &logo, (long)len, line) != PUT_OK) {
            logit("et", "pickS: error publicando pick S\n");
        }
    }
    logit("", "pickS: S %s.%s.%s.%s t=%.3f w=%d amp=%ld\n",
          st->sta, chan, st->net, st->loc, t, weight, amp);
}

/* Linea METRICS por canal lateral (stdout en offline, log en anillo).
 * NO toca el pick, el mensaje TYPE_PICK_SCNL ni el anillo: es solo
 * observabilidad para calibrar las metricas. */
static void metrics_emit(const PickS_Station *st, const char *chan, double t,
                         const PickS_Metrics *m, double azimuth_deg,
                         double dtsp, int weight, int kept)
{
    char line[512];
    snprintf(line, sizeof(line),
             "pickS: METRICS sta=%s chan=%s net=%s loc=%s t=%.3f snr=%.3f "
             "stalta=%.3f rect=%.4f plan=%.4f inc=%.2f azi=%.2f hv=%.4f "
             "dtsp=%.3f w=%d verdict=%s\n",
             st->sta, chan, st->net, st->loc, t, m->snr, m->stalta_peak,
             m->rectilinearity, m->planarity, m->incidence_deg, azimuth_deg,
             m->hv_ratio, dtsp, weight, kept ? "keep" : "drop");
    if (ctx.offline) { fputs(line, stdout); fflush(stdout); }
    else             { logit("", "%s", line); }
}

/*----------------------------- deteccion ------------------------------------*/

static void detect_station(int si, int final_pass)
{
    PickS_Params  *p = &ctx.cfg;
    PickS_Station *st = &ctx.stations[si];
    PickS_Ring3C  *r = &ctx.rings[si];
    long  hi_all, lo_all, lag;
    int   aiclen, polw, tw;
    double *env, *we, *wn, *wz;

    if (!r->inited) return;

    hi_all = lmin3(r->hi[PICKS_COMP_E], r->hi[PICKS_COMP_N], r->hi[PICKS_COMP_Z]);
    lo_all = lmax3(r->lo[PICKS_COMP_E], r->lo[PICKS_COMP_N], r->lo[PICKS_COMP_Z]);
    if (p->Debug >= 2)
        fprintf(stderr, "pickS: detect %s hiE=%ld hiN=%ld hiZ=%ld lo=%ld proc=%ld\n",
                st->sta, r->hi[PICKS_COMP_E], r->hi[PICKS_COMP_N], r->hi[PICKS_COMP_Z],
                (lo_all == LONG_MAX ? -1 : lo_all), r->proc_idx);
    if (hi_all == LONG_MIN || lo_all == LONG_MAX) return;

    lag = final_pass ? 0 :
          (long)((p->AicWinSec / 2.0 + p->PolWinSec / 2.0 + 1.0) * r->fs) + 2;

    aiclen = (int)(p->AicWinSec * r->fs);
    if (aiclen < 8) aiclen = 8;
    if (aiclen > 8192) aiclen = 8192;
    polw = (int)(p->PolWinSec * r->fs);
    if (polw < 4) polw = 4;
    if (polw > 8192) polw = 8192;

    env = (double *)malloc((size_t)aiclen * sizeof(double));
    we  = (double *)malloc((size_t)polw * sizeof(double));
    wn  = (double *)malloc((size_t)polw * sizeof(double));
    wz  = (double *)malloc((size_t)polw * sizeof(double));
    if (!env || !we || !wn || !wz) { free(env); free(we); free(wn); free(wz); return; }

    while (r->proc_idx <= hi_all - 1 - lag) {
        long   i = r->proc_idx;
        double ev, nv, envv, ratio, tnow;

        if (i < lo_all) { r->proc_idx = lo_all; continue; }

        ev   = r->buf[PICKS_COMP_E][i];
        nv   = r->buf[PICKS_COMP_N][i];
        envv = sqrt(ev * ev + nv * nv);
        ratio = PickS_StaLta_Update(&r->st, envv);
        tnow = r->t0 + (double)i / r->fs;

        if (p->Debug >= 3 && (i % 100) == 0)
            fprintf(stderr, "pickS: %s i=%ld t=%.2f env=%.3f ratio=%.3f\n",
                    st->sta, i, tnow, envv, ratio);

        if (!r->triggered && ratio > p->TriggerOn && tnow >= r->dead_until) {
            long a = i - aiclen / 2;
            int  k;
            double onset;

            if (p->Debug >= 2)
                fprintf(stderr, "pickS: trig %s i=%ld ratio=%.2f t=%.2f env=%.2f\n",
                        st->sta, i, ratio, tnow, envv);
            r->triggered = 1;
            if (a < lo_all) a = lo_all;

            if (a + aiclen <= hi_all) {
                for (k = 0; k < aiclen; k++) {
                    double ee = r->buf[PICKS_COMP_E][a + k];
                    double nn = r->buf[PICKS_COMP_N][a + k];
                    env[k] = sqrt(ee * ee + nn * nn);
                }
                if (PickS_Aic_Onset(env, aiclen, (int)(i - a), aiclen / 2, &onset) == 0) {
                    long   onset_idx = a + (long)floor(onset + 0.5);
                    double tpick = r->t0 + (double)onset_idx / r->fs;
                    long   pb = onset_idx - polw / 2;
                    long   pre_a, pre_b, post_a, post_b;
                    PickS_Pol pol;
                    PickS_Metrics m;
                    int    weight = 4, ok = 1;
                    int    comp = PICKS_COMP_E;
                    long   amp = 0;
                    double dtsp = -1.0;

                    if (pb < lo_all) pb = lo_all;
                    if (pb + polw > hi_all) pb = hi_all - polw;

                    if (pb >= lo_all && polw > 0) {
                        for (k = 0; k < polw; k++) {
                            we[k] = r->buf[PICKS_COMP_E][pb + k];
                            wn[k] = r->buf[PICKS_COMP_N][pb + k];
                            wz[k] = r->buf[PICKS_COMP_Z][pb + k];
                        }
                        PickS_Pol_Compute(we, wn, wz, polw, &pol);

                        /* SNR: RMS post (0.5s) / pre (0.5s) de la envolvente */
                        tw = (int)(0.5 * r->fs);
                        if (tw < 1) tw = 1;
                        pre_a = onset_idx - tw; pre_b = onset_idx - (tw / 4);
                        post_a = onset_idx;     post_b = onset_idx + tw;
                        if (pre_a < lo_all) pre_a = lo_all;
                        if (pre_b <= pre_a) pre_b = pre_a + 1;
                        if (post_b > hi_all) post_b = hi_all;
                        {
                            double rp = rms_comp(r, PICKS_COMP_E, pre_a, pre_b);
                            double rp2 = rms_comp(r, PICKS_COMP_N, pre_a, pre_b);
                            double ro = rms_comp(r, PICKS_COMP_E, post_a, post_b);
                            double ro2 = rms_comp(r, PICKS_COMP_N, post_a, post_b);
                            double pre = sqrt(rp * rp + rp2 * rp2);
                            double post = sqrt(ro * ro + ro2 * ro2);
                            m.snr = post / (pre + 1e-9);
                        }

                        /* pico STA/LTA en una ventana corta tras el onset */
                        {
                            long q, qb = onset_idx, qe = onset_idx + (long)(0.5 * r->fs);
                            double peak = ratio;
                            if (qe > hi_all) qe = hi_all;
                            for (q = qb; q < qe; q++) {
                                double ee = r->buf[PICKS_COMP_E][q];
                                double nn = r->buf[PICKS_COMP_N][q];
                                double rr = PickS_StaLta_Update(&r->st, sqrt(ee * ee + nn * nn));
                                if (rr > peak) peak = rr;
                            }
                            m.stalta_peak = peak;
                        }

                        m.rectilinearity = pol.rectilinearity;
                        m.planarity      = pol.planarity;
                        m.incidence_deg  = pol.incidence_deg;
                        m.hv_ratio       = pol.hv_ratio;

                        PickS_Quality_Weight(p, &m, &weight);

                        if (m.snr < p->MinSnr) ok = 0;
                        if (weight > p->MaxWeight) ok = 0;

                        /* Compuerta dura de incidencia (la S es ~horizontal,
                         * incidencia desde la vertical cercana a 90 grados).
                         * Deshabilitada si GateIncidMaxDeg <= GateIncidMinDeg. */
                        if (p->GateIncidMaxDeg > p->GateIncidMinDeg && p->GateIncidMaxDeg > 0.0) {
                            if (m.incidence_deg < p->GateIncidMinDeg ||
                                m.incidence_deg > p->GateIncidMaxDeg)
                                ok = 0;
                        }

                        /* Criterio S (modo hybrid): la S debe seguir a una P de
                         * la misma estación. Se evalúa SIEMPRE (para loguear el
                         * dtsp); si no hay P en la ventana, el pick se descarta.
                         * Esto evita etiquetar como S el ruido horizontal
                         * posterior a la P. */
                        if (p->GuideMode == PICKS_GUIDE_HYBRID && ctx.nrefs > 0) {
                            double tp = PickS_PickRef_Find(ctx.refs, ctx.nrefs, st->sta,
                                                           tpick, p->GuideMinDtSec,
                                                           p->GuideMaxDtSec);
                            if (tp >= 0.0) dtsp = tpick - tp;
                            else           ok = 0;
                        }

                        /* Canal reportado y amplitud (una sola vez). */
                        {
                            long qb = onset_idx, qe = onset_idx + (long)(0.2 * r->fs);
                            double ae, an;
                            if (qe > hi_all) qe = hi_all;
                            if (p->ReportChan == PICKS_REPORT_E) comp = PICKS_COMP_E;
                            else if (p->ReportChan == PICKS_REPORT_N) comp = PICKS_COMP_N;
                            else {
                                ae = peak_amp(r, PICKS_COMP_E, qb, qe);
                                an = peak_amp(r, PICKS_COMP_N, qb, qe);
                                comp = (ae >= an) ? PICKS_COMP_E : PICKS_COMP_N;
                            }
                            ae = peak_amp(r, PICKS_COMP_E, qb, qe);
                            an = peak_amp(r, PICKS_COMP_N, qb, qe);
                            amp = (long)(ae > an ? ae : an);
                        }

                        if (p->MetricsLog)
                            metrics_emit(st, st->chan[comp], tpick, &m, pol.azimuth_deg,
                                         dtsp, weight, ok);

                        if (ok) {
                            emit_pick(si, comp, tpick, weight, amp);
                            r->dead_until = tpick + p->DeadTimeSec;
                        } else if (p->Debug) {
                            logit("", "pickS: descartado %s t=%.3f snr=%.2f w=%d rect=%.2f inc=%.1f\n",
                                  st->sta, tpick, m.snr, weight, m.rectilinearity, m.incidence_deg);
                        }
                    }
                }
            }
        }

        if (r->triggered && ratio < p->TriggerOff) r->triggered = 0;
        r->proc_idx++;
    }

    free(env); free(we); free(wn); free(wz);
}

/*----------------------------- routing de paquetes --------------------------*/

static void route_packet(const char *sta, const char *chan, const char *net,
                         const char *loc, double starttime, double fs,
                         const int32_t *data, int nsamp)
{
    int comp = -1;
    int si = PickS_FindStation(ctx.stations, ctx.nsta, sta, chan, net, loc, &comp);

    if (ctx.cfg.Debug >= 2)
        fprintf(stderr, "pickS: pkt %s.%s.%s.%s n=%d fs=%.1f t=%.3f -> si=%d comp=%d\n",
                sta, chan, net, loc, nsamp, fs, starttime, si, comp);
    if (si < 0) return;

    if (!ctx.rings[si].inited) {
        if (PickS_Ring3C_Init(&ctx.rings[si], &ctx.stations[si], fs,
                              ctx.cfg.BufferSec, ctx.cfg.FilterLowHz,
                              ctx.cfg.FilterHighHz, ctx.cfg.FilterOrder) != 0)
            return;
        PickS_StaLta_Init(&ctx.rings[si].st, fs, ctx.cfg.StaLenSec, ctx.cfg.LtaLenSec);
    }

    PickS_Ring3C_Add(&ctx.rings[si], comp, starttime, fs, data, nsamp);
    detect_station(si, 0);
}

/*----------------------------- guia P ---------------------------------------*/

static void load_ppicks(const char *file)
{
    FILE *f = fopen(file, "r");
    char  line[512];
    if (!f) {
        fprintf(stderr, "pickS: no se pudo abrir ppicks <%s>\n", file);
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        char  sta[PICKS_SCNL], phase;
        double t;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (PickS_ParsePickLine(line, sta, &t, &phase) >= 7) {
            if (phase == 'P') PickS_PickRef_Add(ctx.refs, &ctx.nrefs, PICKS_MAX_PREF, sta, t);
        }
    }
    fclose(f);
}

/*----------------------------- modo offline ---------------------------------*/

static int run_offline(const char *tankfile, const char *ppicksfile)
{
    FILE *f;
    TRACE2_HEADER head;
    int32_t *data = NULL;
    int      maxsamp = 0;

    if (ppicksfile) load_ppicks(ppicksfile);

    f = fopen(tankfile, "rb");
    if (!f) { perror(tankfile); return -1; }

    while (fread(&head, sizeof(TRACE2_HEADER), 1, f) == 1) {
        if (head.nsamp <= 0 || head.nsamp > 200000) break;
        if (head.nsamp > maxsamp) {
            maxsamp = head.nsamp;
            data = (int32_t *)realloc(data, (size_t)maxsamp * sizeof(int32_t));
            if (!data) { fclose(f); return -1; }
        }
        if (fread(data, sizeof(int32_t), (size_t)head.nsamp, f) != (size_t)head.nsamp)
            break;

        route_packet(head.sta, head.chan, head.net, head.loc,
                     head.starttime, head.samprate, data, head.nsamp);
    }
    fclose(f);
    free(data);

    /* Pasada final: procesa las muestras que quedaron pendientes de contexto. */
    {
        int i;
        for (i = 0; i < ctx.nsta; i++) detect_station(i, 1);
    }
    return 0;
}

/*----------------------------- modo anillo ----------------------------------*/

static void status_beat(void)
{
    MSG_LOGO logo;
    char     msg[64];
    time_t   t;
    logo.instid = ctx.MyInstId;
    logo.mod    = ctx.MyModId;
    logo.type   = ctx.TypeHeartBeat;
    time(&t);
    snprintf(msg, sizeof(msg), "%ld %d\n", (long)t, (int)getpid());
    tport_putmsg(&ctx.OutRegion, &logo, (long)strlen(msg), msg);
}

static int run_ring(void)
{
    SHM_INFO inRegion, pickRegion;
    MSG_LOGO wavelogo, picklogo, logo;
    char    *buf;
    long     size;
    int      rc;
    time_t   then, now;
    long     bufsz = MAX_TRACEBUF_SIZ + sizeof(TRACE2_HEADER) + 64;

    buf = (char *)malloc((size_t)bufsz);
    if (!buf) return -1;

    tport_attach(&inRegion, ctx.cfg.InKey);
    if (ctx.cfg.PickKey != ctx.cfg.InKey) tport_attach(&pickRegion, ctx.cfg.PickKey);
    else pickRegion = inRegion;

    if (ctx.cfg.OutKey == ctx.cfg.PickKey) ctx.OutRegion = pickRegion;
    else tport_attach(&ctx.OutRegion, ctx.cfg.OutKey);

    wavelogo.instid = INST_WILDCARD;
    wavelogo.mod    = MOD_WILDCARD;
    wavelogo.type   = ctx.TypeTracebuf2;
    picklogo.instid = INST_WILDCARD;
    picklogo.mod    = MOD_WILDCARD;
    picklogo.type   = ctx.TypePickSCNL;

    /* Purga inicial de ondas y picks. */
    while (tport_getmsg(&inRegion, &wavelogo, 1, &logo, &size, buf, bufsz) == GET_OK) ;
    while (tport_getmsg(&pickRegion, &picklogo, 1, &logo, &size, buf, bufsz) == GET_OK) ;

    time(&then);
    logit("", "pickS: modo anillo iniciado (in=%s pick=%s out=%s)\n",
          ctx.cfg.InRingName, ctx.cfg.PickRingName, ctx.cfg.OutRingName);

    while (tport_getflag(&inRegion) != TERMINATE) {
        int did = 0;

        rc = tport_getmsg(&inRegion, &wavelogo, 1, &logo, &size, buf, bufsz);
        if (rc == GET_OK) {
            TRACE2_HEADER *h = (TRACE2_HEADER *)buf;
            int32_t *d = (int32_t *)(buf + sizeof(TRACE2_HEADER));
            route_packet(h->sta, h->chan, h->net, h->loc,
                         h->starttime, h->samprate, d, h->nsamp);
            did = 1;
        }

        while (tport_getmsg(&pickRegion, &picklogo, 1, &logo, &size, buf, bufsz) == GET_OK) {
            char  st[PICKS_SCNL], phase;
            double t;
            long  z = size;
            if (z >= bufsz) z = bufsz - 1;
            buf[z] = '\0';
            if (PickS_ParsePickLine(buf, st, &t, &phase) >= 7 && phase == 'P')
                PickS_PickRef_Add(ctx.refs, &ctx.nrefs, PICKS_MAX_PREF, st, t);
            did = 1;
        }

        time(&now);
        if (now - then >= ctx.cfg.HeartbeatInt) { then = now; status_beat(); }

        if (!did) sleep_ew(50);
    }

    tport_detach(&inRegion);
    if (ctx.cfg.PickKey != ctx.cfg.InKey) tport_detach(&pickRegion);
    if (ctx.cfg.OutKey != ctx.cfg.PickKey) tport_detach(&ctx.OutRegion);

    free(buf);
    return 0;
}

/*----------------------------- main -----------------------------------------*/

static int resolve_ew(void)
{
    if (GetLocalInst(&ctx.MyInstId) != 0) return -1;
    if (GetModId(ctx.cfg.MyModName, &ctx.MyModId) != 0) return -1;
    if (GetType("TYPE_HEARTBEAT", &ctx.TypeHeartBeat) != 0) return -1;
    if (GetType("TYPE_ERROR", &ctx.TypeError) != 0) return -1;
    if (GetType("TYPE_PICK_SCNL", &ctx.TypePickSCNL) != 0) return -1;
    if (GetType("TYPE_TRACEBUF2", &ctx.TypeTracebuf2) != 0) return -1;
    return 0;
}

int main(int argc, char **argv)
{
    int i;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Uso: pickS <pickS.d> [tankfile [ppicksfile]]\n");
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    PickS_SetDefaults(&ctx.cfg);

    if (PickS_ReadConfig(argv[1], &ctx.cfg) != 0) return 1;

    ctx.offline = (argc >= 3);
    ctx.rings = (PickS_Ring3C *)calloc(PICKS_MAX_STA, sizeof(PickS_Ring3C));
    ctx.refs  = (PickS_PickRef *)calloc(PICKS_MAX_PREF, sizeof(PickS_PickRef));
    if (!ctx.rings || !ctx.refs) { fprintf(stderr, "pickS: sin memoria\n"); return 1; }

    ctx.nsta = PickS_ReadStaList(ctx.cfg.StaFile, ctx.stations, PICKS_MAX_STA);
    if (ctx.nsta <= 0) {
        fprintf(stderr, "pickS: lista de estaciones vacia <%s>\n", ctx.cfg.StaFile);
        return 1;
    }

    if (resolve_ew() != 0) {
        if (!ctx.offline) {
            fprintf(stderr, "pickS: fallo resolviendo tablas Earthworm (¿source ew8_unix.sh?)\n");
            return 1;
        }
        fprintf(stderr, "pickS: aviso: tablas Earthworm no disponibles; modo offline con mod/inst=0\n");
        ctx.MyModId = 0;
        ctx.MyInstId = 0;
    }

    logit_init(argv[1], (short)ctx.MyModId, 16384, ctx.offline ? 0 : ctx.cfg.LogFile);

    if (ctx.offline) {
        const char *tank = argv[2];
        const char *pp   = (argc >= 4) ? argv[3] : NULL;
        int rc = run_offline(tank, pp);
        for (i = 0; i < ctx.nsta; i++) PickS_Ring3C_Free(&ctx.rings[i]);
        free(ctx.rings); free(ctx.refs);
        return (rc == 0) ? 0 : 1;
    }

    if (PickS_ResolveKeys(&ctx.cfg) != 0) return 1;
    {
        int rc = run_ring();
        for (i = 0; i < ctx.nsta; i++) PickS_Ring3C_Free(&ctx.rings[i]);
        free(ctx.rings); free(ctx.refs);
        return rc;
    }
}
