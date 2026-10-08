#ifndef EWGUI_WAVE_H
#define EWGUI_WAVE_H

#include <stddef.h>
#include <stdint.h>

#include "ewgui/dsp.h"

/*
 * Modelo de traza: extracción + interpolación de gaps + DC + filtro y
 * envolvente min/max por columna, con caché del procesado.
 *
 * Extraído de csntvp.c (compute_station_envelope). Sin GTK.
 * Convención de gaps: INT_MAX = muestra ausente.
 */

/* Parámetros del filtro de la envolvente (0 = sin filtro). */
typedef struct {
    int    filter_type;   /* 0=raw, 1=HP, 2=LP, 3=BP (HP+LP) */
    double f1, f2;
    int    order;
} EwFilterParams;

/* Caché del procesado y de la envolvente. Propiedad del llamador;
 * inicializar con ewgui_trace_cache_init antes de usar. */
typedef struct {
    int32_t        *proc_buf;
    long            proc_cap;
    long            proc_abs_start, proc_abs_end;
    int             proc_valid;
    int             proc_found_first;
    EwFilterParams  proc_params;

    double *env_min;
    double *env_max;
    int    *env_has;
    int     env_cap;
    int     env_width;
    int     env_valid;
    double  env_max_abs;
    int     env_found_first;
} EwTraceCache;

void ewgui_trace_cache_init(EwTraceCache *c);
void ewgui_trace_cache_free(EwTraceCache *c);
void ewgui_trace_cache_invalidate(EwTraceCache *c);

/*
 * Calcula (con caché) la envolvente min/max por columna de la ventana visible.
 *   circ, circ_size : buffer circular int32 (gaps = INT_MAX)
 *   last_abs_idx    : mayor índice absoluto con dato
 *   rate            : muestras/s (<=0 -> 20.0)
 *   t_end           : extremo derecho de la ventana (s), p. ej. g_smooth_time
 *   window_secs     : ancho de la ventana visible (s)
 *   pad_secs        : padding hacia atrás para que muera el ringing del filtro
 *   width           : nº de columnas (píxeles) de la ventana visible
 *   p               : parámetros del filtro (NULL = raw)
 * Devuelve 1 si recalculó el procesado, 0 si fue cache-hit, -1 si no se pudo.
 * La envolvente (env_min/env_max/env_has/env_width/env_max_abs) se actualiza
 * siempre que haya datos.
 */
int ewgui_trace_envelope(const int32_t *circ, long circ_size,
                         int64_t last_abs_idx, double rate,
                         double t_end, double window_secs, double pad_secs,
                         int width, const EwFilterParams *p, EwTraceCache *c);

/* ------------------------------------------------------------------
 * EwGuiTrace: modelo de traza que POSEE el buffer circular crudo y su
 * vista filtrada, más la tasa de muestreo y el tiempo del primer dato.
 * Pensado para visores que descargan trazas y las filtran para dibujar
 * (csnhypodbp). Sin GTK.
 * ------------------------------------------------------------------ */
typedef struct EwGuiTrace EwGuiTrace;

EwGuiTrace *ewgui_trace_new(long capacity);
void        ewgui_trace_free(EwGuiTrace *t);

/* Rellena ambos buffers con INT_MAX; length=0, rate=0, oldest=0. */
void        ewgui_trace_clear(EwGuiTrace *t);

long        ewgui_trace_capacity(const EwGuiTrace *t);
long        ewgui_trace_length(const EwGuiTrace *t);
void        ewgui_trace_set_length(EwGuiTrace *t, long n);
double      ewgui_trace_rate(const EwGuiTrace *t);
void        ewgui_trace_set_rate(EwGuiTrace *t, double rate);
double      ewgui_trace_oldest(const EwGuiTrace *t);
void        ewgui_trace_set_oldest(EwGuiTrace *t, double t0);

int32_t    *ewgui_trace_raw(EwGuiTrace *t);
int32_t    *ewgui_trace_filtered(EwGuiTrace *t);

/* Interpola gaps cortos y recorta la cola de INT_MAX del buffer crudo. */
void        ewgui_trace_finish(EwGuiTrace *t);

/* demean(crudo) -> filtrado y aplica el filtro p (filter_type 0 = solo demean). */
void        ewgui_trace_filter(EwGuiTrace *t, const EwFilterParams *p);

#endif /* EWGUI_WAVE_H */
