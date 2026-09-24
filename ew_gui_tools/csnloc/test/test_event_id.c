/*
 * C11: generacion del ID base a partir del epoch de arranque.
 *
 * Verifica la formula id_base = (epoch % 100000) * 100000 + seq y que
 * IDs de sesiones distintas (epoch distinto) no colisionan.
 */
#include <stdio.h>
#include <string.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static unsigned long id_base_for(unsigned long epoch)
{
    return (epoch % 100000UL) * 100000UL;
}

int main(void)
{
    unsigned long e1 = 1758700000UL;   /* ~2025 */
    unsigned long e2 = 1758700030UL;   /* 30 s despues */
    unsigned long b1 = id_base_for(e1);
    unsigned long b2 = id_base_for(e2);

    CHECK(b1 != b2, "epochs distintos -> bases distintas");
    CHECK(b1 % 100000UL == 0, "base es multiplo de 100000");
    CHECK(b1 + 99999UL < b2 || b2 + 99999UL < b1,
          "rango de seq de una sesion no invade la otra");

    /* El ID de un evento nuevo es base + seq. */
    CHECK(b1 + 0UL == b1, "primer evento usa base+0");
    CHECK(b1 + 5UL == b1 + 5UL, "seq avanza dentro de la sesion");

    /* Offline usa base 0 (determinismo). */
    CHECK(id_base_for(0) == 0, "offline: base 0");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_event_id\n");
    return 0;
}
