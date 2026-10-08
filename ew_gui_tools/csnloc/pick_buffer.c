/******************************************************************************
 * pick_buffer.c                                                              *
 *                                                                            *
 * Buffer dinamico de picks con semantica de re-pick: un TYPE_PICK_SCNL del   *
 * mismo SCNL dentro de repick_window_sec reemplaza al anterior (correccion   *
 * manual). Los picks mas viejos que ttl_sec se descartan.                    *
 ******************************************************************************/
#include "csnloc.h"

static int same_scnl(const Pick *a, const Pick *b)
{
    return strcmp(a->sta, b->sta) == 0 &&
           strcmp(a->net, b->net) == 0 &&
           strcmp(a->chan, b->chan) == 0 &&
           strcmp(a->loc, b->loc) == 0 &&
           a->phase == b->phase;   /* P y S del mismo SCNL no se pisan */
}

void PickBuffer_Init(PickBuffer *b, int cap, double repick_window_sec)
{
    if (!b) return;
    if (cap <= 0) cap = CSLOC_MAX_PICKS;
    b->items = (Pick *)calloc((size_t)cap, sizeof(Pick));
    b->n = 0;
    b->cap = b->items ? cap : 0;
    b->repick_window_sec = repick_window_sec;
    b->n_inserted = 0;
    b->n_replaced = 0;
}

void PickBuffer_Free(PickBuffer *b)
{
    if (!b) return;
    free(b->items);
    b->items = NULL;
    b->n = b->cap = 0;
}

/* Devuelve 1 si inserto, 2 si reemplazo, 0 si buffer lleno. */
int PickBuffer_AddOrReplace(PickBuffer *b, const Pick *p)
{
    int i;

    if (!b || !b->items || !p) return 0;

    /* Re-pick del mismo SCNL dentro de la ventana -> reemplaza. */
    for (i = b->n - 1; i >= 0; i--) {
        if (same_scnl(&b->items[i], p)) {
            if (fabs(b->items[i].t_epoch - p->t_epoch) <= b->repick_window_sec) {
                b->items[i] = *p;
                b->items[i].used = 0;
                b->n_replaced++;
                return 2;
            }
        }
    }

    if (b->n >= b->cap) return 0;
    b->items[b->n++] = *p;
    b->n_inserted++;
    return 1;
}

/* Elimina picks mas viejos que ttl_sec respecto de now_epoch. */
int PickBuffer_Prune(PickBuffer *b, double now_epoch, double ttl_sec)
{
    int i, k = 0, removed = 0;

    if (!b || !b->items) return 0;

    for (i = 0; i < b->n; i++) {
        if ((now_epoch - b->items[i].t_epoch) > ttl_sec) {
            removed++;
            continue;
        }
        b->items[k++] = b->items[i];
    }
    b->n = k;
    return removed;
}

/* Copia los picks dentro de la ventana temporal. Devuelve cuantos copio. */
int PickBuffer_Snapshot(PickBuffer *b, double now_epoch, double window_sec,
                        Pick *out, int max_out)
{
    int i, k = 0;

    if (!b || !b->items || !out) return 0;
    for (i = 0; i < b->n && k < max_out; i++) {
        if ((now_epoch - b->items[i].t_epoch) <= window_sec)
            out[k++] = b->items[i];
    }
    return k;
}

int PickBuffer_MarkUsed(PickBuffer *b, const int *idx, int n, int used)
{
    int i;
    if (!b || !b->items || !idx) return 0;
    for (i = 0; i < n; i++) {
        if (idx[i] >= 0 && idx[i] < b->n)
            b->items[idx[i]].used = used;
    }
    return 0;
}
