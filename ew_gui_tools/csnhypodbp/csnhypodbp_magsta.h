/*
 * csnhypodbp_magsta.h - Cache de las magnitudes POR ESTACION que llegan vivas en
 * TYPE_MAGNITUDE (MAG_CHAN_INFO), para la sub-pestaña `magnitudes`.
 *
 * El mensaje trae las estaciones de una magnitud concreta de una solucion
 * concreta, pero la tabla muestra una sola a la vez: se guardan los ultimos
 * conjuntos (qid, modulo, magnitud) en un anillo acotado y el que coincide con
 * la seleccion se usa tal cual. Si no hay (p. ej. la solucion viene del
 * historial), la GUI cae a `station_magnitude` de la BD.
 */
#ifndef CSNHYPODBP_MAGSTA_H
#define CSNHYPODBP_MAGSTA_H

#include "csnhypodbp_magrow.h"

#define CSNHYPODBP_MAGSTA_RING 4

typedef struct {
    int    used;
    int    qid;
    int    mod;      /* modulo origen del ARC (162/163/164) */
    int    slot;     /* 0=ML 1=Mwp 2=Mb 3=Ms */
    int    n;
    CsnhypodbpMagIn vals[CSNHYPODBP_MAGSTA_MAX];
} CsnhypodbpMagStaSet;

typedef struct {
    CsnhypodbpMagStaSet set[CSNHYPODBP_MAGSTA_RING];
    int next;        /* proximo hueco a reutilizar (round-robin) */
} CsnhypodbpMagStaStore;

void csnhypodbp_magsta_init(CsnhypodbpMagStaStore *s);

/* Guarda (o reemplaza) el conjunto de esa solucion y magnitud. `n` se recorta a
 * CSNHYPODBP_MAGSTA_MAX y `n <= 0` no guarda nada. */
void csnhypodbp_magsta_put(CsnhypodbpMagStaStore *s, int qid, int mod, int slot,
                           const CsnhypodbpMagIn *vals, int n);

/* Copia a `out` el conjunto de esa solucion y magnitud. Devuelve cuantas filas
 * copio (0 si no esta). */
int csnhypodbp_magsta_take(const CsnhypodbpMagStaStore *s, int qid, int mod,
                           int slot, CsnhypodbpMagIn *out, int max);

#endif
