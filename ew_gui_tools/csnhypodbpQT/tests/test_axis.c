/*
 * test_axis.c - Eje de tiempo comun (picking y magnitudes).
 *
 * Fija el contrato que comparten las dos pestanas:
 *   - fondo neutro, filete superior, ticks cortos y etiquetas (sin rejilla de
 *     altura completa);
 *   - paso 1-2-5 en multiplos redondos;
 *   - banda gris de arrastre;
 *   - gesto de release: izq->der = zoom, der->izq = reset, gesto corto = nada.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cairo.h>

#include "bpq_internal.h"

static int g_checks, g_fail;

#define CK(c) do {                                                          \
        g_checks++;                                                          \
        if (!(c)) {                                                          \
            g_fail++;                                                        \
            fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c);    \
        }                                                                    \
    } while (0)

#define W 400
#define H 20

static unsigned char g_buf[W * H * 4];

static const unsigned char *px(int x, int y)
{
    return g_buf + ((size_t)y * W + x) * 4;
}

static void paint(const BpqAxisPaint *p)
{
    cairo_surface_t *sf;
    cairo_t *cr;

    memset(g_buf, 0, sizeof(g_buf));
    sf = cairo_image_surface_create_for_data(g_buf, CAIRO_FORMAT_ARGB32, W, H,
                                             W * 4);
    cr = cairo_create(sf);
    bpq_axis_paint(cr, W, H, p);
    cairo_destroy(cr);
    cairo_surface_destroy(sf);
}

static void base_paint(BpqAxisPaint *p, int band)
{
    memset(p, 0, sizeof(*p));
    p->t0 = 1700000000.0;
    p->span = 120.0;
    p->left = 58.0;
    p->right = 394.0;
    p->dpr = 1.0;
    p->band = band;
    if (band) {
        p->band_x0 = 100.0;
        p->band_x1 = 200.0;
    }
}

static void testStep(void)
{
    CK(fabs(bpq_axis_step(10.0, 400.0, 1.0) - 2.0) < 1e-9);
    CK(fabs(bpq_axis_step(60.0, 400.0, 1.0) - 20.0) < 1e-9);
    CK(fabs(bpq_axis_step(120.0, 400.0, 1.0) - 50.0) < 1e-9);
    CK(fabs(bpq_axis_step(600.0, 400.0, 1.0) - 200.0) < 1e-9);
    CK(fabs(bpq_axis_step(1.0, 400.0, 1.0) - 0.2) < 1e-9);
    CK(fabs(bpq_axis_step(0.0, 400.0, 1.0) - 1.0) < 1e-9);
    CK(fabs(bpq_axis_step(120.0, 0.0, 1.0) - 1.0) < 1e-9);
}

static void testPaint(void)
{
    BpqAxisPaint p;

    /* Fondo y filete superior. */
    base_paint(&p, 0);
    paint(&p);
    CK(px(20, 10)[0] >= 225 && px(20, 10)[0] <= 232);
    CK(px(300, 0)[0] < 160);          /* filete */
    CK(px(300, 0)[1] < 160);

    /* Tick en un multiplo redondo (paso 50 s) y NO rejilla de altura completa:
     * a y=2 hay tick en x=198, pero en x=120 no hay nada. */
    CK(px(198, 2)[0] < 160);
    CK(px(120, 2)[0] >= 225);
    CK(px(120, H - 1)[0] >= 225);     /* el fondo no se pinta como rejilla */

    /* Banda gris: 0.5 al 35% sobre 0.90 => ~194. Se muestrea y=2 (debajo del
     * filete y por encima de las etiquetas, que viven en y>=7). */
    base_paint(&p, 1);
    paint(&p);
    CK(px(150, 2)[0] >= 188 && px(150, 2)[0] <= 202);
    CK(px(250, 2)[0] >= 225);
    CK(px(50, 2)[0] >= 225);

    /* Sin banda, la misma zona vuelve al fondo. */
    base_paint(&p, 0);
    paint(&p);
    CK(px(150, 2)[0] >= 225);

    /* Banda invertida y fuera del area: se normaliza/recorta sin romper. */
    base_paint(&p, 1);
    p.band_x0 = 250.0;
    p.band_x1 = 120.0;
    paint(&p);
    CK(px(200, 2)[0] >= 188 && px(200, 2)[0] <= 202);
    CK(px(60, 2)[0] >= 225);
}

static void testTraceAxis(void)
{
    BpqTracesCtx c;
    cairo_surface_t *sf;
    cairo_t *cr;

    memset(&c, 0, sizeof(c));
    c.t0 = 1700000000.0;
    c.window = 120.0;
    c.dpr = 1.0;
    c.rubber_active = 1;
    c.rubber_x0 = 100.0;
    c.rubber_x1 = 200.0;

    memset(g_buf, 0, sizeof(g_buf));
    sf = cairo_image_surface_create_for_data(g_buf, CAIRO_FORMAT_ARGB32, W, H,
                                             W * 4);
    cr = cairo_create(sf);
    bpq_traces_draw_axis(cr, W, H, &c);
    cairo_destroy(cr);
    cairo_surface_destroy(sf);

    CK(px(150, 10)[0] >= 188 && px(150, 10)[0] <= 202);
    CK(px(300, 0)[0] < 160);

    /* El eje de magnitudes delega en el mismo pintor. */
    memset(g_buf, 0, sizeof(g_buf));
    sf = cairo_image_surface_create_for_data(g_buf, CAIRO_FORMAT_ARGB32, W, H,
                                             W * 4);
    cr = cairo_create(sf);
    bpq_mag_draw_axis(cr, W, H, NULL);
    cairo_destroy(cr);
    cairo_surface_destroy(sf);
    CK(px(20, 10)[0] >= 225);
    CK(px(300, 0)[0] < 160);
}

static void testRelease(void)
{
    BpqTracesCtx c;
    int act;

    memset(&c, 0, sizeof(c));
    c.t0 = 1700000000.0;
    c.window = 120.0;
    c.dpr = 1.0;

    c.rubber_active = 1;
    act = bpq_traces_axis_release(&c, W, 100.0, 200.0, 1699999000.0, 120.0);
    CK(act == BPQ_AXIS_ZOOM);
    CK(c.window > 30.0 && c.window < 40.0);
    CK(c.t0 > 1700000010.0 && c.t0 < 1700000020.0);
    CK(c.rubber_active == 0);

    c.t0 = 1700000010.0;
    c.window = 40.0;
    c.rubber_active = 1;
    act = bpq_traces_axis_release(&c, W, 200.0, 100.0, 1699999000.0, 120.0);
    CK(act == BPQ_AXIS_RESET);
    CK(c.t0 == 1699999000.0);
    CK(c.window == 120.0);
    CK(c.rubber_active == 0);

    /* Menos de 2 px: no toca la ventana. */
    c.t0 = 1700000000.0;
    c.window = 120.0;
    act = bpq_traces_axis_release(&c, W, 100.0, 101.0, 1699999000.0, 120.0);
    CK(act == BPQ_AXIS_NONE);
    CK(c.window == 120.0);

    /* 2.5 px pero < 0.1 s: tampoco. */
    c.window = 10.0;
    act = bpq_traces_axis_release(&c, W, 100.0, 102.5, 1699999000.0, 120.0);
    CK(act == BPQ_AXIS_NONE);
    CK(c.window == 10.0);

    /* Sin evento default, el gesto inverso no inventa una ventana. */
    c.window = 40.0;
    act = bpq_traces_axis_release(&c, W, 200.0, 100.0, 0.0, 0.0);
    CK(act == BPQ_AXIS_NONE);
    CK(c.window == 40.0);

    CK(bpq_traces_axis_release(NULL, W, 100.0, 200.0, 0.0, 120.0) ==
       BPQ_AXIS_NONE);
}

int main(void)
{
    testStep();
    testPaint();
    testTraceAxis();
    testRelease();

    printf("== axis checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
