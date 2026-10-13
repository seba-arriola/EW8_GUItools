/*
 * csnhypodbp_magview.h - Traza de VISTA por magnitud (headless, sin GTK ni EW).
 *
 * Es el mismo dato con el procesado que hace que la ventana ajustada signifique
 * algo, en CUENTAS: sirve para juzgar la ventana, no para leer amplitudes (eso lo
 * hace csnmags, que corrige por respuesta instrumental). Por magnitud:
 *
 *   ML  : simulacion Wood-Anderson (lo que mide ML)
 *   Mwp : integracion -> desplazamiento, diezmado para dibujar
 *   Mb  : paso banda Mb_Band (Hz)
 *   Ms  : diezmado a ~1 sps + paso banda Ms_Band (Hz). A 100 sps ese paso banda
 *         es numericamente inutil, de ahi el diezmado.
 *
 * La vista se recorta a [t_start, t_end] (el llamador le suma el margen de
 * contexto que devuelve csnhypodbp_magview_margin), asi que ocupa solo lo que se
 * dibuja: Ms son ~500 muestras, no 90.000.
 */
#ifndef CSNHYPODBP_MAGVIEW_H
#define CSNHYPODBP_MAGVIEW_H

#include "ewgui/wave.h"
#include "csnhypodbp_magcfg.h"
#include "csnhypodbp_view.h"

#define CSNHYPODBP_MAGVIEW_MAX      400000  /* tope de muestras por vista */
#define CSNHYPODBP_MAGVIEW_MS_SPS        1.0
#define CSNHYPODBP_MAGVIEW_MWP_SPS      10.0

typedef struct {
    double *v;       /* muestras (cuentas); NaN en los gaps */
    long    n, cap;
    double  rate;    /* muestras/s de la vista (diezmada si decim > 1) */
    double  oldest;  /* epoch de la primera muestra de la vista */
    int     kind;    /* magnitud que la genero (-1 = vacia) */
    int     decim;   /* factor de decimado aplicado (1 = sin decimar) */
} CsnhypodbpMagView;

void csnhypodbp_magview_init(CsnhypodbpMagView *v);
void csnhypodbp_magview_free(CsnhypodbpMagView *v);

/* Construye la vista de `tr` para `kind`, recortada a [t_start, t_end] (epoch).
 * `c` trae los coeficientes y bandas del .d de csnmags. Devuelve 0 si OK; -1 si
 * no hay traza, el rango no toca datos o no cabe. */
int csnhypodbp_magview_build(CsnhypodbpMagView *v, EwGuiTrace *tr, int kind,
                             const CsnhypodbpMagCfg *c, double t_start, double t_end);

/* Muestra `i` (0..n-1); NaN si esta fuera. */
double csnhypodbp_magview_at(const CsnhypodbpMagView *v, long i);

/* Ventana visible EFECTIVA del canvas de magnitudes (epoch), la unica fuente de
 * verdad: la usan el dibujo, el clic de zoom, el agarre del handle y el arrastre,
 * de modo que lo que se ve y lo que se agarra son la misma escala.
 *   - `view` activa (zoom, o el encuadre congelado de un arrastre) -> esa ventana;
 *   - si no -> encuadre automatico del ancla: [anchor+t0-m, anchor+t1+m] con
 *     m = csnhypodbp_magview_margin(t0,t1).
 * Devuelve 0 (sin escribir salidas) si el ancla o la ventana no sirven. */
int csnhypodbp_magview_frame(const CsnhypodbpView *view, double anchor,
                             double t0, double t1, double *win0, double *span);

/* Nombre del cursor para `gtk_widget_set_cursor_from_name` segun la zona del
 * canvas: sobre el handle de la franja -> "move", sobre un borde -> "ew-resize",
 * en el resto (zoom por clics) -> "crosshair".
 * NO se usan cursores de mano ("pointer"/"grab"): invariante del modulo.
 * Puro: la GUI solo lo aplica, asi que es testeable headless. */
const char *csnhypodbp_magview_cursor(int on_handle, int on_edge);

/* Margen (s) de contexto a cada lado de la ventana ajustada [t0,t1] (offsets
 * respecto del ancla). */
double csnhypodbp_magview_margin(double t0, double t1);

#endif /* CSNHYPODBP_MAGVIEW_H */
