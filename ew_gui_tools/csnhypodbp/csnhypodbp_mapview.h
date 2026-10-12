/******************************************************************************
 * csnhypodbp_mapview.h                                                        *
 *                                                                            *
 * Nucleo PURO (sin GTK) de la geometria de los dos paneles de mapa:           *
 *   - mapa de planta: bbox de los puntos y auto-encuadre (zoom/centro) sobre  *
 *     la imagen equirectangular (contrato de ewgui_geo_project);              *
 *   - panel de perfil: marco (x = longitud, y = -profundidad, y crece hacia   *
 *     arriba) con proyeccion a pixeles y ajuste al contenido.                 *
 * Se testea headless.                                                        *
 ******************************************************************************/
#ifndef CSNHYPODBP_MAPVIEW_H
#define CSNHYPODBP_MAPVIEW_H

#include "csnhypodbp_profile.h"

/* Punto a dibujar en cualquiera de los dos paneles.
 * kind: 0 = solucion del evento, 1 = preferida, 2 = candidata del `locate`. */
typedef struct {
    double lat, lon, depth, mag;
    int    kind;
} CsnhypodbpMapPt;

/* bbox (lon0 <= lon1, lat0 <= lat1) de los puntos con posicion valida.
 * Se saltan los no finitos y los (0,0) (solucion sin posicion). Devuelve 0 si
 * OK o -1 si no hay ningun punto valido (las salidas quedan intactas). */
int csnhypodbp_map_bbox(const CsnhypodbpMapPt *pt, int n,
                        double *lon0, double *lon1, double *lat0, double *lat1);

/* Expande el bbox (lon0..lon1, lat0..lat1) de forma simetrica hasta que cada
 * lado mida al menos `min_span` grados, para que el auto-encuadre no quede
 * demasiado cerca con un grupo de soluciones apretado. `min_span <= 0` o no
 * finito: no hace nada. Se recorta a lon -180..180 y lat -90..90. */
void csnhypodbp_map_span_min(double *lon0, double *lon1, double *lat0, double *lat1,
                             double min_span);

/* Zoom y centro (en pixeles de la imagen) para que el bbox lon/lat entre
 * completo en un canvas de cw x ch con `margin` pixeles de aire. El centro es
 * el que espera la transformacion de csnrv:
 *     screen = (imagen - centro) * zoom + canvas/2
 * Un bbox degenerado (un solo punto) no produce inf/NaN. */
void csnhypodbp_mapfit(double lon0, double lon1, double lat0, double lat1,
                       double img_w, double img_h, int cw, int ch, double margin,
                       double *zoom, double *cx, double *cy);

/* Marco por defecto del panel de perfil a partir de los DATOS del perfil:
 * x = rango de longitud del slab y la topografia (+2% de margen) e
 * y = -(profundidad declarada o el slab mas profundo, +5%) .. elevacion maxima
 * (o +10 km). Garantiza que ni el slab ni la topografia queden fuera del cuadro;
 * las soluciones del evento no lo estiran (para eso estan el zoom y el paneo).
 * Las lineas vacias (n = 0 o bbox sin datos) se ignoran. */
void csnhypodbp_profile_frame(const CsnhypodbpProfileLine *slab,
                              const CsnhypodbpProfileLine *topo,
                              double depth_min_km,
                              double *x0, double *x1, double *y0, double *y1);

/* Indice del marcador bajo (x,y) en pixeles de pantalla, o -1. `sx`/`sy` son las
 * posiciones YA proyectadas de los n puntos. Se recorre en orden inverso al
 * dibujo (candidata -> preferida -> resto), asi con marcadores superpuestos gana
 * el que se ve encima, y dentro de cada grupo el mas cercano. */
int csnhypodbp_map_hit(const CsnhypodbpMapPt *pt, const double *sx, const double *sy,
                       int n, double x, double y, double radius);

/* 1 si el arrastre desde el press supera el umbral en cualquier eje (entonces es
 * paneo, no clic). Valores no finitos cuentan como 0. */
int csnhypodbp_map_drag_exceeded(double dx, double dy, double threshold);

/* Id de una solucion en el mapa ("qid/mod"), para reconocer la elegida en
 * Origins: el orden de los puntos no es el de la tabla. */
#define CSNHYPODBP_MAP_ID_LEN 32

/* Indice de la primera entrada de `ids` igual a `id`. -1 si `id` es NULL o vacio,
 * si n <= 0, o si no esta. */
int csnhypodbp_map_find_id(const char (*ids)[CSNHYPODBP_MAP_ID_LEN], int n,
                           const char *id);

/* Marco del panel de perfil: bbox de datos (x = longitud, y = -profundidad) y
 * area de dibujo en pixeles. */
typedef struct {
    double x0, x1, y0, y1;      /* bbox de datos (y0 < y1); vacio si x1 < x0 */
    double px, py, pw, ph;      /* area de dibujo (ya sin margenes) */
} CsnhypodbpFrame;

/* 1 si el marco tiene datos. Un bbox degenerado (un solo punto) tambien vale:
 * se proyecta al centro del area. */
int  csnhypodbp_frame_valid(const CsnhypodbpFrame *f);

/* Marco vacio con el area de dibujo ya fijada (para ir agregando con _grow). */
void csnhypodbp_frame_reset(CsnhypodbpFrame *f, double px, double py,
                            double pw, double ph);

/* Marco con un bbox explicito. */
void csnhypodbp_frame_set(CsnhypodbpFrame *f, double x0, double x1,
                          double y0, double y1, double px, double py,
                          double pw, double ph);

/* Proyecta un punto de datos a pixeles: x crece a la derecha e y hacia ARRIBA
 * (el panel le pasa -profundidad, asi que mas profundo queda mas abajo). Si el
 * marco esta vacio proyecta al centro del area. */
void csnhypodbp_frame_project(const CsnhypodbpFrame *f, double x, double y,
                              double *sx, double *sy);

/* Extiende el bbox de datos para incluir (x,y) (valores no finitos se ignoran).
 * Con el marco vacio lo inicializa. */
void csnhypodbp_frame_grow(CsnhypodbpFrame *f, double x, double y);
/* Agrega un margen relativo (fx, fy en tanto por uno del span) a cada lado. */
void csnhypodbp_frame_pad(CsnhypodbpFrame *f, double fx, double fy);

/* Aplica al marco un zoom (centrado) y un paneo en UNIDADES DE DATOS, sin tocar
 * el area de dibujo. zoom <= 0 o no finito se ignora. */
void csnhypodbp_frame_view(CsnhypodbpFrame *f, double zoom,
                           double pan_x, double pan_y);

#endif /* CSNHYPODBP_MAPVIEW_H */
