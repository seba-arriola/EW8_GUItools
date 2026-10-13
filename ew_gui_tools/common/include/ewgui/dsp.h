#ifndef EWGUI_DSP_H
#define EWGUI_DSP_H

#include <stddef.h>
#include <stdint.h>

/*
 * DSP puro (sin GTK ni EarthWorm). Unifica el filtro IIR duplicado en
 * csntvp y csnhypodbp, y los helpers de gaps de csnhypodbp.
 *
 * Convención de gaps: INT_MAX marca muestra ausente. El filtro preserva
 * INT_MAX y resetea su estado en cada gap; los helpers también lo respetan.
 */

typedef enum {
    EW_FILTER_HP = 1,   /* High-pass */
    EW_FILTER_LP = 2    /* Low-pass  */
} EwFilterType;

/* Butterworth IIR (orden 2 o 4; 4 = 2 biquads en cascada). */
void ewgui_filter_iir(int32_t *data, size_t n, double fs,
                      EwFilterType type, double fc, int order);

/* Band-pass = HP(f1) seguido de LP(f2) (igual que csntvp). */
void ewgui_filter_bandpass(int32_t *data, size_t n, double fs,
                           double f1, double f2, int order);

/* Interpola runs cortos de INT_MAX (< ~0.5 s) flanqueados por muestras
 * válidas; los runs largos y los de borde quedan como INT_MAX. */
void ewgui_interpolate_short_gaps(int32_t *data, size_t n, double fs);

/* Media de las muestras válidas; escribe `in - mean` en `out` (los gaps
 * se copian como INT_MAX). */
void ewgui_demean(const int32_t *in, size_t n, int32_t *out);

/* Recorta la cola de INT_MAX y devuelve el nuevo largo efectivo. */
size_t ewgui_find_data_end(const int32_t *data, size_t n);

/*
 * Procesado de la traza de VISTA de la sub-pestaña `magnitudes`: es el que hace
 * que la ventana ajustada signifique algo (en Ms, con periodo ~20 s, el pico es
 * invisible en la traza cruda). Es una vista en CUENTAS: sirve para juzgar la
 * ventana, no para leer amplitudes (eso lo hace csnmags, que corrige por
 * respuesta instrumental).
 */

/* Simulacion Wood-Anderson (2 ceros + 2 polos, transformada bilineal): es lo que
 * mide ML. `period`/`damp`/`gain` son los WoodAndersonCoefs del .d de csnmags. */
void ewgui_filter_wood_anderson(int32_t *data, size_t n, double fs,
                                double period, double damp, double gain);

/* Integra (regla del trapecio) a `out` en double: la rampa del desplazamiento
 * que mide Mwp. Los gaps (INT_MAX) dejan NaN. */
void ewgui_integrate(const int32_t *in, size_t n, double dt, double *out);

/* Diezma por decimacion simple (toma 1 de cada `factor`). Devuelve el nuevo
 * largo. Necesario para Ms/Mwp: a 100 sps, un paso banda de 0.05 Hz es
 * numericamente inutil. */
size_t ewgui_decimate(const int32_t *in, size_t n, int factor, int32_t *out);

#endif /* EWGUI_DSP_H */
