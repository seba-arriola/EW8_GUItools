/* Test de PickBuffer: insercion, re-pick (reemplazo) y TTL. */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static Pick mk(const char *sta, double t)
{
    Pick p; memset(&p, 0, sizeof(p));
    strcpy(p.sta, sta); strcpy(p.net, "C1"); strcpy(p.chan, "HHZ");
    strcpy(p.loc, "--"); p.phase = CSLOC_PHASE_P; p.t_epoch = t; p.weight = 1;
    return p;
}

int main(void)
{
    PickBuffer b;
    Pick p1 = mk("BO03", 1000.0), p2 = mk("BO03", 1003.0), p3 = mk("BO03", 1050.0);
    Pick p4 = mk("GO01", 1000.0);
    int r;

    PickBuffer_Init(&b, 16, 10.0);

    r = PickBuffer_AddOrReplace(&b, &p1);
    CHECK(r == 1 && b.n == 1, "insercion inicial");

    r = PickBuffer_AddOrReplace(&b, &p2);
    CHECK(r == 2 && b.n == 1, "re-pick dentro de ventana reemplaza");
    CHECK(b.n_replaced == 1, "contador de reemplazos");
    CHECK(b.items[0].t_epoch == 1003.0, "tiempo actualizado");

    r = PickBuffer_AddOrReplace(&b, &p3);
    CHECK(r == 1 && b.n == 2, "pick fuera de ventana se agrega");

    r = PickBuffer_AddOrReplace(&b, &p4);
    CHECK(r == 1 && b.n == 3, "otra estacion se agrega");

    /* TTL: con now=1200, p3 (1050) expira si ttl=100 -> 1200-1050=150>100 */
    PickBuffer_Prune(&b, 1200.0, 100.0);
    CHECK(b.n == 0, "TTL elimina todos los picks viejos");

    /* snapshot con ventana */
    {
        Pick x1 = mk("X1", 1190.0), x2 = mk("X2", 1000.0);
        PickBuffer_AddOrReplace(&b, &x1);
        PickBuffer_AddOrReplace(&b, &x2);
    }
    {
        Pick out[8];
        int n = PickBuffer_Snapshot(&b, 1200.0, 30.0, out, 8);
        CHECK(n == 1 && strcmp(out[0].sta, "X1") == 0, "snapshot solo ventana");
    }

    PickBuffer_Free(&b);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_repick\n");
    return 0;
}
