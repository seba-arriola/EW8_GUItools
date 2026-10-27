/******************************************************************************
 * event_registry.c                                                           *
 *                                                                            *
 * Registro de eventos activos con versionado dinamico.                       *
 *                                                                            *
 * Modelo: la ventana deslizante solo DESCUBRE eventos nuevos. Una vez creado *
 * un evento, este ACUMULA sus fases (nunca las pierde por el avance de la    *
 * ventana) y las optimiza: incorpora re-picks y fases nuevas, y poda las que *
 * no se ajustan a la solucion vigente. Cada mejora emite el MISMO id con     *
 * version incrementada.                                                      *
 ******************************************************************************/
#include "csnloc.h"

/* ------------------------------------------------------------------------- */
/* Trazas de decision (solo con Debug >= 1; no afectan a produccion)          */
/*                                                                            */
/* event_registry.c es logica pura y testeable sin EarthWorm, por eso NO      */
/* llama a logit() directamente: usa un callback que csnloc.c conecta a       */
/* logit(). En los tests el callback es NULL y no se emite nada.              */
/* ------------------------------------------------------------------------- */
static void (*g_log_cb)(const char *msg) = NULL;

void EventRegistry_SetLogCallback(void (*cb)(const char *msg))
{
    g_log_cb = cb;
}

static void log_decision(const CSLocParams *cfg, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    if (!cfg || cfg->Debug < 1 || !g_log_cb) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_log_cb(buf);
}

/* Avisos que NO dependen de Debug: overlap entre eventos y registro lleno. */
static void log_warn(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    if (!g_log_cb) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_log_cb(buf);
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

/* Mismo SCNL (red/canal/loc incluidos). */
static int same_scnl(const Pick *a, const Pick *b)
{
    return strcmp(a->sta, b->sta) == 0 &&
           strcmp(a->net, b->net) == 0 &&
           strcmp(a->chan, b->chan) == 0 &&
           strcmp(a->loc, b->loc) == 0;
}

/* Misma estacion+fase (csnloc asocia por nombre de estacion). */
static int same_sta_phase(const Pick *a, const Pick *b)
{
    return strcmp(a->sta, b->sta) == 0 && a->phase == b->phase;
}

/* Umbral de residual para podar una fase de la solucion. */
static double prune_tol(const CSLocParams *cfg, int phase)
{
    return (phase == CSLOC_PHASE_S) ? cfg->PhaseResidualMaxSecS
                                    : cfg->PhaseResidualMaxSec;
}

/* ------------------------------------------------------------------------- */
/* Ciclo de vida                                                              */
/* ------------------------------------------------------------------------- */
void EventRegistry_Init(EventRegistry *r)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->n = 0;
}

/* Criterio unico de "mismo evento". El ancla es la primera solucion del
   evento (e->t0/lat/lon); NO se usa el t0 de la ultima deteccion. */
int CSLoc_EventMatch(const CSLocParams *cfg,
                     double t0a, double lata, double lona,
                     double t0b, double latb, double lonb,
                     double *dt_out, double *d_km_out)
{
    double dt, d_km, tmax;

    if (!cfg) return 0;
    dt   = fabs(t0a - t0b);
    d_km = TT_GreatCircleDeg(lata, lona, latb, lonb) * 111.195;

    if (dt_out)   *dt_out = dt;
    if (d_km_out) *d_km_out = d_km;

    /* Distancia 0 se interpreta como "sin ventana espacial": nunca marca
       el mismo evento. El .d siempre debe declarar un valor > 0. */
    if (cfg->EventMatchDistKm <= 0.0) return 0;

    tmax = cfg->EventMatchTimeSec;
    if (cfg->EventMatchVelocityKmS > 0.0)
        tmax += d_km / cfg->EventMatchVelocityKmS;

    return (dt <= tmax && d_km <= cfg->EventMatchDistKm) ? 1 : 0;
}

/* Cuenta picadas del candidato que ya estan en el evento por estacion+fase
   y |dt| <= tol_sec. No compara red/canal/loc (el ARC puede traer otro canal
   para la misma estacion). */
int EventRegistry_SharedPicks(const EventRecord *e, const Pick *window,
                              int nwin, const int *phase_idx, int nph,
                              double tol_sec)
{
    int i, j, n = 0;

    if (!e || !window || !phase_idx || nph <= 0) return 0;
    for (i = 0; i < nph; i++) {
        int pi = phase_idx[i];
        const Pick *p;

        if (pi < 0 || pi >= nwin) continue;
        p = &window[pi];
        for (j = 0; j < e->nphases_stored; j++) {
            if (same_sta_phase(&e->phases[j], p) &&
                fabs(e->phases[j].t_epoch - p->t_epoch) <= tol_sec) {
                n++;
                break;
            }
        }
    }
    return n;
}

/* Para el log de overlap: maximo de picadas compartidas contra CUALQUIER
   evento (aunque no matchee en tiempo/distancia). Devuelve el conteo y el
   indice del evento con mas coincidencias. */
static int max_shared_any(const EventRegistry *r, const HypoCandidate *c,
                          const Pick *window, int nwin,
                          const CSLocParams *cfg, int *idx_out)
{
    int i, best = 0, best_i = -1;

    if (!r || !c || !window || nwin <= 0) return 0;
    for (i = 0; i < r->n; i++) {
        int n = EventRegistry_SharedPicks(&r->ev[i], window, nwin,
                                          c->phase_idx, c->nphases,
                                          cfg->EventPickTolSec);
        if (n > best) { best = n; best_i = i; }
    }
    if (idx_out) *idx_out = best_i;
    return best;
}

/* Busca el evento activo mas cercano al candidato (tiempo + distancia).
   Devuelve 1 y el indice si hay match. shared_out recibe las picadas que
   comparte con ese evento (para diagnostico). */
int EventRegistry_FindMatch(const EventRegistry *r, const HypoCandidate *c,
                            const Pick *window, int nwin,
                            const CSLocParams *cfg, int *idx_out,
                            int *shared_out)
{
    double best = 1e18;
    int    i, best_i = -1, best_shared = 0;

    if (!r || !c || !cfg) return 0;
    for (i = 0; i < r->n; i++) {
        const EventRecord *e = &r->ev[i];
        double dt, d, tmax, score;
        int    shared;

        if (!CSLoc_EventMatch(cfg, c->t0, c->lat, c->lon,
                              e->t0, e->lat, e->lon, &dt, &d))
            continue;

        tmax = cfg->EventMatchTimeSec;
        if (cfg->EventMatchVelocityKmS > 0.0)
            tmax += d / cfg->EventMatchVelocityKmS;

        score = (tmax > 0.0) ? (dt / tmax) : 0.0;
        score *= score;
        if (cfg->EventMatchDistKm > 0.0) {
            double dd = d / cfg->EventMatchDistKm;
            score += dd * dd;
        }

        shared = (window && nwin > 0)
                     ? EventRegistry_SharedPicks(e, window, nwin,
                                                 c->phase_idx, c->nphases,
                                                 cfg->EventPickTolSec)
                     : 0;

        if (score < best - 1e-9 ||
            (fabs(score - best) <= 1e-9 && shared > best_shared)) {
            best = score;
            best_shared = shared;
            best_i = i;
        }
    }
    if (best_i < 0) return 0;
    if (idx_out)    *idx_out = best_i;
    if (shared_out) *shared_out = best_shared;
    return 1;
}

/* Politica de aceptacion: 1 si la solucion nueva mejora o no empeora mas alla
   de los umbrales configurados. */
int EventRegistry_Accept(const EventRecord *prev, const HypoCandidate *c,
                         const CSLocParams *cfg)
{
    if (!prev || !c) return 0;

    /* Mas fases siempre es mejora. */
    if (c->nphases > prev->nphases) return 1;

    /* Menos fases: solo si el RMS mejora claramente. */
    if (c->nphases < prev->nphases) {
        return c->rms_sec < prev->rms_sec;
    }

    /* Mismas fases: aceptar si RMS y gap no empeoran mas alla del umbral. */
    if (c->rms_sec <= prev->rms_sec * (1.0 + cfg->MaxRMSDegrade) &&
        c->gap_deg <= prev->gap_deg + cfg->MaxGapDegradeDeg)
        return 1;

    return 0;
}

/* ------------------------------------------------------------------------- */
/* Fases del evento                                                           */
/* ------------------------------------------------------------------------- */

/* Incorpora un pick al evento. Devuelve 1 si hubo cambio (fase nueva o
   re-pick), 0 si no. No re-incorpora fases podadas identicas. */
int EventRegistry_AddPhase(EventRecord *e, const Pick *p,
                           const CSLocParams *cfg)
{
    int i;

    if (!e || !p) return 0;

    /* Fase podada identica -> no re-incorporar. */
    for (i = 0; i < e->npruned; i++) {
        if (same_scnl(&e->pruned[i], p) &&
            fabs(e->pruned[i].t_epoch - p->t_epoch) < 0.001)
            return 0;
    }

    /* Misma estacion+fase ya presente -> re-pick: reemplaza. */
    for (i = 0; i < e->nphases_stored; i++) {
        if (same_sta_phase(&e->phases[i], p)) {
            if (fabs(e->phases[i].t_epoch - p->t_epoch) < 0.001) {
                /* Mismo tiempo: solo cuenta como cambio si pasa a manual. */
                if (p->manual && !e->phases[i].manual) {
                    e->phases[i].manual = 1;
                    e->has_manual = 1;
                    return 1;
                }
                return 0;   /* identico: sin cambio */
            }
            e->phases[i] = *p;
            if (p->manual) e->has_manual = 1;
            return 1;
        }
    }

    /* Fase nueva: solo si hay espacio. */
    if (e->nphases_stored >= CSLOC_MAX_PHASES) return 0;
    e->phases[e->nphases_stored++] = *p;
    if (p->manual) e->has_manual = 1;
    return 1;
}

/* Poda las fases cuyo residual supera el umbral, siempre que la solucion
   nueva sea mejor. Protege la primera estacion salvo residual grosero.
   Devuelve cuantas podo. */
int EventRegistry_PrunePhases(EventRecord *e, const CSLocParams *cfg)
{
    int i, k = 0, removed = 0;

    if (!e) return 0;

    for (i = 0; i < e->nphases_stored; i++) {
        double tol = prune_tol(cfg, e->phases[i].phase);
        double r   = e->residual[i];
        int    first = (i == 0);

        /* La primera estacion solo se poda con residual grosero (2x). */
        if (first) tol *= 2.0;

        if (r > tol) {
            /* Guardar en la lista de podadas para no re-incorporar. */
            if (e->npruned < CSLOC_MAX_PHASES)
                e->pruned[e->npruned++] = e->phases[i];
            log_decision(cfg, "csnloc: [ev %lu] poda %s.%s.%s.%s fase %s "
                              "resid=%.2f tol=%.2f%s\n",
                         e->id, e->phases[i].sta, e->phases[i].net,
                         e->phases[i].chan, e->phases[i].loc,
                         e->phases[i].phase_name, r, tol,
                         first ? " (primera)" : "");
            removed++;
            continue;
        }
        e->phases[k] = e->phases[i];
        e->residual[k] = e->residual[i];
        k++;
    }
    e->nphases_stored = k;
    return removed;
}

/* ------------------------------------------------------------------------- */
/* Expiración                                                                 */
/* ------------------------------------------------------------------------- */
void EventRegistry_Expire(EventRegistry *r, double now, double ttl_sec)
{
    int i, k = 0;

    if (!r) return;
    for (i = 0; i < r->n; i++) {
        if ((now - r->ev[i].last_update_wall) > ttl_sec) continue;
        r->ev[k++] = r->ev[i];
    }
    r->n = k;
}

/* ------------------------------------------------------------------------- */
/* Re-nucleo con back-projection restringido a la region del evento.          */
/*                                                                            */
/* Se usa cuando el conjunto de fases cambio mucho (se podo una fase o        */
/* entraron varias nuevas): RefineHypo podria quedar en un minimo local. Se   */
/* re-nuclea sobre la grilla mas fina disponible y se elige el candidato mas  */
/* cercano al hipocentro vigente.                                             */
/* ------------------------------------------------------------------------- */
static int renucleate(EventRecord *e, const StationList *st, TTModel *tt,
                      const GridSet *grids, const CSLocParams *cfg)
{
    const Grid *best = NULL;
    HypoCandidate cand[CSLOC_MAX_EVENTS];
    int    nev, i, best_i = -1;
    double best_d = 1e18;

    if (!grids || grids->n <= 0) return 0;

    /* Elegir la grilla mas fina (mayor nivel) que cubra el evento en lat/lon
       Y en profundidad. Si el evento es profundo y la grilla no lo cubre, no
       re-nuclear (RefineHypo sigue siendo valido). */
    for (i = 0; i < grids->n; i++) {
        const Grid *g = &grids->g[i];
        if (!Grid_ContainsLL(g, e->lat, e->lon, 2.0)) continue;
        if (e->depth_km < g->depth_min - 5.0 ||
            e->depth_km > g->depth_max + 5.0) continue;
        if (!best || g->level > best->level) best = g;
    }
    if (!best) return 0;

    nev = BackProject(best, st, e->phases, e->nphases_stored, tt, cfg,
                      cand, CSLOC_MAX_EVENTS, cfg->NumThreads);
    if (nev <= 0) return 0;

    /* Candidato mas cercano al hipocentro vigente. */
    for (i = 0; i < nev; i++) {
        double d = TT_GreatCircleDeg(e->lat, e->lon, cand[i].lat, cand[i].lon);
        if (d < best_d) { best_d = d; best_i = i; }
    }
    if (best_i < 0) return 0;

    /* Refinar el candidato re-nucleado. */
    if (RefineHypo(&cand[best_i], st, e->phases, tt, cfg) != 0) return 0;

    /* Actualizar el hipocentro del evento con el re-nucleado. */
    e->lat = cand[best_i].lat;
    e->lon = cand[best_i].lon;
    e->depth_km = cand[best_i].depth_km;
    e->t0 = cand[best_i].t0;
    e->nphases = cand[best_i].nphases;
    e->rms_sec = cand[best_i].rms_sec;
    e->gap_deg = cand[best_i].gap_deg;
    e->dmin_km = cand[best_i].dmin_km;
    e->score = cand[best_i].score;
    e->grid_level = best->level;
    for (i = 0; i < e->nphases_stored; i++)
        e->residual[i] = cand[best_i].residual[i];
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Fusion de un candidato con el registro                                     */
/* ------------------------------------------------------------------------- */

/* Copia las fases del candidato (indices sobre la ventana) al evento. */
static void seed_phases(EventRecord *e, const HypoCandidate *c,
                        const Pick *window, int nwin)
{
    int i;

    e->nphases_stored = 0;
    for (i = 0; i < c->nphases && e->nphases_stored < CSLOC_MAX_PHASES; i++) {
        int idx = c->phase_idx[i];
        if (idx < 0 || idx >= nwin) continue;
        e->phases[e->nphases_stored] = window[idx];
        e->residual[e->nphases_stored] = c->residual[i];
        if (window[idx].manual) e->has_manual = 1;
        e->nphases_stored++;
    }
}

/* Vuelca el hipocentro del candidato al registro. */
static void store_hypo(EventRecord *e, const HypoCandidate *c)
{
    e->t0 = c->t0;
    e->lat = c->lat;
    e->lon = c->lon;
    e->depth_km = c->depth_km;
    e->nphases = c->nphases;
    e->rms_sec = c->rms_sec;
    e->gap_deg = c->gap_deg;
    e->dmin_km = c->dmin_km;
    e->score = c->score;
    e->grid_level = c->grid_level;
}

int EventRegistry_Upsert(EventRegistry *r, const HypoCandidate *c,
                         const Pick *window, int nwin, const CSLocParams *cfg,
                         const StationList *st, TTModel *tt,
                         const GridSet *grids,
                         unsigned long id_base, double now_epoch,
                         unsigned long *id_out,
                         unsigned int *ver_out, int *manual_out)
{
    int idx = -1, shared = 0;

    if (!r || !c) return 0;

    if (!EventRegistry_FindMatch(r, c, window, nwin, cfg, &idx, &shared)) {
        /* Evento nuevo: crear con las fases del candidato. */
        EventRecord *e;
        int ov_idx = -1, ov_shared;

        if (r->n >= CSLOC_MAX_EVENTS) {
            log_warn("csnloc: registro lleno (%d); evento descartado\n",
                     CSLOC_MAX_EVENTS);
            return 0;
        }

        /* El overlap se mide contra los eventos YA existentes: el nuevo aun
           no esta en el registro (si no, se compararia consigo mismo). */
        ov_shared = max_shared_any(r, c, window, nwin, cfg, &ov_idx);

        e = &r->ev[r->n++];
        memset(e, 0, sizeof(*e));
        e->id = id_base;
        e->version = 0;
        e->emitted = 0;
        e->last_update_wall = now_epoch;
        e->t0_detect = c->t0;       /* legado; no se usa para el match */
        seed_phases(e, c, window, nwin);
        store_hypo(e, c);
        e->version = 1;
        e->emitted = 1;

        /* Comparte picadas con un evento lejano: se permite el evento nuevo
           (caso "muy distinto"), pero se deja rastro para diagnostico. */
        if (ov_shared > 0 && ov_idx >= 0)
            log_warn("csnloc: [ev %lu] overlap=%d con ev %lu "
                     "(dt=%.1f s d=%.0f km); evento nuevo\n",
                     e->id, ov_shared, r->ev[ov_idx].id,
                     fabs(c->t0 - r->ev[ov_idx].t0),
                     TT_GreatCircleDeg(c->lat, c->lon,
                                       r->ev[ov_idx].lat,
                                       r->ev[ov_idx].lon) * 111.195);

        log_decision(cfg, "csnloc: [ev %lu] nuevo v1 lat=%.3f lon=%.3f z=%.1f "
                          "nph=%d rms=%.2f gap=%.0f\n",
                     e->id, e->lat, e->lon, e->depth_km,
                     e->nphases, e->rms_sec, e->gap_deg);
        if (id_out)  *id_out = e->id;
        if (ver_out) *ver_out = e->version;
        if (manual_out) *manual_out = e->has_manual;
        return 1;
    }

    /* Evento existente: incorporar fases nuevas / re-picks. */
    {
        EventRecord *e = &r->ev[idx];
        int changed = 0, i, nnew = 0, npruned_before, got_manual = 0;

        for (i = 0; i < c->nphases; i++) {
            int pi = c->phase_idx[i];
            if (pi < 0 || pi >= nwin) continue;
            if (EventRegistry_AddPhase(e, &window[pi], cfg)) {
                changed = 1;
                nnew++;
                if (window[pi].manual) got_manual = 1;
                log_decision(cfg, "csnloc: [ev %lu] fase nueva %s.%s.%s.%s %s "
                                  "t=%.3f\n",
                             e->id, window[pi].sta, window[pi].net,
                             window[pi].chan, window[pi].loc,
                             window[pi].phase_name, window[pi].t_epoch);
            }
        }

        /* El evento sigue vivo mientras se detecte. El ancla (t0/lat/lon) NO
           se toca: t0_detect quedo congelado en la creacion y no se usa. */
        e->last_update_wall = now_epoch;

        if (shared > 0)
            log_decision(cfg, "csnloc: [ev %lu] match shared=%d "
                              "(dt=%.1f s d=%.0f km)\n",
                         e->id, shared, fabs(c->t0 - e->t0),
                         TT_GreatCircleDeg(c->lat, c->lon,
                                           e->lat, e->lon) * 111.195);

        /* Asociador: la primera solucion ya se emitio; no se refina, no se
         * poda y no se versiona. El evento se conserva para el dedup/TTL. */
        if (cfg->AssociateOnly && e->emitted) return 0;

        if (!changed) return 0;   /* sin picks nuevos: no hacer nada */

        /* Re-localizar con el conjunto acumulado. */
        {
            HypoCandidate h;
            int rc;

            memset(&h, 0, sizeof(h));
            h.lat = e->lat; h.lon = e->lon; h.depth_km = e->depth_km;
            h.t0 = e->t0;
            h.grid_level = e->grid_level;
            for (i = 0; i < e->nphases_stored; i++)
                h.phase_idx[i] = i;
            h.nphases = e->nphases_stored;

            rc = RefineHypo(&h, st, e->phases, tt, cfg);
            if (rc != 0) return 0;

            /* Residuales contra la solucion nueva. */
            for (i = 0; i < e->nphases_stored; i++)
                e->residual[i] = h.residual[i];

            /* Politica de aceptacion: si la solucion nueva no mejora (ni
               empeora dentro del umbral), no se emite version nueva. Una
               picada manual FUERZA la emision para propagar la marca. */
            if (!got_manual && !EventRegistry_Accept(e, &h, cfg)) {
                log_decision(cfg, "csnloc: [ev %lu] version RECHAZADA "
                                  "(nph=%d rms=%.2f gap=%.0f vs nph=%d "
                                  "rms=%.2f gap=%.0f)\n",
                             e->id, h.nphases, h.rms_sec, h.gap_deg,
                             e->nphases, e->rms_sec, e->gap_deg);
                return 0;
            }

            /* Poda por calidad: solo si la solucion nueva es mejor. */
            npruned_before = e->npruned;
            EventRegistry_PrunePhases(e, cfg);

            /* Hipocentro vigente = refinado con las fases que sobrevivieron. */
            h.nphases = e->nphases_stored;
            store_hypo(e, &h);

            /* Re-nucleo si el cambio de fases fue grande: se podo alguna fase
               o entraron varias nuevas. RefineHypo podria estar en un minimo
               local. renucleate() sobreescribe el hipocentro del evento. */
            if ((e->npruned > npruned_before) ||
                (nnew >= cfg->RenucleateMinNewPhases)) {
                log_decision(cfg, "csnloc: [ev %lu] re-nucleo (podadas=%d "
                                  "nuevas=%d)\n",
                             e->id, e->npruned - npruned_before, nnew);
                renucleate(e, st, tt, grids, cfg);
            }

            e->version++;
            e->emitted = 1;
            log_decision(cfg, "csnloc: [ev %lu] v%u lat=%.3f lon=%.3f z=%.1f "
                              "nph=%d rms=%.2f gap=%.0f\n",
                         e->id, e->version, e->lat, e->lon, e->depth_km,
                         e->nphases, e->rms_sec, e->gap_deg);
            if (id_out)  *id_out = e->id;
            if (ver_out) *ver_out = e->version;
            if (manual_out) *manual_out = e->has_manual;
            return 1;
        }
    }
}
