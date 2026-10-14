/*
 * C6: ParseY2K_Solution extrae TODAS las fases (P y S) y la marca manual.
 * Construye un TYPE_HYP2000ARC minimo con el layout de csnloc.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "csnhypodbp_group.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

static void setb(char *b, int off, const char *s)
{
    memcpy(b + off, s, strlen(s));
}

static size_t add_phase(char *buf, size_t n, const char *sta, const char *chan,
                        char phase, const char *time17, const char *resid4)
{
    memset(buf + n, ' ', 114);
    setb(buf + n, 0, sta);
    setb(buf + n, 5, "C1");
    setb(buf + n, 9, chan);
    buf[n + 14] = phase;
    setb(buf + n, 17, time17);
    if (resid4) setb(buf + n, (phase == 'S') ? 50 : 34, resid4);
    buf[n + 114] = '\n';
    n += 115;
    /* Linea de continuacion de la fase. */
    memset(buf + n, ' ', 114);
    setb(buf + n, 0, sta);
    setb(buf + n, 5, "C1");
    setb(buf + n, 9, chan);
    buf[n + 104] = '$'; buf[n + 105] = '1';
    buf[n + 114] = '\n';
    n += 115;
    return n;
}

int main(void)
{
    char buf[4096];
    CsnhypodbpSolution sol;
    size_t n = 0, p0;
    struct tm t;
    double base;
    int i;

    memset(buf, ' ', sizeof(buf));
    setb(buf, 0, "20260309163736"); setb(buf, 14, "00");
    setb(buf, 16, "00"); buf[18] = 'N'; setb(buf, 19, "0000");
    setb(buf, 23, "070"); buf[26] = 'W'; setb(buf, 27, "0000");
    setb(buf, 31, "00333");
    setb(buf, 39, "  2"); setb(buf, 42, "120"); setb(buf, 45, " 50");
    setb(buf, 48, "  50");
    setb(buf, 136, "0000000042");
    buf[161] = '3';
    buf[174] = 'M';
    setb(buf, 178, "0003");
    buf[197] = '\n';
    n = 198;

    /* $1 (cabecera) */
    memset(buf + n, ' ', 197);
    buf[n] = '$'; buf[n + 1] = '1'; buf[n + 197] = '\n';
    n += 198;

    p0 = n;
    n = add_phase(buf, n, "BO03", "HHZ", 'P', "20260309163756.00", "  12");
    n = add_phase(buf, n, "GO01", "HHE", 'S', "20260309164016.00", " -05");

    /* Linea final vacia. */
    memset(buf + n, ' ', 114); buf[n + 114] = '\n'; n += 115;
    buf[n] = '\0';

    memset(&t, 0, sizeof(t));
    t.tm_year = 126; t.tm_mon = 2; t.tm_mday = 9;
    t.tm_hour = 16; t.tm_min = 37; t.tm_sec = 56;
    base = (double)timegm(&t);

    /* Mensaje CORTO (162 <= len < 182): la version de solucion vive en 178..181 y
       antes se leia con un memcpy aunque no cupiera en `len`. El buffer va en el
       heap con el tamano EXACTO: con el `buf` de 4096 de arriba la lectura queda
       dentro de la asignacion y valgrind no la ve. */
    {
        char *corto = malloc(170);
        CsnhypodbpSolution s2;

        CHECK(corto != NULL, "malloc del mensaje corto");
        if (corto) {
            memcpy(corto, buf, 170);
            memset(&s2, 0, sizeof s2);
            csnhypodbp_parse_solution(corto, 170, 163, &s2);
            CHECK(s2.version == 3,
                  "mensaje corto: cae al byte heredado (version 3)");
            free(corto);
        }
    }

    CHECK(csnhypodbp_parse_solution(buf, strlen(buf), 163, &sol) == 0, "parsea");
    CHECK(sol.qid == 42, "qid=42");
    CHECK(sol.version == 3, "version=3");
    CHECK(sol.manual == 1, "marca manual leida");
    CHECK(sol.nph == 2, "nph=2");
    CHECK(sol.npicks == 2, "2 fases (P y S)");
    CHECK(sol.picks[0].sta[0] && strcmp(sol.picks[0].sta, "BO03") == 0,
          "fase 0 BO03");
    CHECK(sol.picks[0].phase == 'P', "fase 0 P");
    CHECK(sol.picks[1].phase == 'S', "fase 1 S");
    CHECK(fabs(sol.picks[0].t_pick - base) < 0.01, "tiempo P correcto");
    for (i = 0; i < sol.npicks; i++)
        CHECK(sol.picks[i].t_pick > 0.0, "tiempo valido");

    /* Residual Y2K: P en [34,37], S en [50,53] (entero x100). */
    CHECK(fabs(sol.picks[0].residual - 0.12) < 1e-6, "residual P 0.12");
    CHECK(fabs(sol.picks[1].residual - (-0.05)) < 1e-6, "residual S -0.05");

    /* Residual en blanco (como el ARC de csnloc) -> NAN. */
    memset(buf + p0 + 34, ' ', 4);
    CHECK(csnhypodbp_parse_solution(buf, strlen(buf), 162, &sol) == 0, "parsea sin residual");
    CHECK(isnan(sol.picks[0].residual), "residual en blanco -> NAN");

    /* Sin byte de marca -> automatica. */
    buf[174] = ' ';
    CHECK(csnhypodbp_parse_solution(buf, strlen(buf), 162, &sol) == 0, "parsea auto");
    CHECK(sol.manual == 0, "sin marca -> automatica");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_parse_solution\n");
    return 0;
}
