#ifndef CSNMAGS_MAG_KEY_H
#define CSNMAGS_MAG_KEY_H

#include <stddef.h>

/*
 * mag_key.c - Clave de evento de magnitud.
 *
 * La magnitud debe identificar la SOLUCION de la que se calculo, no solo el
 * sismo: csnloc (crudo, mod 162), hyp2000_ring (163) y nlloc_ring (164)
 * publican ARCs con el MISMO qid (bytes 136-145 del ARC), asi que el qid por
 * si solo no distingue una solucion de otra. La clave es "(qid)|(mod)".
 */
void mag_key(char *out, size_t n, const char *qid, int mod);

#endif /* CSNMAGS_MAG_KEY_H */
