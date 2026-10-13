/*
 * AC6: la firma de picadas da un event_id estable. Soluciones del MISMO sismo
 * con qid distinto (o solapamiento >= GroupMinMatchPicks) caen en el mismo
 * evento; picadas distintas => evento distinto. Sin BD.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hypodb_ingest.h"

static void mkpick(CsnhypodbpPick *p, const char *sta, char phase, double t)
{
    memset(p, 0, sizeof *p);
    snprintf(p->sta, sizeof p->sta, "%s", sta);
    p->phase = phase;
    p->t_pick = t;
}

int main(void)
{
    HypodbConfig cfg;
    HypodbState *st;
    CsnhypodbpSolution a, b, c, d;
    char e1[64], e2[64], e3[64], e4[64];

    memset(&cfg, 0, sizeof cfg);
    cfg.group_pick_tol_sec = 0.1;
    cfg.group_min_match_picks = 3;
    cfg.event_max_dt_sec = 300.0;

    st = hypodb_state_new(&cfg);
    assert(st);

    /* Sismo 1, 4 picadas, qid 111 */
    memset(&a, 0, sizeof a);
    a.qid = 111;
    a.otime = 1000000000.0;
    a.npicks = 4;
    mkpick(&a.picks[0], "GO01", 'P', 1000.00);
    mkpick(&a.picks[1], "GO02", 'P', 1001.00);
    mkpick(&a.picks[2], "GO03", 'P', 1002.00);
    mkpick(&a.picks[3], "GO01", 'S', 1005.00);
    assert(hypodb_assign_event(st, &a, e1, sizeof e1) == 0);
    assert(strncmp(e1, "E111-", 5) == 0);   /* E<qid>-<yyyymmddhhmmss> */

    /* Mismo sismo, qid distinto, mismas picadas -> mismo evento */
    b = a;
    b.qid = 222;
    assert(hypodb_assign_event(st, &b, e2, sizeof e2) == 0);
    assert(strcmp(e2, e1) == 0);

    /* Solapamiento parcial (>= 3 picadas) -> mismo evento */
    memset(&c, 0, sizeof c);
    c.qid = 333;
    c.npicks = 3;
    mkpick(&c.picks[0], "GO01", 'P', 1000.02);
    mkpick(&c.picks[1], "GO02", 'P', 1001.02);
    mkpick(&c.picks[2], "GO03", 'P', 1002.02);
    assert(hypodb_assign_event(st, &c, e3, sizeof e3) == 0);
    assert(strcmp(e3, e1) == 0);

    /* Otro sismo (picadas distintas) -> evento distinto */
    memset(&d, 0, sizeof d);
    d.qid = 444;
    d.npicks = 3;
    mkpick(&d.picks[0], "XX01", 'P', 2000.0);
    mkpick(&d.picks[1], "XX02", 'P', 2001.0);
    mkpick(&d.picks[2], "XX03", 'P', 2002.0);
    assert(hypodb_assign_event(st, &d, e4, sizeof e4) == 0);
    assert(strcmp(e4, "E444") == 0);
    assert(strcmp(e4, e1) != 0);

    /* Mismo qid de OTRA sesión de csnloc (~27,7 h después) y picadas distintas:
       la guarda EventMaxDtSec impide la fusión, así que es un evento nuevo y su
       id lleva otra hora. */
    {
        CsnhypodbpSolution e;
        char e5[64];

        memset(&e, 0, sizeof e);
        e.qid = 111;
        e.otime = 1000000000.0 + 100000.0;
        e.npicks = 3;
        mkpick(&e.picks[0], "YY01", 'P', 5000.0);
        mkpick(&e.picks[1], "YY02", 'P', 5001.0);
        mkpick(&e.picks[2], "YY03", 'P', 5002.0);
        assert(hypodb_assign_event(st, &e, e5, sizeof e5) == 0);
        assert(strcmp(e5, e1) != 0);
        assert(strncmp(e5, "E111-", 5) == 0);
    }

    hypodb_state_free(st);
    printf("ALL HYPODB GROUP EVENTS TESTS PASSED\n");
    return 0;
}
