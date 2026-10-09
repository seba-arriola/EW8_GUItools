#ifndef CSNTVP_UTIL_H
#define CSNTVP_UTIL_H

#include <stddef.h>

/*
 * Lógica pura de csntvp (sin GTK ni EarthWorm), testeable headless:
 * parseo/etiquetado de picks TYPE_PICK_SCNL y colores hex.
 *
 * Formato TYPE_PICK_SCNL extendido (compatible hacia atrás):
 *   8 mod inst seq STA.CHAN.NET.LOC fmwt YYYYMMDDhhmmss.mmm amp 0 0 [phase] [origin]
 *   - phase  (token 11, opcional): 'P' | 'S'   (ausente => 'P')
 *   - origin (token 12, opcional): 'A' | 'M'   (ausente => 'A'; A=automático, M=manual)
 */

typedef struct {
    int    mod, inst, seq;
    char   sta[16], chan[16], net[8], loc[8];
    char   phase;   /* 'P' | 'S' */
    char   origin;  /* 'A' (automático) | 'M' (manual) */
    double t;       /* tiempo del pick en epoch (UTC) */
} CsntvpPickMsg;

/* Parsea una línea TYPE_PICK_SCNL. Devuelve 1 si es válida, 0 si no. */
int  csntvp_pick_parse(const char *msg, CsntvpPickMsg *out);

/* Rellena buf con "P(A)" | "P(M)" | "S(A)" | "S(M)". */
void csntvp_pick_label(char phase, char origin, char *buf, size_t n);

/* Color "#RRGGBB"/"RRGGBB" -> rgb[0..2] en [0,1]. 0 = OK, -1 = inválido. */
int  csntvp_parse_hexcolor(const char *s, double rgb[3]);

#endif /* CSNTVP_UTIL_H */
