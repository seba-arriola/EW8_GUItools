/*
 * bpq_pin.h - Origen fijado ("pin") de la pestana Events.
 *
 * Nucleo PURO (sin Qt): guarda la identidad de UNA solucion como
 * "<qid>:<mod>:<version>" y decide si sigue vigente. La ventana lo usa para
 * que Procesar (Selected solution / Magnitude / estaciones / ondas) no se
 * mueva cuando el anillo recompone o reordena la tabla de Origins.
 *
 * Regla de uso: el pin SOLO cambia fijando otro origen; no se suelta con un
 * clic simple. Se limpia al cambiar de evento, al buscar/limpiar o cuando la
 * solucion fijada desaparece (bpq_pin_reconcile).
 */
#ifndef BPQ_PIN_H
#define BPQ_PIN_H

#include <stddef.h>

#define BPQ_PIN_KEY_MAX 64

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BpqPin {
    char sol_key[BPQ_PIN_KEY_MAX];
} BpqPin;

void        bpq_pin_init(BpqPin *p);
void        bpq_pin_clear(BpqPin *p);
/* Copia la clave. 0 OK; -1 argumentos vacios o clave de >= BPQ_PIN_KEY_MAX. */
int         bpq_pin_set(BpqPin *p, const char *sol_key);
int         bpq_pin_active(const BpqPin *p);
const char *bpq_pin_key(const BpqPin *p);
int         bpq_pin_matches(const BpqPin *p, const char *sol_key);
/* Conserva el pin si su clave aparece en `keys`; si no, lo limpia.
 * Devuelve 1 si sigue activo, 0 si se limpio (o no habia pin). */
int         bpq_pin_reconcile(BpqPin *p, const char *const *keys, int n);

/* Unica definicion del formato de identidad de una solucion:
 * "<qid>:<mod>:<version>". 0 OK; -1 buffer invalido o truncado. */
int         bpq_origin_key(int qid, int mod, unsigned version, char *out,
                           size_t n);

#ifdef __cplusplus
}
#endif

#endif /* BPQ_PIN_H */
