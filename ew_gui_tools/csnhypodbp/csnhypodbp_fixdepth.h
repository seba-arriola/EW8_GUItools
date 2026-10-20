#ifndef CSNHYPODBP_FIXDEPTH_H
#define CSNHYPODBP_FIXDEPTH_H

#include <stddef.h>

/* Caja `depth` de la barra de picking (profundidad fija del `locate`).
 * Logica pura, sin GTK: la capa de vista solo usa estas dos funciones. */

#define CSNHYPODBP_FIXDEPTH_MIN_KM 0.0
#define CSNHYPODBP_FIXDEPTH_MAX_KM 1000.0

/* 1 = valido (y deja el valor en `km`); 0 = invalido (deja `km` en -1).
 * Acepta espacios alrededor, '+' opcional, digitos y un separador decimal
 * '.' o ','. Rechaza vacio, signo '-', exponente, texto sobrante, NaN/Inf,
 * "12." sin decimales y valores fuera de [0, 1000]. */
int csnhypodbp_fixdepth_parse(const char *txt, double *km);

/* "%.1f" si km es finito y >= 0; cadena vacia en cualquier otro caso. */
void csnhypodbp_fixdepth_format(double km, char *out, size_t n);

#endif /* CSNHYPODBP_FIXDEPTH_H */
