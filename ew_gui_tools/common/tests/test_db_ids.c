/*
 * Test headless del formato de `origin_id` (AC1): fila viva "<event>:<mod>" y
 * confirmacion manual "<event>:<mod>:<version>".
 */

#include <stdio.h>
#include <string.h>

#include "ewgui/db_ids.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

int main(void)
{
    static const char *EID = "E1234567890-20261020120000";
    char buf[128], eid[64];
    int mod, confirmed;
    unsigned version;

    /* --- fila viva: sin sufijo de version --- */
    CHECK(ewgui_origin_id_build(buf, sizeof buf, EID, 162, 0) == 0,
          "build viva");
    CHECK(strcmp(buf, "E1234567890-20261020120000:162") == 0, "build viva (texto)");
    CHECK(ewgui_origin_id(buf, sizeof buf, EID, 162) == 0, "origin_id == build v=0");
    CHECK(strcmp(buf, "E1234567890-20261020120000:162") == 0, "origin_id (texto)");

    mod = confirmed = -1;
    version = 99;
    CHECK(ewgui_origin_id_parse(buf, eid, sizeof eid, &mod, &version, &confirmed) == 0,
          "parse viva");
    CHECK(strcmp(eid, EID) == 0, "parse viva (event_id)");
    CHECK(mod == 162, "parse viva (mod)");
    CHECK(version == 0, "parse viva (version 0)");
    CHECK(confirmed == 0, "parse viva (confirmed 0)");

    /* --- confirmacion: con sufijo --- */
    CHECK(ewgui_origin_id_build(buf, sizeof buf, EID, 162, 7) == 0,
          "build confirmada");
    CHECK(strcmp(buf, "E1234567890-20261020120000:162:7") == 0,
          "build confirmada (texto)");
    CHECK(ewgui_origin_id_parse(buf, eid, sizeof eid, &mod, &version, &confirmed) == 0,
          "parse confirmada");
    CHECK(strcmp(eid, EID) == 0, "parse confirmada (event_id)");
    CHECK(mod == 162, "parse confirmada (mod)");
    CHECK(version == 7, "parse confirmada (version)");
    CHECK(confirmed == 1, "parse confirmada (confirmed 1)");

    /* ida y vuelta en los extremos del campo de version */
    CHECK(ewgui_origin_id_build(buf, sizeof buf, EID, 164, 1) == 0 &&
          ewgui_origin_id_parse(buf, eid, sizeof eid, &mod, &version, &confirmed) == 0 &&
          mod == 164 && version == 1 && confirmed == 1, "round-trip version 1");
    CHECK(ewgui_origin_id_build(buf, sizeof buf, EID, 164, 9999) == 0 &&
          ewgui_origin_id_parse(buf, eid, sizeof eid, &mod, &version, &confirmed) == 0 &&
          mod == 164 && version == 9999 && confirmed == 1, "round-trip version 9999");

    /* cero a la izquierda: el ARC escribe %04u */
    CHECK(ewgui_origin_id_parse("E1-20300101000000:162:0007", eid, sizeof eid,
                                &mod, &version, &confirmed) == 0 &&
          version == 7, "parse acepta %04u");

    /* --- rechazos de build --- */
    CHECK(ewgui_origin_id_build(buf, sizeof buf, EID, 162, 10000) == -1,
          "build rechaza version > 9999");
    CHECK(ewgui_origin_id_build(buf, sizeof buf, EID, 0, 0) == -1,
          "build rechaza mod 0");
    CHECK(ewgui_origin_id_build(buf, sizeof buf, "", 162, 0) == -1,
          "build rechaza event vacio");
    CHECK(ewgui_origin_id_build(buf, 8, EID, 162, 0) == -1,
          "build rechaza buffer corto");
    CHECK(ewgui_origin_id_build(NULL, sizeof buf, EID, 162, 0) == -1,
          "build rechaza out NULL");

    /* --- rechazos de parse --- */
    CHECK(ewgui_origin_id_parse("E1234567890", NULL, 0, &mod, &version,
                                &confirmed) == -1, "parse rechaza sin ':'");
    CHECK(ewgui_origin_id_parse(":162", NULL, 0, &mod, &version, &confirmed) == -1,
          "parse rechaza event vacio");
    CHECK(ewgui_origin_id_parse("E1:x", NULL, 0, &mod, &version, &confirmed) == -1,
          "parse rechaza mod no numerico");
    CHECK(ewgui_origin_id_parse("E1:0", NULL, 0, &mod, &version, &confirmed) == -1,
          "parse rechaza mod 0");
    CHECK(ewgui_origin_id_parse("E1:162:", NULL, 0, &mod, &version, &confirmed) == -1,
          "parse rechaza version vacia");
    CHECK(ewgui_origin_id_parse("E1:162:0", NULL, 0, &mod, &version, &confirmed) == -1,
          "parse rechaza version 0");
    CHECK(ewgui_origin_id_parse("E1:162:10000", NULL, 0, &mod, &version,
                                &confirmed) == -1, "parse rechaza version 10000");
    CHECK(ewgui_origin_id_parse("E1:162:7:1", NULL, 0, &mod, &version,
                                &confirmed) == -1, "parse rechaza 4 campos");
    CHECK(ewgui_origin_id_parse("E1:162:7", eid, 2, &mod, &version,
                                &confirmed) == -1, "parse rechaza buffer corto");

    /* --- los ids derivados siguen cabiendo en VARCHAR(64) --- */
    {
        char oid[64], pid[64], mid[64];

        CHECK(ewgui_origin_id_build(oid, sizeof oid, EID, 162, 9999) == 0 &&
              ewgui_pick_id(pid, sizeof pid, oid, "GO01", "P") == 0 &&
              ewgui_magnitude_id(mid, sizeof mid, oid, "Mwp") == 0,
              "pick_id/magnitude_id caben con origin_id confirmado");
        printf("   -> %s\n   -> %s\n   -> %s\n", oid, pid, mid);
    }

    if (fails) {
        printf("FALLARON %d comprobaciones\n", fails);
        return 1;
    }
    printf("ALL DB IDS TESTS PASSED\n");
    return 0;
}
