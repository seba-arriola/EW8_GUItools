#include "ew_controller_row.h"

/* ---------------- EcModRow ---------------- */
struct _EcModRow {
    GObject parent_instance;
    char *name, *status, *details;
    int   pid;
};

G_DEFINE_TYPE(EcModRow, ec_mod_row, G_TYPE_OBJECT)

enum { M_PROP_0, M_PROP_NAME, M_PROP_PID, M_PROP_STATUS, M_PROP_DETAILS, M_N_PROPS };
static GParamSpec *m_props[M_N_PROPS];

static void ec_mod_row_finalize(GObject *o)
{
    EcModRow *r = EC_MOD_ROW(o);
    g_free(r->name); g_free(r->status); g_free(r->details);
    G_OBJECT_CLASS(ec_mod_row_parent_class)->finalize(o);
}
static void ec_mod_row_set_property(GObject *o, guint id, const GValue *v, GParamSpec *p)
{
    (void)p;
    EcModRow *r = EC_MOD_ROW(o);
    switch (id) {
        case M_PROP_NAME:    g_free(r->name);    r->name    = g_value_dup_string(v); break;
        case M_PROP_PID:     r->pid = g_value_get_int(v); break;
        case M_PROP_STATUS:  g_free(r->status);  r->status  = g_value_dup_string(v); break;
        case M_PROP_DETAILS: g_free(r->details); r->details = g_value_dup_string(v); break;
    }
}
static void ec_mod_row_get_property(GObject *o, guint id, GValue *v, GParamSpec *p)
{
    (void)p;
    EcModRow *r = EC_MOD_ROW(o);
    switch (id) {
        case M_PROP_NAME:    g_value_set_string(v, r->name);    break;
        case M_PROP_PID:     g_value_set_int(v, r->pid);        break;
        case M_PROP_STATUS:  g_value_set_string(v, r->status);  break;
        case M_PROP_DETAILS: g_value_set_string(v, r->details); break;
    }
}
static void ec_mod_row_class_init(EcModRowClass *k)
{
    GObjectClass *o = G_OBJECT_CLASS(k);
    o->finalize = ec_mod_row_finalize;
    o->set_property = ec_mod_row_set_property;
    o->get_property = ec_mod_row_get_property;
    m_props[M_PROP_NAME]    = g_param_spec_string("name", "name", NULL, NULL, G_PARAM_READWRITE);
    m_props[M_PROP_PID]     = g_param_spec_int("pid", "pid", NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE);
    m_props[M_PROP_STATUS]  = g_param_spec_string("status", "status", NULL, NULL, G_PARAM_READWRITE);
    m_props[M_PROP_DETAILS] = g_param_spec_string("details", "details", NULL, NULL, G_PARAM_READWRITE);
    g_object_class_install_properties(o, M_N_PROPS, m_props);
}
static void ec_mod_row_init(EcModRow *r) { (void)r; }

EcModRow *ec_mod_row_new(const char *name, int pid, const char *status, const char *details)
{
    return g_object_new(EC_TYPE_MOD_ROW, "name", name, "pid", pid,
                        "status", status, "details", details, NULL);
}

/* ---------------- EcRingRow ---------------- */
struct _EcRingRow {
    GObject parent_instance;
    char *name;
    int   key, size;
};

G_DEFINE_TYPE(EcRingRow, ec_ring_row, G_TYPE_OBJECT)

enum { R_PROP_0, R_PROP_NAME, R_PROP_KEY, R_PROP_SIZE, R_N_PROPS };
static GParamSpec *r_props[R_N_PROPS];

static void ec_ring_row_finalize(GObject *o)
{
    EcRingRow *r = EC_RING_ROW(o);
    g_free(r->name);
    G_OBJECT_CLASS(ec_ring_row_parent_class)->finalize(o);
}
static void ec_ring_row_set_property(GObject *o, guint id, const GValue *v, GParamSpec *p)
{
    (void)p;
    EcRingRow *r = EC_RING_ROW(o);
    switch (id) {
        case R_PROP_NAME: g_free(r->name); r->name = g_value_dup_string(v); break;
        case R_PROP_KEY:  r->key = g_value_get_int(v); break;
        case R_PROP_SIZE: r->size = g_value_get_int(v); break;
    }
}
static void ec_ring_row_get_property(GObject *o, guint id, GValue *v, GParamSpec *p)
{
    (void)p;
    EcRingRow *r = EC_RING_ROW(o);
    switch (id) {
        case R_PROP_NAME: g_value_set_string(v, r->name); break;
        case R_PROP_KEY:  g_value_set_int(v, r->key); break;
        case R_PROP_SIZE: g_value_set_int(v, r->size); break;
    }
}
static void ec_ring_row_class_init(EcRingRowClass *k)
{
    GObjectClass *o = G_OBJECT_CLASS(k);
    o->finalize = ec_ring_row_finalize;
    o->set_property = ec_ring_row_set_property;
    o->get_property = ec_ring_row_get_property;
    r_props[R_PROP_NAME] = g_param_spec_string("name", "name", NULL, NULL, G_PARAM_READWRITE);
    r_props[R_PROP_KEY]  = g_param_spec_int("key", "key", NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE);
    r_props[R_PROP_SIZE] = g_param_spec_int("size", "size", NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE);
    g_object_class_install_properties(o, R_N_PROPS, r_props);
}
static void ec_ring_row_init(EcRingRow *r) { (void)r; }

EcRingRow *ec_ring_row_new(const char *name, int key, int size)
{
    return g_object_new(EC_TYPE_RING_ROW, "name", name, "key", key, "size", size, NULL);
}
