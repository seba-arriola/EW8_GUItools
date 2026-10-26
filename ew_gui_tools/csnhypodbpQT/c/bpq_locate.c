/*
 * bpq_locate.c - Cadena del boton `locate` (fase 4 del prototipo).
 *
 * No reimplementa nada del motor: arma el set de picadas que el usuario tiene
 * delante (una P y una S por estacion, ranking M > A > Teo, con los checks
 * A/M/Teo y la puerta MaxDistKm) y se lo pasa a csnhypodbp_locate_run, que
 * escribe el ARC, corre csnloc, refina con hyp2000_ring/nlloc_ring y devuelve
 * el residual por fase leido del JSONL.
 *
 * Las DOS trampas de la cadena que se pagan aqui:
 *   - sin COMP3 (canal) HYPOINVERSE empareja STA+NET+COMP3+LOC y rechaza TODAS
 *     las fases, asi que cualquier pick que llegue sin canal/red/loc se completa
 *     con la metadata del catalogo (StaFile);
 *   - el refinador corre con cwd = su WorkDir, asi que la copia del `.d` la hace
 *     el nucleo (csnhypodbp_cfg_with_refiner_opts), no este archivo.
 */
#include "bpq_capi.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Tope de filas que se le pasan a csnhypodbp_locate_picks (MAX_ESTA del GTK). */
#define BPQ_LOC_MAX_ROWS 512

int bpq_locate_build_picks(const BpqStations *st, const CsnhypodbpSolution *base,
                           const CsnhypodbpStaRow *rows, int nrows,
                           const int (*pick_on)[2],
                           int auto_on, int manual_on, int theo_on,
                           double max_dist_km, CsnhypodbpSolution *out)
{
    CsnhypodbpPickFilter f;
    int (*on)[2];
    int i, n;

    if (!base || !rows || nrows <= 0 || nrows > BPQ_LOC_MAX_ROWS || !out)
        return -1;
    memcpy(out, base, sizeof(*out));   /* siembra lat/lon/prof/qid/otime */

    /* Copia de la seleccion con la puerta de distancia ya aplicada: `pick_on`
     * es const y la puerta es del envio, no de la tabla. OJO: se indexa por
     * `rows[i].idx` (posicion en el catalogo), que es lo que consulta
     * csnhypodbp_pick_allowed -- NO por el indice de fila (las filas van
     * ordenadas por distancia y las dos cosas no coinciden). */
    {
        int nmax = 0;

        for (i = 0; i < nrows; i++)
            if (rows[i].idx + 1 > nmax) nmax = rows[i].idx + 1;
        if (nmax <= 0) nmax = nrows;
        on = (int (*)[2])calloc((size_t)nmax * 2, sizeof(int));
        if (!on) return -1;
        for (i = 0; i < nrows; i++) {
            const int idx = rows[i].idx >= 0 ? rows[i].idx : 0;
            int ph;

            if (idx >= nmax) continue;
            for (ph = 0; ph < 2; ph++) {
                int marked = pick_on ? (pick_on[idx][ph] != 0) : 1;

                if (max_dist_km > 0.0 && rows[i].dist_km > max_dist_km) marked = 0;
                on[idx][ph] = marked;
            }
        }
    }

    f.auto_on = auto_on;
    f.manual_on = manual_on;
    f.theo_on = theo_on;

    n = csnhypodbp_locate_picks(rows, nrows, (const int (*)[2])on, &f,
                                out->picks, CSNHYPODBP_MAX_PICKS);
    free(on);
    if (n < 0) n = 0;
    out->npicks = n;
    out->nph = n;

    /* SCNL completo: sin canal, el refinador rechaza la fase en silencio. */
    for (i = 0; i < n; i++) {
        CsnhypodbpPick *pk = &out->picks[i];
        int r;

        if (pk->chan[0] && pk->net[0]) continue;
        for (r = 0; r < nrows; r++) {
            const char *ssta = NULL, *snet = NULL, *schan = NULL, *sloc = NULL;

            if (strcmp(rows[r].sta, pk->sta) != 0) continue;
            if (bpq_stations_scnl(st, rows[r].idx, &ssta, &snet, &schan, &sloc)) {
                if (!pk->chan[0] && schan)
                    snprintf(pk->chan, sizeof(pk->chan), "%s", schan);
                if (!pk->net[0] && snet)
                    snprintf(pk->net, sizeof(pk->net), "%s", snet);
                if (!pk->loc[0] && sloc)
                    snprintf(pk->loc, sizeof(pk->loc), "%s", sloc);
            }
            break;
        }
    }
    return n;
}

/* Cuenta las picadas que el `locate` enviaria, por fase y origen. Usa la MISMA
 * mascara y puerta que bpq_locate_build_picks; sirve para el preflight de la
 * UI: con "solo M" y 0 manuales el mensaje dice M=0 A=3, no un MinPhases del
 * refinador. */
int bpq_locate_count_origins(const CsnhypodbpStaRow *rows, int nrows,
                             const int (*pick_on)[2],
                             int auto_on, int manual_on, int theo_on,
                             double max_dist_km,
                             int *np, int *ns, int *na, int *nm, int *nt)
{
    int i, ph, total = 0;

    if (np) *np = 0;
    if (ns) *ns = 0;
    if (na) *na = 0;
    if (nm) *nm = 0;
    if (nt) *nt = 0;
    if (!rows || nrows <= 0) return 0;

    for (i = 0; i < nrows; i++) {
        const CsnhypodbpStaRow *r = &rows[i];
        const int idx = r->idx >= 0 ? r->idx : 0;

        if (max_dist_km > 0.0 && r->dist_km > max_dist_km) continue;
        for (ph = 0; ph < 2; ph++) {
            const CsnhypodbpStaPick *pk = ph ? &r->s : &r->p;
            int on;

            if (!pk->present) continue;
            if (pick_on && !pick_on[idx][ph]) continue;
            switch (pk->origin) {
            case 'M': case 'm': on = manual_on; break;
            case 'A': case 'a': on = auto_on; break;
            case 'T': case 't': on = theo_on; break;
            default: on = 0; break;
            }
            if (!on) continue;
            total++;
            if (ph == 0) { if (np) (*np)++; } else { if (ns) (*ns)++; }
            switch (pk->origin) {
            case 'M': case 'm': if (nm) (*nm)++; break;
            case 'A': case 'a': if (na) (*na)++; break;
            case 'T': case 't': if (nt) (*nt)++; break;
            default: break;
            }
        }
    }
    return total;
}

int bpq_locate_run(const BpqConfig *cfg, const BpqStations *st,
                   const CsnhypodbpSolution *base, const char *locator,
                   const CsnhypodbpStaRow *rows, int nrows,
                   const int (*pick_on)[2],
                   int auto_on, int manual_on, int theo_on,
                   double fix_depth_km,
                   const CsnhypodbpTankTrace *tank, int ntank,
                   BpqLocateOut *out, volatile int *cancel,
                   char *err, size_t errn)
{
    CsnhypodbpSolution sol;
    const char *ewp, *ewh, *ewv, *sel;
    double fix;
    int rc, np;

    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (err && errn) err[0] = '\0';

    if (!cfg || !st || !base) {
        if (err && errn) snprintf(err, errn, "locate: sin config/catalogo/solucion");
        return 0;
    }

    np = bpq_locate_build_picks(st, base, rows, nrows, pick_on,
                                auto_on, manual_on, theo_on,
                                cfg->max_dist_km, &sol);
    if (np < 0) {
        if (err && errn) snprintf(err, errn, "locate: argumentos invalidos");
        return 0;
    }
    if (np == 0) {
        int present = 0, inside = 0, i;
        double dmin = -1.0;

        for (i = 0; i < nrows; i++) {
            const int has = rows[i].p.present || rows[i].s.present;

            if (has) {
                present++;
                if (dmin < 0.0 || rows[i].dist_km < dmin) dmin = rows[i].dist_km;
            }
            if (cfg->max_dist_km <= 0.0 || rows[i].dist_km <= cfg->max_dist_km)
                inside++;
        }
        if (err && errn)
            snprintf(err, errn,
                     "locate: sin picadas (filas %d, con picada %d, dmin %.0f km, "
                     "dentro de la puerta %d, checks A%d M%d T%d, MaxDistKm %.0f km)",
                     nrows, present, dmin, inside,
                     auto_on, manual_on, theo_on, cfg->max_dist_km);
        return 0;
    }

    ewp = getenv("EW_PARAMS");
    ewh = getenv("EW_HOME");
    ewv = getenv("EW_VERSION");
    if (!ewp || !ewh || !ewv) {
        if (err && errn)
            snprintf(err, errn, "locate: faltan EW_PARAMS/EW_HOME/EW_VERSION");
        return 0;
    }

    sel = (locator && locator[0])
              ? locator
              : (cfg->active_locator[0]
                     ? cfg->active_locator
                     : (cfg->n_locators > 0 ? cfg->locators[0].name : ""));
    if (!sel[0]) {
        if (err && errn) snprintf(err, errn, "locate: no hay localizadores en el .d");
        return 0;
    }

    csnhypodbp_locate_keep_tmp(cfg->locate_keep_tmp);
    /* `fix_depth_km` < 0 = usa el `Fix`/`FixedDepthKm` del .d; >= 0 = override de
     * la barra (el refinador copia la cfg con `FixedDepthKm`). */
    fix = (fix_depth_km >= 0.0)
              ? fix_depth_km
              : (cfg->fix_depth_on ? cfg->fix_depth_km : -1.0);
    rc = csnhypodbp_locate_run(cfg->locators, cfg->n_locators, sel,
                               ewp, ewh, ewv, NULL,
                               &sol, NULL, 0, tank, ntank, NULL, 0, 0u,
                               fix,
                               cfg->locator_timeout_s, NULL, NULL, cancel,
                               &out->sol, &out->mag, err, errn);
    if (rc != 0) {
        /* `err` ya trae el motivo real del nucleo (timeout / ref.err / ARC). */
        if (err && errn && !err[0])
            snprintf(err, errn, "locate: la cadena fallo (rc=%d)", rc);
        return 0;
    }
    return 1;
}
