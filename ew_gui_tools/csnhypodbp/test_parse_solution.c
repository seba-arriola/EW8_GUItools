/*
 * C6: ParseY2K_Solution extrae TODAS las fases (P y S) y la marca manual.
 * Construye un TYPE_HYP2000ARC minimo con el layout de csnloc.
 */
#include <stdio.h>
#include <string.h>
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
                        char phase, const char *time17)
{
    memset(buf + n, ' ', 114);
    setb(buf + n, 0, sta);
    setb(buf + n, 5, "C1");
    setb(buf + n, 9, chan);
    buf[n + 14] = phase;
    setb(buf + n, 17, time17);
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
    size_t n = 0;
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

    n = add_phase(buf, n, "BO03", "HHZ", 'P', "20260309163756.00");
    n = add_phase(buf, n, "GO01", "HHE", 'S', "20260309164016.00");

    /* Linea final vacia. */
    memset(buf + n, ' ', 114); buf[n + 114] = '\n'; n += 115;
    buf[n] = '\0';

    memset(&t, 0, sizeof(t));
    t.tm_year = 126; t.tm_mon = 2; t.tm_mday = 9;
    t.tm_hour = 16; t.tm_min = 37; t.tm_sec = 56;
    base = (double)timegm(&t);

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

    /* Sin byte de marca -> automatica. */
    buf[174] = ' ';
    CHECK(csnhypodbp_parse_solution(buf, strlen(buf), 162, &sol) == 0, "parsea auto");
    CHECK(sol.manual == 0, "sin marca -> automatica");

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_parse_solution\n");
    return 0;
}
