#ifndef WSTORE_FILTER_H
#define WSTORE_FILTER_H

/*
 * Politica de ingesta: que SCNL se guardan en el store (ChannelFilterFile).
 *
 * El archivo tiene una linea por patron `STA NET CHAN LOC` (separados por
 * espacios/tabs); `#` comenta el resto de la linea y las lineas vacias se
 * ignoran. En cada campo vale `*` (cualquier secuencia, incluida vacia) y `?`
 * (exactamente 1 caracter); el resto es literal y case-sensitive. En LOC, `--`
 * (o vacio) es la localizacion nula de EarthWorm y `*` casa con cualquiera.
 *
 * Sin filtro (f == NULL) se acepta todo: es el comportamiento de siempre.
 * No depende de EarthWorm (ni de trace_buf.h): es testeable headless.
 */

#define WSTORE_FILTER_MAX 256   /* patrones maximos por archivo */

typedef struct WStoreFilter WStoreFilter;

/*
 * Carga los patrones de `path` (vacio/NULL => sin filtro, *out = NULL).
 * 0 OK; <0 error (no se puede leer, linea mal formada o demasiados patrones).
 * Los errores van a stderr con `archivo:linea`. Un archivo existente sin
 * patrones devuelve 0 con 0 patrones (y avisa): equivale a guardar todo.
 */
int  wstore_filter_load(const char *path, WStoreFilter **out);

/*
 * 1 = el SCNL pasa (se guarda), 0 = se descarta. f == NULL => 1.
 * Loc: `--`, "" o NULL se normalizan a vacio; en los cuatro campos se recortan
 * los espacios de los extremos antes de comparar.
 */
int  wstore_filter_accept(const WStoreFilter *f, const char *sta, const char *net,
                          const char *chan, const char *loc);

int  wstore_filter_count(const WStoreFilter *f);   /* nº de patrones (0 si NULL) */

void wstore_filter_free(WStoreFilter *f);

#endif /* WSTORE_FILTER_H */
