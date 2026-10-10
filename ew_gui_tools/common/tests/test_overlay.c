/* Test headless del parser de ".lines" (ewgui_overlay.c). Sin GTK ni display. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/overlay.h"

#define OUTDIR "/tmp/opencode"

static int eq(double a, double b) { return fabs(a - b) < 1e-9; }

static const char *write_file(const char *name, const char *content)
{
    static char path[256];
    FILE *f;
    snprintf(path, sizeof path, OUTDIR "/%s", name);
    f = fopen(path, "w");
    assert(f);
    fputs(content, f);
    fclose(f);
    return path;
}

int main(void)
{
    const char *valid =
        "# comentario libre\n"
        "#layer name=subduccion color=FF0000 width=3.0\n"
        "0 0\n"
        "1 1\n"
        ">\n"
        "2 2\n"
        "3 3\n"
        "4 4\n"
        "#layer name=regiones color=00FF00\n"
        "-70 -30\n"
        "-71 -31\n";
    const char *tolerante =
        "0 0\n"
        "1 1\n"
        "#layer\n"
        "abc def\n"
        "5 5\n"
        "#layer name=x\n"
        "6\n"
        "7 7\n"
        "8 8\n"
        ">\n"
        "200 100\n"
        "9 9\n";
    const char *vacio = "# nada\n\n";
    EwOverlay *ov;
    double blue[3] = {0.0, 0.0, 1.0};
    size_t i;

    /* --- archivo válido: dos capas --- */
    ov = ewgui_overlay_new();
    assert(ov);
    assert(ewgui_overlay_append(ov, write_file("ov_valid.lines", valid)) == 0);
    assert(ov->nlayers == 2);

    assert(strcmp(ov->layers[0].name, "subduccion") == 0);
    assert(eq(ov->layers[0].color[0], 1.0) && eq(ov->layers[0].color[1], 0.0) &&
           eq(ov->layers[0].color[2], 0.0));
    assert(eq(ov->layers[0].width, 3.0));
    assert(ov->layers[0].nlines == 2);
    assert(ov->layers[0].lines[0].npts == 2);
    assert(ov->layers[0].lines[1].npts == 3);
    assert(eq(ov->layers[0].lines[0].pts[0].lon, 0.0) &&
           eq(ov->layers[0].lines[0].pts[0].lat, 0.0));
    assert(eq(ov->layers[0].lines[1].pts[2].lon, 4.0) &&
           eq(ov->layers[0].lines[1].pts[2].lat, 4.0));

    assert(strcmp(ov->layers[1].name, "regiones") == 0);
    assert(eq(ov->layers[1].color[1], 1.0));
    assert(eq(ov->layers[1].width, 1.5));   /* default */
    assert(ov->layers[1].nlines == 1 && ov->layers[1].lines[0].npts == 2);
    assert(eq(ov->layers[1].lines[0].pts[0].lon, -70.0) &&
           eq(ov->layers[1].lines[0].pts[0].lat, -30.0));

    /* --- consulta / ajuste --- */
    assert(ewgui_overlay_layer_index(ov, "subduccion") == 0);
    assert(ewgui_overlay_layer_index(ov, "regiones") == 1);
    assert(ewgui_overlay_layer_index(ov, "nope") == -1);
    assert(ewgui_overlay_layer_index(NULL, "x") == -1);

    assert(ewgui_overlay_set_color(ov, "subduccion", blue) == 1);
    assert(eq(ov->layers[0].color[2], 1.0) && eq(ov->layers[0].color[0], 0.0));
    assert(ewgui_overlay_set_color(ov, "noexiste", blue) == 0);

    ewgui_overlay_set_global_width(ov, 2.5);
    assert(eq(ov->layers[0].width, 2.5) && eq(ov->layers[1].width, 2.5));
    ewgui_overlay_set_global_width(ov, 0.0);          /* ignorado */
    assert(eq(ov->layers[0].width, 2.5));
    ewgui_overlay_free(ov);

    /* --- entradas tolerantes: no debe romper --- */
    ov = ewgui_overlay_new();
    assert(ewgui_overlay_append(ov, write_file("ov_tol.lines", tolerante)) == 0);
    assert(ov->nlayers == 2);
    assert(strcmp(ov->layers[0].name, "layer1") == 0);   /* sin name -> autogenerado */
    assert(ov->layers[0].nlines == 1 && ov->layers[0].lines[0].npts == 1);
    assert(strcmp(ov->layers[1].name, "x") == 0);
    assert(ov->layers[1].nlines == 2);
    assert(ov->layers[1].lines[0].npts == 2);
    assert(ov->layers[1].lines[1].npts == 2);   /* fuera de rango: igual se acepta */
    assert(eq(ov->layers[1].lines[1].pts[0].lon, 200.0));
    ewgui_overlay_free(ov);

    /* --- archivo vacío: 0 capas, ok --- */
    ov = ewgui_overlay_new();
    assert(ewgui_overlay_append(ov, write_file("ov_vacio.lines", vacio)) == 0);
    assert(ov->nlayers == 0);
    ewgui_overlay_free(ov);

    /* --- archivo inexistente / args nulos --- */
    ov = ewgui_overlay_new();
    assert(ewgui_overlay_append(ov, OUTDIR "/no_existe_overlay.lines") == -1);
    assert(ewgui_overlay_append(ov, NULL) == -1);
    assert(ewgui_overlay_append(NULL, "x") == -1);
    ewgui_overlay_free(ov);
    ewgui_overlay_free(NULL);

    /* silenciar warning de variable no usada en algunos compiladores */
    (void)i;

    printf("ALL OVERLAY TESTS PASSED\n");
    return 0;
}
