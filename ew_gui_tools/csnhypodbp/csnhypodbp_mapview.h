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
#include <stddef.h>

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

/* Igual que csnhypodbp_mapfit, pero con el CENTRO en la solucion de referencia
 * (la preferida, o la elegida en Origins = la verde) en vez del punto medio del
 * bbox, y con un factor de zoom sobre el encuadre (MapZoom; 1.0 = automatico).
 * El span sigue saliendo del bbox (con el suelo que ya aplico el llamante), asi
 * el zoom no depende de donde caiga la referencia.
 * Referencia no finita o (0,0) -> se vuelve al punto medio del bbox. */
void csnhypodbp_mapfit_at(double ref_lat, double ref_lon,
                          double lon0, double lon1, double lat0, double lat1,
                          double img_w, double img_h, int cw, int ch, double margin,
                          double zoom_factor, double *zoom, double *cx, double *cy);

/* Lienzo de proyeccion cuando el .d no declara `MapImageFile` (o la imagen no
 * se puede cargar). El mapa de planta NO deja de dibujarse: se usa un espacio
 * equirectangular 2:1 con la misma escala que el mapa de mundo entregado
 * (10800x5400, 30 px/grado), asi la grilla, el ancho de las capas .lines y el
 * zoom se comportan igual que con la imagen de fondo. */
#define CSNHYPODBP_MAP_VIRTUAL_W 10800.0
#define CSNHYPODBP_MAP_VIRTUAL_H  5400.0

/* Espacio de proyeccion (en pixeles) del mapa de planta. Un solo origen para el
 * dibujo, las capas .lines y el hit test del clic.
 *   has_image y pw/ph > 0 -> *iw = pw, *ih = ph   (devuelve 0)
 *   en cualquier otro caso -> lienzo virtual       (devuelve 1)
 * Devuelve -1 si falta iw/ih. */
int csnhypodbp_map_canvas(int has_image, int pw, int ph, double *iw, double *ih);

/* Punto geografico -> pixel de pantalla con la transformacion del dibujo:
 *     screen = (proyectado - centro) * zoom + canvas/2 + paneo
 * Es la misma cuenta que usa on_draw_map_plan(), asi la grilla, las etiquetas y
 * el hit test no se pueden desalinear del dibujo. Devuelve 0 si OK y -1 si
 * lat/lon no son finitas, falta algun puntero o la geometria no sirve (iw/ih o
 * zoom <= 0, canvas vacio). Un paneo no finito cuenta como 0. */
int csnhypodbp_map_screen(double lat, double lon, double iw, double ih,
                          int cw, int ch, double zoom, double cx, double cy,
                          double pan_x, double pan_y, double *sx, double *sy);

/* Rango geografico visible en un canvas cw x ch con la transformacion de
 * csnhypodbp_map_screen(), recortado al mundo (lon -180..180, lat -90..90).
 * Devuelve 0 si OK y -1 si la geometria no sirve o no queda nada visible
 * (el mundo entero quedo fuera del canvas). */
int csnhypodbp_map_view_bounds(double iw, double ih, int cw, int ch,
                               double zoom, double cx, double cy,
                               double pan_x, double pan_y,
                               double *lon0, double *lon1, double *lat0, double *lat1);

/* bbox de encuadre de la planta: el de los puntos (con el suelo `min_span` de
 * MapMinSpanDeg) o, si no hay ningun punto con posicion valida, el mundo
 * completo. Con `n` soluciones y su bbox degenerado (un solo punto) se aplica
 * igual el suelo. Devuelve 0 si uso los puntos, 1 si uso el mundo y -1 si falta
 * algun puntero. */
int csnhypodbp_map_fit_bounds(const CsnhypodbpMapPt *pt, int n, double min_span,
                              double *lon0, double *lon1, double *lat0, double *lat1);

/* Paso de la grilla (grados) para un span visible, apuntando a ~8 divisiones.
 * `span_deg` no finito o <= 0 -> 10.0. La serie de pasos es
 * 0.05 0.1 0.2 0.25 0.5 1 2 5 10 15 30 45 90, asi el paso no depende de la
 * escala del lienzo y a cualquier zoom hay meridianos y paralelos visibles. */
double csnhypodbp_map_grid_step(double span_deg);

/* Texto de una etiqueta de eje para `value` con el paso `step` (grados):
 * step >= 1 -> "%.0f"; step >= 0.1 -> "%.1f"; si no -> "%.2f".
 * Un step invalido se trata como 10 (una division). */
void csnhypodbp_map_axis_fmt(double step, double value, char *buf, size_t n);

/* Marca en `keep` (1 = se etiqueta, 0 = se omite) que etiquetas caben segun la
 * separacion minima `min_gap` en pixeles respecto de la ultima aceptada. `pos`
 * va en pixeles de pantalla y ordenada de menor a mayor. `min_gap` invalido o
 * <= 0 se trata como 0. Devuelve 0 si OK (n vale 0 -> nada que etiquetar) y -1
 * si falta pos o keep. */
int csnhypodbp_map_label_pick(const double *pos, int n, double min_gap, int *keep);

/* Fraccion de altura de la planta a partir de `MapSplit`: "7/3" -> 7/11 (0.7) o
 * "0.7" -> 0.7. Devuelve 0 si el texto no sirve o la fraccion no cae en (0,1). */
int csnhypodbp_map_split_parse(const char *s, double *frac);

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

/* Vertices de una estrella de `points` puntas centrada en (cx,cy), radio
 * exterior `r` e interior `inner_ratio*r`, con el primer vertice ARRIBA.
 * Escribe hasta `max` pares (x,y) en xs/ys y devuelve cuantos escribio (0 si no
 * caben 2*points, si r <= 0, si points < 3 o si falta algun puntero).
 * `inner_ratio` fuera de (0,1) se sustituye por 0.45. */
int csnhypodbp_marker_star_path(double cx, double cy, double r, int points,
                                double inner_ratio, double *xs, double *ys, int max);

#endif /* CSNHYPODBP_MAPVIEW_H */
