#ifndef CSNHYPODBP_MANUAL_H
#define CSNHYPODBP_MANUAL_H

#include <stddef.h>

/*
 * csnhypodbp_manual.c - Overlay LOCAL de picadas manuales de la pestaña Ondas.
 *
 * Mientras se pica (P/S + clic izq) las picadas quedan SOLO aqui: no se emiten
 * a PICK_RING (eso ocurre una unica vez al pulsar Confirm). Cancel y el clic
 * derecho actuan solo sobre este overlay.
 */

#define CSNHYPODBP_MANUAL_MAX 256

typedef struct {
    char   sta[16];
    char   chan[16];
    char   phase;    /* 'P' | 'S' */
    double t;        /* epoch */
} CsnhypodbpManualPick;

void csnhypodbp_manual_clear(void);
void csnhypodbp_manual_add(const CsnhypodbpManualPick *p);
/* Quita todas las picadas de una estacion; devuelve cuantas quito. */
int  csnhypodbp_manual_remove_station(const char *sta);
/* Quita las picadas de una traza (estacion+canal); devuelve cuantas quito. */
int  csnhypodbp_manual_remove_trace(const char *sta, const char *chan);
int  csnhypodbp_manual_count(void);
/* Copia hasta `max` picadas; devuelve cuantas hay. */
int  csnhypodbp_manual_list(CsnhypodbpManualPick *out, int max);

#endif /* CSNHYPODBP_MANUAL_H */
