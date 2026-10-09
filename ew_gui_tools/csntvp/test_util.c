/* test_util.c — lógica pura de csntvp (parseo/etiqueta de picks y color hex). */
#include <stdio.h>
#include <string.h>

#include "csntvp_util.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("  [PASS] %s\n", name); } \
    else      { printf("  [FAIL] %s\n", name); failures++; } \
} while (0)

int main(void)
{
    CsntvpPickMsg m;
    char lab[16];

    /* 1. pick_FP: 10 tokens (sin fase ni origen) -> P / A */
    CHECK(csntvp_pick_parse("8 141 255 12 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0", &m) == 1,
          "pick_FP parsea");
    CHECK(m.phase == 'P' && m.origin == 'A', "pick_FP: fase P, origen A");
    CHECK(strcmp(m.sta, "BO03") == 0 && strcmp(m.chan, "HHZ") == 0 &&
          strcmp(m.net, "C1") == 0 && strcmp(m.loc, "--") == 0, "pick_FP: SCNL");

    /* 2. pickS: token 11 = S, token 12 = A */
    CHECK(csntvp_pick_parse("8 165 0 7 FAR1.HHE.C.-- ?1 20260309163736.490 1234 0 0 S A", &m) == 1,
          "pickS parsea");
    CHECK(m.phase == 'S' && m.origin == 'A', "pickS: fase S, origen A");

    /* 3. manual de csntvp: token 12 = M */
    CHECK(csntvp_pick_parse("8 162 255 55 BO03.HHZ.C1.-- ?0 20260309163736.490 0 0 0 P M", &m) == 1,
          "manual P parsea");
    CHECK(m.phase == 'P' && m.origin == 'M', "manual P: fase P, origen M");

    /* 4. manual S */
    CHECK(csntvp_pick_parse("8 162 255 56 FAR1.HHZ.C.-- ?0 20260309163736.490 0 0 0 S M", &m) == 1,
          "manual S parsea");
    CHECK(m.phase == 'S' && m.origin == 'M', "manual S: fase S, origen M");

    /* 5. etiquetas */
    csntvp_pick_label('P', 'A', lab, sizeof(lab)); CHECK(strcmp(lab, "P(A)") == 0, "label P(A)");
    csntvp_pick_label('P', 'M', lab, sizeof(lab)); CHECK(strcmp(lab, "P(M)") == 0, "label P(M)");
    csntvp_pick_label('S', 'A', lab, sizeof(lab)); CHECK(strcmp(lab, "S(A)") == 0, "label S(A)");
    csntvp_pick_label('S', 'M', lab, sizeof(lab)); CHECK(strcmp(lab, "S(M)") == 0, "label S(M)");

    /* 6. mensaje inválido */
    CHECK(csntvp_pick_parse("basura", &m) == 0, "mensaje inválido no parsea");

    /* 7. color hex */
    double rgb[3];
    CHECK(csntvp_parse_hexcolor("#FF0000", rgb) == 0 && rgb[0] > 0.99 && rgb[1] < 0.01 && rgb[2] < 0.01,
          "hex #FF0000 -> rojo");
    CHECK(csntvp_parse_hexcolor("000000", rgb) == 0 && rgb[0] < 0.01 && rgb[1] < 0.01 && rgb[2] < 0.01,
          "hex 000000 -> negro");
    CHECK(csntvp_parse_hexcolor("zzz", rgb) != 0, "hex inválido rechazado");
    CHECK(csntvp_parse_hexcolor("", rgb) != 0, "hex vacío rechazado");

    if (failures == 0) { printf("\nALL UTIL TESTS PASSED\n"); return 0; }
    printf("\n%d UTIL TEST(S) FAILED\n", failures);
    return 1;
}
