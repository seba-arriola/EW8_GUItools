#ifndef EWGUI_GEO_H
#define EWGUI_GEO_H

/*
 * Proyección equirectangular y utilidades de mapa (sin GTK ni cairo).
 * Compartido por csnrv y csnstaevdisp.
 */

/* (lat,lon) -> píxeles sobre una imagen de `img_w` x `img_h` que cubre el
 * mundo entero (lon -180..180, lat -90..90). x/y pueden ser NULL. */
void   ewgui_geo_project(double lat, double lon, double img_w, double img_h,
                         double *x, double *y);

/* Paso de la grilla lat/lon según el zoom (10 / 5 / 2 / 1). */
double ewgui_geo_grid_step(double zoom);

/* Un paso de zoom por rueda (up != 0 = acercar), recortado a [zmin, zmax]. */
double ewgui_geo_zoom_step(double zoom, int up, double zmin, double zmax);

#endif /* EWGUI_GEO_H */
