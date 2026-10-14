#ifndef CSNHYPODBP_MAGCHAN_H
#define CSNHYPODBP_MAGCHAN_H

#include <stddef.h>

#include <rw_mag.h>

/*
 * Canales (una entrada por estacion) de un TYPE_MAGNITUDE.
 *
 * Devuelve el numero de entradas y deja en `*out` un array `calloc` (liberar con
 * free) con ESA capacidad exacta: la que necesita `rd_chan_mag`, que **no acota**
 * sus escrituras contra el tamano que se le pasa. `*out = NULL` y 0 si el
 * mensaje no trae ninguna; -1 si falta memoria o los argumentos no valen.
 *
 * El mensaje del llamante NO se toca: el motor parsea in-place y lo destruiria
 * (sustituye los '\n' por '\0').
 */
int csnhypodbp_magchan_parse(const char *msg, size_t len, MAG_CHAN_INFO **out);

#endif /* CSNHYPODBP_MAGCHAN_H */
