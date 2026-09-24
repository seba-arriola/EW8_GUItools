/* Test de PickSCNL_Parse. */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

int main(void)
{
    Pick p;
    const char *good = "8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0";
    const char *noise = "esto no es un pick";
    const char *shortmsg = "8 151 255";

    CHECK(PickSCNL_Parse(good, (int)strlen(good), &p) == 0, "pick valido parsea");
    CHECK(strcmp(p.sta, "BO03") == 0, "station BO03");
    CHECK(strcmp(p.chan, "HHZ") == 0, "channel HHZ");
    CHECK(strcmp(p.net, "C1") == 0, "network C1");
    CHECK(strcmp(p.loc, "--") == 0, "location --");
    CHECK(p.weight == 1, "weight=1 desde 'U1'");
    CHECK(p.phase == CSLOC_PHASE_P, "fase P por defecto");

    /* 2026-03-09T16:37:36.490Z -> epoch conocido */
    CHECK(p.t_epoch > 1773000000.0 && p.t_epoch < 1780000000.0,
          "epoch en rango esperado");

    CHECK(PickSCNL_Parse(noise, (int)strlen(noise), &p) != 0, "ruido rechazado");
    CHECK(PickSCNL_Parse(shortmsg, (int)strlen(shortmsg), &p) != 0, "mensaje corto rechazado");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_pick_parse\n");
    return 0;
}
