/* Test headless de la tienda maestra (csnhypodbp_master.c): exactamente una
 * fila por grupo, con la identidad puesta en el GRUPO (no en el qid de la
 * solucion). Es la regresion de "eventos repetidos con el mismo ID". */
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_master.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static CsnhypodbpRow *mkrow(int qid)
{
    char id[16];
    snprintf(id, sizeof(id), "%010d", qid);
    return csnhypodbp_row_new("01/01", "00:00:00", "0.00N", "0.00W", "0", "0.0",
                              "0", "0", id, "-", "-", "-", "-", "csnloc", "1",
                              0.0, 1, qid, 0.0, 0.0, 0.0, 0);
}

static guint nitems(GListStore *s)
{
    return g_list_model_get_n_items(G_LIST_MODEL(s));
}

int main(void)
{
    GListStore *store = g_list_store_new(CSNHYPODBP_TYPE_ROW);
    int g1, g2;   /* identidades de grupo opacas (direcciones distintas) */

    /* AC1: upsert de un grupo nuevo -> n=1. */
    {
        CsnhypodbpRow *r1 = mkrow(100);
        CHECK(csnhypodbp_master_upsert(store, &g1, r1) == 0,
              "AC1: upsert nuevo devuelve 0");
        CHECK(nitems(store) == 1, "AC1: n=1");
        g_object_unref(r1);
    }

    /* AC2 (regresion): mismo grupo, fila con OTRO qid -> sigue n=1 y es la nueva. */
    {
        CsnhypodbpRow *r2 = mkrow(200);
        int idx = csnhypodbp_master_upsert(store, &g1, r2);
        CHECK(nitems(store) == 1, "AC2: mismo grupo con otro qid NO duplica");
        gpointer it = g_list_model_get_item(G_LIST_MODEL(store), (guint)idx);
        CHECK(it == r2, "AC2: la fila es la nueva preferida");
        if (it) g_object_unref(it);
        g_object_unref(r2);
    }

    /* AC3: dos grupos -> n=2. */
    {
        CsnhypodbpRow *r3 = mkrow(300);
        csnhypodbp_master_upsert(store, &g2, r3);
        CHECK(nitems(store) == 2, "AC3: dos grupos -> n=2");
        g_object_unref(r3);
    }

    /* AC4: remove de g1 -> n=1. */
    CHECK(csnhypodbp_master_remove(store, &g1) == 1, "AC4: remove quita 1");
    CHECK(nitems(store) == 1, "AC4: n=1 tras remove");

    /* AC5: duplicados preexistentes del mismo grupo se colapsan. */
    {
        CsnhypodbpRow *d1 = mkrow(400), *d2 = mkrow(500), *d3 = mkrow(600);
        csnhypodbp_master_tag(d1, &g2);
        csnhypodbp_master_tag(d2, &g2);
        csnhypodbp_master_tag(d3, &g2);
        g_list_store_append(store, d1);
        g_list_store_append(store, d2);
        g_list_store_append(store, d3);
        g_object_unref(d1); g_object_unref(d2); g_object_unref(d3);
    }
    CHECK(nitems(store) == 4, "AC5: preparados 4 (1 de g2 + 3 duplicados)");
    {
        CsnhypodbpRow *r4 = mkrow(700);
        csnhypodbp_master_upsert(store, &g2, r4);
        CHECK(nitems(store) == 1, "AC5: el upsert colapsa a 1 fila por grupo");
        g_object_unref(r4);
    }

    /* find */
    CHECK(csnhypodbp_master_find(store, &g2) >= 0, "find: g2 presente");
    CHECK(csnhypodbp_master_find(store, &g1) == -1, "find: g1 ya no esta");

    /* AC6 (seleccion pegajosa): el upsert reemplaza la fila del grupo pero
       `find` sigue devolviendo SU indice, y el grupo absorbido desaparece. Con
       eso el llamante puede devolver la seleccion al evento que el usuario tiene
       abierto en vez de al que acaba de llegar. */
    {
        int g3;
        CsnhypodbpRow *a = mkrow(900), *b = mkrow(901);
        int ia, ib;

        ia = csnhypodbp_master_upsert(store, &g3, a);
        CHECK(ia >= 0, "AC6: el grupo nuevo entra en la maestra");
        ib = csnhypodbp_master_upsert(store, &g3, b);   /* reemplazo en el sitio */
        CHECK(ib == ia, "AC6: el reemplazo conserva el indice del grupo");
        CHECK(csnhypodbp_master_find(store, &g3) == ia,
              "AC6: find sigue dando el indice del grupo del usuario");
        CHECK(csnhypodbp_master_remove(store, &g3) == 1, "AC6: remove del absorbido");
        CHECK(csnhypodbp_master_find(store, &g3) == -1, "AC6: el absorbido ya no esta");
        g_object_unref(a); g_object_unref(b);
    }

    g_object_unref(store);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_master_store\n");
    return 0;
}
