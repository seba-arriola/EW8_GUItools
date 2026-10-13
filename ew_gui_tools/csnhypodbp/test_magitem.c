/*
 * Test headless del modelo de fila de la tabla de magnitudes (ver
 * csnhypodbp_magitem.h): celdas formateadas, indice, casilla y caso sin dato.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_magitem.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static CsnhypodbpMagRow mkrow(const char *sta, double dist, double mag, double res,
                              const char *chan, int used)
{
    CsnhypodbpMagRow r;
    memset(&r, 0, sizeof(r));
    snprintf(r.sta, sizeof(r.sta), "%s", sta);
    snprintf(r.chan, sizeof(r.chan), "%s", chan);
    r.dist_km = dist;
    r.mag = mag;
    r.res = res;
    r.used = used;
    /* Sin ventana explicita las celdas t0/t1 van con guion: 0.0 es un offset
       legitimo (p. ej. Mwp_StartOff), asi que el sentinela es NaN. */
    r.t0 = r.t1 = NAN;
    return r;
}

static void test_item(void)
{
    CsnhypodbpMagRow r;
    CsnhypodbpMagItem *it;
    char *sta = NULL, *dist = NULL, *mag = NULL, *res = NULL, *stat = NULL, *chan = NULL;
    char *anchor = NULL, *t0 = NULL, *t1 = NULL;
    gboolean used = FALSE, hasdata = FALSE;

    r = mkrow("GO02", 80.5, 5.25, 0.25, "HHE", 1);
    it = csnhypodbp_mag_item_new(&r, 7);
    CHECK(it != NULL, "item: se crea");
    g_object_get(it, "sta", &sta, "dist", &dist, "mag", &mag, "res", &res,
                 "stat", &stat, "chan", &chan, "used", &used, NULL);
    CHECK(strcmp(sta, "GO02") == 0, "item: estacion");
    CHECK(strcmp(dist, "80") == 0, "item: distancia sin decimales");
    CHECK(strcmp(mag, "5.25") == 0, "item: magnitud con 2 decimales");
    CHECK(strcmp(res, "+0.25") == 0, "item: residual con signo");
    CHECK(strcmp(stat, "A") == 0, "item: stat A con dato");
    CHECK(strcmp(chan, "HHE") == 0, "item: canal de la magnitud");
    CHECK(used == TRUE, "item: casilla marcada por defecto");
    CHECK(csnhypodbp_mag_item_idx(it) == 7, "item: indice de la fila");
    g_free(sta); g_free(dist); g_free(mag); g_free(res); g_free(stat); g_free(chan);
    g_object_unref(it);

    /* Ancla y ventana de la fila (lo que la tabla muestra para ajustar). */
    r = mkrow("GO05", 42.0, NAN, NAN, "", 1);
    r.has_data = 1;
    r.anchor = 'M';
    r.anchor_t = 1000.0;
    r.t0 = 10.0;
    r.t1 = 45.0;
    it = csnhypodbp_mag_item_new(&r, 3);
    anchor = t0 = t1 = NULL;
    g_object_get(it, "anchor", &anchor, "t0", &t0, "t1", &t1, "hasdata", &hasdata, NULL);
    CHECK(strcmp(anchor, "M") == 0, "item: ancla manual -> M");
    CHECK(strcmp(t0, "+10") == 0 && strcmp(t1, "+45") == 0,
          "item: ventana en offsets con signo");
    CHECK(hasdata == TRUE, "item: hasdata de la estacion con traza");
    g_free(anchor); g_free(t0); g_free(t1);
    g_object_unref(it);

    /* Sin ancla ni ventana: guiones y casilla inerte. */
    r = mkrow("GO06", 50.0, NAN, NAN, "", 0);
    it = csnhypodbp_mag_item_new(&r, 4);
    anchor = t0 = t1 = NULL;
    hasdata = TRUE;
    g_object_get(it, "anchor", &anchor, "t0", &t0, "t1", &t1, "hasdata", &hasdata, NULL);
    CHECK(strcmp(anchor, "-") == 0 && strcmp(t0, "-") == 0 && strcmp(t1, "-") == 0,
          "item: sin ancla/ventana -> guiones");
    CHECK(hasdata == FALSE, "item: sin traza la casilla queda inerte");
    g_free(anchor); g_free(t0); g_free(t1);
    g_object_unref(it);

    /* Sin dato: guiones y stat "-". */
    r = mkrow("GO03", 120.0, NAN, NAN, "", 0);
    it = csnhypodbp_mag_item_new(&r, 2);
    sta = dist = mag = res = stat = NULL;
    used = TRUE;
    g_object_get(it, "sta", &sta, "dist", &dist, "mag", &mag, "res", &res,
                 "stat", &stat, "used", &used, NULL);
    CHECK(strcmp(mag, "-") == 0 && strcmp(res, "-") == 0, "item sin dato: guiones");
    CHECK(strcmp(stat, "-") == 0, "item sin dato: stat -");
    CHECK(used == FALSE, "item sin dato: casilla desmarcada");
    CHECK(strcmp(dist, "120") == 0, "item sin dato: la distancia se ve igual");
    g_free(sta); g_free(dist); g_free(mag); g_free(res); g_free(stat);
    g_object_unref(it);

    /* Manual y sin distancia conocida. */
    r = mkrow("GO04", 0.0, 4.9, -0.11, "HHZ", 1);
    r.manual = 1;
    it = csnhypodbp_mag_item_new(&r, 0);
    dist = stat = NULL;
    g_object_get(it, "dist", &dist, "stat", &stat, NULL);
    CHECK(strcmp(dist, "") == 0, "item: sin distancia -> celda vacia");
    CHECK(strcmp(stat, "M") == 0, "item: manual -> M");
    g_free(dist); g_free(stat);
    g_object_unref(it);

    CHECK(csnhypodbp_mag_item_new(NULL, 0) == NULL, "item: fila NULL -> NULL");
}

int main(void)
{
    test_item();
    if (fails) { printf("%d FALLOS\n", fails); return 1; }
    printf("OK test_magitem\n");
    return 0;
}
