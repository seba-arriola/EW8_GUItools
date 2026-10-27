/*
 * test_staitem - modelo de la tabla de estaciones: UNA fila por (estacion,
 * fase); la P lleva estacion+distancia, `stat` = M/A/Teo y `res` formateado.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_staitem.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static int prop_is(CsnhypodbpStaItem *it, const char *name, const char *want)
{
    char *s = NULL;
    int ok;
    g_object_get(it, name, &s, NULL);
    ok = (s != NULL) && (strcmp(s, want) == 0);
    g_free(s);
    return ok;
}

static int prop_bool(CsnhypodbpStaItem *it, const char *name)
{
    gboolean b = FALSE;
    g_object_get(it, name, &b, NULL);
    return b ? 1 : 0;
}

int main(void)
{
    CsnhypodbpStaRow r;
    CsnhypodbpStaItem *p, *s;

    memset(&r, 0, sizeof(r));
    snprintf(r.sta, sizeof(r.sta), "PB15");
    r.idx = 7;
    r.dist_km = 53.4;
    r.has_data = 1;
    r.p.present = 1; r.p.origin = 'M'; r.p.t = 100.0; r.p.resid = -0.0123;
    r.s.present = 1; r.s.origin = 'A'; r.s.t = 130.0; r.s.resid = 0.3;

    p = csnhypodbp_sta_item_new(&r, 0);
    s = csnhypodbp_sta_item_new(&r, 1);

    CHECK(p && s, "crea las dos filas de la estacion");
    CHECK(csnhypodbp_sta_item_idx(p) == 7, "idx de la P");
    CHECK(csnhypodbp_sta_item_phase(p) == 0, "phase 0 = P");
    CHECK(csnhypodbp_sta_item_phase(s) == 1, "phase 1 = S");

    CHECK(prop_is(p, "sta", "PB15") && prop_is(s, "sta", "PB15"),
          "las DOS filas llevan la estacion (son autonomas)");
    CHECK(prop_is(p, "dist", "53") && prop_is(s, "dist", "53"),
          "la distancia va en las DOS filas");
    CHECK(prop_is(p, "pick", "P") && prop_is(s, "pick", "S"), "columna pick");

    CHECK(prop_is(p, "stat", "M"), "stat de la P = M");
    CHECK(prop_is(s, "stat", "A"), "stat de la S = A");
    CHECK(prop_is(p, "res", "-0.01"), "res de la P formateado %+.2f");
    CHECK(prop_is(s, "res", "+0.30"), "res de la S formateado %+.2f");

    CHECK(prop_bool(p, "has-pick") == 1 && prop_bool(s, "has-pick") == 1, "has-pick");
    g_object_unref(p);
    g_object_unref(s);

    /* Teorica sin residual -> `res` vacio; fase ausente -> has-pick 0. */
    memset(&r, 0, sizeof(r));
    snprintf(r.sta, sizeof(r.sta), "GO02");
    r.idx = 2;
    r.p.present = 1; r.p.origin = 'T'; r.p.t = 110.0; r.p.resid = NAN;
    p = csnhypodbp_sta_item_new(&r, 0);
    s = csnhypodbp_sta_item_new(&r, 1);
    CHECK(prop_is(p, "stat", "Teo"), "stat de la teorica");
    CHECK(prop_is(p, "res", ""), "sin residual -> res vacio");
    CHECK(prop_bool(p, "has-pick") == 1, "teorica presente");
    CHECK(prop_bool(s, "has-pick") == 0, "S ausente");
    CHECK(prop_is(s, "stat", "") && prop_is(s, "res", ""), "S ausente -> celdas vacias");
    g_object_unref(p);
    g_object_unref(s);

    CHECK(csnhypodbp_sta_item_new(NULL, 0) == NULL, "fila NULL -> NULL");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_staitem\n");
    return 0;
}
