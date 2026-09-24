/*
 * C4: expiracion de eventos por TTL.
 *
 * Un evento sin actualizaciones durante EventTTLSec se elimina del registro.
 */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

int main(void)
{
    EventRegistry reg;

    EventRegistry_Init(&reg);

    reg.ev[0].id = 1000; reg.ev[0].last_update_epoch = 1000.0;
    reg.ev[1].id = 1001; reg.ev[1].last_update_epoch = 1200.0;
    reg.ev[2].id = 1002; reg.ev[2].last_update_epoch = 1400.0;
    reg.n = 3;

    /* now=1500, ttl=300: el evento 0 (1000) expira (500>300);
       el 1 (1200) queda justo (300 no es > 300); el 2 (1400) queda. */
    EventRegistry_Expire(&reg, 1500.0, 300.0);
    CHECK(reg.n == 2, "expira 1 evento");
    CHECK(reg.ev[0].id == 1001, "conserva el evento 1001");
    CHECK(reg.ev[1].id == 1002, "conserva el evento 1002");

    /* now=1600, ttl=300: el 1001 (1200) expira (400>300). */
    EventRegistry_Expire(&reg, 1600.0, 300.0);
    CHECK(reg.n == 1 && reg.ev[0].id == 1002, "expira el 1001");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_event_ttl\n");
    return 0;
}
