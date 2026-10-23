/*
 * test_canvas.c - Verifica el camino cairo -> buffer que consume la UI Qt
 * (AC13, parte sin Qt): las 7 escenas pintan algo, el pixel central no es el
 * fondo, el stride se valida y el export PNG/SVG/PDF produce archivos.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                       \
    g_checks++;                                                             \
    if (!(cond)) {                                                          \
        g_fail++;                                                           \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                       \
} while (0)

static int any_nonzero(const unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) if (b[i]) return 1;
    return 0;
}

static unsigned px(const unsigned char *b, int stride, int x, int y)
{
    const unsigned char *p = b + (size_t)y * stride + (size_t)x * 4;
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

int main(void)
{
    const int w = 240, h = 160;
    const int stride = 4 * w;
    unsigned char *buf = (unsigned char *)malloc((size_t)stride * h);
    int scene, i;

    CK(buf != NULL);
    if (!buf) return 1;

    for (scene = BPQ_SCENE_MAP_PLAN; scene <= BPQ_SCENE_MWW_BALL; scene++) {
        unsigned corner, center;

        memset(buf, 0, (size_t)stride * h);
        CK(bpq_scene_paint((BpqScene)scene, NULL, buf, w, h, stride) == 0);
        CK(any_nonzero(buf, (size_t)stride * h));       /* pinto algo */

        /* La cruz roja del centro debe diferir del fondo de la esquina. */
        corner = px(buf, stride, 2, 2);
        center = px(buf, stride, w / 2, h / 2);
        CK(corner != center);
        /* fondo opaco (alpha 0xFF): el blit a QImage no debe salir transparente */
        CK(((corner >> 24) & 0xFF) == 0xFF);
    }

    /* El nombre de cada escena existe (lo usan los tests y el dump). */
    for (scene = BPQ_SCENE_MAP_PLAN; scene <= BPQ_SCENE_MWW_BALL; scene++)
        CK(bpq_scene_name((BpqScene)scene)[0] != '?');

    /* Tamanos minimos: no deben inflar el minimo de la ventana (trampa WSLg). */
    for (scene = BPQ_SCENE_MAP_PLAN; scene <= BPQ_SCENE_MWW_BALL; scene++) {
        int mw = 0, mh = 0;
        CK(bpq_scene_min_size((BpqScene)scene, &mw, &mh) == 0);
        CK(mw > 0 && mw <= 400);
        CK(mh > 0 && mh <= 400);
    }

    /* --- validaciones de argumentos --- */
    CK(bpq_scene_paint(BPQ_SCENE_MAP_PLAN, NULL, NULL, w, h, stride) == -1);
    CK(bpq_scene_paint(BPQ_SCENE_MAP_PLAN, NULL, buf, 0, h, stride) == -1);
    CK(bpq_scene_paint(BPQ_SCENE_MAP_PLAN, NULL, buf, w, h, 4 * w - 4) == -1);
    CK(bpq_scene_export(BPQ_SCENE_MAP_PLAN, NULL, NULL, BPQ_FMT_PNG, w, h, 1.0) == -1);
    CK(bpq_scene_min_size(BPQ_SCENE_MAP_PLAN, NULL, NULL) == -1);
    /* escena fuera de rango: nombre "?" pero no debe petar */
    CK(bpq_scene_name((BpqScene)99)[0] == '?');

    /* --- export (los tres formatos) --- */
    {
        const struct { const char *path; BpqFmt fmt; } out[] = {
            { "/tmp/bpq_test_canvas.png", BPQ_FMT_PNG },
            { "/tmp/bpq_test_canvas.svg", BPQ_FMT_SVG },
            { "/tmp/bpq_test_canvas.pdf", BPQ_FMT_PDF },
        };
        for (i = 0; i < 3; i++) {
            FILE *fp;
            long sz;
            CK(bpq_scene_export(BPQ_SCENE_MAP_PLAN, NULL, out[i].path,
                                out[i].fmt, w, h, 1.0) == 0);
            fp = fopen(out[i].path, "rb");
            CK(fp != NULL);
            if (fp) {
                fseek(fp, 0, SEEK_END);
                sz = ftell(fp);
                fclose(fp);
                CK(sz > 100);
                remove(out[i].path);
            }
        }
    }

    free(buf);
    printf("== canvas checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
