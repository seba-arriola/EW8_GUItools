/* C2: parser minimo del ARC (offsets exactos de csnrv.c:441-466). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "crv_capi.h"

#include "arc_fixture.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);             \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

int main(void)
{
    char buf[300];
    CrvArc a;
    size_t len;
    struct tm tm;
    double expect;

    /* 2026-10-23 12:34:56.78 UTC, 20 30.00 S, 70 15.00 W, 12.34 km, qid 42 v7. */
    len = arc_build(buf, "20261023123456", "78", "20", 'S', "3000",
                    "70", 'W', "1500", "1234", "42", "7", '3');
    CHECK(crv_arc_parse(buf, len, &a) == 0, "ARC valido debe parsear");

    memset(&tm, 0, sizeof tm);
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 10 - 1;
    tm.tm_mday = 23;
    tm.tm_hour = 12;
    tm.tm_min = 34;
    tm.tm_sec = 56;
    expect = (double)timegm(&tm) + 0.78;
    CHECK(a.t0 > expect - 0.001 && a.t0 < expect + 0.001,
          "t0 UTC con centesimas");
    CHECK(a.lat > -20.501 && a.lat < -20.499, "lat S = -(20+30/60)");
    CHECK(a.lon > -70.251 && a.lon < -70.249, "lon W = -(70+15/60)");
    CHECK(a.depth_km > 12.339 && a.depth_km < 12.341, "depth /100");
    CHECK(a.qid == 42, "qid en 136..145");
    CHECK(a.version == 7, "version en 178..181");

    /* Dir N/E y version 0 -> fallback al caracter 161. */
    len = arc_build(buf, "20260101000000", "00", "10", 'N', "1500",
                    "20", 'E', "3000", "0050", "99", "0", '4');
    CHECK(crv_arc_parse(buf, len, &a) == 0, "ARC norte/este");
    CHECK(a.lat > 10.249 && a.lat < 10.251, "lat N positiva");
    CHECK(a.lon > 20.499 && a.lon < 20.501, "lon E positiva");
    CHECK(a.version == 4, "version 0 -> char 161");

    /* Mensaje corto: no parsea y no toca el buffer. */
    CHECK(crv_arc_parse(buf, 20, &a) != 0, "ARC corto debe fallar");

    if (g_fail == 0)
        printf("OK: parser ARC (t0, S/W, depth, qid, version y fallback)\n");
    return g_fail;
}
