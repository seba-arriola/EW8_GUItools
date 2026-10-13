/*
 * csnhypodbp_magmarks.c - Ventanas por (estacion, magnitud) (ver
 * csnhypodbp_magmarks.h). Sin GTK ni EarthWorm: se testea headless.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csnhypodbp_magmarks.h"

static CsnhypodbpMagMark *find_mark(const CsnhypodbpMagMarks *m, const char *sta)
{
    int i;

    if (!m || !m->v || !sta || !sta[0]) return NULL;
    for (i = 0; i < m->n; i++)
        if (strcmp(m->v[i].sta, sta) == 0) return &m->v[i];
    return NULL;
}

static CsnhypodbpMagMark *find_or_add(CsnhypodbpMagMarks *m, const char *sta)
{
    CsnhypodbpMagMark *k;

    if (!m || !sta || !sta[0]) return NULL;
    k = find_mark(m, sta);
    if (k) return k;
    if (m->n >= CSNHYPODBP_MAGMARKS_MAX) return NULL;
    if (m->n == m->cap) {
        int cap = m->cap ? m->cap * 2 : 64;
        CsnhypodbpMagMark *nv;

        if (cap > CSNHYPODBP_MAGMARKS_MAX) cap = CSNHYPODBP_MAGMARKS_MAX;
        nv = realloc(m->v, (size_t)cap * sizeof(*nv));
        if (!nv) return NULL;
        m->v = nv;
        m->cap = cap;
    }
    k = &m->v[m->n++];
    memset(k, 0, sizeof(*k));
    snprintf(k->sta, sizeof(k->sta), "%s", sta);
    return k;
}

void csnhypodbp_magmarks_init(CsnhypodbpMagMarks *m)
{
    if (!m) return;
    m->v = NULL;
    m->n = m->cap = 0;
}

void csnhypodbp_magmarks_free(CsnhypodbpMagMarks *m)
{
    if (!m) return;
    free(m->v);
    csnhypodbp_magmarks_init(m);
}

void csnhypodbp_magmarks_clear(CsnhypodbpMagMarks *m)
{
    if (!m) return;
    m->n = 0;
}

void csnhypodbp_magmarks_window(const CsnhypodbpMagMarks *m, const char *sta, int kind,
                                double def_t0, double def_t1, double *t0, double *t1)
{
    const CsnhypodbpMagMark *k = NULL;

    if (kind >= 0 && kind < CSNHYPODBP_MAGMARKS_KIND)
        k = find_mark(m, sta);
    if (k && k->set[kind]) {
        if (t0) *t0 = k->t0[kind];
        if (t1) *t1 = k->t1[kind];
        return;
    }
    if (t0) *t0 = def_t0;
    if (t1) *t1 = def_t1;
}

int csnhypodbp_magmarks_set(CsnhypodbpMagMarks *m, const char *sta, int kind,
                            double t0, double t1)
{
    CsnhypodbpMagMark *k;

    if (kind < 0 || kind >= CSNHYPODBP_MAGMARKS_KIND) return -1;
    if (!isfinite(t0) || !isfinite(t1) || t1 <= t0) return -1;
    k = find_or_add(m, sta);
    if (!k) return -1;
    k->t0[kind] = t0;
    k->t1[kind] = t1;
    k->set[kind] = 1;
    return 0;
}

int csnhypodbp_magmarks_touched(const CsnhypodbpMagMarks *m, const char *sta, int kind)
{
    const CsnhypodbpMagMark *k;

    if (kind < 0 || kind >= CSNHYPODBP_MAGMARKS_KIND) return 0;
    k = find_mark(m, sta);
    return (k && k->set[kind]) ? 1 : 0;
}

int csnhypodbp_magmarks_used(const CsnhypodbpMagMarks *m, const char *sta, int kind,
                             int def_used)
{
    const CsnhypodbpMagMark *k;

    if (kind < 0 || kind >= CSNHYPODBP_MAGMARKS_KIND) return def_used ? 1 : 0;
    k = find_mark(m, sta);
    if (k && k->used_set[kind]) return k->used[kind] ? 1 : 0;
    return def_used ? 1 : 0;
}

int csnhypodbp_magmarks_set_used(CsnhypodbpMagMarks *m, const char *sta, int kind,
                                 int used)
{
    CsnhypodbpMagMark *k;

    if (kind < 0 || kind >= CSNHYPODBP_MAGMARKS_KIND) return -1;
    k = find_or_add(m, sta);
    if (!k) return -1;
    k->used[kind] = used ? 1 : 0;
    k->used_set[kind] = 1;
    return 0;
}

int csnhypodbp_magmarks_touched_count(const CsnhypodbpMagMarks *m)
{
    int i, j, n = 0;

    if (!m || !m->v) return 0;
    for (i = 0; i < m->n; i++)
        for (j = 0; j < CSNHYPODBP_MAGMARKS_KIND; j++)
            if (m->v[i].set[j]) n++;
    return n;
}

char csnhypodbp_magmarks_winsrc(const CsnhypodbpMagMarks *m, const char *sta,
                                int kind, char anchor)
{
    if (csnhypodbp_magmarks_touched(m, sta, kind)) return 'M';
    if (anchor == 'T' || anchor == 't') return 'T';
    return 'A';
}

int csnhypodbp_magmarks_allowed(const CsnhypodbpMagMarks *m, const char *sta, int kind,
                                char anchor, int auto_on, int manual_on, int theo_on)
{
    switch (csnhypodbp_magmarks_winsrc(m, sta, kind, anchor)) {
        case 'M': return manual_on ? 1 : 0;
        case 'T': return theo_on ? 1 : 0;
        default:  return auto_on ? 1 : 0;
    }
}

CsnhypodbpMagGrab csnhypodbp_magmarks_grab(double x, double x0, double x1, double tol_px)
{
    double a, b, t;

    if (!isfinite(x) || !isfinite(x0) || !isfinite(x1)) return CSNHYPODBP_MAGG_NONE;
    a = (x0 <= x1) ? x0 : x1;
    b = (x0 <= x1) ? x1 : x0;
    t = (tol_px > 0.0) ? tol_px : 0.0;
    if (x < a - t || x > b + t) return CSNHYPODBP_MAGG_NONE;
    if (fabs(x - a) <= t && fabs(x - b) <= t) return CSNHYPODBP_MAGG_BOTH;
    if (fabs(x - a) <= t) return CSNHYPODBP_MAGG_T0;
    if (fabs(x - b) <= t) return CSNHYPODBP_MAGG_T1;
    return CSNHYPODBP_MAGG_BOTH;
}

int csnhypodbp_magmarks_handle_hit(double y, double band_top)
{
    if (!isfinite(y) || !isfinite(band_top)) return 0;
    /* 1 px de tolerancia hacia arriba: el borde del canvas redondea. */
    if (y < band_top - 1.0) return 0;
    if (y > band_top + CSNHYPODBP_MAG_HANDLE_H) return 0;
    return 1;
}

int csnhypodbp_mag_ovr_write(const char *path, const CsnhypodbpMagOvrRow *rows, int n)
{
    FILE *fp;
    int i, written = 0;

    if (!path || !path[0] || !rows) return -1;
    fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "# ventanas por (estacion, magnitud) de csnhypodbp (GUI)\n");
    fprintf(fp, "# STA KIND T0_EPOCH T1_EPOCH   (KIND: 0=ML 1=Mwp 2=Mb 3=Ms)\n");
    for (i = 0; i < n; i++) {
        if (!rows[i].sta[0]) continue;
        if (rows[i].kind < 0 || rows[i].kind >= CSNHYPODBP_MAGMARKS_KIND) continue;
        if (!isfinite(rows[i].t0) || !isfinite(rows[i].t1)) continue;
        if (!(rows[i].t1 > rows[i].t0)) continue;
        fprintf(fp, "%s %d %.6f %.6f\n", rows[i].sta, rows[i].kind,
                rows[i].t0, rows[i].t1);
        written++;
    }
    fclose(fp);
    return written;
}
