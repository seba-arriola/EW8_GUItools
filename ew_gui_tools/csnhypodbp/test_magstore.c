/*
 * Test headless del almacen de magnitudes por SOLUCION (qid, modulo): buffer
 * cuando la solucion aun no existe, "toma unica" al crear la fila, y distincion
 * de soluciones que comparten qid (csnloc 162 / hyp2000 163 / nlloc 164).
 */
#include <stdio.h>
#include <string.h>
#include "csnhypodbp_group.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

int main(void)
{
    CsnhypodbpMagStore s;
    char out[CSNHYPODBP_MAG_SLOTS][32];

    csnhypodbp_mag_store_init(&s);

    CHECK(csnhypodbp_mag_store_take(&s, 42, 162, out) == 0, "store vacio -> nada");

    /* Llegan magnitudes antes que la solucion: ML (slot 0) y Ms (slot 3). */
    csnhypodbp_mag_store_put(&s, 42, 162, 0, "5.2-10");
    csnhypodbp_mag_store_put(&s, 42, 162, 3, "4.1-8");

    CHECK(csnhypodbp_mag_store_take(&s, 42, 162, out) == 1, "take de (42,162)");
    CHECK(strcmp(out[0], "5.2-10") == 0, "slot 0 = ML");
    CHECK(strcmp(out[3], "4.1-8") == 0, "slot 3 = Ms");
    CHECK(out[1][0] == '\0' && out[2][0] == '\0', "slots no seteados vacios");
    CHECK(csnhypodbp_mag_store_take(&s, 42, 162, out) == 0, "se toma una sola vez");

    /* AC3: mismo qid, distinto modulo -> entradas independientes. */
    csnhypodbp_mag_store_put(&s, 42, 162, 0, "5.2-10");
    csnhypodbp_mag_store_put(&s, 42, 163, 0, "6.1-18");
    CHECK(csnhypodbp_mag_store_take(&s, 42, 162, out) == 1, "take (42,162)");
    CHECK(strcmp(out[0], "5.2-10") == 0, "crudo 162 conserva su ML");
    CHECK(csnhypodbp_mag_store_take(&s, 42, 163, out) == 1, "take (42,163)");
    CHECK(strcmp(out[0], "6.1-18") == 0, "refinado 163 tiene su propio valor y n de estaciones");
    CHECK(csnhypodbp_mag_store_take(&s, 42, 163, out) == 0, "163 se toma una sola vez");

    /* Otro evento, slot Mwp (1). */
    csnhypodbp_mag_store_put(&s, 7, 164, 1, "6.0-3");
    CHECK(csnhypodbp_mag_store_take(&s, 7, 164, out) == 1, "take (7,164)");
    CHECK(strcmp(out[1], "6.0-3") == 0, "slot 1 = Mwp");
    CHECK(out[0][0] == '\0', "qid 7 sin ML");

    /* Slot invalido se ignora. */
    csnhypodbp_mag_store_put(&s, 99, 162, 9, "x");
    CHECK(csnhypodbp_mag_store_take(&s, 99, 162, out) == 0, "slot invalido ignorado");

    /* AC4: correspondencia magnitud <-> solucion. */
    CHECK(csnhypodbp_mag_match(42, 163, 42, 163) == 1, "match exacto (qid,mod)");
    CHECK(csnhypodbp_mag_match(42, 163, 42, 162) == 0, "mismo qid distinto mod -> NO");
    CHECK(csnhypodbp_mag_match(42, 162, 43, 162) == 0, "distinto qid -> NO");
    CHECK(csnhypodbp_mag_match(42, 0, 42, 163) == 1, "mag sin mod (legacy) -> cualquier solucion del qid");
    CHECK(csnhypodbp_mag_match(42, 0, 43, 163) == 0, "legacy pero distinto qid -> NO");
    CHECK(csnhypodbp_mag_match(42, 162, 42, 0) == 1, "solucion recuperada (mod 0) -> acepta cualquier mod del qid");

    /* AC5: sin magnitud, take devuelve 0 (la fila conserva su "-" por defecto). */
    CHECK(csnhypodbp_mag_store_take(&s, 1234, 162, out) == 0, "solucion sin magnitud -> 0");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_magstore\n");
    return 0;
}
