/*
 * bpq_magpanel.c - Estado de la sub-pestana `magnitudes` (fase 6.1).
 *
 * No reimplementa nada: el estado de las ventanas ajustadas a mano es
 * `csnhypodbp_magmarks` (mismo nucleo que usa la GUI GTK), las filas salen de
 * `csnhypodbp_magrow_build` y la puerta de distancia de `csnhypodbp_magcfg_gate`.
 * Aqui solo se juntan en un objeto que la UI puede crear y pasar a los tests.
 */
#include "bpq_capi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct BpqMagPanel {
    CsnhypodbpMagCfg    cfg;         /* copia: no depende del BpqConfig vivo */
    CsnhypodbpMagMarks  marks;
    int                 kind;        /* magnitud activa */
    CsnhypodbpMagGateOv gate;        /* override de la puerta */
    int                 auto_on, manual_on, theo_on;
};

/* Config efectiva: la del .d con el override de la puerta si esta puesto. */
static void eff_cfg(const BpqMagPanel *p, int kind, CsnhypodbpMagCfg *out)
{
    csnhypodbp_magcfg_apply_override(&p->cfg, kind,
                                     p->gate.on ? &p->gate : NULL, out);
}

BpqMagPanel *bpq_magpanel_new(const BpqConfig *cfg)
{
    BpqMagPanel *p = (BpqMagPanel *)calloc(1, sizeof(*p));

    if (!p) return NULL;
    if (cfg) {
        p->cfg = cfg->magcfg;
        /* Defaults del .d: el override solo cambia lo que el usuario teclee. */
        p->gate.min_delta = cfg->magcfg.ml_min_delta;
        p->gate.max_delta = cfg->magcfg.ml_max_delta;
        p->gate.min_depth = cfg->magcfg.ml_min_depth;
        p->gate.max_depth = cfg->magcfg.ml_max_depth;
    } else {
        csnhypodbp_magcfg_defaults(&p->cfg);
    }
    csnhypodbp_magmarks_init(&p->marks);
    p->kind = 0;
    p->auto_on = 1;
    p->manual_on = 1;
    p->theo_on = 1;
    return p;
}

void bpq_magpanel_free(BpqMagPanel *p)
{
    if (!p) return;
    csnhypodbp_magmarks_free(&p->marks);
    free(p);
}

int bpq_magpanel_kind(const BpqMagPanel *p)
{
    return p ? p->kind : 0;
}

void bpq_magpanel_set_kind(BpqMagPanel *p, int kind)
{
    if (!p) return;
    if (kind < 0 || kind >= BPQ_MAG_KINDS) return;
    p->kind = kind;
}

void bpq_magpanel_set_masks(BpqMagPanel *p, int auto_on, int manual_on, int theo_on)
{
    if (!p) return;
    p->auto_on = auto_on ? 1 : 0;
    p->manual_on = manual_on ? 1 : 0;
    p->theo_on = theo_on ? 1 : 0;
}

void bpq_magpanel_defaults_window(const BpqMagPanel *p, int kind,
                                  double *t0, double *t1)
{
    if (t0) *t0 = NAN;
    if (t1) *t1 = NAN;
    if (!p) return;
    csnhypodbp_magcfg_window(&p->cfg, kind, t0, t1);
}

void bpq_magpanel_window(const BpqMagPanel *p, const char *sta, int kind,
                         double *t0, double *t1)
{
    double d0 = NAN, d1 = NAN;

    if (!p) {
        if (t0) *t0 = NAN;
        if (t1) *t1 = NAN;
        return;
    }
    csnhypodbp_magcfg_window(&p->cfg, kind, &d0, &d1);
    csnhypodbp_magmarks_window(&p->marks, sta, kind, d0, d1, t0, t1);
}

int bpq_magpanel_set_window(BpqMagPanel *p, const char *sta, int kind,
                            double t0, double t1)
{
    if (!p) return 0;
    return csnhypodbp_magmarks_set(&p->marks, sta, kind, t0, t1) == 0 ? 1 : 0;
}

int bpq_magpanel_touched(const BpqMagPanel *p, const char *sta, int kind)
{
    return p ? csnhypodbp_magmarks_touched(&p->marks, sta, kind) : 0;
}

int bpq_magpanel_touched_count(const BpqMagPanel *p)
{
    return p ? csnhypodbp_magmarks_touched_count(&p->marks) : 0;
}

int bpq_magpanel_rows(const BpqMagPanel *p, const CsnhypodbpStaRow *stas, int n,
                      const CsnhypodbpMagIn *vals, int nval, double red,
                      CsnhypodbpMagRow *out, int max)
{
    CsnhypodbpMagCfg eff;
    double d0 = NAN, d1 = NAN;
    int i, nr;

    if (!p || !out || max <= 0) return 0;
    eff_cfg(p, p->kind, &eff);
    csnhypodbp_magcfg_window(&eff, p->kind, &d0, &d1);

    nr = csnhypodbp_magrow_build(out, max, stas, n, vals, nval, red, d0, d1);
    for (i = 0; i < nr; i++) {
        CsnhypodbpMagRow *r = &out[i];

        r->win_src = csnhypodbp_magmarks_winsrc(&p->marks, r->sta, p->kind,
                                                r->anchor);
        csnhypodbp_magmarks_window(&p->marks, r->sta, p->kind, d0, d1,
                                   &r->t0, &r->t1);
        csnhypodbp_magcfg_gate(&eff, p->kind, r->dist_deg, r->dist_km,
                               &r->in_range);
        r->eligible = (r->has_data && r->in_range &&
                       csnhypodbp_magmarks_allowed(&p->marks, r->sta, p->kind,
                                                   r->anchor, p->auto_on,
                                                   p->manual_on,
                                                   p->theo_on)) ? 1 : 0;
        /* `used` no puede ir por delante de la elegibilidad: si el origen de
         * la ventana esta apagado, la estacion no entra al calculo. Es lo que
         * hace la GTK (eligible ? magmarks_used : 0). */
        r->used = r->eligible
                      ? csnhypodbp_magmarks_used(&p->marks, r->sta, p->kind,
                                                 r->has_data && r->in_range)
                      : 0;
    }
    return nr;
}

int bpq_magpanel_fetch_rows(const BpqMagPanel *p, const CsnhypodbpStaRow *stas,
                            int n, unsigned kind_mask, double depth_km,
                            int auto_on, int manual_on, int theo_on,
                            int *out_rows, int max)
{
    int i, pass, nout = 0;

    if (!p || !stas || !out_rows || max <= 0) return 0;
    /* Dos pasadas: primero las P reales (M/A) y despues las Teo. Si el tanque
     * se corta en `max`, no se queda con 48 teoricas y deja fuera las picadas
     * que el operador acaba de marcar a mano. */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < n && nout < max; i++) {
            const CsnhypodbpStaPick *pk = &stas[i].p;
            const int is_theo = (pk->origin == 'T' || pk->origin == 't');
            int ok = 0, k;

            if (!pk->present) continue;
            if (is_theo != (pass == 1)) continue;
            for (k = 0; k < BPQ_MAG_KINDS && !ok; k++) {
                CsnhypodbpMagCfg eff;
                int in_range = 1;

                if (kind_mask && !(kind_mask & (1u << k))) continue;
                eff_cfg(p, k, &eff);
                csnhypodbp_magcfg_gate(&eff, k, stas[i].dist_deg, stas[i].dist_km,
                                       &in_range);
                if (!in_range) continue;
                if (!csnhypodbp_magcfg_depth_ok(&eff, k, depth_km)) continue;
                if (!csnhypodbp_magmarks_allowed(&p->marks, stas[i].sta, k,
                                                 pk->origin, auto_on, manual_on,
                                                 theo_on))
                    continue;
                ok = 1;
            }
            if (ok) out_rows[nout++] = i;
        }
    }
    return nout;
}

int bpq_magrows_filter_in_range(const CsnhypodbpMagRow *in, int n,
                                CsnhypodbpMagRow *out, int max)
{
    int i, nout = 0;

    if (!in || !out || n <= 0 || max <= 0) return 0;
    for (i = 0; i < n && nout < max; i++) {
        if (!in[i].in_range) continue;   /* fuera de la puerta: solo en picking */
        out[nout++] = in[i];
    }
    return nout;
}

/* Solucion que ve csnmags en `calculate`: copia la candidata (origen, lat,
 * lon, prof, qid) y REEMPLAZA sus P/S por las picadas efectivas de la tabla
 * permitidas por los checks de magnitudes. `hypos_write` solo itera
 * base->picks, asi que sin esto las manuales nuevas y las teoricas no llegan. */
int bpq_magpanel_calc_solution(const CsnhypodbpStaRow *stas, int n,
                               int auto_on, int manual_on, int theo_on,
                               const CsnhypodbpSolution *base,
                               CsnhypodbpSolution *out)
{
    CsnhypodbpPickFilter f;
    int np;

    if (!stas || n <= 0 || !base || !out) return -1;
    *out = *base;
    f.auto_on = auto_on ? 1 : 0;
    f.manual_on = manual_on ? 1 : 0;
    f.theo_on = theo_on ? 1 : 0;
    np = csnhypodbp_locate_picks(stas, n, NULL, &f, out->picks,
                                 CSNHYPODBP_MAX_PICKS);
    out->npicks = np;
    out->nph = np;
    return np;
}

int bpq_magpanel_ovr(const BpqMagPanel *p, const CsnhypodbpStaRow *stas, int n,
                     unsigned kind_mask, CsnhypodbpMagOvrRow *out, int max)
{
    CsnhypodbpMagAnchor *anchors;
    double d0[BPQ_MAG_KINDS], d1[BPQ_MAG_KINDS];
    int i, na = 0, k, nr;

    if (!p || !out || max <= 0 || n <= 0) return 0;
    anchors = (CsnhypodbpMagAnchor *)calloc((size_t)n, sizeof(*anchors));
    if (!anchors) return 0;

    for (i = 0; i < n; i++) {
        const CsnhypodbpStaPick *pk = &stas[i].p;   /* P efectiva (M>A>Teo) */

        if (!pk->present || !(pk->t > 0.0)) continue;
        snprintf(anchors[na].sta, sizeof(anchors[na].sta), "%s", stas[i].sta);
        anchors[na].anchor_t = pk->t;
        na++;
    }
    for (k = 0; k < BPQ_MAG_KINDS; k++) {
        d0[k] = NAN;
        d1[k] = NAN;
        csnhypodbp_magcfg_window(&p->cfg, k, &d0[k], &d1[k]);
    }
    nr = csnhypodbp_mag_ovr_build_anchors(&p->marks, anchors, na, d0, d1,
                                          kind_mask, out, max);
    free(anchors);
    return nr;
}

int bpq_magpanel_req_window(const BpqMagPanel *p, const CsnhypodbpStaRow *row,
                            int kind, const CsnhypodbpMagOvrRow *ovr, int novr,
                            double *t0, double *seconds)
{
    double a = 0.0, b = 0.0, lo, hi;
    int i;

    if (!p || !row || !t0 || !seconds) return 0;
    if (kind < 0 || kind >= BPQ_MAG_KINDS) return 0;
    if (!row->p.present || !(row->p.t > 0.0)) return 0;

    csnhypodbp_magcfg_window(&p->cfg, kind, &a, &b);
    /* Mismo recorte que csnmags: -2 s (Mwp/ML), -1 s (Mb), 0 (Ms). El margen
     * de 5 s cubre ese recorte y el redondeo de la ventana. */
    lo = row->p.t + a - 2.0;
    hi = row->p.t + b;
    for (i = 0; i < novr; i++) {
        if (ovr[i].kind != kind) continue;
        if (strcmp(ovr[i].sta, row->sta) != 0) continue;
        if (!(ovr[i].t1 > ovr[i].t0)) continue;
        lo = ovr[i].t0;
        hi = ovr[i].t1;
        break;
    }
    lo -= 5.0;
    hi += 5.0;
    if (!(hi > lo)) return 0;
    *t0 = lo;
    *seconds = hi - lo;
    return 1;
}

int bpq_magpanel_ovr_write(const BpqMagPanel *p, const CsnhypodbpStaRow *stas,
                           int n, const char *path)
{
    CsnhypodbpMagOvrRow rows[600];
    int nr;

    if (!path || !path[0]) return -1;
    nr = bpq_magpanel_ovr(p, stas, n, 0u, rows, 600);
    if (nr < 0) return -1;
    return csnhypodbp_mag_ovr_write(path, rows, nr);
}

static double gate_lo(const CsnhypodbpMagGateOv *g, const CsnhypodbpMagCfg *c,
                      int kind)
{
    double lo = 0.0, hi = 0.0, km = 0.0;

    if (g->on) return g->min_delta;
    csnhypodbp_magcfg_gate_limits(c, kind, &lo, &hi, &km);
    return lo;
}

static double gate_hi(const CsnhypodbpMagGateOv *g, const CsnhypodbpMagCfg *c,
                      int kind)
{
    double lo = 0.0, hi = 0.0, km = 0.0;

    if (g->on) return g->max_delta;
    csnhypodbp_magcfg_gate_limits(c, kind, &lo, &hi, &km);
    return hi;
}

void bpq_magpanel_gate_texts(const BpqMagPanel *p, int kind,
                             char *lo, size_t nlo, char *hi, size_t nhi)
{
    if (lo && nlo) lo[0] = '\0';
    if (hi && nhi) hi[0] = '\0';
    if (!p) return;
    if (lo && nlo) snprintf(lo, nlo, "%.1f", gate_lo(&p->gate, &p->cfg, kind));
    if (hi && nhi) snprintf(hi, nhi, "%.1f", gate_hi(&p->gate, &p->cfg, kind));
}

int bpq_magpanel_gate_set(BpqMagPanel *p, const char *lo_txt, const char *hi_txt)
{
    double lo = 0.0, hi = 0.0;

    if (!p) return 0;
    /* Solo se aplica si las dos cajas valen (como la barra de la GTK).
     * OJO: el nucleo devuelve 0 en exito (no 1). */
    if (csnhypodbp_mag_ov_parse(lo_txt, CSNHYPODBP_MAG_OV_MIN_DELTA, &lo) != 0)
        return 0;
    if (csnhypodbp_mag_ov_parse(hi_txt, CSNHYPODBP_MAG_OV_MAX_DELTA, &hi) != 0)
        return 0;
    if (hi > 0.0 && hi < lo) return 0;

    p->gate.on = 1;
    p->gate.min_delta = lo;
    p->gate.max_delta = hi;
    return 1;
}

void bpq_magpanel_gate_reset(BpqMagPanel *p)
{
    if (!p) return;
    p->gate.on = 0;
    p->gate.min_delta = p->cfg.ml_min_delta;
    p->gate.max_delta = p->cfg.ml_max_delta;
    p->gate.min_depth = p->cfg.ml_min_depth;
    p->gate.max_depth = p->cfg.ml_max_depth;
}

void bpq_magpanel_status(const CsnhypodbpMagRow *rows, int n, char *out,
                         size_t errn)
{
    int con_dato = 0, fuera = 0, filtradas = 0, enuso = 0;

    if (!out || !errn) return;
    csnhypodbp_magrow_counts(rows, n, &con_dato, &fuera, &filtradas, &enuso);
    snprintf(out, errn, "%d estaciones | %d con dato | %d en uso | %d fuera "
             "de la puerta | %d filtradas", n, con_dato, enuso, fuera, filtradas);
}
