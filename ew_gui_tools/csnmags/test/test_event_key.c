/*
 * test_event_key - test headless de la clave de evento de magnitud.
 *
 * Regresion de "una magnitud por qid": dos soluciones del mismo sismo
 * (mismo qid) pero de modulos distintos (csnloc crudo 162 / hyp2000 163 /
 * nlloc 164) deben ser eventos DISTINTOS para csnmags.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mag_key.h"

int main(void)
{
    char a[32], b[32], c[32];

    mag_key(a, sizeof(a), "8210600000", 162);
    mag_key(b, sizeof(b), "8210600000", 163);
    mag_key(c, sizeof(c), "8210600000", 162);

    /* Mismo qid, distinto modulo => claves distintas. */
    assert(strcmp(a, b) != 0);
    /* Misma solucion => misma clave. */
    assert(strcmp(a, c) == 0);
    /* La clave NO es el qid desnudo. */
    assert(strcmp(a, "8210600000") != 0);
    /* Distinto qid, mismo modulo => claves distintas. */
    mag_key(c, sizeof(c), "8210600001", 162);
    assert(strcmp(a, c) != 0);

    printf("test_event_key OK: %s != %s\n", a, b);
    return 0;
}
