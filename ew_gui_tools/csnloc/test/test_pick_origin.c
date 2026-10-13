/*
 * C1: origen de la picada (12.º token A/M) en TYPE_PICK_SCNL.
 *
 * El repick manual de la GUI emite "... 0 0 0 P M"; pickS emite "S A";
 * pick_FP no emite los tokens extra. Ausente => automatica.
 */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

int main(void)
{
    Pick p;

    const char *no_tok =
        "8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0";
    const char *p_a =
        "8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0 P A";
    const char *s_a =
        "8 151 255 4974 BO03.HHE.C1.-- U1 20260309163736.490 22 0 0 S A";
    const char *p_m =
        "8 158 255 12 BO03.HHZ.C1.-- ?0 20260309163736.500 0 0 0 P M";
    const char *p_m_low =
        "8 158 255 13 BO03.HHZ.C1.-- ?0 20260309163736.500 0 0 0 p m";
    /* 13.er token: magnitud preferida elegida en la GUI (L/W/B/S o '-'). */
    const char *p_m_l =
        "8 158 255 12 BO03.HHZ.C1.-- ?0 20260309163736.500 0 0 0 P M L";
    const char *p_m_dash =
        "8 158 255 12 BO03.HHZ.C1.-- ?0 20260309163736.500 0 0 0 P M -";

    CHECK(PickSCNL_Parse(no_tok, (int)strlen(no_tok), &p) == 0, "sin tokens parsea");
    CHECK(p.phase == CSLOC_PHASE_P, "sin tokens -> fase P");
    CHECK(p.manual == 0, "sin tokens -> automatica");
    CHECK(p.pref_mag == 0, "sin tokens -> sin magnitud preferida");

    CHECK(PickSCNL_Parse(p_a, (int)strlen(p_a), &p) == 0, "P A parsea");
    CHECK(p.manual == 0, "P A -> automatica");

    CHECK(PickSCNL_Parse(s_a, (int)strlen(s_a), &p) == 0, "S A parsea");
    CHECK(p.phase == CSLOC_PHASE_S, "S A -> fase S");
    CHECK(p.manual == 0, "S A -> automatica");

    CHECK(PickSCNL_Parse(p_m, (int)strlen(p_m), &p) == 0, "P M parsea");
    CHECK(p.phase == CSLOC_PHASE_P, "P M -> fase P");
    CHECK(p.manual == 1, "P M -> manual");

    CHECK(PickSCNL_Parse(p_m_low, (int)strlen(p_m_low), &p) == 0, "p m parsea");
    CHECK(p.manual == 1, "p m -> manual");

    CHECK(PickSCNL_Parse(p_m_l, (int)strlen(p_m_l), &p) == 0, "P M L parsea");
    CHECK(p.manual == 1, "P M L -> manual");
    CHECK(p.pref_mag == 'L', "13.er token -> magnitud preferida L");

    CHECK(PickSCNL_Parse(p_m_dash, (int)strlen(p_m_dash), &p) == 0, "P M - parsea");
    CHECK(p.pref_mag == 0, "'-' -> sin magnitud preferida");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_pick_origin\n");
    return 0;
}
