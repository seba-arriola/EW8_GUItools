/* Test headless del núcleo de export (ewgui_export.c).
 * No necesita GTK ni display: usa un EwDrawFn propio con canvas == NULL. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ewgui/export.h"

#define OUTDIR "/tmp/opencode"

static int g_calls = 0;

static void fake_draw(EwGuiCanvas *canvas, cairo_t *cr,
                      int width, int height, void *user_data)
{
    (void)canvas;
    (void)user_data;
    g_calls++;
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 1.0);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, width, height);
    cairo_stroke(cr);
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
    unsigned char buf[16];
    FILE *f = fopen(p, "rb");
    size_t got;
    if (!f || n > sizeof(buf)) { if (f) fclose(f); return 0; }
    got = fread(buf, 1, n, f);
    fclose(f);
    return got == n && memcmp(buf, magic, n) == 0;
}

static int file_contains(const char *p, const char *needle)
{
    char buf[4096];
    size_t got;
    FILE *f = fopen(p, "rb");
    if (!f)
        return 0;
    got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = '\0';
    return strstr(buf, needle) != NULL;
}

int main(void)
{
    char path[256];

    mkdir(OUTDIR, 0777);   /* ya existe en el entorno; no falla si falta */

    /* E5: inferencia de formato por extensión */
    assert(ewgui_export_format_from_path("a.svg") == EW_EXPORT_SVG);
    assert(ewgui_export_format_from_path("a.SVG") == EW_EXPORT_SVG);
    assert(ewgui_export_format_from_path("dir/a.pdf") == EW_EXPORT_PDF);
    assert(ewgui_export_format_from_path("a.png") == EW_EXPORT_PNG);
    assert(ewgui_export_format_from_path("a.ps")  == EW_EXPORT_PS);
    assert(ewgui_export_format_from_path("a.txt") == EW_EXPORT_UNKNOWN);
    assert(ewgui_export_format_from_path("noext") == EW_EXPORT_UNKNOWN);
    assert(ewgui_export_format_from_path(NULL)    == EW_EXPORT_UNKNOWN);

    /* E1: SVG válido (texto con <svg) */
    snprintf(path, sizeof path, OUTDIR "/export_test.svg");
    assert(ewgui_export_render(fake_draw, NULL, path, EW_EXPORT_SVG, 200, 100, 1.0) == 0);
    assert(file_size(path) > 0);
    assert(file_contains(path, "<svg"));

    /* E2: PDF válido (cabecera %PDF) */
    snprintf(path, sizeof path, OUTDIR "/export_test.pdf");
    assert(ewgui_export_render(fake_draw, NULL, path, EW_EXPORT_PDF, 200, 100, 1.0) == 0);
    assert(file_starts_with(path, (const unsigned char *)"%PDF", 4));

    /* E3: PNG válido (firma) y escalado 2x */
    snprintf(path, sizeof path, OUTDIR "/export_test.png");
    assert(ewgui_export_render(fake_draw, NULL, path, EW_EXPORT_PNG, 200, 100, 2.0) == 0);
    assert(file_starts_with(path, (const unsigned char *)"\x89PNG\r\n\x1a\n", 8));

    /* E4: path no escribible -> -1 */
    assert(ewgui_export_render(fake_draw, NULL, "/nonexistent-xyz/foo.svg",
                               EW_EXPORT_SVG, 200, 100, 1.0) == -1);

    /* Argumentos inválidos */
    assert(ewgui_export_render(NULL, NULL, path, EW_EXPORT_SVG, 200, 100, 1.0) == -1);
    assert(ewgui_export_render(fake_draw, NULL, path, EW_EXPORT_UNKNOWN, 200, 100, 1.0) == -1);
    assert(ewgui_export_render(fake_draw, NULL, path, EW_EXPORT_SVG, 0, 100, 1.0) == -1);
    /* scale inválido -> se normaliza, no falla */
    assert(ewgui_export_render(fake_draw, NULL, path, EW_EXPORT_SVG, 10, 10, 0.0) == 0);

    assert(g_calls >= 4);   /* uno por cada export que llega a dibujar */

    printf("ALL EXPORT TESTS PASSED\n");
    return 0;
}
