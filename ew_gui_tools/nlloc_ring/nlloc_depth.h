#ifndef NLLOC_DEPTH_H
#define NLLOC_DEPTH_H

#include <stddef.h>

/* Profundidad fija en NLLoc: NO existe de forma nativa (el motor siempre
 * busca en 3D), asi que se emula con la receta oficial: LOCGRID con 2 nodos
 * en z y un rango muy pequeno, centrado en la profundidad pedida.
 * Logica pura (sin GTK ni el motor): testeable headless. */

/* Reescribe una linea LOCGRID para fijar la profundidad:
 *   nz = 2, z0 = depth_km - spacing_km/2, dz = spacing_km
 * conservando nx/ny/x0/y0/dx/dy y el sufijo (PROB_DENSITY SAVE, ...).
 * Devuelve 0, o -1 si `in` no es un LOCGRID parseable o spacing <= 0. */
int nlloc_locgrid_fixdepth(const char *in, char *out, size_t outn,
                           double depth_km, double spacing_km);

/* Si `in` es `LOCSEARCH OCT ...`, pone init_num_cells_z = 1 (receta oficial
 * de profundidad fija) conservando el resto. Cualquier otra linea se copia
 * tal cual. Devuelve 0, o -1 si `out` es invalido. */
int nlloc_locsearch_fixz(const char *in, char *out, size_t outn);

/* Reemplaza la linea LOCSEARCH de la plantilla por `LOCSEARCH <spec>`.
 * `spec` es "OCT nx ny nz min_node max_nodes scatter density stop" (sin
 * LOCSEARCH). Devuelve 0, o -1 si `in` no es LOCSEARCH o `spec` esta vacio. */
int nlloc_locsearch_set(const char *in, const char *spec, char *out, size_t outn);

#endif /* NLLOC_DEPTH_H */
