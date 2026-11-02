/* C8: seleccion del evento vivo (crv_event_decide) y regresion del reporte.
 *
 * El visor muestra el evento de MAYOR hora de origen, no el ultimo ARC que
 * llega: un refinador con live_usage=1 republica correcciones de eventos
 * antiguos cada time_recalc_live, y aceptar la ultima degradaba "Time Since
 * Quake" a cientos de horas hasta reiniciar.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static char g_dir[256];
static char g_path[600];

static CrvArc mk(double t0, int qid, int version)
{
    CrvArc a;

    memset(&a, 0, sizeof a);
    a.t0 = t0;
    a.qid = qid;
    a.version = version;
    a.lat = -20.0;
    a.lon = -70.0;
    a.depth_km = 10.0;
    return a;
}

static size_t build(char *buf, const char *t0_14, const char *qid,
                    const char *ver4, char ver1)
{
    return arc_build(buf, t0_14, "00", "20", 'S', "3000",
                     "70", 'W', "1500", "1000", qid, ver4, ver1);
}

/* --- nucleo puro --------------------------------------------------------- */

static void test_decide(void)
{
    /* Base epoch realista: la colision de qid (27.77 h antes) debe seguir
     * siendo un instante valido, no un t0 negativo. */
    const double T = 1793623055.0;
    CrvSnapshot cur;
    CrvArc in;

    memset(&cur, 0, sizeof cur);
    in = mk(T, 42, 1);
    CHECK(crv_event_decide(&cur, NULL, CRV_EVENT_MATCH_SEC) == CRV_EVENT_INVALID,
          "sin ARC");
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_FIRST,
          "sin datos => FIRST");

    cur.has_data = 1;
    cur.has_depth = 0;              /* seed de BD */
    cur.qid = 42;
    cur.version = 9999;
    cur.origin_time = T;

    in = mk(T + 2.0, 42, 10000);
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_UPDATE,
          "misma identidad => UPDATE (rellena depth del seed)");
    in = mk(T, 42, 1000);           /* wrap 9999 -> 1000 del mismo evento */
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_UPDATE,
          "version envuelta => UPDATE");
    in = mk(T + 700.0, 42, 1);      /* mismo qid, fuera de ventana y mas nuevo */
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_NEW,
          "mismo qid fuera de ventana y mas nuevo => NEW");
    in = mk(T, 43, 1);              /* qid distinto con la misma hora */
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_NEW,
          "qid distinto con la misma hora => NEW");
    in = mk(T - 99720.0, 42, 1);    /* colision de qid a 27.77 h */
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_STALE,
          "qid repetido y mas antiguo => STALE");
    in = mk(T - 1.0, 43, 1);
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_STALE,
          "evento distinto y mas antiguo => STALE");
    in = mk(T, 0, 1);
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_INVALID,
          "qid invalido");
    in = mk(0.0, 42, 1);
    CHECK(crv_event_decide(&cur, &in, CRV_EVENT_MATCH_SEC) == CRV_EVENT_INVALID,
          "t0 invalido");
}

/* --- secuencia del reporte, por el camino real del anillo ----------------- */

static void test_reported_symptom(void)
{
    CrvConfig cfg;
    CrvCore *core;
    CrvSnapshot s;
    char buf[300];
    size_t len;
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.heart_beat_int = 30;
    cfg.db_hours = 24;
    core = crv_core_new(&cfg);
    CHECK(core != NULL, "core_new");
    if (!core)
        return;

    /* El evento mas reciente (seed de BD / primer vivo). */
    len = build(buf, "20261102121100", "1354900293", "0001", '1');
    CHECK(crv_core_ingest_arc(core, buf, len) ==
          (CRV_CHANGED_SUMMARY | CRV_CHANGED_EVENT), "primer evento");

    /* 200 iteraciones de un evento de 17 dias antes (400+ h), que es lo que
     * hyp2000_ring repite cada 30 s. Ninguna puede mover la vista. */
    for (i = 0; i < 200; i++) {
        char ver[5];
        snprintf(ver, sizeof ver, "%04d", i);
        len = build(buf, "20261016081100", "1354900024", ver, ver[3]);
        CHECK(crv_core_ingest_arc(core, buf, len) == 0,
              "iteracion de evento antiguo ignorada");
    }
    /* Y el wrap real 9999 -> 1000 tampoco. */
    len = build(buf, "20261016081100", "1354900024", "9999", '9');
    CHECK(crv_core_ingest_arc(core, buf, len) == 0, "version 9999 ignorada");
    len = build(buf, "20261016081100", "1354900024", "1000", '0');
    CHECK(crv_core_ingest_arc(core, buf, len) == 0, "version envuelta ignorada");

    crv_core_snapshot(core, &s);
    CHECK(s.qid == 1354900293, "sigue el evento mas reciente");
    CHECK(s.origin_time == crv_iso_epoch("2026-11-02 12:11:00"),
          "hora de origen intacta");

    /* Un evento de verdad mas nuevo si entra (y resetea magnitudes). */
    len = build(buf, "20261102130000", "1354900294", "0001", '1');
    CHECK(crv_core_ingest_arc(core, buf, len) ==
          (CRV_CHANGED_SUMMARY | CRV_CHANGED_EVENT), "evento nuevo");
    crv_core_snapshot(core, &s);
    CHECK(s.qid == 1354900294 && s.mag.qid == 1354900294,
          "evento nuevo aplicado");

    /* Una iteracion vieja del evento anterior ya no entra. */
    len = build(buf, "20261102121100", "1354900293", "0002", '2');
    CHECK(crv_core_ingest_arc(core, buf, len) == 0,
          "el evento anterior quedo atras");

    /* Iteracion del evento MOSTRADO: se acepta aunque la version baje (seed
     * con version 0 / wrap) y no resetea las magnitudes. */
    len = build(buf, "20261102130000", "1354900294", "1000", '0');
    CHECK(crv_core_ingest_arc(core, buf, len) == CRV_CHANGED_SUMMARY,
          "iteracion del evento mostrado");
    crv_core_snapshot(core, &s);
    CHECK(s.mag.qid == 1354900294, "sin reset de magnitudes en UPDATE");

    crv_core_free(core);
}

/* --- log de la decision --------------------------------------------------- */

static void log_start(void)
{
    snprintf(g_dir, sizeof g_dir, "/tmp/crv_select_XXXXXX");
    if (!mkdtemp(g_dir)) {
        printf("FAIL: mkdtemp\n");
        g_fail = 1;
        return;
    }
    setenv("EW_LOG", g_dir, 1);
    if (crv_log_init(1) != 1) {
        printf("FAIL: crv_log_init(1)\n");
        g_fail = 1;
    }
}

static void log_check(void)
{
    struct tm tm;
    const time_t now = time(NULL);
    char buf[32768], stamp[16];
    FILE *f;
    size_t n;

    if (!g_dir[0])
        return;
    gmtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%Y%m%d", &tm);
    snprintf(g_path, sizeof g_path, "%s/csnrvQT_%s.log", g_dir, stamp);

    f = fopen(g_path, "rb");
    CHECK(f != NULL, "log del modulo");
    if (!f)
        return;
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    CHECK(strstr(buf, "-> aceptado (primero)") != NULL, "log: primero");
    CHECK(strstr(buf, "-> aceptado (evento nuevo)") != NULL, "log: evento nuevo");
    CHECK(strstr(buf, "-> ignorado (evento mas antiguo)") != NULL,
          "log: evento antiguo");
}

int main(void)
{
    log_start();
    test_decide();
    test_reported_symptom();
    log_check();

    if (g_path[0])
        unlink(g_path);
    if (g_dir[0])
        rmdir(g_dir);

    if (g_fail == 0)
        printf("OK: seleccion del evento vivo (nunca retroceder de hora de origen)\n");
    return g_fail;
}
