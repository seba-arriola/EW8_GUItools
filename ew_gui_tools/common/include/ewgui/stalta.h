#ifndef EWGUI_STALTA_H
#define EWGUI_STALTA_H

#include <stddef.h>
#include <stdint.h>

/*
 * STA/LTA clásico (energía) para la traza de vista. Sin GTK.
 *
 * CF[n] = (media de x^2 en la STA) / (media de x^2 en la LTA), con la LTA
 * siempre >= STA. La CF es 0 mientras la LTA no está llena y en los gaps
 * (INT_MAX), que reinician el detector.
 *
 * OJO con el tiempo: el disparo NO es el onset. La STA tarda en llenarse,
 * así que el cruce del umbral llega después del arribo, con un retardo que
 * depende del SNR. ewgui_stalta_triggers() devuelve el índice CRUDO del
 * cruce; el llamador decide qué corrección estática aplica (el `.d` de
 * csnhypodbp usa -sta/2 por defecto, y lo muestra como estimación, nunca
 * como pick).
 */

typedef struct {
    double  fs;
    size_t  nsta, nlta;   /* ventanas en muestras (nsta >= 1, nlta >= nsta) */
    double *buf;          /* potencias, nlta slots */
    size_t  pos;          /* próximo slot a escribir */
    size_t  count;        /* muestras escritas, saturado en nlta */
    double  sum_sta, sum_lta;
} EwGuiStaLta;

void   ewgui_stalta_init(EwGuiStaLta *s, double fs, double sta_s, double lta_s);
void   ewgui_stalta_free(EwGuiStaLta *s);
double ewgui_stalta_update(EwGuiStaLta *s, double x);

/* CF de una traza completa. cf_out debe tener n doubles. */
void   ewgui_stalta_run(const int32_t *x, size_t n, double fs,
                        double sta_s, double lta_s, double *cf);

/* Cruces de `on` hacia arriba, cerrados con `off`. Devuelve el nº escrito
 * en idx (hasta max). */
int    ewgui_stalta_triggers(const double *cf, size_t n,
                             double on, double off, size_t *idx, int max);

#endif /* EWGUI_STALTA_H */
