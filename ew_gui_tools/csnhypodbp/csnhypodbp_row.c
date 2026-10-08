#include <stdio.h>

#include "csnhypodbp_row.h"

struct _CsnhypodbpRow {
    GObject parent_instance;
    char  *cols[13];
    double otime, lat, lon, depth;
    int    qver, qid, mod;
};

G_DEFINE_TYPE(CsnhypodbpRow, csnhypodbp_row, G_TYPE_OBJECT)

enum {
    PROP_0, PROP_C0, PROP_C1, PROP_C2, PROP_C3, PROP_C4, PROP_C5, PROP_C6,
    PROP_C7, PROP_C8, PROP_C9, PROP_C10, PROP_C11, PROP_C12,
    PROP_OTIME, PROP_QVER, PROP_QID, PROP_LAT, PROP_LON, PROP_DEPTH, PROP_MOD, N_PROPS
};
static GParamSpec *props[N_PROPS];

static void csnhypodbp_row_finalize(GObject *o)
{
    CsnhypodbpRow *r = CSNHYPODBP_ROW(o);
    for (int i = 0; i < 13; i++) g_free(r->cols[i]);
    G_OBJECT_CLASS(csnhypodbp_row_parent_class)->finalize(o);
}

static void csnhypodbp_row_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps)
{
    (void)ps;
    CsnhypodbpRow *r = CSNHYPODBP_ROW(o);
    if (id >= PROP_C0 && id <= PROP_C12) {
        int i = (int)(id - PROP_C0);
        g_free(r->cols[i]);
        r->cols[i] = g_value_dup_string(v);
    } else switch (id) {
        case PROP_OTIME: r->otime = g_value_get_double(v); break;
        case PROP_QVER:  r->qver  = g_value_get_int(v);    break;
        case PROP_QID:   r->qid   = g_value_get_int(v);    break;
        case PROP_LAT:   r->lat   = g_value_get_double(v); break;
        case PROP_LON:   r->lon   = g_value_get_double(v); break;
        case PROP_DEPTH: r->depth = g_value_get_double(v); break;
        case PROP_MOD:   r->mod   = g_value_get_int(v);    break;
    }
}

static void csnhypodbp_row_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps)
{
    (void)ps;
    CsnhypodbpRow *r = CSNHYPODBP_ROW(o);
    if (id >= PROP_C0 && id <= PROP_C12) {
        g_value_set_string(v, r->cols[id - PROP_C0]);
    } else switch (id) {
        case PROP_OTIME: g_value_set_double(v, r->otime); break;
        case PROP_QVER:  g_value_set_int(v, r->qver);     break;
        case PROP_QID:   g_value_set_int(v, r->qid);      break;
        case PROP_LAT:   g_value_set_double(v, r->lat);   break;
        case PROP_LON:   g_value_set_double(v, r->lon);   break;
        case PROP_DEPTH: g_value_set_double(v, r->depth); break;
        case PROP_MOD:   g_value_set_int(v, r->mod);      break;
    }
}

static void csnhypodbp_row_class_init(CsnhypodbpRowClass *k)
{
    GObjectClass *o = G_OBJECT_CLASS(k);
    o->finalize = csnhypodbp_row_finalize;
    o->set_property = csnhypodbp_row_set_property;
    o->get_property = csnhypodbp_row_get_property;

    for (int i = 0; i < 13; i++) {
        char name[8]; snprintf(name, sizeof(name), "c%d", i);
        props[PROP_C0 + i] = g_param_spec_string(name, name, NULL, NULL, G_PARAM_READWRITE);
    }
    props[PROP_OTIME] = g_param_spec_double("otime", "otime", NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READWRITE);
    props[PROP_QVER]  = g_param_spec_int("qver", "qver", NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE);
    props[PROP_QID]   = g_param_spec_int("qid", "qid", NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE);
    props[PROP_LAT]   = g_param_spec_double("lat", "lat", NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READWRITE);
    props[PROP_LON]   = g_param_spec_double("lon", "lon", NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READWRITE);
    props[PROP_DEPTH] = g_param_spec_double("depth", "depth", NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READWRITE);
    props[PROP_MOD]   = g_param_spec_int("mod", "mod", NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE);
    g_object_class_install_properties(o, N_PROPS, props);
}

static void csnhypodbp_row_init(CsnhypodbpRow *r)
{
    for (int i = 0; i < 13; i++) r->cols[i] = NULL;
}

CsnhypodbpRow *csnhypodbp_row_new(const char *c0, const char *c1, const char *c2,
                                  const char *c3, const char *c4, const char *c5,
                                  const char *c6, const char *c7, const char *c8,
                                  const char *c9, const char *c10, const char *c11,
                                  const char *c12, double otime, int qver, int qid,
                                  double lat, double lon, double depth, int mod)
{
    return g_object_new(CSNHYPODBP_TYPE_ROW,
        "c0", c0, "c1", c1, "c2", c2, "c3", c3, "c4", c4, "c5", c5, "c6", c6,
        "c7", c7, "c8", c8, "c9", c9, "c10", c10, "c11", c11, "c12", c12,
        "otime", otime, "qver", qver, "qid", qid, "lat", lat, "lon", lon,
        "depth", depth, "mod", mod, NULL);
}

const char *csnhypodbp_row_col(CsnhypodbpRow *r, int i)
{
    return (i >= 0 && i < 13 && r->cols[i]) ? r->cols[i] : "";
}
