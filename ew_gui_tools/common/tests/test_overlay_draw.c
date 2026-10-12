/* Test headless del dibujo cairo de la capa de líneas (ewgui_overlay_draw)
 * y de que el export reutiliza el EwDrawFn. Sin GTK ni display. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <cairo.h>

#include "ewgui/export.h"
#include "ewgui/overlay.h"

#define OUTDIR "/tmp/opencode"

/* Proyección equirectangular de mundo a una superficie de 100x50. */
static void map_world(double lat, double lon, void *ud, double *sx, double *sy)
{
    (void)ud;
    *sx = (lon + 180.0) / 360.0 * 100.0;
    *sy = (90.0 - lat) / 180.0 * 50.0;
}

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

static cairo_surface_t *new_surface(void)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 50);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
    return s;
}

/* Lee el píxel (x,y) como R,G,B (formato ARGB32 little-endian: B,G,R,A). */
static void pixel(cairo_surface_t *s, int x, int y, int *r, int *g, int *b)
{
    int stride = cairo_image_surface_get_stride(s);
    unsigned char *p = cairo_image_surface_get_data(s) + y * stride + x * 4;
    cairo_surface_flush(s);
    *b = p[0];
    *g = p[1];
    *r = p[2];
}

static void export_draw(EwGuiCanvas *canvas, cairo_t *cr,
                        int width, int height, void *ud)
{
    (void)canvas;
    (void)width;
    (void)height;
    ewgui_overlay_draw(cr, (const EwOverlay *)ud, map_world, NULL, 1.0);
}

static long file_size(const char *p)
{
    FILE *f = fopen(p, "rb");
    long n;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static int file_starts_with(const char *p, const unsigned char *magic, size_t n)
{
    unsigned char buf[8];
    FILE *f = fopen(p, "rb");
    size_t got;
    if (!f || n > sizeof(buf)) { if (f) fclose(f); return 0; }
    got = fread(buf, 1, n, f);
    fclose(f);
    return got == n && memcmp(buf, magic, n) == 0;
}

int main(void)
{
    EwOverlay *ov;
    cairo_surface_t *s;
    int r, g, b;

    mkdir(OUTDIR, 0777);

    ov = ewgui_overlay_new();
    assert(ov);
    assert(ewgui_overlay_append(ov, write_file("ov_draw.lines",
        "#layer name=ecuador color=FF0000 width=2\n"
        "-90 0\n"
        "90 0\n")) == 0);
    assert(ov->nlayers == 1);

    /* D1: la línea se pinta con el color de la capa en el ecuador (y=25) */
    s = new_surface();
    {
        cairo_t *cr = cairo_create(s);
        ewgui_overlay_draw(cr, ov, map_world, NULL, 1.0);
        cairo_destroy(cr);
    }
    pixel(s, 50, 25, &r, &g, &b);
    assert(r > 200 && g < 80 && b < 80);
    /* lejos de la línea sigue blanco */
    pixel(s, 50, 10, &r, &g, &b);
    assert(r > 200 && g > 200 && b > 200);
    cairo_surface_destroy(s);

    /* D2: visible=0 no pinta */
    ov->layers[0].visible = 0;
    s = new_surface();
    {
        cairo_t *cr = cairo_create(s);
        ewgui_overlay_draw(cr, ov, map_world, NULL, 1.0);
        cairo_destroy(cr);
    }
    pixel(s, 50, 25, &r, &g, &b);
    assert(r > 200 && g > 200 && b > 200);
    cairo_surface_destroy(s);
    ov->layers[0].visible = 1;

    /* D3: args nulos no crashean */
    ewgui_overlay_draw(NULL, ov, map_world, NULL, 1.0);
    ewgui_overlay_draw((cairo_t *)0, NULL, map_world, NULL, 1.0);

    /* D5: ewgui_overlay_set_visible apaga la capa pedida y respeta las demás */
    {
        EwOverlay *ov2 = ewgui_overlay_new();
        assert(ov2);
        assert(ewgui_overlay_append(ov2, write_file("ov_vis.lines",
            "#layer name=ecuador color=FF0000 width=2\n"
            "-90 0\n"
            "90 0\n"
            ">\n"
            "#layer name=sesenta color=0000FF width=2\n"
            "-90 60\n"
            "90 60\n")) == 0);
        assert(ov2->nlayers == 2);
        assert(ewgui_overlay_set_visible(ov2, "sesenta", 0) == 1);
        assert(ov2->layers[1].visible == 0);
        assert(ov2->layers[0].visible == 1);
        assert(ewgui_overlay_set_visible(ov2, "no_existe", 0) == 0);
        assert(ewgui_overlay_set_visible(NULL, "sesenta", 0) == 0);

        s = new_surface();
        {
            cairo_t *cr = cairo_create(s);
            ewgui_overlay_draw(cr, ov2, map_world, NULL, 1.0);
            cairo_destroy(cr);
        }
        pixel(s, 50, 25, &r, &g, &b);          /* ecuador: se sigue pintando */
        assert(r > 200 && g < 80 && b < 80);
        pixel(s, 50, 8, &r, &g, &b);           /* lat 60 (y=8.3): apagada */
        assert(r > 200 && g > 200 && b > 200);
        cairo_surface_destroy(s);

        /* Y se vuelve a encender. */
        assert(ewgui_overlay_set_visible(ov2, "sesenta", 1) == 1);
        assert(ov2->layers[1].visible == 1);
        s = new_surface();
        {
            cairo_t *cr = cairo_create(s);
            ewgui_overlay_draw(cr, ov2, map_world, NULL, 1.0);
            cairo_destroy(cr);
        }
        pixel(s, 50, 8, &r, &g, &b);
        assert(b > 200 && r < 80 && g < 80);
        cairo_surface_destroy(s);
        ewgui_overlay_free(ov2);
    }

    /* D4: el export reutiliza el draw -> PNG válido y no vacío */
    {
        const char *out = OUTDIR "/overlay_export.png";
        assert(ewgui_export_render(export_draw, ov, out,
                                   EW_EXPORT_PNG, 100, 50, 1.0) == 0);
        assert(file_size(out) > 0);
        assert(file_starts_with(out, (const unsigned char *)"\x89PNG\r\n\x1a\n", 8));
    }

    ewgui_overlay_free(ov);
    printf("ALL OVERLAY DRAW TESTS PASSED\n");
    return 0;
}
