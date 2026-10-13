#ifndef CSNHYPODBP_VIEW_H
#define CSNHYPODBP_VIEW_H

#include <stddef.h>

/*
 * csnhypodbp_view.c - Estado de la vista de ondas (headless, sin GTK/EW).
 *
 * La vista muestra los canales de UNA estacion y todas sus trazas comparten
 * `winStart` (se alinean a su propia P), asi que el zoom es un unico intervalo
 * RELATIVO a winStart que aplica a todas las trazas visibles.
 *
 *   zoom: 1.er clic = ancla; 2.º clic a la derecha (>= 0.1 s) = zoom a ese
 *         rango; 2.º clic a la izquierda o encima = vuelve al default.
 *
 * El eje de tiempo se dibuja en UTC absoluto (winStart + t) porque toda la
 * vista es de la misma estacion.
 */

typedef struct {
    double def_span;    /* ancho por defecto (= g_dScreenTime) */
    double t0, t1;      /* ventana visible, relativa a winStart (s) */
    int    active;      /* t0/t1 != default */
    int    have_anchor; /* hay un 1.er clic pendiente */
    double anchor;      /* tiempo del ancla, relativo a winStart (s) */
} CsnhypodbpView;

typedef enum {
    CSNHYPODBP_ACT_NONE = 0,
    CSNHYPODBP_ACT_ANCHOR,
    CSNHYPODBP_ACT_ZOOM,
    CSNHYPODBP_ACT_RESET
} CsnhypodbpAct;

#define CSNHYPODBP_ZOOM_FACTOR_MIN 0.1
#define CSNHYPODBP_ZOOM_FACTOR_MAX 50.0
#define CSNHYPODBP_ZOOM_STEP       1.5
#define CSNHYPODBP_ZOOM_MIN_SPAN   0.1   /* s: zoom mas angosto se ignora */

/* Reinicia la vista al default [0, span] (span<=0 -> 1.0) y limpia el ancla. */
void          csnhypodbp_view_set_default(CsnhypodbpView *v, double span);

/* Procesa un clic en `rel` (segundos respecto de winStart). Devuelve la accion
 * aplicada. En ZOOM/RESET actualiza t0/t1; en ANCHOR/NONE solo el ancla. */
CsnhypodbpAct csnhypodbp_view_click(CsnhypodbpView *v, double rel);

/* Mapeos tiempo-relativo <-> x del canvas (margen izquierdo + ancho de dibujo). */
double        csnhypodbp_view_rel_to_x(const CsnhypodbpView *v, double rel,
                                       int margin_left, int draw_width);
double        csnhypodbp_view_x_to_rel(const CsnhypodbpView *v, double x,
                                       int margin_left, int draw_width);

/* Nombre del cursor segun la zona del canvas: cruz sobre el area de trazas y
 * normal sobre el margen de etiquetas. No se usan cursores de mano (invariante
 * del modulo: ver test_ui_invariants.sh). */
const char    *csnhypodbp_view_cursor(int in_plot);

/* --- Ancla de la ventana del picking -------------------------------------- */

/* 1 solo si la accion aplicada a la vista obliga a re-anclar la ventana: el
 * RESET (deshacer el zoom). Anclar, hacer zoom y nada NO re-anclan, y picar
 * tampoco: la ventana no se mueve mientras el usuario mira las trazas. */
int    csnhypodbp_view_reanchor_on(CsnhypodbpAct act);

/* Tiempo de la P de referencia al re-anclar: la P ganadora si existe, si no el
 * respaldo (selected_otime + 15). */
double csnhypodbp_view_ref_pick(int have_p, double p_t, double fallback);

/* x de una marca cuya `rel` puede caer fuera de [t0,t1]: devuelve una x DENTRO
 * del area de trazas y `*edge` = -1 izquierda / 0 dentro / +1 derecha, para
 * dibujar el indicador de borde en vez de perder la marca en silencio. */
double csnhypodbp_view_mark_x(const CsnhypodbpView *v, double rel, int margin_left,
                              int draw_width, int *edge);

/* --- Zoom temporal (eje de tiempo) ------------------------------------
 * El ancho visible (span = t1-t0) se expande/contrae con Shift+->/<- anclado
 * al CENTRO de la ventana; el registro (trazas, marcas y eje) se estira con el.
 */
#define CSNHYPODBP_TIME_SPAN_MAX 86400.0  /* tope blando de expansion (s) */

/* dir=+1 expande (span x STEP), -1 contrae (span / STEP), 0 resetea al default.
 * Ancla = centro de la ventana actual; span acotado a [ZOOM_MIN_SPAN,
 * max(TIME_SPAN_MAX, def_span)]. Cancela have_anchor. Devuelve el span. */
double        csnhypodbp_view_time_zoom(CsnhypodbpView *v, int dir);

/* --- Arrastre de zoom sobre el eje de tiempo --------------------------
 * Mantener pulsado y arrastrar sobre el eje: izq->der hace zoom al rango;
 * der->izq resetea la ventana. Estado puro, en tiempos relativos a winStart. */
typedef struct {
    int    active;      /* 1 = arrastrando */
    double anchor_rel;  /* rel del punto de press */
    double cur_rel;     /* rel del puntero */
} CsnhypodbpAxisDrag;

void          csnhypodbp_view_drag_begin(CsnhypodbpAxisDrag *d, double rel);
void          csnhypodbp_view_drag_update(CsnhypodbpAxisDrag *d, double rel);
/* Cierra el arrastre y aplica: d>=+MIN -> ZOOM; d<=-MIN -> RESET; |d|<MIN o
 * !active -> NONE (sin cambios). En ZOOM/RESET cancela have_anchor. */
CsnhypodbpAct csnhypodbp_view_drag_end(CsnhypodbpAxisDrag *d, CsnhypodbpView *v);

/* Banda gris del arrastre: 1 si hay que dibujarla (active). Devuelve xa<xb ya
 * normalizados (min/max) y recortados a [margin_left, margin_left+draw_width]. */
int           csnhypodbp_view_axis_band_x(const CsnhypodbpView *v,
                                          const CsnhypodbpAxisDrag *d,
                                          int margin_left, int draw_width,
                                          double *xa, double *xb);

/* --- Continuidad del trazo al hacer zoom ------------------------------
 * El dibujo normal es un "envelope" min/max por columna de pixel; cuando un
 * pixel cubre menos de CSNHYPODBP_POLYLINE_SPP muestras (zoom alto) ese
 * envelope degenera en rectas de altura 0 y la traza se ve a guiones. En ese
 * caso hay que dibujar una polilinea por muestras (continua).
 *   spp = (span / ncol) * rate   (muestras por pixel)
 */
#define CSNHYPODBP_POLYLINE_SPP 2.0
int           csnhypodbp_view_use_polyline(double span, int ncol, double rate);

/* Mitad del grosor minimo de una columna del envelope, en unidades de datos,
 * para que la banda dibujada mida al menos `line_px` px con la escala vertical
 * dada. Garantiza que una zona plana (min==max) siga dejando trazo visible.
 * Devuelve 0.0 si scale <= 0.0 o line_px <= 0.0. */
double        csnhypodbp_view_min_half(double scale, double line_px);

/* Tramos contiguos de columnas con datos (has[px] != 0). Escribe pares
 * [start,end) en `runs` (2 enteros por tramo) y devuelve el numero de tramos;
 * -1 si no caben en `max_runs`. */
int           csnhypodbp_view_band_runs(const int *has, int ncol,
                                        int *runs, int max_runs);

/* Offset x (en [0,ncol)) de una muestra de tiempo absoluto `t_abs`, para una
 * ventana [win0, win0+span] proyectada sobre `ncol` columnas. Puede salir del
 * rango si `t_abs` esta fuera de la ventana (el llamador filtra). */
double        csnhypodbp_view_sample_x(double t_abs, double win0, double span,
                                       int ncol);

/* Ticks "bonitos" (1/2/5 x 10^k) alineados a multiplos absolutos dentro de
 * [t0,t1]. Devuelve cuantos escribio (<= out_max). */
int           csnhypodbp_view_ticks(double t0, double t1, int max_ticks,
                                    double *out, int out_max);
double        csnhypodbp_view_tick_step(double span, int max_ticks);

/* Formatea un epoch como hora UTC "HH:MM:SS". */
void          csnhypodbp_view_fmt_utc(double t_abs, char *buf, size_t n);

/* Exageracion vertical: dir=+1 sube, -1 baja, 0 resetea; con clamp. */
double        csnhypodbp_zoom_step(double f, int dir);

/* --- Colores configurables (pColor/sColor del .d) ----------------------
 * Envuelve ewgui_parse_hex_color con semantica de fallback: si el valor es
 * invalido NO toca `rgb` (se conserva el default del modulo). 0 = OK. */
int           csnhypodbp_view_parse_color(const char *s, double rgb[3]);

/* --- Indicador de picado (circulo de fase junto al puntero) ------------
 * 1 si hay que dibujar el indicador. `canvas_is_live` excluye la exportacion
 * (ewgui_export_render invoca el callback con canvas==NULL). */
int           csnhypodbp_view_show_pick_cue(int canvas_is_live, int click_mode_pick,
                                            int cursor_valid);

/* --- Seleccion / reset de vista ---------------------------------------
 * Reglas puras para decidir cuando reiniciar el zoom y para la lista de
 * estaciones. NULL y "" equivalen a "ninguna". */
int           csnhypodbp_station_changed(const char *prev, const char *next);
int           csnhypodbp_solution_changed(int oq, int om, int ov,
                                          int nq, int nm, int nv);
int           csnhypodbp_station_row_to_select(const char *sel,
                                               const char *const *names, int n);
int           csnhypodbp_should_autoselect(int has_selection);

/* --- Vista PEGAJOSA ante llegadas del anillo ---------------------------
 * El bloque de reset (zoom, ancla de la P, marcas y ventanas de magnitud) solo
 * corre si el cambio de solucion lo provoco el USUARIO; `ring_sync` lo marca
 * mientras una llegada del anillo recompone la seleccion. */
int           csnhypodbp_view_should_reset(int changed, int ring_sync);

/* Identidad de una solucion (qid, mod, version): conserva la elegida al
 * recomponer el detalle tras una llegada. */
int           csnhypodbp_view_solution_same(int aq, int am, int av,
                                            int bq, int bm, int bv);

/* --- Trazas: que se borra al terminar un fetch -------------------------
 * 1 = borrar la traza de esa estacion antes de repoblarla: si el evento
 * renderizado cambio (todo el canvas es de otro sismo) o si el job trae datos
 * para ella. Si el job no trae nada y es el MISMO evento se conserva lo que ya
 * estaba (un refetch que no devuelve nada no debe vaciar la pantalla). */
int           csnhypodbp_fetch_clear_on(int prev_qid, int prev_mod, int prev_ver,
                                        int job_qid, int job_mod, int job_ver,
                                        int has_data);

void          csnhypodbp_view_cancel_anchor(CsnhypodbpView *v);

#endif /* CSNHYPODBP_VIEW_H */
