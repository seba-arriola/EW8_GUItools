/*
 * bpq_emit.c - Formateadores PUROS de los mensajes que publica el prototipo.
 *
 * Estan separados de la publicacion (bpq_rings.c) a proposito: asi el contrato
 * de bytes es verificable sin anillos ni EarthWorm (AC9). El formato del pick es
 * el MISMO literal que csnhypodbp_ws.c:1728.
 */
#include "bpq_capi.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * TYPE_PICK_SCNL: "%d %d %d %d <STA>.<CHAN>.<NET>.<LOC> ?0 <ts> 0 0 0 <fase> M <pref>"
 *
 * Ojo con el timestamp: se usa "%02d.%03d" y NO "%06.3f" porque ese ancho mete un
 * ESPACIO cuando los segundos son < 10 y csnloc descarta la picada
 * (csnhypodbp_ws.c:1720-1724).
 *
 * `net`/`loc` reales: con "--" HYPOINVERSE no empareja la estacion.
 */
int bpq_pick_msg_format(char *out, size_t n, unsigned char type,
                        unsigned char mod, unsigned char inst, int seq,
                        const char *sta, const char *chan, const char *net,
                        const char *loc, char phase, double t, char pref)
{
    char ts[32];
    time_t sec = (time_t)t;
    double frac = t - (double)sec;
    struct tm *tm;
    int ms;

    if (!out || n == 0 || !sta || !chan) return -1;
    out[0] = '\0';

    tm = gmtime(&sec);
    if (!tm) return -1;
    if (frac < 0.0) frac = 0.0;
    ms = (int)(frac * 1000.0 + 0.5);
    if (ms >= 1000) {
        ms -= 1000;
        sec += 1;
        tm = gmtime(&sec);
    }
    if (!tm) return -1;

    snprintf(ts, sizeof(ts), "%04d%02d%02d%02d%02d%02d.%03d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec, ms);

    snprintf(out, n, "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 %c M %c\n",
             (int)type, (int)mod, (int)inst, seq, sta, chan,
             (net && net[0]) ? net : "--", (loc && loc[0]) ? loc : "--",
             ts, phase, pref ? pref : '-');
    return 0;
}
