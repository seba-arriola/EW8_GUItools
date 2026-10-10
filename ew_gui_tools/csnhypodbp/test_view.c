/*
 * test_view - estado de la vista de ondas (headless): zoom por dos clics,
 * mapeos tiempo<->x, ticks "bonitos" del eje y exageracion vertical.
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "csnhypodbp_view.h"

int main(void)
{
    CsnhypodbpView v;

    /* Exageracion vertical: +1 x1.5, -1 /1.5, 0 reset, con clamp. */
    assert(fabs(csnhypodbp_zoom_step(1.0, +1) - 1.5) < 1e-9);
    assert(fabs(csnhypodbp_zoom_step(1.0, -1) - (1.0 / 1.5)) < 1e-9);
    assert(csnhypodbp_zoom_step(3.0, 0) == 1.0);
    assert(csnhypodbp_zoom_step(1e9, +1) == CSNHYPODBP_ZOOM_FACTOR_MAX);
    assert(csnhypodbp_zoom_step(1e-9, -1) == CSNHYPODBP_ZOOM_FACTOR_MIN);

    /* set_default deja la ventana en [0, span] sin zoom ni ancla. */
    csnhypodbp_view_set_default(&v, 120.0);
    assert(!v.active && !v.have_anchor);
    assert(v.t0 == 0.0 && v.t1 == 120.0);
    csnhypodbp_view_set_default(&v, 0.0);   /* span invalido -> 1.0 */
    assert(v.t1 == 1.0);

    /* Clic 1 ancla; clic 2 a la derecha (>=0.1) hace zoom. */
    csnhypodbp_view_set_default(&v, 120.0);
    assert(csnhypodbp_view_click(&v, 50.0) == CSNHYPODBP_ACT_ANCHOR);
    assert(v.have_anchor && !v.active);
    assert(csnhypodbp_view_click(&v, 90.0) == CSNHYPODBP_ACT_ZOOM);
    assert(v.active && !v.have_anchor);
    assert(v.t0 == 50.0 && v.t1 == 90.0);

    /* Clic 1 ancla; clic 2 a la izquierda -> reset al default. */
    csnhypodbp_view_set_default(&v, 120.0);
    assert(csnhypodbp_view_click(&v, 90.0) == CSNHYPODBP_ACT_ANCHOR);
    assert(csnhypodbp_view_click(&v, 50.0) == CSNHYPODBP_ACT_RESET);
    assert(!v.active && v.t0 == 0.0 && v.t1 == 120.0);

    /* Span < MIN: se ignora y se conserva el ancla. */
    csnhypodbp_view_set_default(&v, 120.0);
    assert(csnhypodbp_view_click(&v, 50.0) == CSNHYPODBP_ACT_ANCHOR);
    assert(csnhypodbp_view_click(&v, 50.05) == CSNHYPODBP_ACT_NONE);
    assert(v.have_anchor);
    assert(csnhypodbp_view_click(&v, 50.0) == CSNHYPODBP_ACT_RESET);

    /* rel_to_x / x_to_rel son inversas (default y con zoom). */
    csnhypodbp_view_set_default(&v, 60.0);
    for (double rel = 0.0; rel <= 60.0; rel += 7.0) {
        double x = csnhypodbp_view_rel_to_x(&v, rel, 160, 800);
        double rr = csnhypodbp_view_x_to_rel(&v, x, 160, 800);
        assert(fabs(rr - rel) < 1e-6);
    }
    assert(csnhypodbp_view_rel_to_x(&v, 0.0, 160, 800) == 160.0);
    assert(csnhypodbp_view_rel_to_x(&v, 60.0, 160, 800) == 960.0);
    csnhypodbp_view_click(&v, 10.0);
    csnhypodbp_view_click(&v, 30.0);
    {
        double x = csnhypodbp_view_rel_to_x(&v, 20.0, 160, 800);
        double rr = csnhypodbp_view_x_to_rel(&v, x, 160, 800);
        assert(fabs(rr - 20.0) < 1e-6);
    }

    /* Ticks: paso "bonito" y posiciones alineadas dentro del rango. */
    assert(fabs(csnhypodbp_view_tick_step(120.0, 12) - 10.0) < 1e-9);
    assert(fabs(csnhypodbp_view_tick_step(120.0, 6) - 20.0) < 1e-9);
    {
        double out[64];
        int n = csnhypodbp_view_ticks(1003.0, 1123.0, 12, out, 64);
        int i;
        assert(n >= 1 && n <= 13);
        assert(out[0] >= 1003.0 && out[0] <= 1013.0);
        for (i = 0; i < n; i++)
            assert(out[i] >= 1003.0 - 1e-9 && out[i] <= 1123.0 + 1e-9);
        for (i = 1; i < n; i++)
            assert(fabs((out[i] - out[i - 1]) - 10.0) < 1e-6);
    }
    assert(csnhypodbp_view_ticks(10.0, 10.0, 12, (double[4]){0}, 4) == 0);

    /* C1: continuidad. spp = (span/ncol)*rate ; polilinea si spp < 2. */
    /* 120 s en 900 px a 100 sps -> spp=13.3 -> envelope. */
    assert(csnhypodbp_view_use_polyline(120.0, 900, 100.0) == 0);
    /* zoom a 2 s -> spp=0.22 -> polilinea. */
    assert(csnhypodbp_view_use_polyline(2.0, 900, 100.0) == 1);
    /* spp=2.5 -> envelope ; spp=1.5 -> polilinea (sin tocar la frontera exacta). */
    assert(csnhypodbp_view_use_polyline(22.5, 900, 100.0) == 0);
    assert(csnhypodbp_view_use_polyline(13.5, 900, 100.0) == 1);
    /* 1 sps a 120 s en 900 px -> spp=0.133 -> polilinea (canal LH). */
    assert(csnhypodbp_view_use_polyline(120.0, 900, 1.0) == 1);
    /* entradas invalidas -> envelope (no rompe). */
    assert(csnhypodbp_view_use_polyline(0.0, 900, 100.0) == 0);
    assert(csnhypodbp_view_use_polyline(120.0, 0, 100.0) == 0);
    assert(csnhypodbp_view_use_polyline(120.0, 900, 0.0) == 0);

    /* C2: sample_x -> offset en [0,ncol), monotono, extremos exactos. */
    {
        double win0 = 1000.0, span = 10.0;
        int ncol = 800;
        double prev = -1.0, t;
        assert(fabs(csnhypodbp_view_sample_x(win0, win0, span, ncol) - 0.0) < 1e-9);
        assert(fabs(csnhypodbp_view_sample_x(win0 + span, win0, span, ncol) - 800.0) < 1e-9);
        assert(fabs(csnhypodbp_view_sample_x(win0 + span / 2.0, win0, span, ncol) - 400.0) < 1e-9);
        for (t = win0; t <= win0 + span + 1e-9; t += 0.25) {
            double x = csnhypodbp_view_sample_x(t, win0, span, ncol);
            assert(x >= 0.0 && x <= (double)ncol + 1e-9);
            assert(x > prev);
            prev = x;
        }
        assert(csnhypodbp_view_sample_x(win0, win0, 0.0, ncol) == 0.0);
        assert(csnhypodbp_view_sample_x(win0, win0, span, 0) == 0.0);
    }

    /* C5: color valido actualiza; invalido conserva el default. */
    {
        double rgb[3] = { 0.1, 0.2, 0.3 };
        assert(csnhypodbp_view_parse_color("FF0000", rgb) == 0);
        assert(fabs(rgb[0] - 1.0) < 1e-9 && fabs(rgb[1] - 0.0) < 1e-9 && fabs(rgb[2] - 0.0) < 1e-9);
        assert(csnhypodbp_view_parse_color("#009900", rgb) == 0);
        assert(fabs(rgb[0] - 0.0) < 1e-9 && fabs(rgb[1] - 0.6) < 1e-9 && fabs(rgb[2] - 0.0) < 1e-9);
        /* invalidos: rgb NO cambia (se conserva el default). */
        assert(csnhypodbp_view_parse_color("nope", rgb) == -1);
        assert(fabs(rgb[1] - 0.6) < 1e-9);
        assert(csnhypodbp_view_parse_color(NULL, rgb) == -1);
        assert(fabs(rgb[1] - 0.6) < 1e-9);
        assert(csnhypodbp_view_parse_color("#12345", rgb) == -1);
        assert(fabs(rgb[1] - 0.6) < 1e-9);
    }

    /* C3: el indicador de picado solo se dibuja en pantalla, en modo picado y
       con el puntero sobre el canvas (en la exportacion canvas==NULL). */
    assert(csnhypodbp_view_show_pick_cue(1, 1, 1) == 1);
    assert(csnhypodbp_view_show_pick_cue(0, 1, 1) == 0);   /* export */
    assert(csnhypodbp_view_show_pick_cue(1, 0, 1) == 0);   /* modo zoom */
    assert(csnhypodbp_view_show_pick_cue(1, 1, 0) == 0);   /* sin puntero */
    assert(csnhypodbp_view_show_pick_cue(0, 0, 0) == 0);

    /* C6: zoom temporal (Shift+<-/->): anclado al centro, con topes. */
    csnhypodbp_view_set_default(&v, 120.0);
    assert(fabs(csnhypodbp_view_time_zoom(&v, +1) - 180.0) < 1e-9);
    assert(fabs(v.t0 - (-30.0)) < 1e-9 && fabs(v.t1 - 150.0) < 1e-9 && v.active);
    csnhypodbp_view_set_default(&v, 120.0);
    assert(fabs(csnhypodbp_view_time_zoom(&v, -1) - 80.0) < 1e-9);
    assert(fabs(v.t0 - 20.0) < 1e-9 && fabs(v.t1 - 100.0) < 1e-9 && v.active);
    assert(fabs(csnhypodbp_view_time_zoom(&v, 0) - 120.0) < 1e-9);
    assert(v.t0 == 0.0 && v.t1 == 120.0 && !v.active);
    /* tope minimo: por debajo de 0.1 s no baja. */
    csnhypodbp_view_set_default(&v, 0.1);
    csnhypodbp_view_time_zoom(&v, -1);
    assert(fabs((v.t1 - v.t0) - CSNHYPODBP_ZOOM_MIN_SPAN) < 1e-9);
    /* tope maximo: no supera CSNHYPODBP_TIME_SPAN_MAX. */
    csnhypodbp_view_set_default(&v, 120.0);
    v.t0 = 0.0; v.t1 = CSNHYPODBP_TIME_SPAN_MAX; v.active = 1;
    csnhypodbp_view_time_zoom(&v, +1);
    assert(fabs((v.t1 - v.t0) - CSNHYPODBP_TIME_SPAN_MAX) < 1e-9);
    /* el tope nunca es menor que def_span (Time window grande). */
    csnhypodbp_view_set_default(&v, CSNHYPODBP_TIME_SPAN_MAX * 2.0);
    csnhypodbp_view_time_zoom(&v, -1);
    assert(fabs((v.t1 - v.t0) - (CSNHYPODBP_TIME_SPAN_MAX * 2.0 / 1.5)) < 1e-6);

    /* C7: arrastre del eje: izq->der zoom; der->izq reset; corto ignorado. */
    {
        CsnhypodbpAxisDrag d;
        csnhypodbp_view_set_default(&v, 120.0);
        csnhypodbp_view_drag_begin(&d, 10.0);
        assert(d.active && d.anchor_rel == 10.0);
        csnhypodbp_view_drag_update(&d, 40.0);
        assert(csnhypodbp_view_drag_end(&d, &v) == CSNHYPODBP_ACT_ZOOM);
        assert(v.t0 == 10.0 && v.t1 == 40.0 && v.active && !d.active);
        /* der->izq: reset al default. */
        csnhypodbp_view_drag_begin(&d, 40.0);
        csnhypodbp_view_drag_update(&d, 10.0);
        assert(csnhypodbp_view_drag_end(&d, &v) == CSNHYPODBP_ACT_RESET);
        assert(v.t0 == 0.0 && v.t1 == 120.0 && !v.active);
        /* arrastre < MIN: sin cambios. */
        csnhypodbp_view_drag_begin(&d, 50.0);
        csnhypodbp_view_drag_update(&d, 50.05);
        assert(csnhypodbp_view_drag_end(&d, &v) == CSNHYPODBP_ACT_NONE);
        assert(v.t0 == 0.0 && v.t1 == 120.0);
        /* end sin active -> NONE. */
        d.active = 0;
        assert(csnhypodbp_view_drag_end(&d, &v) == CSNHYPODBP_ACT_NONE);
    }

    /* C8: banda gris del arrastre: normalizada y recortada al area. */
    {
        CsnhypodbpAxisDrag d;
        double xa = -1.0, xb = -1.0;
        csnhypodbp_view_set_default(&v, 120.0);
        d.active = 0; d.anchor_rel = 0.0; d.cur_rel = 0.0;
        assert(csnhypodbp_view_axis_band_x(&v, &d, 160, 600, &xa, &xb) == 0);
        d.active = 1; d.anchor_rel = 10.0; d.cur_rel = 40.0;
        assert(csnhypodbp_view_axis_band_x(&v, &d, 160, 600, &xa, &xb) == 1);
        assert(fabs(xa - 210.0) < 1e-9 && fabs(xb - 360.0) < 1e-9);
        d.anchor_rel = 40.0; d.cur_rel = 10.0;   /* invertidos -> mismo rango */
        assert(csnhypodbp_view_axis_band_x(&v, &d, 160, 600, &xa, &xb) == 1);
        assert(fabs(xa - 210.0) < 1e-9 && fabs(xb - 360.0) < 1e-9);
        /* fuera de rango -> recortada a [160, 760]. */
        d.anchor_rel = -10.0; d.cur_rel = 200.0;
        assert(csnhypodbp_view_axis_band_x(&v, &d, 160, 600, &xa, &xb) == 1);
        assert(fabs(xa - 160.0) < 1e-9 && fabs(xb - 760.0) < 1e-9);
    }

    /* C9: grosor minimo de la banda (envelope continuo, zona plana visible). */
    assert(fabs(csnhypodbp_view_min_half(1.0, 0.8) - 0.4) < 1e-9);
    assert(fabs(csnhypodbp_view_min_half(2.0, 1.0) - 0.25) < 1e-9);
    assert(csnhypodbp_view_min_half(0.0, 1.0) == 0.0);
    assert(csnhypodbp_view_min_half(1.0, 0.0) == 0.0);
    assert(csnhypodbp_view_min_half(-1.0, 1.0) == 0.0);

    /* C10: tramos contiguos de columnas con datos (un hueco real corta). */
    {
        const int has[7] = {1, 1, 0, 0, 1, 1, 0};
        int runs[8];
        assert(csnhypodbp_view_band_runs(has, 7, runs, 4) == 2);
        assert(runs[0] == 0 && runs[1] == 2);
        assert(runs[2] == 4 && runs[3] == 6);
        assert(csnhypodbp_view_band_runs(has, 7, runs, 1) == -1); /* no cabe */
        {
            const int z[4] = {0, 0, 0, 0};
            assert(csnhypodbp_view_band_runs(z, 4, runs, 4) == 0);
        }
        assert(csnhypodbp_view_band_runs(has, 0, runs, 4) == 0);
        assert(csnhypodbp_view_band_runs(NULL, 7, runs, 4) == 0);
    }

    /* C11: reset de zoom condicionado (estacion / solucion). */
    assert(csnhypodbp_station_changed("", "") == 0);
    assert(csnhypodbp_station_changed("A", "A") == 0);
    assert(csnhypodbp_station_changed("A", "B") == 1);
    assert(csnhypodbp_station_changed("", "A") == 1);
    assert(csnhypodbp_station_changed(NULL, "A") == 1);
    assert(csnhypodbp_station_changed("A", NULL) == 1);
    assert(csnhypodbp_solution_changed(1, 2, 3, 1, 2, 3) == 0);
    assert(csnhypodbp_solution_changed(1, 2, 3, 1, 2, 4) == 1);
    assert(csnhypodbp_solution_changed(0, 0, 0, 1, 2, 3) == 1);

    /* C12: indice de fila de estacion a seleccionar por nombre. */
    {
        const char *names[3] = { "AAK", "BOB", "CCO" };
        assert(csnhypodbp_station_row_to_select("BOB", names, 3) == 1);
        assert(csnhypodbp_station_row_to_select("ZZZ", names, 3) == -1);
        assert(csnhypodbp_station_row_to_select("", names, 3) == -1);
        assert(csnhypodbp_station_row_to_select(NULL, names, 3) == -1);
        assert(csnhypodbp_station_row_to_select("AAK", names, 0) == -1);
    }

    /* C13: politica de auto-seleccion del anillo (no robar). */
    assert(csnhypodbp_should_autoselect(0) == 1);
    assert(csnhypodbp_should_autoselect(1) == 0);

    /* C14: cancelar el ancla no altera el zoom. */
    {
        csnhypodbp_view_set_default(&v, 120.0);
        v.t0 = 10.0; v.t1 = 20.0; v.active = 1; v.have_anchor = 1;
        csnhypodbp_view_cancel_anchor(&v);
        assert(v.have_anchor == 0);
        assert(v.t0 == 10.0 && v.t1 == 20.0 && v.active == 1);
    }

    /* Formato UTC. */
    {
        char b[16];
        csnhypodbp_view_fmt_utc(0.0, b, sizeof(b));
        assert(strcmp(b, "00:00:00") == 0);
        csnhypodbp_view_fmt_utc(3661.0, b, sizeof(b));
        assert(strcmp(b, "01:01:01") == 0);
    }

    printf("test_view OK\n");
    return 0;
}
