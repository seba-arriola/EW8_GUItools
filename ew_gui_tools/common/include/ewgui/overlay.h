#ifndef EWGUI_OVERLAY_H
#define EWGUI_OVERLAY_H

/*
 * Capa vectorial de líneas (subducción, fallas, límites de regiones...).
 *
 * Formato de datos ".lines" (texto plano; ver common/tools/OVERLAYS.md):
 *
 *   # comentario
 *   #layer name=<N> [color=<RRGGBB>] [width=<F>]
 *   <lon> <lat>
 *   ...
 *   >                     -> cierra la polilínea; el siguiente vértice abre otra
 *
 * Puro (solo cairo para el dibujo): sin GTK, testeable headless.
 */

#include <cairo.h>
#include <stddef.h>

#define EWGUI_OVERLAY_NAME_MAX  64
#define EWGUI_OVERLAY_MAX_FILES 16

typedef struct { double lon, lat; } EwOverlayPoint;

typedef struct {
    EwOverlayPoint *pts;
    size_t          npts;
} EwOverlayPolyline;

typedef struct {
    char               name[EWGUI_OVERLAY_NAME_MAX];
    double             color[3];   /* RGB 0..1                      */
    double             width;      /* px de pantalla                */
    int                visible;    /* runtime; 1 por defecto        */
    EwOverlayPolyline *lines;
    size_t             nlines;
} EwOverlayLayer;

typedef struct {
    EwOverlayLayer *layers;
    size_t          nlayers;
} EwOverlay;

/* --- Ciclo de vida ------------------------------------------------------- */

EwOverlay *ewgui_overlay_new(void);

/*
 * Añade las capas de `path` al overlay. Devuelve 0 si el archivo se abrió
 * (aunque no tuviera capas) y -1 si no se pudo abrir. Los errores de formato
 * son tolerantes: se saltan la línea y se avisa por stderr.
 */
int  ewgui_overlay_append(EwOverlay *ov, const char *path);

void ewgui_overlay_free(EwOverlay *ov);

/* --- Consulta / ajuste --------------------------------------------------- */

/* Índice de la primera capa llamada `name`; -1 si no está. */
int  ewgui_overlay_layer_index(const EwOverlay *ov, const char *name);

/* Aplica `rgb` a toda capa llamada `layer`; devuelve cuántas cambió. */
int  ewgui_overlay_set_color(EwOverlay *ov, const char *layer, const double rgb[3]);

/* Sobrescribe el ancho de todas las capas (<=0 se ignora). */
void ewgui_overlay_set_global_width(EwOverlay *ov, double width);

/* --- Dibujo -------------------------------------------------------------- */

/* `map` proyecta (lat, lon) a la coordenada de dibujo (pantalla o mundo).
 * `width_scale` multiplica el ancho de cada capa: 1.0 si `map` ya devuelve
 * píxeles de pantalla, 1/zoom si se dibuja dentro de la transformación. */
typedef void (*EwOverlayMapFn)(double lat, double lon, void *ud,
                               double *sx, double *sy);

void ewgui_overlay_draw(cairo_t *cr, const EwOverlay *ov,
                        EwOverlayMapFn map, void *ud, double width_scale);

#endif /* EWGUI_OVERLAY_H */
