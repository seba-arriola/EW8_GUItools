#include <math.h>
#include <stdio.h>

#include "csnhypodbp_staitem.h"

struct _CsnhypodbpStaItem {
    GObject  parent_instance;
    gboolean has_pick;
    char    *sta, *dist, *pick, *stat, *res;
    int      idx, phase;
};

G_DEFINE_TYPE(CsnhypodbpStaItem, csnhypodbp_sta_item, G_TYPE_OBJECT)

enum {
    PROP_0, PROP_STA, PROP_DIST, PROP_PICK, PROP_STAT, PROP_RES,
    PROP_IDX, PROP_PHASE, PROP_HAS_PICK, N_PROPS
};
static GParamSpec *props[N_PROPS];

static void csnhypodbp_sta_item_finalize(GObject *o)
{
    CsnhypodbpStaItem *it = CSNHYPODBP_STA_ITEM(o);
    g_free(it->sta); g_free(it->dist); g_free(it->pick);
    g_free(it->stat); g_free(it->res);
    G_OBJECT_CLASS(csnhypodbp_sta_item_parent_class)->finalize(o);
}

static void csnhypodbp_sta_item_set_property(GObject *o, guint id, const GValue *v,
                                             GParamSpec *ps)
{
    (void)ps;
    CsnhypodbpStaItem *it = CSNHYPODBP_STA_ITEM(o);
    switch (id) {
        case PROP_HAS_PICK: it->has_pick = g_value_get_boolean(v); break;
        case PROP_IDX:      it->idx      = g_value_get_int(v);     break;
        case PROP_PHASE:    it->phase    = g_value_get_int(v);     break;
        case PROP_STA:  g_free(it->sta);  it->sta  = g_value_dup_string(v); break;
        case PROP_DIST: g_free(it->dist); it->dist = g_value_dup_string(v); break;
        case PROP_PICK: g_free(it->pick); it->pick = g_value_dup_string(v); break;
        case PROP_STAT: g_free(it->stat); it->stat = g_value_dup_string(v); break;
        case PROP_RES:  g_free(it->res);  it->res  = g_value_dup_string(v); break;
        default: break;
    }
}

static void csnhypodbp_sta_item_get_property(GObject *o, guint id, GValue *v,
                                             GParamSpec *ps)
{
    (void)ps;
    CsnhypodbpStaItem *it = CSNHYPODBP_STA_ITEM(o);
    switch (id) {
        case PROP_HAS_PICK: g_value_set_boolean(v, it->has_pick); break;
        case PROP_IDX:      g_value_set_int(v, it->idx);          break;
        case PROP_PHASE:    g_value_set_int(v, it->phase);        break;
        case PROP_STA:  g_value_set_string(v, it->sta  ? it->sta  : ""); break;
        case PROP_DIST: g_value_set_string(v, it->dist ? it->dist : ""); break;
        case PROP_PICK: g_value_set_string(v, it->pick ? it->pick : ""); break;
        case PROP_STAT: g_value_set_string(v, it->stat ? it->stat : ""); break;
        case PROP_RES:  g_value_set_string(v, it->res  ? it->res  : ""); break;
        default: break;
    }
}

static void csnhypodbp_sta_item_class_init(CsnhypodbpStaItemClass *k)
{
    GObjectClass *o = G_OBJECT_CLASS(k);
    o->finalize     = csnhypodbp_sta_item_finalize;
    o->set_property = csnhypodbp_sta_item_set_property;
    o->get_property = csnhypodbp_sta_item_get_property;

    props[PROP_HAS_PICK] = g_param_spec_boolean("has-pick", "has-pick", NULL, FALSE,
                                                G_PARAM_READWRITE);
    props[PROP_IDX]      = g_param_spec_int("idx", "idx", NULL, G_MININT, G_MAXINT, -1,
                                            G_PARAM_READWRITE);
    props[PROP_PHASE]    = g_param_spec_int("phase", "phase", NULL, 0, 1, 0,
                                            G_PARAM_READWRITE);
    props[PROP_STA]  = g_param_spec_string("sta", "sta", NULL, "", G_PARAM_READWRITE);
    props[PROP_DIST] = g_param_spec_string("dist", "dist", NULL, "", G_PARAM_READWRITE);
    props[PROP_PICK] = g_param_spec_string("pick", "pick", NULL, "", G_PARAM_READWRITE);
    props[PROP_STAT] = g_param_spec_string("stat", "stat", NULL, "", G_PARAM_READWRITE);
    props[PROP_RES]  = g_param_spec_string("res", "res", NULL, "", G_PARAM_READWRITE);
    g_object_class_install_properties(o, N_PROPS, props);
}

static void csnhypodbp_sta_item_init(CsnhypodbpStaItem *it)
{
    it->has_pick = FALSE;
    it->idx = -1;
    it->phase = 0;
    it->sta = it->dist = it->pick = it->stat = it->res = NULL;
}

/* Texto del origen para la columna `stat`. */
static const char *origin_text(char origin)
{
    if (origin == 'T' || origin == 't') return "Teo";
    if (origin == 'M' || origin == 'm') return "M";
    if (origin == 'A' || origin == 'a') return "A";
    return "";
}

CsnhypodbpStaItem *csnhypodbp_sta_item_new(const CsnhypodbpStaRow *r, int phase)
{
    char sta[16] = "", dist[24] = "", stat[8] = "", res[24] = "";
    const CsnhypodbpStaPick *p;

    if (!r) return NULL;
    if (phase != 1) phase = 0;
    p = (phase == 0) ? &r->p : &r->s;

    snprintf(sta, sizeof(sta), "%s", r->sta);
    /* La distancia va en las DOS filas: cada fila se lee sola. */
    if (r->dist_km > 0.0) snprintf(dist, sizeof(dist), "%.0f", r->dist_km);
    if (p->present) {
        snprintf(stat, sizeof(stat), "%s", origin_text(p->origin));
        if (!isnan(p->resid)) snprintf(res, sizeof(res), "%+.3f", p->resid);
    }

    return g_object_new(CSNHYPODBP_TYPE_STA_ITEM,
        "sta",      sta,
        "dist",     dist,
        "pick",     (phase == 0) ? "P" : "S",
        "stat",     stat,
        "res",      res,
        "idx",      r->idx,
        "phase",    phase,
        "has-pick", p->present ? TRUE : FALSE,
        NULL);
}

int csnhypodbp_sta_item_idx(CsnhypodbpStaItem *it)
{
    return it ? it->idx : -1;
}

int csnhypodbp_sta_item_phase(CsnhypodbpStaItem *it)
{
    return it ? it->phase : 0;
}
