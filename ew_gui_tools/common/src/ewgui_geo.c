#include "ewgui/geo.h"

void ewgui_geo_project(double lat, double lon, double img_w, double img_h,
                       double *x, double *y)
{
    if (x)
        *x = img_w * (lon + 180.0) / 360.0;
    if (y)
        *y = img_h * (90.0 - lat) / 180.0;
}

double ewgui_geo_grid_step(double zoom)
{
    if (zoom > 30.0) return 1.0;
    if (zoom > 15.0) return 2.0;
    if (zoom > 5.0)  return 5.0;
    return 10.0;
}

double ewgui_geo_zoom_step(double zoom, int up, double zmin, double zmax)
{
    zoom = up ? zoom * 1.2 : zoom / 1.2;
    if (zoom < zmin) zoom = zmin;
    if (zoom > zmax) zoom = zmax;
    return zoom;
}
