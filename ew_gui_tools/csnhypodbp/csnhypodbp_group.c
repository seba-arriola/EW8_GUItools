/******************************************************************************
 * csnhypodbp_group.c                                                         *
 *                                                                            *
 * Nucleo PURO (sin GTK) del agrupamiento de soluciones hipocentrales.         *
 * Ver csnhypodbp_group.h para el criterio.                                    *
 ******************************************************************************/
#include "csnhypodbp_group.h"

#include <math.h>
#include <stdio.h>
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

/* Campo de 4 chars del ARC Y2K (residual por fase, entero escalado x100).
 * Devuelve 1 y el valor en segundos si el campo tiene algun digito; 0 si esta
 * en blanco (el ARC de csnloc no escribe residual). */
static int arc_field4(const char *p, size_t l, int off, double *out)
{
    char b[5];
    int i, seen = 0;

    if ((size_t)off + 4 > l) return 0;
    for (i = 0; i < 4; i++) {
        b[i] = p[off + i];
        if (b[i] != ' ') seen = 1;
    }
    b[4] = 0;
    if (!seen) return 0;
    *out = (double)atoi(b) / 100.0;
    return 1;
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
        memcpy(b, msg + EWGUI_ARC_QID_OFF, EWGUI_ARC_QID_LEN);
        b[EWGUI_ARC_QID_LEN] = 0; qid = atoi(b);
        qver = 0;   /* si los 4 bytes de version no caben, decide el byte heredado */
        /* La version va en 178..181: con `len` entre 162 y 181 el memcpy leia
           FUERA del mensaje (inofensivo dentro del buffer de anillo de 64 KB, pero
           mal). Si no caben esos 4 bytes, se cae al byte heredado de 161. */
        if (len >= EWGUI_ARC_QVER_OFF + EWGUI_ARC_QVER_LEN) {
            memcpy(b, msg + EWGUI_ARC_QVER_OFF, EWGUI_ARC_QVER_LEN);
            b[EWGUI_ARC_QVER_LEN] = 0;
            qver = atoi(b);
        }
        if (qver == 0) { b[0] = msg[EWGUI_ARC_QID_LEGACY_OFF]; b[1] = 0; qver = atoi(b); }
    }
    out->qid = qid;
    out->version = (unsigned)(qver < 0 ? 0 : qver);

    out->manual = (len > EWGUI_ARC_MANUAL_OFF &&
                   msg[EWGUI_ARC_MANUAL_OFF] == EWGUI_ARC_MANUAL_CHAR) ? 1 : 0;
    /* BMTYPI (columna 123): tipo de la magnitud preferida (L/W/B/S); 0 si viene
       en blanco (ARC de un csnloc viejo, o sin eleccion). */
    out->pref_mag = (len > EWGUI_ARC_BMTYPI_OFF) ? msg[EWGUI_ARC_BMTYPI_OFF] : 0;
    if (out->pref_mag == ' ') out->pref_mag = 0;

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
                        for (i = 0; i < 2 && p[5 + i] != ' '; i++) pk->net[i] = p[5 + i];
                        pk->net[i] = 0;
                        for (i = 0; i < 2 && p[111 + i] != ' '; i++) pk->loc[i] = p[111 + i];
                        pk->loc[i] = 0;
                        if (strcmp(pk->loc, "--") == 0) pk->loc[0] = 0;
                        {
                            /* Peso: LPWT (P, offset 16) o LSWT (S, offset 49).
                               Codigos 0..4 (0 = mejor); 1.0 - code/4. */
                            char wc = (fam == 'S') ? p[49] : p[16];
                            pk->weight = (wc >= '0' && wc <= '4')
                                             ? 1.0 - (double)(wc - '0') / 4.0
                                             : 1.0;
                        }
                        pk->phase = fam;
                        pk->t_pick = t;
                        pk->residual = NAN;
                        {
                            /* Residual Y2K: P en [34,37], S en [50,53]. El ARC
                               de csnloc lo deja en blanco -> NAN. */
                            double rv;
                            if (arc_field4(p, l, (fam == 'S') ? 50 : 34, &rv))
                                pk->residual = rv;
                        }
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
    int i, best = -1, any_manual = 0, any_confirmed = 0;

    if (!sols || n <= 0) return -1;
    for (i = 0; i < n; i++) {
        if (sols[i].confirmed) any_confirmed = 1;
        if (sols[i].manual) any_manual = 1;
    }

    /* 1) Confirmadas a mano: manda la confirmacion MAS RECIENTE (`created`),
       porque la version es el contador de csnloc y no el orden del clic.
       Desempate: mayor version y luego mayor hora origen. */
    if (any_confirmed) {
        for (i = 0; i < n; i++) {
            if (!sols[i].confirmed) continue;
            if (best < 0 ||
                sols[i].created > sols[best].created + 1e-9 ||
                (fabs(sols[i].created - sols[best].created) <= 1e-9 &&
                 (sols[i].version > sols[best].version ||
                  (sols[i].version == sols[best].version &&
                   sols[i].otime > sols[best].otime))))
                best = i;
        }
        return best;
    }

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

/* ------------------------------------------------------------------------- */
/* ARC Y2K desde una solucion.                                               */
/*                                                                           */
/* El layout NO es libre: HYPOINVERSE lee la linea 2 como resumen SIEMPRE    */
/* (hyphs.for:222, `READ (14,1001,END=80) STR2`) y hyfile.for decide el      */
/* formato por ella (`$1` -> ITYPE=10/JCP=5). Sin la linea `$1` el motor     */
/* toma la primera fase por resumen, desalinea el conteo y muere con         */
/* "Sequential READ ... after EOF" (WEXITSTATUS=2). Por eso el ARC se emite  */
/* exactamente como FormatHYP2000ARC (csnloc/hypo_out.c):                    */
/*   L1 197 | L2 197 `$1` | (fase 114 + continuacion 114) x N | terminador.  */
/*                                                                           */
/* La hora de fase va como SS.cc (F5.2 del motor), no como SSmmm: `22000`    */
/* se lee 220.00 s y arruina el ajuste. La S lleva peso 4 y su tiempo tambien */
/* en las columnas canonicas 41-49; con peso 0 HYPOINVERSE la mete por la    */
/* rama P y nunca por la S (hyloc.for:204/211).                              */
/* ------------------------------------------------------------------------- */
static void put_at(char *dst, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (l >= n) memcpy(dst, s, n);
    else { memcpy(dst, s, l); memset(dst + l, ' ', n - l); }
}

int csnhypodbp_arc_build(const CsnhypodbpSolution *sol, int manual, char *out, size_t outn)
{
    char hdr[200], pl[130], t[64];
    size_t n = 0;
    int i;
    time_t sec;
    double frac;
    struct tm *tm;
    if (!sol || !out || outn < 512) return -1;
    memset(hdr, ' ', sizeof(hdr));

    sec = (time_t)sol->otime;
    frac = sol->otime - (double)sec;
    tm = gmtime(&sec);
    if (!tm) return -1;
    snprintf(t, sizeof(t), "%04d%02d%02d%02d%02d%02d%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, (int)tm->tm_sec, (int)(frac * 100.0 + 0.5));
    memcpy(hdr, t, 16);
    {
        double a = fabs(sol->lat); int d = (int)a;
        snprintf(t, sizeof(t), "%02d%c%04d", d, sol->lat < 0 ? 'S' : 'N',
                 (int)((a - d) * 60.0 * 100.0 + 0.5));
        memcpy(hdr + 16, t, 7);
    }
    {
        double a = fabs(sol->lon); int d = (int)a;
        snprintf(t, sizeof(t), "%03d%c%04d", d, sol->lon < 0 ? 'W' : 'E',
                 (int)((a - d) * 60.0 * 100.0 + 0.5));
        memcpy(hdr + 23, t, 8);
    }
    snprintf(t, sizeof(t), "%05d", (int)(sol->depth * 100.0 + 0.5)); memcpy(hdr + 31, t, 5);
    snprintf(t, sizeof(t), "%03d", sol->nph); memcpy(hdr + 39, t, 3);
    snprintf(t, sizeof(t), "%03d", sol->gap); memcpy(hdr + 42, t, 3);
    snprintf(t, sizeof(t), "%04d", (int)(sol->rms * 100.0 + 0.5)); memcpy(hdr + 48, t, 4);
    snprintf(t, sizeof(t), "%010d", sol->qid); memcpy(hdr + 136, t, 10);
    hdr[161] = (char)('0' + (int)(sol->version % 10));
    hdr[EWGUI_ARC_MANUAL_OFF] = manual ? EWGUI_ARC_MANUAL_CHAR : EWGUI_ARC_AUTO_CHAR;
    if (sol->pref_mag) hdr[EWGUI_ARC_BMTYPI_OFF] = sol->pref_mag;
    snprintf(t, sizeof(t), "%04u", sol->version); memcpy(hdr + 178, t, 4);

    memcpy(out + n, hdr, 197); n += 197; out[n++] = '\n';

    /* Linea 2 `$1`: obligatoria (ver cabecera). Marca el ARC como archive-2000
       con shadow y es la que el motor lee como resumen del evento. */
    if (n + 198 >= outn) return -1;
    {
        char l2[200];
        memset(l2, ' ', sizeof(l2));
        l2[0] = '$'; l2[1] = '1';
        memcpy(out + n, l2, 197); n += 197; out[n++] = '\n';
    }

    for (i = 0; i < sol->npicks; i++) {
        const CsnhypodbpPick *pk = &sol->picks[i];
        /* Centesimas ANTES de descomponer: redondear al escribir daria "60.00"
           para 59.995..59.999 (segundo invalido). Redondeando el epoch, gmtime
           hace el acarreo de segundo/minuto/hora. */
        double tt = floor(pk->t_pick * 100.0 + 0.5) / 100.0;
        time_t ps = (time_t)tt;
        double frac = tt - (double)ps;
        struct tm *ptm = gmtime(&ps);
        double psec;
        char phase = pk->phase;
        int  is_s = (phase == 'S');
        const char *loc = pk->loc[0] ? pk->loc : "--";
        const char *net = pk->net[0] ? pk->net : "--";

        if (!ptm) continue;
        if (phase != 'P' && phase != 'S') continue;
        /* fase + continuacion + terminador tienen que caber. */
        if (n + 2 * 115 + 115 >= outn) return -1;

        psec = (double)ptm->tm_sec + frac;
        memset(pl, ' ', sizeof(pl));
        put_at(pl + 0, pk->sta, 5);
        put_at(pl + 5, net, 2);
        put_at(pl + 9, pk->chan, 3);
        pl[14] = phase;
        pl[15] = ' ';
        pl[16] = is_s ? '4' : '0';   /* LSWT/LPWT; S=4 o el motor la trata como P */
        snprintf(t, sizeof(t), "%04d%02d%02d%02d%02d%05.2f",
                 ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
                 ptm->tm_hour, ptm->tm_min, psec);
        memcpy(pl + 17, t, 17);
        if (is_s) {
            /* Tiempo S en sus columnas canonicas (41-45) + remark KSRK (46) y
               LSWT (49); sin esto hyphs.for:552 anula el remark y la S no
               entra al calculo. */
            char secbuf[8];
            snprintf(secbuf, sizeof(secbuf), "%05.2f", psec);
            memcpy(pl + 41, secbuf, 5);
            pl[46] = 'S';
            pl[47] = ' ';
            pl[49] = '0';
        }
        if (!isnan(pk->residual)) {
            /* Residual Y2K: P en [34,37], S en [50,53] (entero x100). */
            int off = is_s ? 50 : 34;
            snprintf(t, sizeof(t), "%4ld", lround(pk->residual * 100.0));
            put_at(pl + off, t, 4);
        }
        put_at(pl + 111, loc, 2);
        memcpy(out + n, pl, 114); n += 114; out[n++] = '\n';

        /* Continuacion de la fase (`$1` en 104): una por fase. El motor la lee
           como shadow cuando JCP=5. */
        memset(pl, ' ', sizeof(pl));
        put_at(pl + 0, pk->sta, 5);
        put_at(pl + 5, net, 2);
        put_at(pl + 9, pk->chan, 3);
        pl[104] = '$'; pl[105] = '1';
        put_at(pl + 111, loc, 2);
        memcpy(out + n, pl, 114); n += 114; out[n++] = '\n';
    }

    /* ---- linea final vacia (terminador de fases) ---- */
    if (n + 115 >= outn) return -1;
    memset(pl, ' ', sizeof(pl));
    memcpy(out + n, pl, 114); n += 114; out[n++] = '\n';
    out[n] = '\0';
    return 0;
}

