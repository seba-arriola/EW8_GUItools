/*
 * csnhypodbp_magsta.c - Cache de magnitudes por estacion (ver
 * csnhypodbp_magsta.h). Sin GTK ni EarthWorm: se testea headless.
 */

#include <string.h>

#include "csnhypodbp_magsta.h"

void csnhypodbp_magsta_init(CsnhypodbpMagStaStore *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

/* Entrada que coincide con esa solucion y magnitud (o NULL). */
static CsnhypodbpMagStaSet *find_set(CsnhypodbpMagStaStore *s, int qid, int mod,
                                     int slot)
{
    int i;

    if (!s) return NULL;
    for (i = 0; i < CSNHYPODBP_MAGSTA_RING; i++)
        if (s->set[i].used && s->set[i].qid == qid && s->set[i].mod == mod &&
            s->set[i].slot == slot)
            return &s->set[i];
    return NULL;
}

void csnhypodbp_magsta_put(CsnhypodbpMagStaStore *s, int qid, int mod, int slot,
                           const CsnhypodbpMagIn *vals, int n)
{
    CsnhypodbpMagStaSet *e;

    if (!s || !vals || n <= 0) return;
    if (n > CSNHYPODBP_MAGSTA_MAX) n = CSNHYPODBP_MAGSTA_MAX;
    e = find_set(s, qid, mod, slot);
    if (!e) {
        e = &s->set[s->next];
        s->next = (s->next + 1) % CSNHYPODBP_MAGSTA_RING;
        e->used = 1;
        e->qid = qid;
        e->mod = mod;
        e->slot = slot;
    }
    memcpy(e->vals, vals, (size_t)n * sizeof(*vals));
    e->n = n;
}

int csnhypodbp_magsta_take(const CsnhypodbpMagStaStore *s, int qid, int mod,
                           int slot, CsnhypodbpMagIn *out, int max)
{
    const CsnhypodbpMagStaSet *e;
    int i, n;

    if (!s || !out || max <= 0) return 0;
    e = NULL;
    for (i = 0; i < CSNHYPODBP_MAGSTA_RING; i++)
        if (s->set[i].used && s->set[i].qid == qid && s->set[i].mod == mod &&
            s->set[i].slot == slot) {
            e = &s->set[i];
            break;
        }
    if (!e) return 0;
    n = e->n < max ? e->n : max;
    memcpy(out, e->vals, (size_t)n * sizeof(*out));
    return n;
}
