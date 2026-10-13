/*
 * Test headless de la traza de vista por magnitud (ver csnhypodbp_magview.h):
 * recorte al rango, decimado por magnitud, gaps a NaN y margen de contexto.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_magview.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

#define PI 3.14159265358979323846

/* Traza sintetica: 20000 muestras a 100 sps (200 s) desde t=1000 s, seno de 1 Hz. */
static EwGuiTrace *mk_trace(void)
{
    EwGuiTrace *tr = ewgui_trace_new(20000);
    int32_t *raw;
    int i;

    if (!tr) return NULL;
    ewgui_trace_set_span(tr, 1000.0, 100.0, 20000);
    raw = ewgui_trace_raw(tr);
    for (i = 0; i < 20000; i++)
        raw[i] = (int32_t)(1000.0 * sin(2.0 * PI * 1.0 * (double)i / 100.0));
    ewgui_trace_finish(tr);
    return tr;
}

static void test_build(void)
{
    CsnhypodbpMagCfg c;
    CsnhypodbpMagView v;
    EwGuiTrace *tr = mk_trace();

    if (!tr) { CHECK(0, "magview: crea la traza"); return; }
    csnhypodbp_magcfg_defaults(&c);
    csnhypodbp_magview_init(&v);

    /* ML: a tasa completa (el Wood-Anderson no cambia el muestreo). */
    CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_ML, &c, 1010.0, 1055.0) == 0,
          "magview ML: construye");
    CHECK(v.kind == CSNHYPODBP_MAG_ML && v.decim == 1, "magview ML: sin decimar");
    CHECK(fabs(v.rate - 100.0) < 1e-9, "magview ML: tasa de la traza");
    CHECK(v.n == 4501, "magview ML: muestras del rango");
    CHECK(fabs(v.oldest - 1010.0) < 1e-6, "magview ML: oldest del recorte");
    CHECK(isfinite(csnhypodbp_magview_at(&v, 0)) &&
          isfinite(csnhypodbp_magview_at(&v, v.n - 1)), "magview ML: muestras finitas");
    csnhypodbp_magview_free(&v);

    /* Mb: mismo muestreo, paso banda. */
    CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_MB, &c, 1010.0, 1040.0) == 0,
          "magview Mb: construye");
    CHECK(v.decim == 1 && v.n == 3001, "magview Mb: tasa completa");
    csnhypodbp_magview_free(&v);

    /* Ms: diezmado a ~1 sps (a 100 sps el paso banda de 0.05 Hz es inutil). */
    CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_MS, &c, 1010.0, 1055.0) == 0,
          "magview Ms: construye");
    CHECK(v.decim == 100 && fabs(v.rate - 1.0) < 1e-9, "magview Ms: diezmado a 1 sps");
    CHECK(v.n == 46, "magview Ms: muestras diezmadas");
    csnhypodbp_magview_free(&v);

    /* Mwp: integrado (desplazamiento) y diezmado a ~10 sps. */
    CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_MWP, &c, 1010.0, 1055.0) == 0,
          "magview Mwp: construye");
    CHECK(v.decim == 10 && fabs(v.rate - 10.0) < 1e-9, "magview Mwp: diezmado a 10 sps");
    CHECK(v.n == 451, "magview Mwp: muestras diezmadas");
    csnhypodbp_magview_free(&v);

    /* Un gap del buffer crudo sale NaN en la vista (mismo indice de la rebanada). */
    {
        int32_t *raw = ewgui_trace_raw(tr);

        raw[1000 + 500] = INT32_MAX;
        CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_ML, &c, 1010.0, 1055.0) == 0,
              "magview: construye con gap");
        CHECK(isnan(csnhypodbp_magview_at(&v, 500)), "magview: el gap sale NaN");
        csnhypodbp_magview_free(&v);
        raw[1000 + 500] = 0;
    }

    /* Recorte por el final de la traza. */
    CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_ML, &c, 1010.0, 9999.0) == 0,
          "magview: rango que se pasa del final");
    CHECK(v.n == 19000, "magview: se recorta al final de la traza");
    csnhypodbp_magview_free(&v);

    /* Fuera de la traza y argumentos invalidos. */
    CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_ML, &c, 5000.0, 5100.0) == -1,
          "magview: rango fuera -> -1");
    CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_ML, &c, 1055.0, 1010.0) == -1,
          "magview: rango invertido -> -1");
    CHECK(csnhypodbp_magview_build(NULL, tr, CSNHYPODBP_MAG_ML, &c, 1010.0, 1055.0) == -1,
          "magview: salida NULL -> -1");
    CHECK(csnhypodbp_magview_build(&v, NULL, CSNHYPODBP_MAG_ML, &c, 1010.0, 1055.0) == -1,
          "magview: traza NULL -> -1");
    CHECK(csnhypodbp_magview_build(&v, tr, 9, &c, 1010.0, 1055.0) == -1,
          "magview: magnitud fuera de rango -> -1");
    CHECK(csnhypodbp_magview_build(&v, tr, CSNHYPODBP_MAG_ML, NULL, 1010.0, 1055.0) == -1,
          "magview: config NULL -> -1");
    CHECK(isnan(csnhypodbp_magview_at(&v, 0)), "magview: vista vacia -> NaN");
    CHECK(isnan(csnhypodbp_magview_at(NULL, 0)) && isnan(csnhypodbp_magview_at(&v, -1)),
          "magview: fuera de rango -> NaN");

    ewgui_trace_free(tr);
}

static void test_margin(void)
{
    CHECK(fabs(csnhypodbp_magview_margin(10.0, 45.0) - 28.75) < 1e-9,
          "margin: 20 s + 1/4 del ancho");
    CHECK(fabs(csnhypodbp_magview_margin(0.0, 400.0) - 120.0) < 1e-9,
          "margin: crece con la ventana");
    CHECK(fabs(csnhypodbp_magview_margin(NAN, NAN) - 20.0) < 1e-9,
          "margin: sin ventana queda el minimo");
}

static void test_frame(void)
{
    CsnhypodbpView v;
    double win0 = 0.0, span = 0.0;

    v.active = 0; v.t0 = 0.0; v.t1 = 0.0;

    /* Sin zoom: encuadre automatico del ancla. margin(-10,40) = 20 + 12.5 = 32.5 s. */
    CHECK(csnhypodbp_magview_frame(&v, 1000.0, -10.0, 40.0, &win0, &span) == 1,
          "frame: sin zoom hay encuadre");
    CHECK(fabs(win0 - 957.5) < 1e-9, "frame: win0 = ancla + t0 - margen");
    CHECK(fabs(span - 115.0) < 1e-9, "frame: span = ventana + 2*margen");

    /* Con zoom manda la vista, y la ventana de la magnitud no interviene: su
       tiempo absoluto (ancla + t0) es independiente del zoom. */
    v.active = 1; v.t0 = 2000.0; v.t1 = 2100.0;
    CHECK(csnhypodbp_magview_frame(&v, 1000.0, -10.0, 40.0, &win0, &span) == 1 &&
          fabs(win0 - 2000.0) < 1e-9 && fabs(span - 100.0) < 1e-9,
          "frame: con zoom manda la vista");
    CHECK(csnhypodbp_magview_frame(&v, 1000.0, -300.0, 300.0, &win0, &span) == 1 &&
          fabs(win0 - 2000.0) < 1e-9 && fabs(span - 100.0) < 1e-9,
          "frame: con zoom la ventana de la magnitud no interviene");

    /* Vista activa degenerada -> encuadre automatico. */
    v.t1 = v.t0;
    CHECK(csnhypodbp_magview_frame(&v, 1000.0, -10.0, 40.0, &win0, &span) == 1 &&
          fabs(win0 - 957.5) < 1e-9,
          "frame: zoom degenerado -> encuadre automatico");

    /* Sin ancla no hay ventana que mostrar. */
    CHECK(csnhypodbp_magview_frame(&v, 0.0, -10.0, 40.0, &win0, &span) == 0,
          "frame: sin ancla -> 0");
    CHECK(csnhypodbp_magview_frame(&v, NAN, -10.0, 40.0, &win0, &span) == 0,
          "frame: ancla NaN -> 0");
    CHECK(csnhypodbp_magview_frame(&v, 1000.0, NAN, 40.0, &win0, &span) == 0,
          "frame: t0 NaN -> 0");
    CHECK(csnhypodbp_magview_frame(NULL, 1000.0, -10.0, 40.0, &win0, &span) == 1,
          "frame: view NULL -> encuadre automatico");
    CHECK(csnhypodbp_magview_frame(&v, 1000.0, -10.0, 40.0, NULL, NULL) == 1,
          "frame: salidas NULL no revientan");
}

static void test_cursor(void)
{
    CHECK(strcmp(csnhypodbp_magview_cursor(0, 0), "crosshair") == 0,
          "cursor: fuera de la franja -> crosshair (zoom)");
    CHECK(strcmp(csnhypodbp_magview_cursor(0, 1), "ew-resize") == 0,
          "cursor: borde de la franja -> ew-resize");
    CHECK(strcmp(csnhypodbp_magview_cursor(1, 1), "move") == 0,
          "cursor: handle -> move");
    CHECK(strcmp(csnhypodbp_magview_cursor(1, 0), "move") == 0,
          "cursor: el handle manda sobre el resto");
    /* Invariante del modulo: nunca un cursor de mano. */
    CHECK(strcmp(csnhypodbp_magview_cursor(1, 1), "pointer") != 0 &&
          strcmp(csnhypodbp_magview_cursor(1, 1), "grab") != 0 &&
          strcmp(csnhypodbp_magview_cursor(1, 1), "grabbing") != 0,
          "cursor: sin cursor de mano");
}

int main(void)
{
    test_build();
    test_margin();
    test_frame();
    test_cursor();
    if (fails) { printf("%d FALLOS\n", fails); return 1; }
    printf("OK test_magview\n");
    return 0;
}
