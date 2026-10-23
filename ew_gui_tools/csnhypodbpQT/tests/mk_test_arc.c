/*
 * mk_test_arc.c - Genera un TYPE_HYP2000ARC valido a partir de una solucion
 * sintetica (usa el MISMO csnhypodbp_arc_build que la GUI GTK).
 *
 * Sirve de fixture para los tests del GUI y para demos sin EarthWorm:
 *   mk_test_arc /tmp/test.arc
 *   BPQ_TEST_ARC=/tmp/test.arc BPQ_MODE=--dump csnhypodbpQT csnhypodbp.d
 *
 * Uso: mk_test_arc [archivo]      (sin argumento: stdout)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"

int main(int argc, char **argv)
{
    CsnhypodbpSolution s;
    char arc[65536];
    FILE *f = stdout;
    int i;

    memset(&s, 0, sizeof(s));
    s.qid     = 100;
    s.version = 1;
    s.mod     = 163;
    s.otime   = 1700000000.0;
    s.lat     = -20.123;
    s.lon     = -70.456;
    s.depth   = 33.4;
    s.rms     = 0.2;
    s.nph     = 12;
    s.gap     = 180;
    s.manual  = 0;
    s.npicks  = 6;
    for (i = 0; i < s.npicks; i++) {
        snprintf(s.picks[i].sta, sizeof(s.picks[i].sta), "PB%02d", i);
        snprintf(s.picks[i].chan, sizeof(s.picks[i].chan), "HHZ");
        snprintf(s.picks[i].net, sizeof(s.picks[i].net), "CX");
        s.picks[i].phase    = (i < 4) ? 'P' : 'S';
        s.picks[i].t_pick   = 1700000000.0 + 10.0 * i;
        s.picks[i].weight   = 0.5;
        s.picks[i].residual = 0.01 * i;
    }

    if (csnhypodbp_arc_build(&s, 0, arc, sizeof(arc)) != 0) {
        fprintf(stderr, "mk_test_arc: csnhypodbp_arc_build fallo\n");
        return 1;
    }

    if (argc > 1) {
        f = fopen(argv[1], "w");
        if (!f) {
            fprintf(stderr, "mk_test_arc: no puedo escribir %s\n", argv[1]);
            return 1;
        }
    }
    fputs(arc, f);
    if (f != stdout) fclose(f);
    return 0;
}
