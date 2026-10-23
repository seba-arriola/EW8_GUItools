/* C5: estado vivo (version por qid, reset al cambiar de evento) y formatos. */
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

static size_t arc_qid(char *buf, const char *qid, const char *ver4, char ver1)
{
    return arc_build(buf, "20261023123456", "00", "20", 'S', "3000",
                     "70", 'W', "1500", "1000", qid, ver4, ver1);
}

int main(void)
{
    CrvConfig cfg;
    CrvCore *core;
    CrvSnapshot s;
    char buf[300];
    char txt[256];
    size_t len;
    struct tm tm;
    time_t t0;

    memset(&cfg, 0, sizeof cfg);
    cfg.heart_beat_int = 30;
    cfg.db_hours = 24;
    cfg.initial_zoom = 8.0;
    core = crv_core_new(&cfg);
    CHECK(core != NULL, "core_new");
    if (!core)
        return 1;

    /* Primer ARC: evento nuevo. */
    len = arc_qid(buf, "42", "1", '1');
    CHECK(crv_core_ingest_arc(core, buf, len) ==
          (CRV_CHANGED_SUMMARY | CRV_CHANGED_EVENT), "primer ARC");
    crv_core_snapshot(core, &s);
    CHECK(s.has_data && s.has_depth, "has_data/has_depth");
    CHECK(s.qid == 42 && s.version == 1, "qid/version");
    CHECK(s.mag.qid == 42, "mag reset al qid nuevo");

    /* Misma version o menor: se descarta y no genera flags. */
    CHECK(crv_core_ingest_arc(core, buf, len) == 0, "version repetida");
    len = arc_qid(buf, "42", "0", '0');
    CHECK(crv_core_ingest_arc(core, buf, len) == 0, "version vieja");

    /* Version mayor del mismo qid: actualiza sin resetear magnitudes.
     * El parser real de MAG necesita un wr_mag (test_magmsg); aqui solo
     * interesa que el ARC no toque el estado de magnitudes. */
    CHECK(crv_core_ingest_mag(core, "x", 0) == 0, "MAG invalida");
    len = arc_qid(buf, "42", "2", '2');
    CHECK(crv_core_ingest_arc(core, buf, len) == CRV_CHANGED_SUMMARY,
          "version mayor sin EVENT");
    crv_core_snapshot(core, &s);
    CHECK(s.version == 2, "version 2");

    /* qid nuevo: reset de magnitudes y flag EVENT. */
    len = arc_qid(buf, "43", "1", '1');
    CHECK(crv_core_ingest_arc(core, buf, len) ==
          (CRV_CHANGED_SUMMARY | CRV_CHANGED_EVENT), "qid nuevo");
    crv_core_snapshot(core, &s);
    CHECK(s.qid == 43 && s.mag.qid == 43, "reset al qid nuevo");

    /* --- formatos ------------------------------------------------------- */
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 10 - 1;
    tm.tm_mday = 23;
    tm.tm_hour = 12;
    tm.tm_min = 34;
    tm.tm_sec = 56;
    t0 = timegm(&tm);

    crv_fmt_origin((double)t0, txt, sizeof txt);
    CHECK(strcmp(txt, "12:34:56 UTC\n23/10/2026") == 0, "fmt_origin");

    crv_fmt_coords(-20.5, -70.25, txt, sizeof txt);
    CHECK(strcmp(txt, "Lat: -20.500  Lon: -70.250") == 0, "fmt_coords");

    crv_fmt_depth(12.34, txt, sizeof txt);
    CHECK(strcmp(txt, "Depth: 12 km") == 0, "fmt_depth");

    crv_fmt_elapsed(t0 + 3661, (double)t0, 1, txt, sizeof txt);
    CHECK(strcmp(txt, "01:01:01") == 0, "fmt_elapsed");
    crv_fmt_elapsed(t0 - 10, (double)t0, 1, txt, sizeof txt);
    CHECK(strcmp(txt, "00:00:00") == 0, "fmt_elapsed negativo");
    crv_fmt_elapsed(t0, 0.0, 0, txt, sizeof txt);
    CHECK(strcmp(txt, "--:--:--") == 0, "fmt_elapsed sin datos");

    CHECK(crv_iso_epoch("2026-10-23 12:34:56") == (double)t0, "iso_epoch");
    CHECK(crv_iso_epoch("no es fecha") == 0.0, "iso_epoch invalida");
    CHECK(crv_core_should_quit(core) == 0, "no quit sin anillo");

    crv_core_free(core);
    if (g_fail == 0)
        printf("OK: estado vivo (version por qid, reset) y formatos\n");
    return g_fail;
}
