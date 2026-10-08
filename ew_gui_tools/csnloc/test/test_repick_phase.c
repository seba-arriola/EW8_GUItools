/* Test de PickBuffer: P y S del mismo SCNL deben coexistir (clave SCNL+fase). */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static Pick mk(const char *sta, const char *chan, int phase, double t)
{
    Pick p; memset(&p, 0, sizeof(p));
    strcpy(p.sta, sta); strcpy(p.net, "C1"); strcpy(p.chan, chan);
    strcpy(p.loc, "--"); p.phase = phase; p.t_epoch = t; p.weight = 1;
    return p;
}

int main(void)
{
    PickBuffer b;
    Pick pP, pS1, pS2, pSe;
    int r;

    pP  = mk("BO03", "HHZ", CSLOC_PHASE_P, 1000.0);
    pS1 = mk("BO03", "HHZ", CSLOC_PHASE_S, 1003.0);
    pS2 = mk("BO03", "HHZ", CSLOC_PHASE_S, 1005.0);
    pSe = mk("BO03", "HHE", CSLOC_PHASE_S, 1003.0);

    PickBuffer_Init(&b, 16, 10.0);

    r = PickBuffer_AddOrReplace(&b, &pP);
    CHECK(r == 1 && b.n == 1, "P insertado");

    /* S del mismo SCNL 3 s despues: debe coexistir, no reemplazar el P. */
    r = PickBuffer_AddOrReplace(&b, &pS1);
    CHECK(r == 1 && b.n == 2, "S no pisa al P del mismo SCNL");
    CHECK(b.n_replaced == 0, "sin reemplazos");

    /* S del mismo SCNL dentro de ventana: reemplaza al S anterior. */
    r = PickBuffer_AddOrReplace(&b, &pS2);
    CHECK(r == 2 && b.n == 2, "re-pick del S reemplaza");
    CHECK(b.n_replaced == 1, "contador de reemplazos S");

    /* Otra componente (HHE) con fase S: coexiste. */
    r = PickBuffer_AddOrReplace(&b, &pSe);
    CHECK(r == 1 && b.n == 3, "S en HHE coexiste con S en HHZ");

    PickBuffer_Free(&b);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_repick_phase\n");
    return 0;
}
