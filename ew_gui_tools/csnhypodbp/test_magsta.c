/*
 * Test headless de la cache de magnitudes por estacion (ver
 * csnhypodbp_magsta.h): guardar, reemplazar, anillo acotado y fallos.
 */

#include <stdio.h>
#include <string.h>

#include "csnhypodbp_magsta.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static CsnhypodbpMagStaStore store;

static CsnhypodbpMagIn mk(const char *sta, double mag)
{
    CsnhypodbpMagIn v;
    memset(&v, 0, sizeof(v));
    snprintf(v.sta, sizeof(v.sta), "%s", sta);
    snprintf(v.chan, sizeof(v.chan), "%s", "HHZ");
    v.mag = mag;
    return v;
}

static void test_put_take(void)
{
    CsnhypodbpMagIn vals[3], out[8];

    vals[0] = mk("GO01", 5.1);
    vals[1] = mk("GO02", 5.3);
    vals[2] = mk("GO03", 4.9);

    csnhypodbp_magsta_init(&store);
    csnhypodbp_magsta_put(&store, 100, 163, 0, vals, 3);
    CHECK(csnhypodbp_magsta_take(&store, 100, 163, 0, out, 8) == 3,
          "take: devuelve las 3 estaciones");
    CHECK(strcmp(out[0].sta, "GO01") == 0 && strcmp(out[2].sta, "GO03") == 0 &&
          out[1].mag > 5.29 && out[1].mag < 5.31,
          "take: conserva orden y valores");

    CHECK(csnhypodbp_magsta_take(&store, 100, 163, 1, out, 8) == 0,
          "take: otra magnitud -> 0");
    CHECK(csnhypodbp_magsta_take(&store, 101, 163, 0, out, 8) == 0,
          "take: otra solucion -> 0");
    CHECK(csnhypodbp_magsta_take(&store, 100, 162, 0, out, 8) == 0,
          "take: otro modulo -> 0");
    CHECK(csnhypodbp_magsta_take(&store, 100, 163, 0, out, 2) == 2,
          "take: respeta el tope");

    /* Reemplazo de la misma clave. */
    vals[0] = mk("GO09", 6.6);
    csnhypodbp_magsta_put(&store, 100, 163, 0, vals, 1);
    CHECK(csnhypodbp_magsta_take(&store, 100, 163, 0, out, 8) == 1 &&
          strcmp(out[0].sta, "GO09") == 0,
          "put: reemplaza el conjunto de la misma clave");
}

static void test_ring(void)
{
    CsnhypodbpMagIn vals[1], out[8];
    int i;

    csnhypodbp_magsta_init(&store);
    for (i = 0; i < CSNHYPODBP_MAGSTA_RING; i++) {
        vals[0] = mk("GO01", 5.0 + i);
        csnhypodbp_magsta_put(&store, 200 + i, 163, 0, vals, 1);
    }
    CHECK(csnhypodbp_magsta_take(&store, 200, 163, 0, out, 8) == 1,
          "anillo: el primero sigue estando");
    CHECK(csnhypodbp_magsta_take(&store, 200 + CSNHYPODBP_MAGSTA_RING - 1, 163, 0,
                                 out, 8) == 1,
          "anillo: el ultimo esta");

    /* Uno mas: se reutiliza el hueco mas viejo. */
    vals[0] = mk("GO02", 7.7);
    csnhypodbp_magsta_put(&store, 999, 163, 0, vals, 1);
    CHECK(csnhypodbp_magsta_take(&store, 999, 163, 0, out, 8) == 1 &&
          strcmp(out[0].sta, "GO02") == 0,
          "anillo: entra el nuevo");
    CHECK(csnhypodbp_magsta_take(&store, 200, 163, 0, out, 8) == 0,
          "anillo: el mas viejo se fue");
    CHECK(csnhypodbp_magsta_take(&store, 200 + CSNHYPODBP_MAGSTA_RING - 1, 163, 0,
                                 out, 8) == 1,
          "anillo: el resto sigue");
}

static void test_guards(void)
{
    CsnhypodbpMagIn vals[1], out[8];

    vals[0] = mk("GO01", 5.0);
    csnhypodbp_magsta_init(&store);
    csnhypodbp_magsta_put(&store, 1, 163, 0, vals, 0);
    CHECK(csnhypodbp_magsta_take(&store, 1, 163, 0, out, 8) == 0,
          "put: n = 0 no guarda");
    csnhypodbp_magsta_put(NULL, 1, 163, 0, vals, 1);
    csnhypodbp_magsta_put(&store, 1, 163, 0, NULL, 1);
    CHECK(1, "put: NULL no revienta");
    CHECK(csnhypodbp_magsta_take(NULL, 1, 163, 0, out, 8) == 0,
          "take: store NULL -> 0");
    CHECK(csnhypodbp_magsta_take(&store, 1, 163, 0, NULL, 8) == 0,
          "take: salida NULL -> 0");
    CHECK(csnhypodbp_magsta_take(&store, 1, 163, 0, out, 0) == 0,
          "take: max 0 -> 0");
    csnhypodbp_magsta_init(NULL);
    CHECK(1, "init: NULL no revienta");
}

int main(void)
{
    test_put_take();
    test_ring();
    test_guards();
    if (fails) { printf("%d FALLOS\n", fails); return 1; }
    printf("OK test_magsta\n");
    return 0;
}
