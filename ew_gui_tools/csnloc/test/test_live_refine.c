/*
 * test_live_refine.c - contrato del motor vivo COMPARTIDO (csnloc/live_refine).
 *
 * No usa anillos ni motores: localizador/rewrite/publish son callbacks falsos.
 * Fija:
 *   1. El ARC se siembra con las fases del seed + las picadas asociadas.
 *   2. Cada pasada valida publica una version NUEVA (seed.version+1, +2).
 *   3. Sin picadas nuevas no se republica.
 *   4. El seed y las picadas se pasan con el mod que traiga el anillo.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../live_refine.h"
#include "../csnloc.h"
#include "../live_assoc.h"
#include "../../csnhypodbp/csnhypodbp_group.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

typedef struct {
    int      n;
    int      fail;
    unsigned version[4];
    int      npicks[4];
} Pub;

static int fake_locate(void *ud, const char *in, int in_len,
                       char *out, int out_cap, int *out_len)
{
    (void)ud;
    if (in_len <= 0 || in_len >= out_cap) return -1;
    memcpy(out, in, (size_t)in_len);
    out[in_len] = '\0';
    *out_len = in_len;
    return 0;
}

static int fake_rewrite(void *ud, char *arc, int cap, int len,
                        const char *in, int in_len)
{
    (void)ud;
    (void)cap;
    (void)in;
    (void)in_len;
    return len;
}

static int fake_publish(void *ud, const char *in, int in_len,
                        const char *out, int out_len)
{
    Pub *p = (Pub *)ud;
    CsnhypodbpSolution sol;

    (void)in;
    (void)in_len;
    if (p->fail) return -1;
    if (p->n >= 4) return -1;
    if (csnhypodbp_parse_solution(out, (size_t)out_len, 0, &sol) != 0)
        return -1;
    p->version[p->n] = sol.version;
    p->npicks[p->n] = sol.npicks;
    p->n++;
    return 0;
}

static void set_station(StationList *st)
{
    memset(st, 0, sizeof(*st));
    st->n = 1;
    snprintf(st->st[0].sta, sizeof(st->st[0].sta), "TST01");
    st->st[0].lat = 0.0;
    st->st[0].lon = 0.0;
    st->lat_geoc_sta[0] = 0.0;
}

static Pick mkpick(char phase, double t)
{
    Pick p;

    memset(&p, 0, sizeof(p));
    snprintf(p.sta, sizeof(p.sta), "TST01");
    snprintf(p.net, sizeof(p.net), "CX");
    snprintf(p.chan, sizeof(p.chan), phase == CSLOC_PHASE_S ? "HHN" : "HHZ");
    p.phase = phase;
    snprintf(p.phase_name, sizeof(p.phase_name), "%s",
             phase == CSLOC_PHASE_S ? "S" : "P");
    p.t_epoch = t;
    return p;
}

int main(void)
{
    const char *dir = getenv("EW_PARAMS");
    StationList st;
    TTModel tt;
    LiveAssocCfg assoc;
    LiveLocCheckCfg check;
    LiveRefineOps ops;
    LiveRefine *r;
    Pub pub;
    CsnhypodbpSolution seed;
    char arc[8192];
    double tp = 0.0, ts = 0.0;
    Pick pp, ps;

    if (!dir || !dir[0]) dir = "../../run_working_v8/params";
    if (TTModel_Init(&tt, dir, "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init (%s)\n", dir);
        return 1;
    }
    set_station(&st);
    if (TTModel_Predict(&tt, 0.0, 300.0, CSLOC_PHASE_P, &tp, NULL) != 0 ||
        TTModel_Predict(&tt, 0.0, 300.0, CSLOC_PHASE_S, &ts, NULL) != 0) {
        printf("FAIL: TTModel_Predict\n");
        TTModel_Free(&tt);
        return 1;
    }

    /* Seed de csnloc v1 sin fases: el motor añade las picadas. */
    memset(&seed, 0, sizeof(seed));
    seed.qid = 123;
    seed.version = 1;
    seed.otime = 1000.0;
    seed.lat = 0.0;
    seed.lon = 0.0;
    seed.depth = 300.0;
    seed.rms = 0.5;
    seed.gap = 180;
    seed.npicks = 0;
    seed.nph = 0;
    CHECK(csnhypodbp_arc_build(&seed, 0, arc, sizeof(arc)) == 0,
          "ARC semilla v1 construido");

    memset(&assoc, 0, sizeof(assoc));
    assoc.lookback_sec = 300.0;
    assoc.res_p_sec = 3.0;
    assoc.res_s_sec = 5.0;
    memset(&check, 0, sizeof(check));
    check.min_phases = 1;
    check.max_rms = 100.0;
    check.max_gap_deg = 360.0;

    memset(&ops, 0, sizeof(ops));
    ops.locate = fake_locate;
    ops.rewrite = fake_rewrite;
    ops.publish = fake_publish;
    ops.ud = &pub;
    ops.stations = &st;
    ops.tt = &tt;
    ops.assoc = assoc;
    ops.check = check;
    ops.recalc_sec = 0;
    ops.max_iter = 1;
    ops.n_retry = 0;
    ops.max_events = 4;
    ops.event_ttl_sec = 100.0;
    ops.tag = "test_live_refine";

    memset(&pub, 0, sizeof(pub));
    r = live_refine_new(&ops);
    CHECK(r != NULL, "motor vivo creado");
    if (!r) {
        TTModel_Free(&tt);
        return 1;
    }

    live_refine_seed(r, arc, (int)strlen(arc), 0);
    pp = mkpick(CSLOC_PHASE_P, 1000.0 + tp);
    CHECK(live_refine_add_pick(r, &pp) == 0, "picada P aceptada");

    CHECK(live_refine_tick(r, (time_t)2000) == 1, "primera pasada publica");
    CHECK(pub.n == 1 && pub.version[0] == 2 && pub.npicks[0] == 1,
          "v2 con la P asociada (seed v1 + 1)");

    ps = mkpick(CSLOC_PHASE_S, 1000.0 + ts);
    CHECK(live_refine_add_pick(r, &ps) == 0, "picada S aceptada");
    CHECK(live_refine_tick(r, (time_t)2001) == 1, "segunda pasada publica");
    CHECK(pub.n == 2 && pub.version[1] == 3 && pub.npicks[1] == 2,
          "v3 con P+S (cada pasada valida es un origen nuevo)");

    /* Sin picadas nuevas no se republica la misma solucion. */
    CHECK(live_refine_tick(r, (time_t)2002) == 0, "sin picadas nuevas no publica");
    CHECK(pub.n == 2, "no se duplica la ultima version");

    /* Si publish falla, no se cuenta ni se avanza el seed: el siguiente tick
       reintenta la MISMA version. */
    {
        LiveRefine *r2;
        Pub p2;

        memset(&p2, 0, sizeof(p2));
        p2.fail = 1;
        ops.ud = &p2;
        ops.n_retry = 99;
        r2 = live_refine_new(&ops);
        CHECK(r2 != NULL, "motor de prueba de publish creado");
        if (r2) {
            live_refine_seed(r2, arc, (int)strlen(arc), 0);
            live_refine_add_pick(r2, &pp);
            CHECK(live_refine_tick(r2, (time_t)3000) == 0,
                  "publish fallido no cuenta como publicacion");
            CHECK(p2.n == 0, "publish fallido no publica");
            p2.fail = 0;
            CHECK(live_refine_tick(r2, (time_t)3001) == 1,
                  "reintenta tras publish fallido");
            CHECK(p2.n == 1 && p2.version[0] == 2,
                  "la version no avanzo por el publish fallido");
            live_refine_free(r2);
        }
    }

    live_refine_free(r);
    TTModel_Free(&tt);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("test_live_refine OK\n");
    return 0;
}
