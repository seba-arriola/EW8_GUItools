/******************************************************************************
 * csnhypodbp_master.c                                                        *
 *                                                                            *
 * Implementacion de la tienda maestra (una fila por grupo). Ver .h.           *
 ******************************************************************************/
#include "csnhypodbp_master.h"

#define CSNHYPODBP_MASTER_KEY "master-group"

void csnhypodbp_master_tag(CsnhypodbpRow *row, gpointer group)
{
    if (row)
        g_object_set_data(G_OBJECT(row), CSNHYPODBP_MASTER_KEY, group);
}

gpointer csnhypodbp_master_group(CsnhypodbpRow *row)
{
    return row ? g_object_get_data(G_OBJECT(row), CSNHYPODBP_MASTER_KEY) : NULL;
}

int csnhypodbp_master_find(GListStore *store, gpointer group)
{
    guint i, n;

    if (!store)
        return -1;
    n = g_list_model_get_n_items(G_LIST_MODEL(store));
    for (i = 0; i < n; i++) {
        gpointer item = g_list_model_get_item(G_LIST_MODEL(store), i);
        int hit = (item && csnhypodbp_master_group(CSNHYPODBP_ROW(item)) == group);
        if (item) g_object_unref(item);
        if (hit) return (int)i;
    }
    return -1;
}

int csnhypodbp_master_upsert(GListStore *store, gpointer group, CsnhypodbpRow *row)
{
    guint i, n;
    int first = -1;

    if (!store || !row)
        return -1;

    csnhypodbp_master_tag(row, group);

    /* Pase 1: localizar la primera ocurrencia y colapsar duplicados. */
    n = g_list_model_get_n_items(G_LIST_MODEL(store));
    for (i = 0; i < n; ) {
        gpointer item = g_list_model_get_item(G_LIST_MODEL(store), i);
        int match = (item && csnhypodbp_master_group(CSNHYPODBP_ROW(item)) == group);
        if (item) g_object_unref(item);
        if (!match) { i++; continue; }
        if (first < 0) { first = (int)i; i++; continue; }  /* deja la primera */
        g_list_store_remove(store, i);                     /* colapsa el resto */
        n--;
    }

    if (first < 0) {
        g_list_store_append(store, row);
        return (int)g_list_model_get_n_items(G_LIST_MODEL(store)) - 1;
    }

    {
        gpointer cur = g_list_model_get_item(G_LIST_MODEL(store), (guint)first);
        if (cur != row)
            g_list_store_splice(store, (guint)first, 1, (gpointer *)&row, 1);
        if (cur) g_object_unref(cur);
    }
    return first;
}

int csnhypodbp_master_remove(GListStore *store, gpointer group)
{
    guint i, n;
    int removed = 0;

    if (!store)
        return 0;
    n = g_list_model_get_n_items(G_LIST_MODEL(store));
    for (i = 0; i < n; ) {
        gpointer item = g_list_model_get_item(G_LIST_MODEL(store), i);
        int match = (item && csnhypodbp_master_group(CSNHYPODBP_ROW(item)) == group);
        if (item) g_object_unref(item);
        if (match) { g_list_store_remove(store, i); n--; removed++; }
        else i++;
    }
    return removed;
}
