/*
 * csnhypodbp_magchan - entradas por estacion de un TYPE_MAGNITUDE.
 *
 * POR QUE EXISTE: `rd_chan_mag` de EarthWorm **no acota** sus escrituras contra
 * el tamano que se le pasa (el 4.o argumento solo alimenta su `memset` inicial:
 * rw_mag.c:114 de EarthWorm 8) y ademas parsea IN-PLACE
 * (`tmpMsg = msg`, :119, y `line[len] = '\0'`, :134), o sea que destruye el
 * buffer del llamante. Dimensionar contando '\n' NO sirve de proxy: el formato
 * no parte solo por '\n' (medido 2026-10-14 con `wr_mag_scnl`: 2 '\n' para 3
 * estaciones), y quedarse corto escribe fuera del heap, que glibc detecta
 * minutos despues con SIGABRT (signal 6) y sin dejar log. Le paso a
 * `hypodb_ingest` y estaba latente en `csnhypodbp_ws.c`.
 */
#include "csnhypodbp_magchan.h"

#include <stdlib.h>
#include <string.h>

int csnhypodbp_magchan_parse(const char *msg, size_t len, MAG_CHAN_INFO **out)
{
    MAG_CHAN_INFO *scratch;
    char *copy;
    int cap, n = 0;

    if (out)
        *out = NULL;
    if (!msg || len == 0 || !out)
        return -1;

    /* Cota superior PROVADA en el motor: avanza `index += len + 1` con
       `len >= 0` (rw_mag.c:135) dentro de `while (index < msglen-1)`, asi que
       nunca escribe mas de msglen-1 entradas. */
    cap = (int)len;
    scratch = calloc((size_t)cap, sizeof(*scratch));
    /* Copia con HOLGURA y a CERO: el motor lee 1 byte mas alla del mensaje
       (`tokenlength` -> strchr/strlen, rw_mag.c:384-385); con el relleno a cero
       esa lectura se corta en el primer NUL en vez de seguir por el heap. */
    copy = calloc(len + 64, 1);
    if (!scratch || !copy) {
        free(scratch);
        free(copy);
        return -1;
    }
    memcpy(copy, msg, len);

    /* El rc se IGNORA a proposito: el motor devuelve -1 cuando una linea queda
       partida (mensajes truncados por el llamante) aunque las entradas completas
       YA esten escritas; descartarlas seria perder esas magnitudes. */
    (void)rd_chan_mag(copy, (int)len, scratch,
                      (int)((size_t)cap * sizeof(*scratch)));
    free(copy);

    while (n < cap && scratch[n].sta[0] != '\0')
        n++;
    if (n == 0) {
        free(scratch);
        return 0;
    }

    *out = calloc((size_t)n, sizeof(**out));
    if (!*out) {
        free(scratch);
        return -1;
    }
    memcpy(*out, scratch, (size_t)n * sizeof(**out));
    free(scratch);
    return n;
}
