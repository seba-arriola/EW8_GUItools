/*
 * locate_chain.c - arnes MANUAL (sin GTK) del boton `locate`.
 *
 * Corre la cadena offline COMPLETA (picks -> csnloc -> refinador -> magnitud)
 * con los mismos parametros que la GUI, partiendo de un ARC de entrada, y NO
 * borra el directorio temporal: quedan a la vista picks.txt, event.arc,
 * csnloc.err y ref.err para diagnosticar por que falla.
 *
 *   make -C ew_gui_tools/csnhypodbp chain
 *   ./locate_chain <arc_entrada> [refinador] [tmp_root]
 *
 * Toma EW_PARAMS / EW_HOME / EW_VERSION del entorno (igual que la GUI). No forma
 * parte de `make test`: necesita resources/ y los binarios instalados.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "csnhypodbp_locate.h"
#include "csnhypodbp_locator.h"

#define CHAIN_MAX_LOC 8

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long sz;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return NULL; }
    *n = fread(b, 1, (size_t)sz, f);
    b[*n] = '\0';
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    const char *arc_path, *refiner, *tmp_root;
    const char *ewp = getenv("EW_PARAMS");
    const char *ewh = getenv("EW_HOME");
    const char *ewv = getenv("EW_VERSION");
    CsnhypodbpLocator loc[CHAIN_MAX_LOC];
    CsnhypodbpSolution base, out;
    CsnhypodbpMagOut mag;
    char err[1024];
    char *arc;
    size_t n = 0;
    int nloc = 0, rc;

    if (argc < 2) {
        fprintf(stderr, "uso: %s <arc_entrada> [refinador] [tmp_root]\n", argv[0]);
        return 2;
    }
    arc_path = argv[1];
    refiner  = (argc > 2) ? argv[2] : "hyp2000_ring";
    tmp_root = (argc > 3) ? argv[3] : "/tmp/opencode/locate_chain";
    if (!ewp || !ewh || !ewv) {
        fprintf(stderr, "faltan EW_PARAMS / EW_HOME / EW_VERSION en el entorno\n");
        return 2;
    }
    mkdir(tmp_root, 0755);   /* mkdtemp necesita que el padre exista (EEXIST ok) */

    arc = slurp(arc_path, &n);
    if (!arc) { fprintf(stderr, "no leo <%s>\n", arc_path); return 2; }
    printf("== ARC %s (%zu bytes)\n", arc_path, n);

    memset(&base, 0, sizeof(base));
    if (csnhypodbp_parse_solution(arc, n, 0, &base) != 0) {
        fprintf(stderr, "no parseo el ARC de entrada\n");
        free(arc);
        return 2;
    }
    printf("== base: qid=%d ver=%u nph=%d npicks=%d lat=%.4f lon=%.4f depth=%.1f rms=%.2f\n",
           base.qid, base.version, base.nph, base.npicks, base.lat, base.lon,
           base.depth, base.rms);

    csnhypodbp_locator_add(loc, &nloc, CHAIN_MAX_LOC, "csnloc",       "csnloc",       "csnloc.d");
    csnhypodbp_locator_add(loc, &nloc, CHAIN_MAX_LOC, "hyp2000_ring", "hyp2000_ring", "hyp2000_ring.d");
    csnhypodbp_locator_add(loc, &nloc, CHAIN_MAX_LOC, "nlloc_ring",   "nlloc_ring",   "nlloc_ring.d");

    csnhypodbp_locate_keep_tmp(1);
    memset(&out, 0, sizeof(out));
    memset(&mag, 0, sizeof(mag));
    memset(err, 0, sizeof(err));
    rc = csnhypodbp_locate_run(loc, nloc, refiner, ewp, ewh, ewv, tmp_root,
                               &base, NULL, 0, NULL, 0, NULL, 0, 0u,
                               &out, &mag, err, sizeof(err));
    printf("== locate_run rc=%d\n   err: %s\n", rc, err);
    printf("== out: qid=%d ver=%u nph=%d npicks=%d lat=%.4f lon=%.4f depth=%.1f rms=%.3f\n",
           out.qid, out.version, out.nph, out.npicks, out.lat, out.lon, out.depth, out.rms);
    /* SCNL de las primeras picadas: sin canal, HYPOINVERSE rechaza la fase. */
    {
        int pi;
        for (pi = 0; pi < out.npicks && pi < 5; pi++)
            printf("== out.pick[%d]: %s.%s.%s.%s %c\n", pi, out.picks[pi].sta,
                   out.picks[pi].chan, out.picks[pi].net, out.picks[pi].loc,
                   out.picks[pi].phase);
    }
    printf("== mag: ml='%s' mwp='%s' mb='%s' ms='%s'\n", mag.ml, mag.mwp, mag.mb, mag.ms);
    free(arc);
    return rc ? 1 : 0;
}
