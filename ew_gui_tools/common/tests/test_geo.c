#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "ewgui/geo.h"

static int eq(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void)
{
    double x, y;

    /* Proyección: esquinas y centro */
    ewgui_geo_project(0.0, 0.0, 1000, 500, &x, &y);
    assert(eq(x, 500.0) && eq(y, 250.0));
    ewgui_geo_project(90.0, -180.0, 1000, 500, &x, &y);
    assert(eq(x, 0.0) && eq(y, 0.0));
    ewgui_geo_project(-90.0, 180.0, 1000, 500, &x, &y);
    assert(eq(x, 1000.0) && eq(y, 500.0));
    /* salidas nulas permitidas */
    ewgui_geo_project(0.0, 0.0, 1000, 500, &x, NULL);
    assert(eq(x, 500.0));

    /* Paso de grilla por tramos */
    assert(eq(ewgui_geo_grid_step(0.5), 10.0));
    assert(eq(ewgui_geo_grid_step(6.0), 5.0));
    assert(eq(ewgui_geo_grid_step(20.0), 2.0));
    assert(eq(ewgui_geo_grid_step(40.0), 1.0));

    /* Zoom por rueda + recorte */
    assert(eq(ewgui_geo_zoom_step(1.0, 1, 0.2, 50.0), 1.2));
    assert(eq(ewgui_geo_zoom_step(1.0, 0, 0.2, 50.0), 1.0 / 1.2));
    assert(eq(ewgui_geo_zoom_step(50.0, 1, 0.2, 50.0), 50.0));
    assert(eq(ewgui_geo_zoom_step(0.2, 0, 0.2, 50.0), 0.2));

    printf("ALL GEO TESTS PASSED\n");
    return 0;
}
