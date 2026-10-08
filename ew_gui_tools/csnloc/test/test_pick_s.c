/*
 * Fase 1.5: csnloc debe ACEPTAR la fase S cuando el mensaje TYPE_PICK_SCNL
 * trae el token de fase opcional (11o campo). Hoy pick_FP no lo emite (es un
 * picker P), pero putpick manual y pickers futuros si pueden.
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

    /* Sin token de fase: default P (comportamiento actual de pick_FP). */
    const char *no_phase =
        "8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0";
    /* Con token explicito P. */
    const char *p_phase =
        "8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0 P";
    /* Con token explicito S (mayuscula y minuscula). */
    const char *s_phase =
        "8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0 S";
    const char *s_lower =
        "8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0 s";

    CHECK(PickSCNL_Parse(no_phase, (int)strlen(no_phase), &p) == 0,
          "pick sin fase parsea");
    CHECK(p.phase == CSLOC_PHASE_P, "sin token -> fase P por defecto");

    CHECK(PickSCNL_Parse(p_phase, (int)strlen(p_phase), &p) == 0,
          "pick con token P parsea");
    CHECK(p.phase == CSLOC_PHASE_P, "token P -> fase P");
    CHECK(strcmp(p.phase_name, "P") == 0, "phase_name P");

    CHECK(PickSCNL_Parse(s_phase, (int)strlen(s_phase), &p) == 0,
          "pick con token S parsea");
    CHECK(p.phase == CSLOC_PHASE_S, "token S -> fase S");
    CHECK(strcmp(p.phase_name, "S") == 0, "phase_name S");

    CHECK(PickSCNL_Parse(s_lower, (int)strlen(s_lower), &p) == 0,
          "pick con token s parsea");
    CHECK(p.phase == CSLOC_PHASE_S, "token s -> fase S");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_pick_s\n");
    return 0;
}
