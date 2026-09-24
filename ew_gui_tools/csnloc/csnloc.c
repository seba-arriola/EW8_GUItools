/******************************************************************************
 * csnloc.c                                                                   *
 *                                                                            *
 * Módulo Earthworm nativo de asociación de fases + localización hipocentral   *
 * rápida. Reemplaza localmente el camino ew2glass -> GLASS3 -> glass2ew.      *
 *                                                                            *
 * Pipeline:                                                                  *
 *   PICK_RING -> buffer de picks -> back-projection 4D + DBSCAN ->           *
 *   refinamiento local -> TYPE_HYP2000ARC -> HYPO_RING                       *
 *                                                                            *
 * Uso: csnloc <csnloc.d>                  (modo anillo, produccion)          *
 *      csnloc <csnloc.d> <picksfile>      (modo offline, reloj virtual)     *
 *                                                                             *
 * Modo offline: lee picks TYPE_PICK_SCNL de un fichero (los que produce      *
 * pick_FP offline) y los procesa con un reloj VIRTUAL (now = t_epoch del     *
 * pick) en vez de time(). Emite un JSON por evento a stdout. No toca rings.  *
 ******************************************************************************/

#include <signal.h>
#include <unistd.h>

#include "csnloc.h"
#include <kom.h>
#include <earthworm.h>

#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif

/* Valores por defecto (todos sobreescribibles desde csnloc.d). */
static void set_defaults(CSLocParams *c)
{
    memset(c, 0, sizeof(*c));
    strcpy(c->MyModName, "MOD_CSNLOC");
    strcpy(c->InRingName, "PICK_RING");
    strcpy(c->OutRingName, "HYPO_RING");
    c->InKey = 0;
    c->OutKey = 0;
    c->HeartbeatInt = 30;
    c->LogFile = 1;
    c->Debug = 0;
    c->DumpHypo = 0;

    strcpy(c->StaFile, "estaciones_107.txt");
    strcpy(c->TauModel, "iasp91");

    c->n_gridfiles = 0;
    c->GridActivationMinPicks = 3;
    c->GridActivationMarginDeg = 1.0;
    c->EventDedupSec = 30.0;
    c->EventDedupKm  = 100.0;

    c->AssocWindowSec = 120.0;
    c->RePickWindowSec = 10.0;
    c->PickTTLSec = 300.0;
    c->T0ToleranceSec = 2.0;
    c->DBSCAN_Eps = 60.0;
    c->DBSCAN_MinPts = 3;
    c->BackProjThreshold = 0.0;   /* 0 -> automatico (50% del maximo) */
    c->MaxEventsPerWindow = 5;
    c->MinPhasesPerEvent = 3;
    c->MaxRMS = 2.0;
    c->PhaseWeightP = 1.0;
    c->PhaseWeightS = 0.8;

    c->NumThreads = 4;
    c->RefineIterations = 3;
    c->RefineNodeKm = 5.0;

    strcpy(c->AgencyID, "CL");
    strcpy(c->Author, "csnloc");
    c->EventTTLSec = 300.0;
}

/* Registra un archivo de grilla con su nivel (GRID_LEVEL_*). */
static void add_gridfile(CSLocParams *c, const char *path, int level)
{
    if (!c || !path || !path[0]) return;
    if (c->n_gridfiles >= CSLOC_MAX_GRIDS) {
        fprintf(stderr, "csnloc: demasiadas grillas (max %d), ignoro <%s>\n",
                CSLOC_MAX_GRIDS, path);
        return;
    }
    strncpy(c->GridFiles[c->n_gridfiles], path, CSLOC_STR - 1);
    c->GridFiles[c->n_gridfiles][CSLOC_STR - 1] = '\0';
    c->GridFileLevel[c->n_gridfiles] = level;
    c->n_gridfiles++;
}

static int read_config(const char *file, CSLocParams *c)
{
    char *com, *str;

    if (!k_open(file)) {
        fprintf(stderr, "csnloc: no se pudo abrir %s\n", file);
        return -1;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if      (k_its("MyModuleId"))   { str = k_str(); if (str) strncpy(c->MyModName, str, sizeof(c->MyModName)-1); }
        else if (k_its("InRing"))       { str = k_str(); if (str) strncpy(c->InRingName, str, sizeof(c->InRingName)-1); }
        else if (k_its("OutRing"))      { str = k_str(); if (str) strncpy(c->OutRingName, str, sizeof(c->OutRingName)-1); }
        else if (k_its("HeartBeatInt")) c->HeartbeatInt = k_int();
        else if (k_its("LogFile"))      c->LogFile = k_int();
        else if (k_its("Debug"))        c->Debug = k_int();
        else if (k_its("DumpHypo"))     c->DumpHypo = k_int();
        else if (k_its("StaFile"))      { str = k_str(); if (str) strncpy(c->StaFile, str, sizeof(c->StaFile)-1); }
        else if (k_its("TauTable"))     { str = k_str(); if (str) {
                                          /* permite "iasp91.tbl" o "iasp91" */
                                          char t[CSLOC_STR];
                                          snprintf(t, sizeof(t), "%s", str);
                                          if (strstr(t, ".tbl")) *strstr(t, ".tbl") = '\0';
                                          snprintf(c->TauModel, sizeof(c->TauModel), "%s", t);
                                          } }
        else if (k_its("GlobalGrid"))   { str = k_str(); add_gridfile(c, str, GRID_LEVEL_GLOBAL); }
        else if (k_its("RegionalGrid")) { str = k_str(); add_gridfile(c, str, GRID_LEVEL_REGIONAL); }
        else if (k_its("LocalGrid"))    { str = k_str(); add_gridfile(c, str, GRID_LEVEL_LOCAL); }
        else if (k_its("GridActivationMinPicks")) c->GridActivationMinPicks = k_int();
        else if (k_its("GridActivationMarginDeg")) c->GridActivationMarginDeg = k_val();
        else if (k_its("EventDedupSec")) c->EventDedupSec = k_val();
        else if (k_its("EventDedupKm"))  c->EventDedupKm = k_val();
        else if (k_its("AssocWindowSec")) c->AssocWindowSec = k_val();
        else if (k_its("RePickWindowSec")) c->RePickWindowSec = k_val();
        else if (k_its("PickTTLSec"))   c->PickTTLSec = k_val();
        else if (k_its("T0ToleranceSec")) c->T0ToleranceSec = k_val();
        else if (k_its("DBSCAN_Eps"))   c->DBSCAN_Eps = k_val();
        else if (k_its("DBSCAN_MinPts"))c->DBSCAN_MinPts = k_int();
        else if (k_its("BackProjThreshold")) c->BackProjThreshold = k_val();
        else if (k_its("MaxEventsPerWindow")) c->MaxEventsPerWindow = k_int();
        else if (k_its("MinPhasesPerEvent")) c->MinPhasesPerEvent = k_int();
        else if (k_its("MaxRMS"))       c->MaxRMS = k_val();
        else if (k_its("PhaseWeightP")) c->PhaseWeightP = k_val();
        else if (k_its("PhaseWeightS")) c->PhaseWeightS = k_val();
        else if (k_its("NumThreads"))   c->NumThreads = k_int();
        else if (k_its("RefineIterations")) c->RefineIterations = k_int();
        else if (k_its("RefineNodeKm")) c->RefineNodeKm = k_val();
        else if (k_its("AgencyID"))     { str = k_str(); if (str) strncpy(c->AgencyID, str, sizeof(c->AgencyID)-1); }
        else if (k_its("Author"))       { str = k_str(); if (str) strncpy(c->Author, str, sizeof(c->Author)-1); }
        else if (k_its("EventTTLSec"))  c->EventTTLSec = k_val();
        else continue;

        if (k_err()) { fprintf(stderr, "csnloc: error de config en '%s'\n", com); k_close(); return -1; }
    }
    k_close();

    /* Resolver claves de anillo. */
    c->InKey = GetKey(c->InRingName);
    if (c->InKey == -1) { fprintf(stderr, "csnloc: anillo invalido <%s>\n", c->InRingName); return -1; }
    c->OutKey = GetKey(c->OutRingName);
    if (c->OutKey == -1) { fprintf(stderr, "csnloc: anillo invalido <%s>\n", c->OutRingName); return -1; }
    return 0;
}

static void lookup(CSLocCtx *ctx)
{
    if (GetLocalInst(&ctx->MyInstId) != 0) { fprintf(stderr, "csnloc: GetLocalInst fallo\n"); exit(1); }
    if (GetModId(ctx->cfg.MyModName, &ctx->MyModId) != 0) { fprintf(stderr, "csnloc: modulo invalido <%s>\n", ctx->cfg.MyModName); exit(1); }
    if (GetType("TYPE_HEARTBEAT", &ctx->TypeHeartBeat) != 0) { fprintf(stderr, "csnloc: TYPE_HEARTBEAT invalido\n"); exit(1); }
    if (GetType("TYPE_ERROR", &ctx->TypeError) != 0) { fprintf(stderr, "csnloc: TYPE_ERROR invalido\n"); exit(1); }
    if (GetType("TYPE_PICK_SCNL", &ctx->TypePickSCNL) != 0) { fprintf(stderr, "csnloc: TYPE_PICK_SCNL invalido\n"); exit(1); }
    if (GetType("TYPE_HYP2000ARC", &ctx->TypeHyp2000Arc) != 0) { fprintf(stderr, "csnloc: TYPE_HYP2000ARC invalido\n"); exit(1); }
}

static void status(CSLocCtx *ctx, unsigned char type, short ierr, const char *note)
{
    MSG_LOGO logo;
    char     msg[512];
    time_t   t;

    logo.instid = ctx->MyInstId;
    logo.mod    = ctx->MyModId;
    logo.type   = type;

    time(&t);
    if (type == ctx->TypeHeartBeat) {
        snprintf(msg, sizeof(msg), "%ld %d\n", (long)t, (int)ctx->MyPid);
    } else {
        snprintf(msg, sizeof(msg), "%ld %hd %s\n", (long)t, ierr, note ? note : "");
        logit("et", "csnloc: error: %s\n", note ? note : "");
    }
    tport_putmsg(&ctx->OutRegion, &logo, (long)strlen(msg), msg);
}

static void sig_handler(int sig)
{
    (void)sig;
    /* Deja que el loop principal vea TERMINATE via tport_getflag. */
    fprintf(stderr, "csnloc: señal recibida, terminando...\n");
    _exit(0);
}

/* ------------------------------------------------------------------------- */
/* Epoch (segundos, UTC) -> "YYYY-MM-DDTHH:MM:SS.mmmZ".                       */
/* ------------------------------------------------------------------------- */
static void Epoch_ToISO(double t, char *buf, int buflen)
{
    time_t    sec = (time_t)t;
    struct tm tm;
    double    frac = t - (double)sec;
    char      base[24];
    int       ms;

    if (frac < 0.0) { frac += 1.0; sec -= 1; }
    ms = (int)(frac * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; sec += 1; }
    if (ms < 0)   ms = 0;
    if (ms > 999) ms = 999;
    gmtime_r(&sec, &tm);

    /* strftime da ancho fijo "YYYY-MM-DDTHH:MM:SS" (19 chars). */
    if (strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm) == 0) {
        snprintf(buf, buflen, "?");
        return;
    }
    snprintf(buf, buflen, "%s.%03dZ", base, ms);
}

/* ------------------------------------------------------------------------- */
/* Emision de un evento: JSON a stdout (offline) o HYP2000ARC al anillo.      */
/* ------------------------------------------------------------------------- */
static void emit_hypo(CSLocCtx *ctx, const HypoCandidate *h,
                      const Pick *window, int nwin, char *arc)
{
    if (ctx->Offline) {
        char t0iso[32];
        int  i;

        Epoch_ToISO(h->t0, t0iso, sizeof(t0iso));
        fprintf(ctx->OfflineOut,
                "{\"event\":%lu,\"t0\":%.3f,\"t0_utc\":\"%s\","
                "\"lat\":%.4f,\"lon\":%.4f,\"depth_km\":%.1f,"
                "\"nphases\":%d,\"rms_sec\":%.3f,\"gap_deg\":%.1f,"
                "\"dmin_km\":%.1f,\"score\":%.3f,\"grid_level\":%d,"
                "\"nwin\":%d,\"phases\":[",
                h->id, h->t0, t0iso,
                h->lat, h->lon, h->depth_km,
                h->nphases, h->rms_sec, h->gap_deg,
                h->dmin_km, h->score, h->grid_level, nwin);

        for (i = 0; i < h->nphases; i++) {
            int idx = h->phase_idx[i];
            const Pick *p;
            char piso[32];

            if (idx < 0 || idx >= nwin) continue;
            p = &window[idx];
            Epoch_ToISO(p->t_epoch, piso, sizeof(piso));
            fprintf(ctx->OfflineOut,
                    "%s{\"sta\":\"%s\",\"net\":\"%s\",\"chan\":\"%s\","
                    "\"loc\":\"%s\",\"phase\":\"%s\",\"t_epoch\":%.3f,"
                    "\"t_utc\":\"%s\",\"residual\":%.3f,\"weight\":%d}",
                    (i ? "," : ""), p->sta, p->net, p->chan, p->loc,
                    p->phase_name[0] ? p->phase_name : Phase_Name(p->phase),
                    p->t_epoch, piso, h->residual[i], p->weight);
        }
        fprintf(ctx->OfflineOut, "]}\n");
        fflush(ctx->OfflineOut);
        return;
    }

    {
        MSG_LOGO logo;

        logo.instid = ctx->MyInstId;
        logo.mod    = ctx->MyModId;
        logo.type   = ctx->TypeHyp2000Arc;

        if (tport_putmsg(&ctx->OutRegion, &logo, (long)strlen(arc), arc) == PUT_OK) {
            logit("t", "csnloc: evento %lu lat=%.3f lon=%.3f z=%.1f km "
                       "nph=%d rms=%.2f gap=%d\n",
                  h->id, h->lat, h->lon, h->depth_km,
                  h->nphases, h->rms_sec, (int)(h->gap_deg + 0.5));
        } else {
            logit("et", "csnloc: fallo escribiendo HYP2000ARC al anillo\n");
        }
    }
}

/* ------------------------------------------------------------------------- *
 * Dedup de eventos entre grillas.                                            *
 *                                                                            *
 * Las grillas de distinta resolucion que cubren el mismo sismo producen       *
 * eventos casi coincidentes. Se ordenan por evidencia y se descarta un        *
 * evento si ya hay uno aceptado a menos de EventDedupSec y EventDedupKm.      *
 * El criterio de "mejor" prioriza mas fases, luego la grilla mas fina.        *
 * ------------------------------------------------------------------------- */
static int event_better(const HypoCandidate *a, const HypoCandidate *b)
{
    if (a->nphases != b->nphases)     return a->nphases > b->nphases;
    if (a->grid_level != b->grid_level) return a->grid_level > b->grid_level;
    return a->score > b->score;
}

static int DedupEvents(const HypoCandidate *in, int n,
                       const CSLocParams *cfg,
                       HypoCandidate *out, int max_out)
{
    int    *order, i, j, nout = 0;
    double tmax = (cfg->EventDedupSec >= 0.0) ? cfg->EventDedupSec : 30.0;
    double dmax_deg = ((cfg->EventDedupKm > 0.0) ? cfg->EventDedupKm : 100.0)
                      / 111.195;

    if (n <= 0 || max_out <= 0) return 0;
    order = (int *)malloc(sizeof(int) * (size_t)n);
    if (!order) return 0;
    for (i = 0; i < n; i++) order[i] = i;

    for (i = 1; i < n; i++) {
        int key = order[i];
        j = i - 1;
        while (j >= 0 && event_better(&in[key], &in[order[j]])) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    for (i = 0; i < n && nout < max_out; i++) {
        int idx = order[i];
        int dup = 0;
        for (j = 0; j < nout; j++) {
            if (fabs(in[idx].t0 - out[j].t0) > tmax) continue;
            if (TT_GreatCircleDeg(in[idx].lat, in[idx].lon,
                                  out[j].lat, out[j].lon) <= dmax_deg) {
                dup = 1;
                break;
            }
        }
        if (!dup) out[nout++] = in[idx];
    }
    free(order);
    return nout;
}

/* ------------------------------------------------------------------------- */
/* Procesa la ventana actual: back-projection, refinado y emisión.            */
/* ------------------------------------------------------------------------- */
static void process_window(CSLocCtx *ctx, double now_epoch)
{
    Pick          window[CSLOC_MAX_PICKS];
    int           pick_sidx[CSLOC_MAX_PICKS];
    HypoCandidate cand[CSLOC_MAX_EVENTS];
    HypoCandidate *nuc, *events;
    int           nwin, nev, nall = 0, i, gi, lev;

    nwin = PickBuffer_Snapshot(&ctx->picks, now_epoch,
                               ctx->cfg.AssocWindowSec, window, CSLOC_MAX_PICKS);
    if (nwin < ctx->cfg.MinPhasesPerEvent) return;

    for (i = 0; i < nwin; i++)
        pick_sidx[i] = Stations_Find(&ctx->stations, window[i].sta);

    /* Buffers en heap (sizeof(HypoCandidate) es grande). */
    nuc = (HypoCandidate *)calloc((size_t)CSLOC_MAX_NUC, sizeof(HypoCandidate));
    events = (HypoCandidate *)calloc(
                 (size_t)(ctx->grids.n > 0 ? ctx->grids.n : 1) * CSLOC_MAX_EVENTS,
                 sizeof(HypoCandidate));
    if (!nuc || !events) { free(nuc); free(events); return; }

    /* Cascada de gruesa a fina. Cada grilla activa se nuclea y ensambla por
       separado (las nucleaciones de resoluciones distintas no son comparables);
       luego los eventos se deduplican. Las grillas finas se activan por
       cobertura de estaciones o por cercania a un evento de nivel mas grueso. */
    for (lev = GRID_LEVEL_GLOBAL; lev <= GRID_LEVEL_LOCAL; lev++) {
        for (gi = 0; gi < ctx->grids.n; gi++) {
            Grid *g = &ctx->grids.g[gi];
            int   n, ne;

            if (g->level != lev) continue;
            if (!Grid_IsActive(g, window, pick_sidx, nwin, &ctx->stations,
                               &ctx->cfg, events, nall))
                continue;

            n = BackProject_Nucleations(g, &ctx->stations, window, nwin,
                                        &ctx->tt, &ctx->cfg, nuc,
                                        CSLOC_MAX_NUC, ctx->cfg.NumThreads);
            if (n <= 0) continue;

            ne = AssembleCandidates(nuc, n, &ctx->stations, window, nwin,
                                    &ctx->tt, &ctx->cfg, events + nall,
                                    CSLOC_MAX_EVENTS);
            if (ne > 0) nall += ne;
        }
    }
    free(nuc);

    if (getenv("CSNLOC_DEBUG"))
        fprintf(stderr, "[dbg] pw nwin=%d grids=%d nall=%d\n",
                nwin, ctx->grids.n, nall);
    if (nall <= 0) { free(events); return; }

    nev = DedupEvents(events, nall, &ctx->cfg, cand, CSLOC_MAX_EVENTS);
    free(events);
    if (nev <= 0) return;
    if (nev > ctx->cfg.MaxEventsPerWindow) nev = ctx->cfg.MaxEventsPerWindow;

    for (i = 0; i < nev; i++) {
        char arc[65536];

        /* El índice de fase se refiere a la ventana; el refinado usa esa
           misma copia, por lo que es consistente. */
        if (RefineHypo(&cand[i], &ctx->stations, window, &ctx->tt,
                       &ctx->cfg) != 0)
            continue;
        if (cand[i].nphases < ctx->cfg.MinPhasesPerEvent) continue;
        if (ctx->cfg.MaxRMS > 0.0 && cand[i].rms_sec > ctx->cfg.MaxRMS) continue;

        cand[i].id = ctx->next_event_id++;

        if (FormatHYP2000ARC(&cand[i], &ctx->stations, window, &ctx->cfg,
                             cand[i].id, arc, sizeof(arc)) != 0)
            continue;

        if (ctx->cfg.DumpHypo)
            logit("t", "csnloc: --- HYP2000ARC evento %lu ---\n%s"
                       "csnloc: --- fin HYP2000ARC evento %lu ---\n",
                  cand[i].id, arc, cand[i].id);

        emit_hypo(ctx, &cand[i], window, nwin, arc);
    }
}

/* ------------------------------------------------------------------------- */
/* Modo offline: procesa un fichero de picks con reloj VIRTUAL.               */
/* ------------------------------------------------------------------------- */
static int pick_time_cmp(const void *a, const void *b)
{
    double ta = ((const Pick *)a)->t_epoch;
    double tb = ((const Pick *)b)->t_epoch;
    return (ta < tb) ? -1 : (ta > tb) ? 1 : 0;
}

static int run_offline(CSLocCtx *ctx, const char *picksfile)
{
    FILE  *fp;
    char   line[1024];
    double now = 0.0, timeLastCheck = 0.0;
    double cadence = (double)((int)(ctx->cfg.AssocWindowSec / 2.0) + 1);
    Pick  *arr = NULL;
    int    npick = 0, cap = 0, nbad = 0, i;

    fp = fopen(picksfile, "r");
    if (!fp) {
        logit("et", "csnloc: no se pudo abrir picks <%s>\n", picksfile);
        return -1;
    }

    /* 1) Leer y ordenar por tiempo: la ventana deslizante exige orden
       temporal (los picks pueden venir desordenados entre canales). */
    while (fgets(line, sizeof(line), fp)) {
        Pick  p;
        char *nl;

        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        if (PickSCNL_Parse(line, (int)strlen(line), &p) != 0) {
            nbad++;
            if (ctx->cfg.Debug)
                logit("t", "csnloc: pick descartado (malformado): %s\n", line);
            continue;
        }

        if (npick >= cap) {
            int ncap = cap ? cap * 2 : 256;
            Pick *tmp = (Pick *)realloc(arr, (size_t)ncap * sizeof(Pick));
            if (!tmp) {
                logit("et", "csnloc: sin memoria para %d picks\n", ncap);
                free(arr);
                fclose(fp);
                return -1;
            }
            arr = tmp;
            cap = ncap;
        }
        arr[npick++] = p;
    }
    fclose(fp);

    if (npick > 1)
        qsort(arr, (size_t)npick, sizeof(Pick), pick_time_cmp);

    /* 2) Procesar con reloj virtual monotono. */
    for (i = 0; i < npick; i++) {
        PickBuffer_AddOrReplace(&ctx->picks, &arr[i]);
        if (arr[i].t_epoch > now) now = arr[i].t_epoch;

        if (now - timeLastCheck >= cadence) {
            timeLastCheck = now;
            PickBuffer_Prune(&ctx->picks, now, ctx->cfg.PickTTLSec);
            if (ctx->picks.n >= ctx->cfg.MinPhasesPerEvent)
                process_window(ctx, now);
        }
    }
    free(arr);

    /* Asentamiento final: replica el SETTLE del replay en vivo para que el
       ultimo sismo alcance a localizarse. */
    if (npick > 0) {
        double end = now + ctx->cfg.AssocWindowSec / 2.0 + 1.0 + 4.0;

        while (now < end) {
            now += cadence;
            PickBuffer_Prune(&ctx->picks, now, ctx->cfg.PickTTLSec);
            if (ctx->picks.n >= ctx->cfg.MinPhasesPerEvent)
                process_window(ctx, now);
        }
    }

    logit("t", "csnloc: offline terminado (%d picks, %d descartados, "
               "insertados=%lu reemplazados=%lu)\n",
          npick, nbad, ctx->picks.n_inserted, ctx->picks.n_replaced);
    return 0;
}

/* ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    CSLocCtx     ctx;
    MSG_LOGO     getlogo[1], reclogo;
    SHM_INFO     inRegion;
    char         msg[4096];
    long         recsize;
    time_t       timeNow, timeLastBeat = 0, timeLastCheck = 0;
    int          res, i;

    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Uso: csnloc <archivo.d> [archivo_picks]\n"
                        "     Sin archivo_picks: modo anillo (produccion).\n"
                        "     Con archivo_picks: modo offline, reloj virtual,\n"
                        "     emite un JSON por evento a stdout.\n");
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    set_defaults(&ctx.cfg);
    if (read_config(argv[1], &ctx.cfg) < 0) return 1;

    ctx.Offline    = (argc == 3);
    ctx.OfflineOut = stdout;

    lookup(&ctx);
    /* Buffer amplio: DumpHypo puede volcar el HYP2000ARC completo (~KB). */
    logit_init(argv[1], ctx.MyModId, 16384, ctx.cfg.LogFile);
    logit("t", "csnloc: iniciando (in=%s out=%s threads=%d)\n",
          ctx.cfg.InRingName, ctx.cfg.OutRingName, ctx.cfg.NumThreads);

    ctx.MyPid = getpid();
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* --- metadata de estaciones --- */
    if (Stations_Load(ctx.cfg.StaFile, &ctx.stations) != 0) {
        logit("et", "csnloc: no se pudo cargar StaFile <%s>\n", ctx.cfg.StaFile);
        return 1;
    }
    logit("t", "csnloc: %d estaciones cargadas de %s\n",
          ctx.stations.n, ctx.cfg.StaFile);

    /* --- tabla de tiempos de viaje --- */
    if (TTModel_Init(&ctx.tt, ".", ctx.cfg.TauModel,
                     0.0, 180.0, 0.5) != 0) {
        logit("et", "csnloc: fallo inicializando modelo <%s> "
                    "(se esperan %s.tbl/.hed en el cwd)\n",
              ctx.cfg.TauModel, ctx.cfg.TauModel);
        return 1;
    }
    logit("t", "csnloc: tabla TT %s %dx%d lista\n",
          ctx.cfg.TauModel, ctx.tt.nd, ctx.tt.nk);

    /* --- grillas anidadas --- */
    if (ctx.cfg.n_gridfiles <= 0) {
        logit("et", "csnloc: no hay grillas configuradas "
                    "(claves GlobalGrid/RegionalGrid/LocalGrid)\n");
        return 1;
    }
    res = GridSet_Load(&ctx.grids, &ctx.cfg, &ctx.stations);
    if (res != 0) {
        logit("et", "csnloc: fallo cargando grillas (res=%d)\n", res);
        return 1;
    }
    for (i = 0; i < ctx.grids.n; i++) {
        const Grid *g = &ctx.grids.g[i];
        logit("t", "csnloc: grilla '%s' nivel=%d %dx%dx%d = %d nodos "
                   "(%.1f km, %d estaciones, nnz=%d)\n",
              g->name, g->level, g->nx, g->ny, g->nz, g->n_nodes,
              g->node_km, g->n_sta, g->nnz);
    }

    /* --- buffers --- */
    PickBuffer_Init(&ctx.picks, CSLOC_MAX_PICKS, ctx.cfg.RePickWindowSec);
    ctx.next_event_id = 1;

    if (ctx.Offline) {
        int rc = run_offline(&ctx, argv[2]);

        PickBuffer_Free(&ctx.picks);
        GridSet_Free(&ctx.grids);
        TTModel_Free(&ctx.tt);
        return rc == 0 ? 0 : 1;
    }

    /* --- anillos (solo modo produccion) --- */
    tport_attach(&ctx.OutRegion, ctx.cfg.OutKey);
    tport_attach(&inRegion, ctx.cfg.InKey);

    getlogo[0].instid = INST_WILDCARD;
    getlogo[0].mod    = MOD_WILDCARD;
    getlogo[0].type   = ctx.TypePickSCNL;

    while (!ctx.Terminate &&
           tport_getflag(&ctx.OutRegion) != TERMINATE &&
           tport_getflag(&ctx.OutRegion) != ctx.MyPid) {
        time(&timeNow);

        if (timeNow - timeLastBeat >= ctx.cfg.HeartbeatInt) {
            timeLastBeat = timeNow;
            status(&ctx, ctx.TypeHeartBeat, 0, "");
        }

        res = tport_getmsg(&inRegion, getlogo, 1, &reclogo, &recsize,
                           msg, sizeof(msg) - 1);
        if (res == GET_OK || res == GET_MISS) {
            if (res == GET_OK) {
                Pick p;
                if (recsize > 0 && recsize < (long)sizeof(msg) - 1) {
                    msg[recsize] = '\0';
                    if (PickSCNL_Parse(msg, (int)recsize, &p) == 0) {
                        PickBuffer_AddOrReplace(&ctx.picks, &p);
                    } else if (ctx.cfg.Debug) {
                        logit("t", "csnloc: pick descartado (malformado)\n");
                    }
                }
            }
        } else {
            sleep_ew(50);
        }

        /* Cada AssocWindowSec/2 se poda y se intenta localizar. */
        if (timeNow - timeLastCheck >= (int)(ctx.cfg.AssocWindowSec / 2.0) + 1) {
            timeLastCheck = timeNow;
            PickBuffer_Prune(&ctx.picks, (double)timeNow, ctx.cfg.PickTTLSec);
            if (ctx.picks.n >= ctx.cfg.MinPhasesPerEvent)
                process_window(&ctx, (double)timeNow);
        }
    }

    logit("t", "csnloc: terminando (insertados=%lu reemplazados=%lu)\n",
          ctx.picks.n_inserted, ctx.picks.n_replaced);

    tport_detach(&inRegion);
    tport_detach(&ctx.OutRegion);
    PickBuffer_Free(&ctx.picks);
    GridSet_Free(&ctx.grids);
    TTModel_Free(&ctx.tt);

    return 0;
}
