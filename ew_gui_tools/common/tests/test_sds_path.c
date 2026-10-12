/*
 * Test headless de la ruta SDS de SeisComP (criterio C5): layout
 * <root>/<YYYY>/<NET>/<STA>/<CHAN>.D/<NET>.<STA>.<LOC>.<CHAN>.D.<YYYY>.<DDD>.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ewgui/sds.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static double epoch(int year, int mon, int day)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = day;
    return (double)timegm(&tm);
}

int main(void)
{
    char buf[512];

    CHECK(ewgui_sds_path(buf, sizeof buf, "/mnt/sds", "C1", "GO01", "00",
                         "HHZ", epoch(2030, 1, 1)) == 0, "construye la ruta");
    CHECK(strcmp(buf, "/mnt/sds/2030/C1/GO01/HHZ.D/C1.GO01.00.HHZ.D.2030.001") == 0,
          "ruta 2030-01-01 (doy 001)");
    printf("   -> %s\n", buf);

    CHECK(ewgui_sds_path(buf, sizeof buf, "/mnt/sds", "C1", "GO01", "00",
                         "HHZ", epoch(2030, 12, 31)) == 0, "construye la ruta (fin de ano)");
    CHECK(strcmp(buf, "/mnt/sds/2030/C1/GO01/HHZ.D/C1.GO01.00.HHZ.D.2030.365") == 0,
          "ruta 2030-12-31 (doy 365)");

    CHECK(ewgui_sds_path(buf, sizeof buf, "/mnt/sds", "IU", "RCBR", "",
                         "BHZ", epoch(2032, 12, 31)) == 0, "construye la ruta (bisiesto)");
    CHECK(strcmp(buf, "/mnt/sds/2032/IU/RCBR/BHZ.D/IU.RCBR..BHZ.D.2032.366") == 0,
          "ruta 2032-12-31 (doy 366, loc vacia)");

    /* loc nulo de EarthWorm ("--") -> se escribe vacio, como el SDS real. */
    CHECK(ewgui_sds_path(buf, sizeof buf, "/mnt/sds", "C1", "GO01", "--",
                         "HHZ", epoch(2030, 1, 1)) == 0, "construye la ruta (loc --)");
    CHECK(strcmp(buf, "/mnt/sds/2030/C1/GO01/HHZ.D/C1.GO01..HHZ.D.2030.001") == 0,
          "loc '--' -> '..' (nulo de EarthWorm)");

    /* caso real: 2016-12-25 (doy 360) en el SDS montado en /mnt/sds/archive. */
    CHECK(ewgui_sds_path(buf, sizeof buf, "/mnt/sds/archive", "C1", "LL07", "--",
                         "HHZ", epoch(2016, 12, 25)) == 0, "construye la ruta (2016)");
    CHECK(strcmp(buf, "/mnt/sds/archive/2016/C1/LL07/HHZ.D/C1.LL07..HHZ.D.2016.360") == 0,
          "ruta real 2016-12-25 (doy 360)");

    /* buffer pequeno -> error, no desborda */
    CHECK(ewgui_sds_path(buf, 8, "/mnt/sds", "C1", "GO01", "00", "HHZ",
                         epoch(2030, 1, 1)) == -1, "buffer pequeno -> -1");

    /* argumentos nulos -> error */
    CHECK(ewgui_sds_path(NULL, sizeof buf, "/mnt/sds", "C1", "GO01", "00",
                         "HHZ", epoch(2030, 1, 1)) == -1, "out NULL -> -1");

    if (fails) {
        printf("FALLARON %d comprobaciones\n", fails);
        return 1;
    }
    printf("ALL SDS PATH TESTS PASSED\n");
    return 0;
}
