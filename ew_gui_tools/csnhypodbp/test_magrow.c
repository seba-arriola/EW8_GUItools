/*
 * Test headless del modelo de la tabla de magnitudes (ver
 * csnhypodbp_magrow.h): parser NSLC de la BD, emparejado por estacion, residual
 * contra la magnitud de red y formato de las celdas.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_magrow.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static void test_split_nslc(void)
{
    char net[8] = "", sta[8] = "", loc[8] = "", chan[8] = "";

    CHECK(csnhypodbp_magrow_split_nslc("C1.GO01..HHZ", net, sizeof(net), sta,
                                       sizeof(sta), loc, sizeof(loc), chan,
                                       sizeof(chan)) == 1,
          "nslc: 4 campos");
    CHECK(strcmp(net, "C1") == 0 && strcmp(sta, "GO01") == 0 &&
          strcmp(loc, "") == 0 && strcmp(chan, "HHZ") == 0,
          "nslc: NET.STA..CHAN");
    CHECK(csnhypodbp_magrow_split_nslc("C1.GO01.00.BHZ", net, sizeof(net), sta,
                                       sizeof(sta), loc, sizeof(loc), chan,
                                       sizeof(chan)) == 1 &&
          strcmp(loc, "00") == 0 && strcmp(chan, "BHZ") == 0,
          "nslc: con location real");

    CHECK(csnhypodbp_magrow_split_nslc("GO01.HHZ", net, sizeof(net), sta,
                                       sizeof(sta), loc, sizeof(loc), chan,
                                       sizeof(chan)) == 0 && net[0] == '\0' &&
          sta[0] == '\0' && chan[0] == '\0',
          "nslc: menos de 4 campos -> 0 y vacios");
    CHECK(csnhypodbp_magrow_split_nslc("", net, sizeof(net), sta, sizeof(sta),
                                       loc, sizeof(loc), chan, sizeof(chan)) == 0,
          "nslc: cadena vacia -> 0");
    CHECK(csnhypodbp_magrow_split_nslc(NULL, net, sizeof(net), sta, sizeof(sta),
                                       loc, sizeof(loc), chan, sizeof(chan)) == 0,
          "nslc: NULL -> 0");
}

static void test_build(void)
{
    CsnhypodbpStaRow stas[3];
    CsnhypodbpMagIn vals[3];
    CsnhypodbpMagRow rows[8];
    char txt[32];
    int n;

    memset(stas, 0, sizeof(stas));
    memset(vals, 0, sizeof(vals));
    snprintf(stas[0].sta, sizeof(stas[0].sta), "GO01"); stas[0].dist_km = 35.0;
    snprintf(stas[1].sta, sizeof(stas[1].sta), "GO02"); stas[1].dist_km = 80.5;
    snprintf(stas[2].sta, sizeof(stas[2].sta), "GO03"); stas[2].dist_km = 120.0;
    /* GO01 y GO02 con traza; GO03 sin ella (no se puede forzar). */
    stas[0].has_data = 1;
    stas[1].has_data = 1;
    /* Ancla = P efectiva de la estacion (aqui manual y automatica). */
    stas[0].p.present = 1; stas[0].p.origin = 'M'; stas[0].p.t = 1000.0;
    stas[1].p.present = 1; stas[1].p.origin = 'A'; stas[1].p.t = 1001.0;

    snprintf(vals[0].sta, sizeof(vals[0].sta), "GO02");
    snprintf(vals[0].chan, sizeof(vals[0].chan), "HHE");
    vals[0].mag = 5.25; vals[0].amp = 1234.0; vals[0].period = 0.9;
    /* Este valor es de una estacion que NO esta en la lista: se ignora. */
    snprintf(vals[1].sta, sizeof(vals[1].sta), "ZZ99");
    vals[1].mag = 6.0;
    /* Y este es de GO01, pero sin dato: la fila queda con guion. */
    snprintf(vals[2].sta, sizeof(vals[2].sta), "GO01");
    snprintf(vals[2].chan, sizeof(vals[2].chan), "HHZ");
    vals[2].mag = NAN;

    n = csnhypodbp_magrow_build(rows, 8, stas, 3, vals, 3, 5.00, 10.0, 45.0);
    CHECK(n == 3, "build: una fila por estacion");
    CHECK(strcmp(rows[0].sta, "GO01") == 0 && strcmp(rows[1].sta, "GO02") == 0 &&
          strcmp(rows[2].sta, "GO03") == 0, "build: conserva el orden");
    CHECK(fabs(rows[1].dist_km - 80.5) < 1e-12, "build: trae la distancia");
    CHECK(fabs(rows[1].mag - 5.25) < 1e-12 && fabs(rows[1].res - 0.25) < 1e-9,
          "build: valor y residual contra la red");
    CHECK(strcmp(rows[1].chan, "HHE") == 0, "build: canal de la magnitud");
    CHECK(rows[1].used == 1, "build: la fila entra al calculo por defecto");
    CHECK(rows[0].has_data == 1 && rows[2].has_data == 0,
          "build: marca las estaciones con traza");
    CHECK(rows[2].used == 0, "build: sin traza no entra al calculo");
    CHECK(rows[0].anchor == 'M' && fabs(rows[0].anchor_t - 1000.0) < 1e-9,
          "build: ancla = P efectiva (manual)");
    CHECK(strcmp(csnhypodbp_magrow_anchor_text(&rows[0]), "M") == 0 &&
          strcmp(csnhypodbp_magrow_anchor_text(&rows[1]), "A") == 0 &&
          strcmp(csnhypodbp_magrow_anchor_text(&rows[2]), "-") == 0,
          "build: texto del ancla (M/A/-)");
    csnhypodbp_magrow_t0_text(&rows[0], txt, sizeof(txt));
    CHECK(strcmp(txt, "+10") == 0, "celda t0: offset con signo");
    csnhypodbp_magrow_t1_text(&rows[0], txt, sizeof(txt));
    CHECK(strcmp(txt, "+45") == 0, "celda t1: offset con signo");

    csnhypodbp_magrow_mag_text(&rows[1], txt, sizeof(txt));
    CHECK(strcmp(txt, "5.25") == 0, "celda mag: %.2f");
    csnhypodbp_magrow_res_text(&rows[1], txt, sizeof(txt));
    CHECK(strcmp(txt, "+0.25") == 0, "celda res: %+.2f");
    CHECK(strcmp(csnhypodbp_magrow_stat_text(&rows[1]), "A") == 0,
          "stat: automatica con dato");

    csnhypodbp_magrow_mag_text(&rows[0], txt, sizeof(txt));
    CHECK(strcmp(txt, "-") == 0, "celda mag sin dato: -");
    csnhypodbp_magrow_res_text(&rows[0], txt, sizeof(txt));
    CHECK(strcmp(txt, "-") == 0, "celda res sin dato: -");
    CHECK(strcmp(csnhypodbp_magrow_stat_text(&rows[0]), "-") == 0,
          "stat sin dato: -");

    csnhypodbp_magrow_mag_text(&rows[2], txt, sizeof(txt));
    CHECK(strcmp(txt, "-") == 0 && strcmp(csnhypodbp_magrow_stat_text(&rows[2]), "-") == 0,
          "estacion sin valor: guiones");

    /* Marca manual y residual negativo. */
    rows[1].manual = 1;
    rows[1].res = -1.2;
    CHECK(strcmp(csnhypodbp_magrow_stat_text(&rows[1]), "M") == 0,
          "stat: manual -> M");
    csnhypodbp_magrow_res_text(&rows[1], txt, sizeof(txt));
    CHECK(strcmp(txt, "-1.20") == 0, "celda res negativa");

    /* Sin magnitud de red no hay residual (aunque la estacion tenga valor). */
    n = csnhypodbp_magrow_build(rows, 8, stas, 3, vals, 3, NAN, 10.0, 45.0);
    CHECK(n == 3 && !isfinite(rows[1].res) && isfinite(rows[1].mag),
          "build: sin magnitud de red el residual es NaN");
    CHECK(strcmp(csnhypodbp_magrow_stat_text(&rows[1]), "A") == 0,
          "stat con dato y sin residual: A");

    /* Repetidos: gana el primero. */
    vals[2].mag = 4.0;
    n = csnhypodbp_magrow_build(rows, 8, stas, 3, vals, 3, 5.0, 10.0, 45.0);
    CHECK(n == 3 && fabs(rows[0].mag - 4.0) < 1e-12,
          "build: valor repetido por estacion, gana el primero");

    /* Sin P efectiva no hay ancla, y sin ventana las celdas van con guion. */
    stas[1].p.present = 0;
    n = csnhypodbp_magrow_build(rows, 8, stas, 3, vals, 3, 5.0, NAN, NAN);
    CHECK(n == 3 && rows[1].anchor == '\0' &&
          strcmp(csnhypodbp_magrow_anchor_text(&rows[1]), "-") == 0,
          "build: sin P no hay ancla");
    csnhypodbp_magrow_t0_text(&rows[1], txt, sizeof(txt));
    CHECK(strcmp(txt, "-") == 0, "celda t0 sin ventana: -");
    stas[1].p.present = 1; stas[1].p.origin = 'A'; stas[1].p.t = 1001.0;

    /* Limites y nulos. */
    CHECK(csnhypodbp_magrow_build(rows, 0, stas, 3, vals, 3, 5.0, 10.0, 45.0) == 0,
          "build: max 0 -> 0 filas");
    CHECK(csnhypodbp_magrow_build(NULL, 8, stas, 3, vals, 3, 5.0, 10.0, 45.0) == 0,
          "build: salida NULL -> 0 filas");
    CHECK(csnhypodbp_magrow_build(rows, 8, stas, 3, NULL, 0, 5.0, 10.0, 45.0) == 3,
          "build: sin valores -> filas vacias");
    CHECK(csnhypodbp_magrow_build(rows, 2, stas, 3, vals, 3, 5.0, 10.0, 45.0) == 2,
          "build: respeta el tope");
}

static void test_from_db(void)
{
    CsnhypodbpMagDbRow rows[3];
    CsnhypodbpMagIn out[8];
    int n;

    rows[0].channel = "C1.GO01..HHE";
    rows[0].read_amplitude = 1200.0;
    rows[0].calculated_magnitude = 5.1;
    rows[1].channel = "C1.GO02.00.HHZ";
    rows[1].read_amplitude = 0.0;            /* sin amplitud en la BD */
    rows[1].calculated_magnitude = 4.8;
    rows[2].channel = "GO03.HHZ";            /* NSLC incompleto: se descarta */
    rows[2].read_amplitude = 900.0;
    rows[2].calculated_magnitude = 5.5;

    n = csnhypodbp_magrow_from_db(out, 8, rows, 3);
    CHECK(n == 2, "from_db: descarta el NSLC incompleto");
    CHECK(strcmp(out[0].sta, "GO01") == 0 && strcmp(out[0].chan, "HHE") == 0 &&
          fabs(out[0].mag - 5.1) < 1e-9 && fabs(out[0].amp - 1200.0) < 1e-9,
          "from_db: estacion, canal, magnitud y amplitud");
    CHECK(strcmp(out[1].sta, "GO02") == 0 && !isfinite(out[1].amp),
          "from_db: amplitud 0 -> sin dato");
    CHECK(!isfinite(out[1].period), "from_db: la BD no guarda periodo");

    /* Magnitud 0 en la BD = sin dato (la capa de BD no distingue NULL). */
    rows[1].calculated_magnitude = 0.0;
    n = csnhypodbp_magrow_from_db(out, 8, rows, 3);
    CHECK(n == 2 && !isfinite(out[1].mag), "from_db: magnitud 0 -> sin dato");

    CHECK(csnhypodbp_magrow_from_db(out, 0, rows, 3) == 0, "from_db: max 0 -> 0");
    CHECK(csnhypodbp_magrow_from_db(NULL, 8, rows, 3) == 0, "from_db: salida NULL -> 0");
    CHECK(csnhypodbp_magrow_from_db(out, 8, NULL, 3) == 0, "from_db: filas NULL -> 0");
}

static void test_gate_rows(void)
{
    CsnhypodbpMagRow rows[3];
    CsnhypodbpStaRow stas[3];
    CsnhypodbpMagIn vals[1];
    int cd = -1, fu = -1, fi = -1, us = -1;

    memset(rows, 0, sizeof(rows));
    memset(stas, 0, sizeof(stas));
    memset(vals, 0, sizeof(vals));
    snprintf(stas[0].sta, sizeof(stas[0].sta), "GO01");
    snprintf(stas[1].sta, sizeof(stas[1].sta), "GO02");
    snprintf(stas[2].sta, sizeof(stas[2].sta), "GO03");
    stas[0].dist_deg = 1.5;  stas[0].dist_km = 166.8;  stas[0].has_data = 1;
    stas[1].dist_deg = 4.5;  stas[1].dist_km = 500.3;  stas[1].has_data = 1;
    stas[2].dist_deg = 30.0; stas[2].dist_km = 3335.7; stas[2].has_data = 0;
    snprintf(vals[0].sta, sizeof(vals[0].sta), "GO01");
    vals[0].mag = 5.0;

    CHECK(csnhypodbp_magrow_build(rows, 3, stas, 3, vals, 1, 5.0, 10.0, 45.0) == 3,
          "gate: construye las 3 filas");
    CHECK(fabs(rows[2].dist_deg - 30.0) < 1e-9 && fabs(rows[0].dist_km - 166.8) < 1e-9,
          "gate: la distancia en grados llega a la fila");
    CHECK(rows[0].in_range == 1 && rows[0].eligible == 1,
          "gate: por defecto entra (la puerta la aplica el llamador)");

    /* Lo que hace rebuild_mag_list: origen de la ventana, puerta y filtro. */
    rows[0].win_src = 'M';   rows[0].eligible = 0; rows[0].used = 0;
    rows[1].win_src = 'A';   rows[1].in_range = 0; rows[1].used = 0;
    rows[2].win_src = 'T';
    CHECK(strcmp(csnhypodbp_magrow_vent_text(&rows[0]), "M") == 0 &&
          strcmp(csnhypodbp_magrow_vent_text(&rows[1]), "A") == 0 &&
          strcmp(csnhypodbp_magrow_vent_text(&rows[2]), "Teo") == 0,
          "gate: texto del origen de la ventana");
    CHECK(strcmp(csnhypodbp_magrow_vent_text(NULL), "-") == 0,
          "gate: origen sin fila -> guion");

    csnhypodbp_magrow_counts(rows, 3, &cd, &fu, &fi, &us);
    CHECK(cd == 1, "gate: cuenta las filas con dato");
    CHECK(fu == 1, "gate: cuenta las descartadas por distancia");
    CHECK(fi == 1, "gate: cuenta las filtradas por A/M/Teo");
    CHECK(us == 0, "gate: cuenta las marcadas");
    csnhypodbp_magrow_counts(rows, 3, NULL, NULL, NULL, NULL);
    csnhypodbp_magrow_counts(NULL, 3, &cd, &fu, &fi, &us);
    CHECK(cd == 0 && fu == 0 && fi == 0 && us == 0,
          "gate: sin filas los contadores quedan en 0");
}

int main(void)
{
    test_split_nslc();
    test_build();
    test_from_db();
    test_gate_rows();
    if (fails) { printf("%d FALLOS\n", fails); return 1; }
    printf("OK test_magrow\n");
    return 0;
}
