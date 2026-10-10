/******************************************************************************
 * csnhypodbp_trace.h                                                          *
 *                                                                             *
 * Nucleo PURO (sin GTK) de la pestana de ondas: parseo de TYPE_PICK_SCNL      *
 * (token 11 fase / token 12 origen), captura por estacion y seleccion de la    *
 * referencia P (picada o teorica). Se testea headless.                        *
 *                                                                             *
 * Regla del origen (identica a csntvp): token 12 'M'/'m' => manual; ausente o  *
 * cualquier otro => automatico 'A'. La fase: token 11 'S'/'s' => S, si no P.  *
 ******************************************************************************/
#ifndef CSNHYPODBP_TRACE_H
#define CSNHYPODBP_TRACE_H

#include <stddef.h>

/* Capacidad de la captura circular por estacion (compilada). */
#define CSNHYPODBP_CAP_MAX 64

/* Picada capturada del PICK_RING. */
typedef struct {
    char   sta[16];
    char   chan[16];
    char   phase;    /* 'P' | 'S' */
    char   origin;   /* 'A' (automatico) | 'M' (manual) */
    double t;        /* epoch (s) */
} CsnhypodbpTracePick;

/* Parseo de un mensaje TYPE_PICK_SCNL. Devuelve 1 si es valido y copia en
 * `out`; 0 si no lo es. */
int csnhypodbp_pick_parse(const char *msg, CsnhypodbpTracePick *out);

/* Captura circular por estacion. */
typedef struct {
    CsnhypodbpTracePick picks[CSNHYPODBP_CAP_MAX];
    int n;      /* cuantas hay (<= CSNHYPODBP_CAP_MAX) */
    int next;   /* indice circular de insercion    */
} CsnhypodbpPickCap;

void csnhypodbp_cap_init(CsnhypodbpPickCap *cap);
void csnhypodbp_cap_add(CsnhypodbpPickCap *cap, const CsnhypodbpTracePick *p);

/* Picada de `phase` de `sta` (ignora canal) mas cercana a t0 dentro de
 * window_sec. Devuelve 1 y copia si la encuentra; 0 si no. */
int csnhypodbp_cap_find(const CsnhypodbpPickCap *cap, const char *sta,
                        char phase, double t0, double window_sec,
                        CsnhypodbpTracePick *out);

/* Copia hasta `max_out` picadas de `sta`/`phase` dentro de window_sec.
 * Devuelve cuantas copio (<= max_out). */
int csnhypodbp_cap_find_all(const CsnhypodbpPickCap *cap, const char *sta,
                            char phase, double t0, double window_sec,
                            CsnhypodbpTracePick *out, int max_out);

/* Referencia P: primer pick de fase P en `p` (n) si existe (devuelve 1 y
 * *ref_t = su tiempo); si no, devuelve 0 y *ref_t = theo_p. */
int csnhypodbp_ref_pick(const CsnhypodbpTracePick *p, int n,
                        double theo_p, double *ref_t);

/* Color de traza: negra (0,0,0) si hay P picada, gris (0.7,0.7,0.7) si no. */
void csnhypodbp_trace_rgb(int has_p_pick, double rgb[3]);

/* Fase elegida por tecla: 'S'/'s' => 'S', resto => 'P'. */
char csnhypodbp_phase_from_char(char c);

#endif /* CSNHYPODBP_TRACE_H */
