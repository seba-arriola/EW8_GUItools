/*
 * test_traces.c - Canvas de trazas del picking (fase 3).
 *
 * Verifica, sin Qt y sin EarthWorm:
 *   - bpq_trace_new/bpq_trace_data (ida y vuelta de las muestras);
 *   - que el canvas pinte la traza (negro), las etiquetas y las marcas de P/S
 *     con el color de su origen (manual / automatica);
 *   - el eje de tiempos, y los bordes (sin filas, sin trazas, argumentos nulos).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>

#include "bpq_internal.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

static const int W = 400, H = 140;

typedef struct {
    unsigned char *buf;
    int            stride;
} Frame;

static void frame_alloc(Frame *f)
{
    f->stride = 4 * W;
    f->buf = (unsigned char *)calloc((size_t)f->stride * H, 1);
}

static void frame_free(Frame *f)
{
    free(f->buf);
    f->buf = NULL;
}

static int count_px(const Frame *f, int what)
{
    int x, y, n = 0;

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            const unsigned char *p = f->buf + (size_t)y * f->stride + (size_t)x * 4;
            const int b = p[0], g = p[1], r = p[2];

            switch (what) {
            case 0: if (r > 245 && g > 245 && b > 245) continue; n++; break; /* no-blanco */
            case 1: if (r < 70 && g < 70 && b < 70) n++; break;               /* negro */
            case 2: if (r > 150 && g < 90 && b < 90) n++; break;              /* rojo */
            /* Azul dominante: la marca automatica se aclara al 40% y el
             * antialias puede subir R/G; el canal B sigue muy por encima. */
            case 3: if (b > 150 && b > r + 40 && b > g + 40) n++; break;
            }
        }
    }
    return n;
}

/* Pixeles NEGROS (traza) dentro de un rectangulo inclusivo. Se excluye el
 * margen de etiquetas y el separador de fila (alpha 0.12 no llega a negro). */
static int count_black_rect(const Frame *f, int x0, int x1, int y0, int y1)
{
    int x, y, n = 0;

    if (x0 < 0) x0 = 0;
    if (x1 >= W) x1 = W - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= H) y1 = H - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            const unsigned char *p = f->buf + (size_t)y * f->stride + (size_t)x * 4;

            if (p[2] < 70 && p[1] < 70 && p[0] < 70) n++;
        }
    return n;
}

/* Alto (en filas) que ocupa la tinta negra dentro del rectangulo. */
static int black_extent(const Frame *f, int x0, int x1, int y0, int y1)
{
    int x, y, first = -1, last = -1;

    if (x0 < 0) x0 = 0;
    if (x1 >= W) x1 = W - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= H) y1 = H - 1;
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            const unsigned char *p = f->buf + (size_t)y * f->stride + (size_t)x * 4;

            if (p[2] < 70 && p[1] < 70 && p[0] < 70) {
                if (first < 0) first = y;
                last = y;
                break;
            }
        }
    }
    return (first < 0) ? 0 : last - first + 1;
}

int main(void)
{
    Frame fr;
    BpqTrace *tr[2];
    BpqTracesCtx c;
    CsnhypodbpStaRow rows[2];
    CsnhypodbpColorSet cp, cs;
    double base_p[3] = { 1.0, 0.0, 0.0 };   /* P: rojo */
    double base_s[3] = { 0.0, 0.0, 1.0 };   /* S: azul  */
    int32_t *s0, *s1;
    int i;

    printf("== test_traces ==\n");

    /* --- 1. traza sintetica: ida y vuelta de las muestras --- */
    s0 = (int32_t *)malloc((size_t)3000 * sizeof(int32_t));
    s1 = (int32_t *)malloc((size_t)1500 * sizeof(int32_t));
    CK(s0 != NULL && s1 != NULL);
    if (!s0 || !s1) return 1;
    for (i = 0; i < 3000; i++) s0[i] = (int32_t)(1000.0 * sin(i * 0.05));
    for (i = 0; i < 1500; i++) s1[i] = (int32_t)(500.0 * cos(i * 0.08));

    tr[0] = bpq_trace_new(1000.0, 100.0, s0, 3000);
    tr[1] = bpq_trace_new(1000.0, 50.0, s1, 1500);
    CK(tr[0] != NULL && tr[1] != NULL);
    CK(bpq_trace_n(tr[0]) == 3000);
    CK(fabs(bpq_trace_rate(tr[0]) - 100.0) < 1e-9);
    CK(fabs(bpq_trace_t0(tr[0]) - 1000.0) < 1e-9);
    {
        const int32_t *d = NULL;
        long n = 0;
        double t0 = 0.0, rate = 0.0;
        CK(bpq_trace_data(tr[0], &d, &n, &t0, &rate) == 1);
        CK(d != NULL && n == 3000 && rate == 100.0 && t0 == 1000.0);
        CK(bpq_trace_data(NULL, &d, &n, &t0, &rate) == 0);
        CK(bpq_trace_data(tr[0], NULL, &n, &t0, &rate) == 0);
    }
    /* Argumentos invalidos del constructor. */
    CK(bpq_trace_new(1000.0, 100.0, s0, 0) == NULL);
    CK(bpq_trace_new(1000.0, 0.0, s0, 3000) == NULL);
    CK(bpq_trace_new(1000.0, 100.0, NULL, 3000) == NULL);

    /* --- 2. filas: una con P manual, otra con S automatica --- */
    memset(rows, 0, sizeof(rows));
    snprintf(rows[0].sta, sizeof(rows[0].sta), "%s", "ANTO");
    rows[0].dist_km = 104.0;
    rows[0].p.present = 1;
    rows[0].p.origin = 'M';
    rows[0].p.t = 1010.0;
    snprintf(rows[1].sta, sizeof(rows[1].sta), "%s", "GO01");
    rows[1].dist_km = 208.0;
    rows[1].s.present = 1;
    rows[1].s.origin = 'A';
    rows[1].s.t = 1020.0;

    csnhypodbp_color_set_init(&cp, base_p);
    csnhypodbp_color_set_init(&cs, base_s);

    frame_alloc(&fr);
    memset(&c, 0, sizeof(c));
    {
        static const char *chans[2] = { "HHZ", "HHN" };

        c.chan = chans;
    }
    c.rows = rows;
    c.n = 2;
    c.tr = tr;
    c.t0 = 1000.0;
    c.window = 30.0;
    c.per_screen = 2;                   /* las DOS filas (P y S) visibles */
    c.selected = 0;
    c.show_p = 1;
    c.show_s = 1;
    c.colors_p = &cp;
    c.colors_s = &cs;

    memset(fr.buf, 0, (size_t)fr.stride * H);
    CK(bpq_scene_paint(BPQ_SCENE_TRACES, &c, fr.buf, W, H, fr.stride) == 0);
    CK(count_px(&fr, 0) > 1000);        /* hay dibujo */
    CK(count_px(&fr, 1) > 100);         /* traza negra */
    CK(count_px(&fr, 2) > 5);           /* marca P manual en rojo */
    CK(count_px(&fr, 3) > 5);           /* marca S automatica en azul */

    /* La cuneta es SOLO el canal, en negrita grande: bbox alto y ancho. */
    {
        char lbl[16];
        int minx = W, maxx = -1, miny = H, maxy = -1, x, y;

        CK(bpq_traces_band_label(&c, 0, lbl, sizeof(lbl)) == 1);
        CK(strcmp(lbl, "HHZ") == 0);
        CK(bpq_traces_band_label(&c, 1, lbl, sizeof(lbl)) == 1);
        CK(strcmp(lbl, "HHN") == 0);
        CK(bpq_traces_band_label(&c, 2, lbl, sizeof(lbl)) == 0);
        CK(bpq_traces_band_label(NULL, 0, lbl, sizeof(lbl)) == 0);
        CK(fabs(bpq_traces_left_margin(1.0) - 72.0) < 1e-9);

        for (y = 0; y < H; y++)
            for (x = 0; x < 72; x++) {
                const unsigned char *p = fr.buf + (size_t)y * fr.stride +
                                         (size_t)x * 4;

                if (p[0] < 70 && p[1] < 70 && p[2] < 70) {
                    if (x < minx) minx = x;
                    if (x > maxx) maxx = x;
                    if (y < miny) miny = y;
                    if (y > maxy) maxy = y;
                }
            }
        CK(maxx - minx + 1 >= 40);      /* 27 px bold, no 9 px */
        CK(maxy - miny + 1 >= 18);      /* altura de las mayusculas */
    }

    /* --- 3. eje de tiempos --- */
    {
        unsigned char *abuf;
        const int AH = 20;
        abuf = (unsigned char *)calloc((size_t)fr.stride * AH, 1);
        CK(abuf != NULL);
        if (abuf) {
            int x, y, n = 0;
            CK(bpq_scene_paint(BPQ_SCENE_TRACE_AXIS, &c, abuf, W, AH,
                               fr.stride) == 0);
            for (y = 0; y < AH; y++)
                for (x = 0; x < W; x++) {
                    const unsigned char *p = abuf + (size_t)y * fr.stride +
                                             (size_t)x * 4;
                    if (p[0] < 230 || p[1] < 230 || p[2] < 235) n++;
                }
            CK(n > 100);                /* ticks y etiquetas */
            free(abuf);
        }
    }

    /* --- 4. bordes: sin filas, sin trazas y sin colores --- */
    memset(fr.buf, 0, (size_t)fr.stride * H);
    c.n = 0;
    CK(bpq_scene_paint(BPQ_SCENE_TRACES, &c, fr.buf, W, H, fr.stride) == 0);
    CK(count_px(&fr, 1) == 0);

    c.n = 2;
    c.tr = NULL;                        /* estaciones sin datos */
    memset(fr.buf, 0, (size_t)fr.stride * H);
    CK(bpq_scene_paint(BPQ_SCENE_TRACES, &c, fr.buf, W, H, fr.stride) == 0);
    /* Sin trazas siguen dibujandose etiquetas, separadores y marcas. */
    CK(count_px(&fr, 0) > 200);
    CK(count_px(&fr, 2) > 5);

    c.colors_p = NULL;
    c.colors_s = NULL;
    CK(bpq_scene_paint(BPQ_SCENE_TRACES, &c, fr.buf, W, H, fr.stride) == 0);
    CK(bpq_scene_paint(BPQ_SCENE_TRACE_AXIS, NULL, fr.buf, W, 20, fr.stride) == 0);

    /* --- 5. clic sobre la traza: fila e instante --- */
    {
        const double x0 = 72.0, pw = W - x0 - 6.0;
        int row = -1;
        double t = 0.0;

        /* `tracesPerScreen` manda el alto de banda y cuantas caben: el clic en el
         * centro de la ULTIMA fila visible tiene que devolver esa fila (con un
         * viewport chico puede ser la 0, no siempre la 1). */
        c.per_screen = 2;
        {
            const double dpr = (c.dpr > 0.0) ? c.dpr : 1.0;
            const int band = bpq_traces_band_h(&c, H);
            int vis = 0;
            const int first = bpq_traces_visible(&c, H, &vis);
            const int target = first + vis - 1;
            const double y = dpr + (target - first) * band + band / 2.0;

            CK(band > 0);
            CK(vis >= 1);
            CK(bpq_traces_hit(&c, W, H, x0 + pw / 2.0, y, &row, &t) == 0);
            CK(row == target);
        }
        CK(fabs(t - 1015.0) < 0.05);          /* mitad de la ventana 1000..1030 */
        CK(bpq_traces_hit(&c, W, H, 10.0, 40.0, &row, &t) == -1);   /* etiquetas */
        CK(bpq_traces_hit(&c, W, H, x0 + 10.0, H - 1.0, &row, &t) == -1);
        CK(bpq_traces_hit(NULL, W, H, 100.0, 40.0, &row, &t) == -1);
    }

    /* --- 6. zoom: escala por ventana, polilinea y gaps (2026-10-23) ------ */
    {
        CsnhypodbpStaRow zrow;
        BpqTrace *ztr[1];
        BpqTracesCtx z;
        int32_t *zs;
        const int N = 3000;
        const int mx = 72;               /* left_margin con dpr = 1 */
        const int pw = W - mx - 6;
        int band, i;

        zs = (int32_t *)malloc((size_t)N * sizeof(int32_t));
        CK(zs != NULL);
        if (zs) {
            memset(&zrow, 0, sizeof(zrow));
            snprintf(zrow.sta, sizeof(zrow.sta), "%s", "TEST");
            memset(&z, 0, sizeof(z));
            z.rows = &zrow;
            z.n = 1;
            z.tr = ztr;
            z.selected = 0;
            z.per_screen = 1;
            band = bpq_traces_band_h(&z, H);
            CK(band > 0);

            /* 6a. La escala vertical sale de la VENTANA: un burst 50x mayor
             * fuera del encuadre no debe aplastar la onda visible. */
            for (i = 0; i < N; i++) {
                if (i < 1000)
                    zs[i] = (int32_t)(100.0 * sin(2.0 * M_PI * 2.0 * (i / 100.0)));
                else if (i >= 1900 && i < 2100)
                    zs[i] = 5000;
                else
                    zs[i] = 0;
            }
            ztr[0] = bpq_trace_new(1000.0, 100.0, zs, N);
            CK(ztr[0] != NULL);
            z.t0 = 1000.0;
            z.window = 10.0;
            memset(fr.buf, 0, (size_t)fr.stride * H);
            CK(bpq_scene_paint(BPQ_SCENE_TRACES, &z, fr.buf, W, H, fr.stride) == 0);
            CK(black_extent(&fr, mx, W - 1, 0, band) >= band / 2);

            /* 6b. spp < 2: polilinea muestra a muestra. La envolvente sola
             * dejaria tinta SOLO en los dos extremos de una senal alternante. */
            for (i = 0; i < N; i++) zs[i] = (i & 1) ? 1000 : -1000;
            bpq_trace_free(ztr[0]);
            ztr[0] = bpq_trace_new(1000.0, 100.0, zs, N);
            CK(ztr[0] != NULL);
            z.window = 0.2;
            memset(fr.buf, 0, (size_t)fr.stride * H);
            CK(bpq_scene_paint(BPQ_SCENE_TRACES, &z, fr.buf, W, H, fr.stride) == 0);
            {
                const int yc = 1 + band / 2;
                const int dy = (int)(0.15 * band);

                CK(count_black_rect(&fr, mx, W - 1, yc - dy, yc + dy) > 20);
            }

            /* 6c. Gap dentro de la ventana: la banda se corta, no se une. */
            for (i = 0; i < N; i++) {
                if (i >= 1000 && i < 1400) zs[i] = INT32_MAX;
                else zs[i] = (int32_t)(800.0 * sin(2.0 * M_PI * 1.0 * (i / 100.0)));
            }
            bpq_trace_free(ztr[0]);
            ztr[0] = bpq_trace_new(1000.0, 100.0, zs, N);
            CK(ztr[0] != NULL);
            z.window = 20.0;
            memset(fr.buf, 0, (size_t)fr.stride * H);
            CK(bpq_scene_paint(BPQ_SCENE_TRACES, &z, fr.buf, W, H, fr.stride) == 0);
            {
                /* muestras 1000..1399 = t 1010..1014, con 5 columnas de margen */
                const int gx0 = mx + (10 * pw) / 20 + 5;
                const int gx1 = mx + (14 * pw) / 20 - 5;

                CK(gx1 > gx0);
                CK(count_black_rect(&fr, gx0, gx1, 0, band) == 0);
            }

            bpq_trace_free(ztr[0]);
            free(zs);
        }
    }

    /* --- 7. scroll: la barra y las flechas comparten la misma cuenta ----- */
    {
        CsnhypodbpStaRow srow[3];
        BpqTrace *trs[3] = { NULL, NULL, NULL };
        BpqTracesCtx c3;
        int nvis = 0;

        memset(srow, 0, sizeof(srow));
        memset(&c3, 0, sizeof(c3));
        c3.rows = srow;
        c3.tr = trs;
        c3.n = 3;
        c3.per_screen = 1;
        c3.dpr = 1.0;

        CK(bpq_traces_page_rows(&c3, H) == 1);
        CK(bpq_traces_scroll_max(&c3, H) == 2);
        CK(bpq_traces_scroll_to(&c3, H, 0) == 0);
        CK(bpq_traces_visible(&c3, H, &nvis) == 0 && nvis == 1);
        CK(bpq_traces_scroll_to(&c3, H, 1) == 1);
        CK(bpq_traces_visible(&c3, H, &nvis) == 1 && nvis == 1);
        CK(bpq_traces_scroll_to(&c3, H, 99) == 2);
        CK(bpq_traces_scroll_to(&c3, H, -5) == 0);

        /* Si todo cabe, no hay scroll (rango 0). Hace falta un viewport que
         * quepa tres bandas: con H=140 el suelo de banda solo deja una. */
        c3.per_screen = 3;
        CK(bpq_traces_page_rows(&c3, 3 * H) == 3);
        CK(bpq_traces_scroll_max(&c3, 3 * H) == 0);
        CK(bpq_traces_scroll_to(&c3, 3 * H, 7) == 0);

        /* Sin filas: todo a 0, sin divisiones raras. */
        c3.n = 0;
        CK(bpq_traces_page_rows(&c3, H) == 0);
        CK(bpq_traces_scroll_max(&c3, H) == 0);
        CK(bpq_traces_scroll_to(&c3, H, 3) == 0);
    }

    frame_free(&fr);
    bpq_trace_free(tr[0]);
    bpq_trace_free(tr[1]);
    free(s0);
    free(s1);
    printf("== traces checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
