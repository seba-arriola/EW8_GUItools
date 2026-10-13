/*
 * csnhypodbp_magitem.c - Modelo de fila de la tabla de magnitudes (ver
 * csnhypodbp_magitem.h). Las celdas de texto las formatea el nucleo puro
 * (csnhypodbp_magrow_*_text), asi el formato es el mismo en la tabla y en los
 * tests headless.
 */

#include <math.h>
#include <stdio.h>

#include "csnhypodbp_magitem.h"

struct _CsnhypodbpMagItem {
    GObject   parent_instance;
    gboolean  used;
    gboolean  hasdata;   /* la estacion tiene traza: la casilla es sensible */
    char     *sta, *dist, *mag, *res, *stat, *chan;
    char     *anchor, *t0, *t1;   /* ancla de la P y ventana vigente (texto) */
    /* Valores numericos para ordenar por cabecera (las celdas son texto): -1 =
     * sin dato, igual que `nmag` en la tabla de Eventos. */
    double    distv, magv, resv, t0v, t1v;
    int       idx;
};

G_DEFINE_TYPE(CsnhypodbpMagItem, csnhypodbp_mag_item, G_TYPE_OBJECT)

enum {
    PROP_0, PROP_STA, PROP_DIST, PROP_MAG, PROP_RES, PROP_STAT, PROP_CHAN,
    PROP_IDX, PROP_USED, PROP_DISTV, PROP_MAGV, PROP_RESV,
    PROP_ANCHOR, PROP_T0, PROP_T1, PROP_T0V, PROP_T1V, PROP_HASDATA, N_PROPS
};
static GParamSpec *props[N_PROPS];

static void csnhypodbp_mag_item_finalize(GObject *o)
{
    CsnhypodbpMagItem *it = CSNHYPODBP_MAG_ITEM(o);
    g_free(it->sta); g_free(it->dist); g_free(it->mag);
    g_free(it->res); g_free(it->stat); g_free(it->chan);
    g_free(it->anchor); g_free(it->t0); g_free(it->t1);
    G_OBJECT_CLASS(csnhypodbp_mag_item_parent_class)->finalize(o);
}

static void csnhypodbp_mag_item_set_property(GObject *o, guint id, const GValue *v,
                                             GParamSpec *ps)
{
    (void)ps;
    CsnhypodbpMagItem *it = CSNHYPODBP_MAG_ITEM(o);
    switch (id) {
        case PROP_USED: it->used = g_value_get_boolean(v); break;
        case PROP_HASDATA: it->hasdata = g_value_get_boolean(v); break;
        case PROP_IDX:  it->idx  = g_value_get_int(v);     break;
        case PROP_DISTV: it->distv = g_value_get_double(v); break;
        case PROP_MAGV:  it->magv  = g_value_get_double(v); break;
        case PROP_RESV:  it->resv  = g_value_get_double(v); break;
        case PROP_T0V:   it->t0v   = g_value_get_double(v); break;
        case PROP_T1V:   it->t1v   = g_value_get_double(v); break;
        case PROP_ANCHOR: g_free(it->anchor); it->anchor = g_value_dup_string(v); break;
        case PROP_T0:   g_free(it->t0); it->t0 = g_value_dup_string(v); break;
        case PROP_T1:   g_free(it->t1); it->t1 = g_value_dup_string(v); break;
        case PROP_STA:  g_free(it->sta);  it->sta  = g_value_dup_string(v); break;
        case PROP_DIST: g_free(it->dist); it->dist = g_value_dup_string(v); break;
        case PROP_MAG:  g_free(it->mag);  it->mag  = g_value_dup_string(v); break;
        case PROP_RES:  g_free(it->res);  it->res  = g_value_dup_string(v); break;
        case PROP_STAT: g_free(it->stat); it->stat = g_value_dup_string(v); break;
        case PROP_CHAN: g_free(it->chan); it->chan = g_value_dup_string(v); break;
        default: break;
    }
}

static void csnhypodbp_mag_item_get_property(GObject *o, guint id, GValue *v,
                                             GParamSpec *ps)
{
    (void)ps;
    CsnhypodbpMagItem *it = CSNHYPODBP_MAG_ITEM(o);
    switch (id) {
        case PROP_USED: g_value_set_boolean(v, it->used); break;
        case PROP_HASDATA: g_value_set_boolean(v, it->hasdata); break;
        case PROP_IDX:  g_value_set_int(v, it->idx);      break;
        case PROP_DISTV: g_value_set_double(v, it->distv); break;
        case PROP_MAGV:  g_value_set_double(v, it->magv);  break;
        case PROP_RESV:  g_value_set_double(v, it->resv);  break;
        case PROP_T0V:   g_value_set_double(v, it->t0v);   break;
        case PROP_T1V:   g_value_set_double(v, it->t1v);   break;
        case PROP_ANCHOR: g_value_set_string(v, it->anchor ? it->anchor : ""); break;
        case PROP_T0:   g_value_set_string(v, it->t0 ? it->t0 : ""); break;
        case PROP_T1:   g_value_set_string(v, it->t1 ? it->t1 : ""); break;
        case PROP_STA:  g_value_set_string(v, it->sta  ? it->sta  : ""); break;
        case PROP_DIST: g_value_set_string(v, it->dist ? it->dist : ""); break;
        case PROP_MAG:  g_value_set_string(v, it->mag  ? it->mag  : ""); break;
        case PROP_RES:  g_value_set_string(v, it->res  ? it->res  : ""); break;
        case PROP_STAT: g_value_set_string(v, it->stat ? it->stat : ""); break;
        case PROP_CHAN: g_value_set_string(v, it->chan ? it->chan : ""); break;
        default: break;
    }
}

static void csnhypodbp_mag_item_class_init(CsnhypodbpMagItemClass *k)
{
    GObjectClass *o = G_OBJECT_CLASS(k);
    o->finalize     = csnhypodbp_mag_item_finalize;
    o->set_property = csnhypodbp_mag_item_set_property;
    o->get_property = csnhypodbp_mag_item_get_property;

    props[PROP_USED] = g_param_spec_boolean("used", "used", NULL, TRUE,
                                            G_PARAM_READWRITE);
    props[PROP_IDX]  = g_param_spec_int("idx", "idx", NULL, G_MININT, G_MAXINT, -1,
                                        G_PARAM_READWRITE);
    props[PROP_STA]  = g_param_spec_string("sta", "sta", NULL, "", G_PARAM_READWRITE);
    props[PROP_DIST] = g_param_spec_string("dist", "dist", NULL, "", G_PARAM_READWRITE);
    props[PROP_MAG]  = g_param_spec_string("mag", "mag", NULL, "", G_PARAM_READWRITE);
    props[PROP_RES]  = g_param_spec_string("res", "res", NULL, "", G_PARAM_READWRITE);
    props[PROP_STAT] = g_param_spec_string("stat", "stat", NULL, "", G_PARAM_READWRITE);
    props[PROP_CHAN] = g_param_spec_string("chan", "chan", NULL, "", G_PARAM_READWRITE);
    props[PROP_DISTV] = g_param_spec_double("distv", "distv", NULL, -1.0, G_MAXDOUBLE,
                                            -1.0, G_PARAM_READWRITE);
    props[PROP_MAGV]  = g_param_spec_double("magv", "magv", NULL, -1.0, G_MAXDOUBLE,
                                            -1.0, G_PARAM_READWRITE);
    props[PROP_RESV]  = g_param_spec_double("resv", "resv", NULL, -G_MAXDOUBLE,
                                            G_MAXDOUBLE, -1e30, G_PARAM_READWRITE);
    props[PROP_T0V]   = g_param_spec_double("t0v", "t0v", NULL, -G_MAXDOUBLE,
                                            G_MAXDOUBLE, -1e30, G_PARAM_READWRITE);
    props[PROP_T1V]   = g_param_spec_double("t1v", "t1v", NULL, -G_MAXDOUBLE,
                                            G_MAXDOUBLE, -1e30, G_PARAM_READWRITE);
    props[PROP_ANCHOR] = g_param_spec_string("anchor", "anchor", NULL, "",
                                             G_PARAM_READWRITE);
    props[PROP_T0]    = g_param_spec_string("t0", "t0", NULL, "", G_PARAM_READWRITE);
    props[PROP_T1]    = g_param_spec_string("t1", "t1", NULL, "", G_PARAM_READWRITE);
    props[PROP_HASDATA] = g_param_spec_boolean("hasdata", "hasdata", NULL, FALSE,
                                               G_PARAM_READWRITE);
    g_object_class_install_properties(o, N_PROPS, props);
}

static void csnhypodbp_mag_item_init(CsnhypodbpMagItem *it)
{
    it->used = TRUE;
    it->hasdata = FALSE;
    it->idx = -1;
    it->distv = it->magv = it->resv = it->t0v = it->t1v = -1e30;
    it->sta = it->dist = it->mag = it->res = it->stat = it->chan = NULL;
    it->anchor = it->t0 = it->t1 = NULL;
}

CsnhypodbpMagItem *csnhypodbp_mag_item_new(const CsnhypodbpMagRow *r, int idx)
{
    char dist[24] = "", mag[24] = "", res[24] = "", t0[24] = "", t1[24] = "";

    if (!r) return NULL;
    if (r->dist_km > 0.0) snprintf(dist, sizeof(dist), "%.0f", r->dist_km);
    csnhypodbp_magrow_mag_text(r, mag, sizeof(mag));
    csnhypodbp_magrow_res_text(r, res, sizeof(res));
    csnhypodbp_magrow_t0_text(r, t0, sizeof(t0));
    csnhypodbp_magrow_t1_text(r, t1, sizeof(t1));

    return g_object_new(CSNHYPODBP_TYPE_MAG_ITEM,
        "sta",  r->sta,
        "dist", dist,
        "mag",  mag,
        "res",  res,
        "stat", csnhypodbp_magrow_stat_text(r),
        "chan", r->chan,
        "idx",  idx,
        "used", r->used ? TRUE : FALSE,
        "hasdata", r->has_data ? TRUE : FALSE,
        "anchor", csnhypodbp_magrow_anchor_text(r),
        "t0", t0,
        "t1", t1,
        "distv", (r->dist_km > 0.0) ? r->dist_km : -1.0,
        "magv",  isfinite(r->mag) ? r->mag : -1.0,
        "resv",  isfinite(r->res) ? r->res : -1e30,
        "t0v", isfinite(r->t0) ? r->t0 : -1e30,
        "t1v", isfinite(r->t1) ? r->t1 : -1e30,
        NULL);
}

int csnhypodbp_mag_item_idx(CsnhypodbpMagItem *it)
{
    return it ? it->idx : -1;
}
