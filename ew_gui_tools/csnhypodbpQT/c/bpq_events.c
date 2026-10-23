/*
 * bpq_events.c - Store de eventos del prototipo.
 *
 * Port de la parte de datos de csnhypodbp_ws.c (EventSol/EventGroup y
 * procesar_mensaje_sismo/rebuild_event_store) SIN GTK y sin globales:
 *
 *   - agrupacion: `csnhypodbp_group_matches` (mismo qid **o** firma de picadas
 *     compatible, >= GroupMinMatchPicks dentro de GroupPickTolSec);
 *   - deduplicacion de soluciones por (qid, mod, version);
 *   - solucion preferida: `csnhypodbp_preferred_index` (confirmada > manual >
 *     mas fases y menor RMS);
 *   - magnitudes por (qid, mod) con `csnhypodbp_mag_store_*`, aplicadas tambien
 *     si el TYPE_MAGNITUDE llega ANTES que el ARC;
 *   - celdas con los MISMOS formatos que la tabla GTK (ws.c:509-526).
 *
 * Memoria: el store es acotado. Un CsnhypodbpSolution pesa ~10 KB (lleva 256
 * picadas), asi que las soluciones de cada grupo se reservan por demanda y se
 * topan en `min(GroupMaxSolutions, BPQ_SOL_CAP_MAX)`.
 */
#include "bpq_capi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BPQ_MAX_GROUPS 128
#define BPQ_SOL_CAP_MAX 16

typedef struct {
    CsnhypodbpSolution *sols;
    CsnhypodbpSolMeta  *meta;
    char (*mag)[CSNHYPODBP_MAG_SLOTS][32];   /* [sol][slot][32] */
    int nsols, cap;
    const BpqEvents *owner;   /* back-pointer: etiquetas de modulo en las celdas */
} BpqGroup;

struct BpqEvents {
    BpqConfig           cfg;
    CsnhypodbpGroupKey *keys;      /* [BPQ_MAX_GROUPS]: clave/union por grupo */
    BpqGroup           *groups;    /* [BPQ_MAX_GROUPS] */
    int                 ngroups;
    CsnhypodbpMagStore  pending;   /* magnitudes que llegan antes del ARC */
    int                 sol_cap_max;
};

/* --- utilidades ----------------------------------------------------------- */

static const char *mod_label(const BpqEvents *e, int mod)
{
    static char buf[16];
    int i;
    for (i = 0; i < e->cfg.n_modlabels; i++)
        if (e->cfg.modlabels[i].id == mod) return e->cfg.modlabels[i].label;
    snprintf(buf, sizeof(buf), "%d", mod);
    return buf;
}

static int mag_slot_of(const char *type)
{
    if (!type) return -1;
    if (!strcmp(type, "ML") || !strcmp(type, "Ml"))   return 0;
    if (!strcmp(type, "Mwp") || !strcmp(type, "MWP")) return 1;
    if (!strcmp(type, "Mb") || !strcmp(type, "MB"))   return 2;
    if (!strcmp(type, "Ms") || !strcmp(type, "MS"))   return 3;
    return -1;
}

static void mag_text(const BpqMagMsg *m, char *out, size_t n)
{
    /* Mismo texto que ws.c:1131: "%.1f-%d". */
    snprintf(out, n, "%.1f-%d", m->value, m->nsta);
}

/* Copia la magnitud al slot indicado de la solucion `j` si el (qid,mod) casa. */
static int group_apply_mag(BpqGroup *g, int qid, int mod, int slot, const char *txt)
{
    int j, applied = 0;
    for (j = 0; j < g->nsols; j++) {
        if (!csnhypodbp_mag_match(qid, mod, g->sols[j].qid, g->sols[j].mod)) continue;
        if (slot < 0 || slot >= CSNHYPODBP_MAG_SLOTS) continue;
        snprintf(g->mag[j][slot], sizeof(g->mag[j][slot]), "%s", txt);
        applied = 1;
    }
    return applied;
}

static int group_ensure_cap(BpqGroup *g, int want)
{
    int   newcap;
    void *p;

    if (want <= g->cap) return 0;
    newcap = g->cap ? g->cap * 2 : 2;
    if (newcap < want) newcap = want;

    p = realloc(g->sols, (size_t)newcap * sizeof(*g->sols));
    if (!p) return -1;
    g->sols = (CsnhypodbpSolution *)p;

    p = realloc(g->meta, (size_t)newcap * sizeof(*g->meta));
    if (!p) return -1;
    g->meta = (CsnhypodbpSolMeta *)p;

    p = realloc(g->mag, (size_t)newcap * sizeof(*g->mag));
    if (!p) return -1;
    g->mag = (char (*)[CSNHYPODBP_MAG_SLOTS][32])p;

    g->cap = newcap;
    return 0;
}

/* --- ciclo de vida -------------------------------------------------------- */

BpqEvents *bpq_events_new(const BpqConfig *cfg)
{
    BpqEvents *e;

    if (!cfg) return NULL;
    e = (BpqEvents *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->cfg = *cfg;
    e->keys = (CsnhypodbpGroupKey *)calloc(BPQ_MAX_GROUPS, sizeof(*e->keys));
    e->groups = (BpqGroup *)calloc(BPQ_MAX_GROUPS, sizeof(*e->groups));
    if (!e->keys || !e->groups) {
        free(e->keys);
        free(e->groups);
        free(e);
        return NULL;
    }
    csnhypodbp_mag_store_init(&e->pending);
    e->sol_cap_max = e->cfg.group_max_solutions > 0 ? e->cfg.group_max_solutions : 8;
    if (e->sol_cap_max > BPQ_SOL_CAP_MAX) e->sol_cap_max = BPQ_SOL_CAP_MAX;
    return e;
}

void bpq_events_free(BpqEvents *e)
{
    int i;
    if (!e) return;
    for (i = 0; i < e->ngroups; i++) {
        free(e->groups[i].sols);
        free(e->groups[i].meta);
        free(e->groups[i].mag);
    }
    free(e->groups);
    free(e->keys);
    free(e);
}

int bpq_events_count(const BpqEvents *e)
{
    return e ? e->ngroups : 0;
}

const BpqEvent *bpq_events_at(const BpqEvents *e, int i)
{
    if (!e || i < 0 || i >= e->ngroups) return NULL;
    return (const BpqEvent *)&e->groups[i];
}

/* --- ingesta -------------------------------------------------------------- */

void bpq_events_clear(BpqEvents *e)
{
    int i;

    if (!e) return;
    for (i = 0; i < e->ngroups; i++) {
        free(e->groups[i].sols);
        free(e->groups[i].meta);
        free(e->groups[i].mag);
        memset(&e->groups[i], 0, sizeof(e->groups[i]));
        memset(&e->keys[i], 0, sizeof(e->keys[i]));
    }
    e->ngroups = 0;
}

int bpq_events_feed_arc(BpqEvents *e, const CsnhypodbpSolution *sol)
{
    int idx[BPQ_MAX_GROUPS];
    int nmatch, gi, j, found = -1;

    if (!e || !sol) return -1;

    /* 1) ¿A que grupo pertenece? Mismo qid o firma compatible. */
    nmatch = csnhypodbp_group_matches(e->keys, e->ngroups, sol->qid,
                                      sol->picks, sol->npicks,
                                      e->cfg.group_pick_tol_s,
                                      e->cfg.group_min_match_picks,
                                      idx, BPQ_MAX_GROUPS);
    if (nmatch > 0) gi = idx[0];
    else {
        if (e->ngroups >= BPQ_MAX_GROUPS) return -2;
        gi = e->ngroups++;
        memset(&e->keys[gi], 0, sizeof(e->keys[gi]));
        e->keys[gi].qid = sol->qid;
        memset(&e->groups[gi], 0, sizeof(e->groups[gi]));
        e->groups[gi].owner = e;
    }

    /* 2) Deduplicar por (qid, mod, version). */
    for (j = 0; j < e->groups[gi].nsols; j++) {
        const CsnhypodbpSolution *s = &e->groups[gi].sols[j];
        if (s->qid == sol->qid && s->mod == sol->mod && s->version == sol->version) {
            found = j;
            break;
        }
    }

    if (found < 0) {
        BpqGroup *g = &e->groups[gi];
        if (g->nsols >= e->sol_cap_max) return -2;   /* tope por grupo */
        if (group_ensure_cap(g, g->nsols + 1) != 0) return -2;
        found = g->nsols++;
        g->sols[found] = *sol;
        memset(&g->meta[found], 0, sizeof(g->meta[found]));
        memset(g->mag[found], 0, sizeof(g->mag[found]));
        /* "created" ordena el detalle por defecto (Created DESC). */
        g->meta[found].created = (double)time(NULL);
    } else {
        e->groups[gi].sols[found] = *sol;
    }

    /* 3) Metadatos de la preferida (mismos campos que usa el nucleo). */
    e->groups[gi].meta[found].manual    = sol->manual;
    e->groups[gi].meta[found].confirmed = sol->confirmed;
    e->groups[gi].meta[found].version   = sol->version;
    e->groups[gi].meta[found].nph       = sol->nph;
    e->groups[gi].meta[found].rms       = sol->rms;
    e->groups[gi].meta[found].otime     = sol->otime;

    /* 4) La clave del grupo acumula la union de picadas. */
    csnhypodbp_group_union(&e->keys[gi], sol->picks, sol->npicks,
                           e->cfg.group_pick_tol_s);

    /* 5) Magnitudes que habian llegado antes que este ARC. */
    {
        char out[CSNHYPODBP_MAG_SLOTS][32];
        if (csnhypodbp_mag_store_take(&e->pending, sol->qid, sol->mod, out)) {
            for (j = 0; j < CSNHYPODBP_MAG_SLOTS; j++) {
                if (!out[j][0]) continue;
                snprintf(e->groups[gi].mag[found][j],
                         sizeof(e->groups[gi].mag[found][j]), "%s", out[j]);
            }
        }
    }
    return 0;
}

int bpq_events_feed_mag(BpqEvents *e, const BpqMagMsg *m)
{
    int slot, gi, applied = 0;
    char txt[32];

    if (!e || !m) return -1;
    slot = mag_slot_of(m->type);
    if (slot < 0) return 0;      /* tipo que la GUI no muestra: se ignora */
    mag_text(m, txt, sizeof(txt));

    for (gi = 0; gi < e->ngroups; gi++) {
        if (group_apply_mag(&e->groups[gi], m->qid, m->mod, slot, txt))
            applied = 1;
    }
    if (!applied) {
        /* Aun no hay solucion para ese (qid,mod): queda pendiente. */
        csnhypodbp_mag_store_put(&e->pending, m->qid, m->mod, slot, txt);
    }
    return 0;
}

/* --- acceso a soluciones -------------------------------------------------- */

int bpq_event_nsols(const BpqEvent *ev)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    return g ? g->nsols : 0;
}

const CsnhypodbpSolution *bpq_event_sol(const BpqEvent *ev, int j)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    if (!g || j < 0 || j >= g->nsols) return NULL;
    return &g->sols[j];
}

const CsnhypodbpSolMeta *bpq_event_sol_meta(const BpqEvent *ev, int j)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    if (!g || j < 0 || j >= g->nsols) return NULL;
    return &g->meta[j];
}

int bpq_event_preferred(const BpqEvent *ev)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    if (!g || g->nsols <= 0) return -1;
    return csnhypodbp_preferred_index(g->meta, g->nsols);
}

/* --- celdas --------------------------------------------------------------- */

static const char *const kMasterHdr[15] = {
    "Date", "Lat.", "Lon.", "Dep", "Res", "Azm", "#Stn", "ID",
    "Ml", "Mwp", "Mb", "Ms", "Mod", "Ver", "Est."
};
static const char *const kOriginHdr[15] = {
    "Mod", "Ver", "Est.", "Created", "Date", "Lat.", "Lon.", "Dep", "Res",
    "#Stn", "ID", "Ml", "Mwp", "Mb", "Ms"
};

int bpq_event_ncells(void)  { return 15; }

const char *bpq_event_header(int col)
{
    return (col >= 0 && col < 15) ? kMasterHdr[col] : "";
}

int bpq_origin_ncells(void) { return 15; }

const char *bpq_origin_header(int col)
{
    return (col >= 0 && col < 15) ? kOriginHdr[col] : "";
}

/* Rellena las 15 celdas de la solucion `j` (indice de columna de la MAESTRA). */
static void fill_cells(const BpqGroup *g, int j, char cell[15][32])
{
    const CsnhypodbpSolution *s = &g->sols[j];
    char fecha[32];
    int i;
    static const char dash[] = "-";

    for (i = 0; i < 15; i++) snprintf(cell[i], 32, "%s", dash);

    csnhypodbp_search_fmt_utc((time_t)s->otime, fecha, sizeof(fecha));
    snprintf(cell[0], 32, "%s", fecha);
    snprintf(cell[1], 32, "%.2f%c", fabs(s->lat), s->lat < 0 ? 'S' : 'N');
    snprintf(cell[2], 32, "%.2f%c", fabs(s->lon), s->lon < 0 ? 'W' : 'E');
    snprintf(cell[3], 32, "%.0f", s->depth);
    snprintf(cell[4], 32, "%.1f", s->rms);
    snprintf(cell[5], 32, "%.0f", (double)s->gap);
    snprintf(cell[6], 32, "%d", s->nph);
    snprintf(cell[7], 32, "%010d", s->qid);
    for (i = 0; i < CSNHYPODBP_MAG_SLOTS; i++)
        if (g->mag[j][i][0]) snprintf(cell[8 + i], 32, "%s", g->mag[j][i]);
    snprintf(cell[12], 32, "%s", mod_label(g->owner, s->mod));
    snprintf(cell[13], 32, "%u", s->version);
    snprintf(cell[14], 32, "%s", s->confirmed ? "C" : (s->manual || g->meta[j].manual) ? "M" : "A");
}

void bpq_event_cell(const BpqEvent *ev, int col, char *out, size_t n)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    char cell[15][32];
    int p;

    if (!out || n == 0) return;
    out[0] = '\0';
    if (!g || col < 0 || col >= 15) return;
    p = bpq_event_preferred(ev);
    if (p < 0) return;
    fill_cells(g, p, cell);
    snprintf(out, n, "%s", cell[col]);
}

void bpq_event_sol_cell(const BpqEvent *ev, int j, int col, char *out, size_t n)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    char cell[15][32];
    if (!out || n == 0) return;
    out[0] = '\0';
    if (!g || j < 0 || j >= g->nsols || col < 0 || col >= 15) return;
    fill_cells(g, j, cell);
    snprintf(out, n, "%s", cell[col]);
}

void bpq_origin_cell(const BpqEvent *ev, int j, int col, char *out, size_t n)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    char c[15][32];
    char created[32];
    int mcol;

    if (!out || n == 0) return;
    out[0] = '\0';
    if (!g || j < 0 || j >= g->nsols) return;
    fill_cells(g, j, c);

    switch (col) {
    case 0:  snprintf(out, n, "%s", c[12]); break;               /* Mod */
    case 1:  snprintf(out, n, "%s", c[13]); break;               /* Ver */
    case 2:  snprintf(out, n, "%s", c[14]); break;               /* Est. */
    case 3:                                                      /* Created */
        csnhypodbp_search_fmt_utc((time_t)g->meta[j].created, created, sizeof(created));
        snprintf(out, n, "%s", created);
        break;
    case 4:  snprintf(out, n, "%s", c[0]); break;                /* Date */
    case 5:  snprintf(out, n, "%s", c[1]); break;                /* Lat. */
    case 6:  snprintf(out, n, "%s", c[2]); break;                /* Lon. */
    case 7:  snprintf(out, n, "%s", c[3]); break;                /* Dep */
    case 8:  snprintf(out, n, "%s", c[4]); break;                /* Res */
    case 9:  snprintf(out, n, "%s", c[6]); break;                /* #Stn */
    case 10: snprintf(out, n, "%s", c[7]); break;                /* ID */
    default:
        mcol = col - 11;                                         /* Ml..Ms */
        if (mcol >= 0 && mcol < CSNHYPODBP_MAG_SLOTS)
            snprintf(out, n, "%s", c[8 + mcol]);
        break;
    }
}

/* --- orden ---------------------------------------------------------------- */

int bpq_event_sort_kind(int col)
{
    if (col == 0 || col == 3 || col == 4) return 1;   /* fecha/num con formato */
    if (col == 12 || col == 14) return 0;             /* Mod, Est: texto */
    if (col == 13) return 1;                          /* Ver */
    if (col >= 1 && col <= 11) return 1;
    return 0;
}

double bpq_event_sort_num(const BpqEvent *ev, int col)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    const CsnhypodbpSolution *s;
    int p = bpq_event_preferred(ev);
    if (!g || p < 0) return NAN;
    s = &g->sols[p];
    switch (col) {
    case 0:  return s->otime;
    case 1:  return s->lat;
    case 2:  return s->lon;
    case 3:  return s->depth;
    case 4:  return s->rms;
    case 5:  return (double)s->gap;
    case 6:  return (double)s->nph;
    case 7:  return (double)s->qid;
    case 8: case 9: case 10: case 11: {
        const char *t = g->mag[p][col - 8];
        return t[0] ? atof(t) : NAN;      /* "3.4-5" -> 3.4 */
    }
    case 13: return (double)s->version;
    default: return NAN;
    }
}

double bpq_origin_sort_num(const BpqEvent *ev, int j, int col)
{
    const BpqGroup *g = (const BpqGroup *)ev;
    const CsnhypodbpSolution *s;
    if (!g || j < 0 || j >= g->nsols) return NAN;
    s = &g->sols[j];
    switch (col) {
    case 0:  return (double)s->mod;
    case 1:  return (double)s->version;
    case 3:  return g->meta[j].created;
    case 4:  return s->otime;
    case 5:  return s->lat;
    case 6:  return s->lon;
    case 7:  return s->depth;
    case 8:  return s->rms;
    case 9:  return (double)s->nph;
    case 10: return (double)s->qid;
    case 11: case 12: case 13: case 14: {
        const char *t = g->mag[j][col - 11];
        return t[0] ? atof(t) : NAN;
    }
    default: return NAN;
    }
}
