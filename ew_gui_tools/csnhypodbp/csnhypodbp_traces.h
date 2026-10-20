#ifndef CSNHYPODBP_TRACES_H
#define CSNHYPODBP_TRACES_H

/* Rejilla vertical del canvas de trazas de la pestana `picking` (nucleo puro,
 * sin GTK). `tracesPerScreen` no cambia QUE se dibuja (los canales de la
 * estacion seleccionada), solo CUANTAS bandas caben a la vez: el resto queda
 * por debajo del viewport y se alcanza con la rueda.
 */

/* tracesPerScreen: solo 1..3 (mas trazas por pantalla saturan la vista). */
#define CSNHYPODBP_TRACES_PER_SCREEN_MIN 1
#define CSNHYPODBP_TRACES_PER_SCREEN_MAX 3

/* Suelo de alto de banda: fuente unica (csnhypodbp.h lo reusa como
 * CSNHYPODBP_BAND_MIN, que tambien usa el canvas de magnitudes). */
#define CSNHYPODBP_TRACES_BAND_MIN 110

/* Margen vertical del canvas (30 px arriba + 30 abajo) y centro de la primera
 * banda: mismo contrato que ya usa on_draw_signal en csnhypodbp_gui.c. */
#define CSNHYPODBP_TRACES_MARGIN 60
#define CSNHYPODBP_TRACES_TOP    30

typedef struct {
    int slot_count;   /* huecos de la rejilla: max(n_rows, per_screen); >= 1 */
    int band_h;       /* alto de banda en px; >= band_min */
    int canvas_h;     /* alto total que pide el canvas: slot_count*band_h + MARGIN */
    int per_screen;   /* tracesPerScreen normalizado (1..3) */
} CsnhypodbpTracesLayout;

/* Alto de banda derivado del VIEWPORT del scrolled (no del canvas): asi pedir
 * mas alto no realimenta el calculo. `band_min` es CSNHYPODBP_BAND_MIN.
 * Propiedades (las cubre test_traces.c):
 *   - band_h no depende de n_rows: anadir/quitar canales no encoge las bandas;
 *   - n_rows > per_screen  => canvas_h > viewport_h (aparece el scroll);
 *   - n_rows <= per_screen => no hay scroll si el viewport da para el suelo. */
CsnhypodbpTracesLayout csnhypodbp_traces_layout(int n_rows, int per_screen,
                                                int viewport_h, int band_min);

/* Devuelve el valor si esta en [MIN,MAX]; 0 si es invalido (el llamante avisa
 * en el log y conserva el valor previo). */
int csnhypodbp_traces_per_screen_norm(int raw);

#endif
