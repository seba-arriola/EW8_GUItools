/******************************************************************************
 * csnhypodbp_group.c                                                         *
 *                                                                            *
 * Nucleo PURO (sin GTK) del agrupamiento de soluciones hipocentrales.         *
 * Ver csnhypodbp_group.h para el criterio.                                    *
 ******************************************************************************/
#include "csnhypodbp_group.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ewgui/arc_layout.h>

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */
static char family(char phase)
{
    if (phase == 'P' || phase == 'p') return 'P';
    if (phase == 'S' || phase == 's') return 'S';
    return 0;
}

static int digits_at(const char *s, int n)
{
    int i, any = 0;
    for (i = 0; i < n; i++) {
        if (s[i] >= '0' && s[i] <= '9') any = 1;
        else if (s[i] != '.' && s[i] != ' ') return 0;
    }
    return any;
}

/* Epoch UTC desde YYYYMMDDHHMMSS + fraccion. Devuelve 0 si invalido.
   Acepta la fraccion como decimal ("SS.ss") o como centesimas ("SSCC"). */
static double ymdhms_to_epoch(const char *s)
{
    struct tm t;
    int y, mo, d, h, mi;
    double sec;
    char b[8];

    b[0] = s[0]; b[1] = s[1]; b[2] = s[2]; b[3] = s[3]; b[4] = 0;
    y = atoi(b);
    if (y < 1900) return 0.0;
    b[0] = s[4];  b[1] = s[5];  b[2] = 0;  mo = atoi(b);
    b[0] = s[6];  b[1] = s[7];  b[2] = 0;  d  = atoi(b);
    b[0] = s[8];  b[1] = s[9];  b[2] = 0;  h  = atoi(b);
    b[0] = s[10]; b[1] = s[11]; b[2] = 0;  mi = atoi(b);
    b[0] = s[12]; b[1] = s[13]; b[2] = 0;  sec = atof(b);
    if (s[14] == '.') {
        b[0] = s[15]; b[1] = s[16]; b[2] = 0; sec += atof(b) / 100.0;
    } else if (s[14] >= '0' && s[14] <= '9') {
        b[0] = s[14]; b[1] = s[15]; b[2] = 0; sec += atof(b) / 100.0;
    }

    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h;        t.tm_min = mi;      t.tm_sec = (int)sec;
    {
        time_t e = timegm(&t);
        return (double)e + (sec - (double)(int)sec);
    }
}

/* ------------------------------------------------------------------------- */
/* Parseo del mensaje ARC                                                     */
/* ------------------------------------------------------------------------- */
int csnhypodbp_parse_solution(const char *msg, size_t len, int mod,
                              CsnhypodbpSolution *out)
{
    int    qid, qver;
    double lat, lon, depth;
    const char *p;

    if (!msg || !out || len < 162) return -1;
    memset(out, 0, sizeof(*out));
    out->mod = mod;

    out->otime = ymdhms_to_epoch(msg);
    if (out->otime <= 0.0) return -1;

    {
        char b[8];
        b[0] = msg[16]; b[1] = msg[17]; b[2] = 0;
        lat = atof(b);
        b[0] = msg[19]; b[1] = msg[20]; b[2] = msg[21]; b[3] = msg[22]; b[4] = 0;
        lat += (atof(b) / 100.0) / 60.0;
        out->lat = (msg[18] == 'S') ? -lat : lat;

        b[0] = msg[23]; b[1] = msg[24]; b[2] = msg[25]; b[3] = 0;
        lon = atof(b);
        b[0] = msg[27]; b[1] = msg[28]; b[2] = msg[29]; b[3] = msg[30]; b[4] = 0;
        lon += (atof(b) / 100.0) / 60.0;
        out->lon = (msg[26] == 'W') ? -lon : lon;

        b[0] = msg[31]; b[1] = msg[32]; b[2] = msg[33]; b[3] = msg[34]; b[4] = msg[35]; b[5] = 0;
        depth = atof(b) / 100.0;
        out->depth = depth;

        b[0] = msg[39]; b[1] = msg[40]; b[2] = msg[41]; b[3] = 0;
        out->nph = atoi(b);
        b[0] = msg[42]; b[1] = msg[43]; b[2] = msg[44]; b[3] = 0;
        out->gap = atoi(b);
        b[0] = msg[48]; b[1] = msg[49]; b[2] = msg[50]; b[3] = msg[51]; b[4] = 0;
        out->rms = atof(b) / 100.0;
    }

    {
        char b[11];
        memcpy(b, msg + 136, 10); b[10] = 0; qid = atoi(b);
        memcpy(b, msg + 178, 4);  b[4] = 0;  qver = atoi(b);
        if (qver == 0) { b[0] = msg[161]; b[1] = 0; qver = atoi(b); }
    }
    out->qid = qid;
    out->version = (unsigned)(qver < 0 ? 0 : qver);

    out->manual = (len > EWGUI_ARC_MANUAL_OFF &&
                   msg[EWGUI_ARC_MANUAL_OFF] == EWGUI_ARC_MANUAL_CHAR) ? 1 : 0;

    /* Fases: lineas de >=114 chars que no empiezan por '$'. */
    p = memchr(msg, '\n', len);
    while (p) {
        const char *nl;
        size_t l;
        p++;
        nl = memchr(p, '\n', (size_t)((msg + len) - p));
        l = nl ? (size_t)(nl - p) : (size_t)((msg + len) - p);
        if (l >= 114 && p[0] != '$') {
            char fam = family(p[14]);
            if (fam) {
                const char *ts = p + 17;
                if (digits_at(ts, 17)) {
                    double t = ymdhms_to_epoch(ts);
                    if (t > 0.0 && out->npicks < CSNHYPODBP_MAX_PICKS) {
                        CsnhypodbpPick *pk = &out->picks[out->npicks];
                        int i;
                        memset(pk, 0, sizeof(*pk));
                        for (i = 0; i < 5 && p[i] != ' '; i++) pk->sta[i] = p[i];
                        pk->sta[i] = 0;
                        for (i = 0; i < 3 && p[9 + i] != ' '; i++) pk->chan[i] = p[9 + i];
                        pk->chan[i] = 0;
                        pk->phase = fam;
                        pk->t_pick = t;
                        out->npicks++;
                    }
                }
            }
        }
        if (!nl) break;
        p = nl;
    }

    return 0;
}

/* ------------------------------------------------------------------------- */
/* Firma / comparacion                                                        */
/* ------------------------------------------------------------------------- */
int csnhypodbp_pick_match(const CsnhypodbpPick *a, const CsnhypodbpPick *b,
                          double tol_sec)
{
    if (!a || !b) return 0;
    if (strcmp(a->sta, b->sta) != 0) return 0;
    if (family(a->phase) == 0 || family(a->phase) != family(b->phase)) return 0;
    return fabs(a->t_pick - b->t_pick) <= tol_sec;
}

int csnhypodbp_picks_subset(const CsnhypodbpPick *a, int na,
                            const CsnhypodbpPick *b, int nb,
                            double tol_sec, int min_match)
{
    char used[CSNHYPODBP_MAX_PICKS];
    int  i, j;

    if (!a || !b || na <= 0 || nb <= 0 || na > nb) return 0;
    if (min_match > 0 && na < min_match) return 0;
    if (nb > CSNHYPODBP_MAX_PICKS) nb = CSNHYPODBP_MAX_PICKS;
    memset(used, 0, sizeof(used));

    for (i = 0; i < na; i++) {
        int hit = 0;
        for (j = 0; j < nb; j++) {
            if (used[j]) continue;
            if (csnhypodbp_pick_match(&a[i], &b[j], tol_sec)) {
                used[j] = 1; hit = 1; break;
            }
        }
        if (!hit) return 0;
    }
    return 1;
}

int csnhypodbp_picks_overlap(const CsnhypodbpPick *a, int na,
                             const CsnhypodbpPick *b, int nb,
                             double tol_sec)
{
    char used[CSNHYPODBP_MAX_PICKS];
    int  i, j, hits = 0;

    if (!a || !b || na <= 0 || nb <= 0) return 0;
    if (na > CSNHYPODBP_MAX_PICKS) na = CSNHYPODBP_MAX_PICKS;
    if (nb > CSNHYPODBP_MAX_PICKS) nb = CSNHYPODBP_MAX_PICKS;
    memset(used, 0, sizeof(used));

    for (i = 0; i < na; i++) {
        for (j = 0; j < nb; j++) {
            if (used[j]) continue;
            if (csnhypodbp_pick_match(&a[i], &b[j], tol_sec)) {
                used[j] = 1;
                hits++;
                break;
            }
        }
    }
    return hits;
}

int csnhypodbp_picks_compatible(const CsnhypodbpPick *a, int na,
                                const CsnhypodbpPick *b, int nb,
                                double tol_sec, int min_match)
{
    if (min_match < 1) min_match = 1;
    return csnhypodbp_picks_overlap(a, na, b, nb, tol_sec) >= min_match;
}

static uint64_t fnv1a(const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = 1469598103934665603ULL;
    size_t   i;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

uint64_t csnhypodbp_picks_signature(const CsnhypodbpPick *p, int n)
{
    uint64_t acc = 0;
    int      i;
    for (i = 0; i < n; i++) {
        struct { char sta[8]; char fam; long long cs; } k;
        memset(&k, 0, sizeof(k));
        strncpy(k.sta, p[i].sta, sizeof(k.sta) - 1);
        k.fam = family(p[i].phase);
        k.cs  = (long long)llround(p[i].t_pick * 100.0);
        acc ^= fnv1a(&k, sizeof(k));
    }
    return acc;
}

/* ------------------------------------------------------------------------- */
/* Grupos                                                                     */
/* ------------------------------------------------------------------------- */
int csnhypodbp_group_matches(const CsnhypodbpGroupKey *groups, int ngroups,
                             int qid, const CsnhypodbpPick *picks, int npicks,
                             double tol_sec, int min_match,
                             int *out_idx, int out_cap)
{
    int i, k = 0;

    if (!groups || !out_idx || out_cap <= 0) return 0;
    for (i = 0; i < ngroups && k < out_cap; i++) {
        int match = 0;
        if (groups[i].qid == qid) {
            match = 1;
        } else if (npicks > 0 && groups[i].npicks > 0) {
            int mn = (npicks < groups[i].npicks) ? npicks : groups[i].npicks;
            if (mn >= min_match)
                match = csnhypodbp_picks_compatible(picks, npicks,
                                                    groups[i].picks,
                                                    groups[i].npicks,
                                                    tol_sec, min_match);
        }
        if (match) out_idx[k++] = i;
    }
    return k;
}

void csnhypodbp_group_union(CsnhypodbpGroupKey *g, const CsnhypodbpPick *picks,
                            int npicks, double tol_sec)
{
    int i, j;

    if (!g || !picks) return;
    for (i = 0; i < npicks && g->npicks < CSNHYPODBP_MAX_PICKS; i++) {
        int dup = 0;
        for (j = 0; j < g->npicks; j++) {
            if (csnhypodbp_pick_match(&picks[i], &g->picks[j], tol_sec)) {
                dup = 1; break;
            }
        }
        if (!dup) g->picks[g->npicks++] = picks[i];
    }
}

/* ------------------------------------------------------------------------- */
/* Almacen de magnitudes                                                      */
/* ------------------------------------------------------------------------- */
void csnhypodbp_mag_store_init(CsnhypodbpMagStore *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

static CsnhypodbpMagSlot *mag_slot_get(CsnhypodbpMagStore *s, int qid, int mod,
                                       int create)
{
    int i, free_i = -1;
    if (!s) return NULL;
    for (i = 0; i < CSNHYPODBP_MAG_STORE_CAP; i++) {
        if (s->slots[i].used && s->slots[i].qid == qid && s->slots[i].mod == mod)
            return &s->slots[i];
        if (!s->slots[i].used && free_i < 0) free_i = i;
    }
    if (!create || free_i < 0) return NULL;
    s->slots[free_i].used = 1;
    s->slots[free_i].qid = qid;
    s->slots[free_i].mod = mod;
    for (i = 0; i < CSNHYPODBP_MAG_SLOTS; i++) s->slots[free_i].mag[i][0] = '\0';
    return &s->slots[free_i];
}

void csnhypodbp_mag_store_put(CsnhypodbpMagStore *s, int qid, int mod, int slot,
                              const char *mag)
{
    CsnhypodbpMagSlot *e;
    if (!s || slot < 0 || slot >= CSNHYPODBP_MAG_SLOTS || !mag) return;
    e = mag_slot_get(s, qid, mod, 1);
    if (!e) return;
    strncpy(e->mag[slot], mag, 31);
    e->mag[slot][31] = '\0';
}

int csnhypodbp_mag_store_take(CsnhypodbpMagStore *s, int qid, int mod,
                              char out[CSNHYPODBP_MAG_SLOTS][32])
{
    CsnhypodbpMagSlot *e = mag_slot_get(s, qid, mod, 0);
    int i;
    if (!e || !out) return 0;
    for (i = 0; i < CSNHYPODBP_MAG_SLOTS; i++) {
        strncpy(out[i], e->mag[i], 31);
        out[i][31] = '\0';
    }
    e->used = 0;
    e->qid = 0;
    e->mod = 0;
    return 1;
}

int csnhypodbp_mag_match(int qid_msg, int mod_msg, int qid_sol, int mod_sol)
{
    if (qid_msg != qid_sol) return 0;
    /* Sin modulo (magnitud de csnmags viejo, o solucion recuperada del
       StateFile que no guarda mod): coincide con cualquier mod del qid. */
    if (mod_msg == 0 || mod_sol == 0) return 1;
    return mod_msg == mod_sol;
}

/* ------------------------------------------------------------------------- */
/* Preferida                                                                  */
/* ------------------------------------------------------------------------- */
int csnhypodbp_preferred_index(const CsnhypodbpSolMeta *sols, int n)
{
    int i, best = -1, any_manual = 0;

    if (!sols || n <= 0) return -1;
    for (i = 0; i < n; i++) if (sols[i].manual) { any_manual = 1; break; }

    if (any_manual) {
        for (i = 0; i < n; i++) {
            if (!sols[i].manual) continue;
            if (best < 0 ||
                sols[i].version > sols[best].version ||
                (sols[i].version == sols[best].version &&
                 sols[i].otime > sols[best].otime))
                best = i;
        }
        return best;
    }

    for (i = 0; i < n; i++) {
        if (best < 0) { best = i; continue; }
        if (sols[i].nph > sols[best].nph) { best = i; continue; }
        if (sols[i].nph < sols[best].nph) continue;
        if (sols[i].rms < sols[best].rms - 1e-9) { best = i; continue; }
        if (sols[i].rms > sols[best].rms + 1e-9) continue;
        if (sols[i].version > sols[best].version) { best = i; continue; }
        if (sols[i].version == sols[best].version &&
            sols[i].otime > sols[best].otime)
            best = i;
    }
    return best;
}
