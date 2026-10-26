/*
 * test_locsearch.c - Override de la linea LOCSEARCH de NLLoc (puro, sin motor).
 *
 * La plantilla 3D de /mnt/d trae OCT 96 48 6 0.05 50000 10000 4 0: 27.648
 * celdas iniciales y stop=0, o sea minutos por evento con 9-20 fases. El
 * override debe reemplazar SOLO la primera linea y dejar que FixedDepthKm
 * siga forzando init_num_cells_z=1.
 */
#include <stdio.h>
#include <string.h>

#include "../nlloc_depth.h"

static int checks = 0, fail = 0;

#define CK(c) do {                                                         \
    checks++;                                                              \
    if (!(c)) {                                                            \
        fail++;                                                            \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #c);      \
    }                                                                      \
} while (0)

int main(void)
{
    char out[512], fixed[512];
    const char *tmpl = "LOCSEARCH OCT 96 48 6 0.05 50000 10000 4 0\n";
    const char *spec = "OCT 24 24 6 1.5 20000 1000 0 1";

    CK(nlloc_locsearch_set(tmpl, spec, out, sizeof(out)) == 0);
    CK(strcmp(out, "LOCSEARCH OCT 24 24 6 1.5 20000 1000 0 1\n") == 0);

    /* Solo una linea LOCSEARCH; una linea que no lo es no se toca. */
    CK(nlloc_locsearch_set("LOCGRID 1 1 1 0 0 0 1 1 1\n", spec,
                           out, sizeof(out)) == -1);
    CK(nlloc_locsearch_set(tmpl, "", out, sizeof(out)) == -1);
    CK(nlloc_locsearch_set(tmpl, "OCT 24 24 6\n", out, sizeof(out)) == -1);
    CK(nlloc_locsearch_set(tmpl, "OCT 24\n", out, sizeof(out)) == -1);

    /* FixedDepthKm despues del override: nz=1 conservando la busqueda nueva. */
    CK(nlloc_locsearch_set(tmpl, spec, out, sizeof(out)) == 0);
    CK(nlloc_locsearch_fixz(out, fixed, sizeof(fixed)) == 0);
    CK(strcmp(fixed, "LOCSEARCH OCT 24 24 1 1.5 20000 1000 0 1\n") == 0);

    /* La linea original sigue funcionando con fixz (compatibilidad). */
    CK(nlloc_locsearch_fixz(tmpl, fixed, sizeof(fixed)) == 0);
    CK(strcmp(fixed,
              "LOCSEARCH OCT 96 48 1 0.05 50000 10000 4 0\n") == 0);

    printf("== locsearch checks=%d fallas=%d ==\n", checks, fail);
    return fail ? 1 : 0;
}
