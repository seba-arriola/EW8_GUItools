/*
 * rebuild_arc.c - lee un ARC por stdin, lo parsea y lo reconstruye con el
 * builder canonico (csnhypodbp_arc_build).
 *
 * Es el puente que usa test_fixdepth.sh para comprobar que un ARC de csnloc
 * (layout real, con linea $1 y continuaciones) pasa por el builder y sigue
 * siendo digerible por HYPOINVERSE. Regresion del fallo 2026-10-27: sin la
 * linea $1 el motor leia la primera fase como resumen y abortaba con
 * "Sequential READ or WRITE not allowed after EOF marker" (hyphs.for:222).
 */
#include <stdio.h>
#include <string.h>

#include "../csnhypodbp/csnhypodbp_group.h"

int main(void)
{
    static char in[65536], out[65536];
    size_t n = fread(in, 1, sizeof(in) - 1, stdin);
    CsnhypodbpSolution sol;

    in[n] = '\0';
    if (csnhypodbp_parse_solution(in, n, 162, &sol) != 0) {
        fprintf(stderr, "rebuild_arc: no se pudo parsear el ARC de entrada\n");
        return 2;
    }
    if (csnhypodbp_arc_build(&sol, sol.manual, out, sizeof(out)) != 0) {
        fprintf(stderr, "rebuild_arc: arc_build fallo\n");
        return 3;
    }
    fputs(out, stdout);
    return 0;
}
