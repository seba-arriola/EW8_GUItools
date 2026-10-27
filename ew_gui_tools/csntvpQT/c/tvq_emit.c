/* Formato de la picada publicada, byte-identico a csntvp_gui.c:320-326:
 *
 *   %d %d %d %d STA.CHAN.NET.LOC ?0 YYYYMMDDhhmmss.mmm 0 0 0 <fase> M
 *
 * El timestamp se arma campo a campo con gmtime (`%06.3f` sobre los segundos)
 * y se cambia la coma decimal por punto: `snprintf("%f")` depende de
 * LC_NUMERIC y el protocolo TYPE_PICK_SCNL no admite comas.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tvq_capi.h"

int tvq_pick_msg_format(char *out, size_t n, int type, int mod, int inst,
                        int seq, const char *sta, const char *chan,
                        const char *net, const char *loc, char phase, double t)
{
    char ts[64];
    time_t sec = (time_t)t;
    double frac = t - (double)sec;
    struct tm *ptm;
    int w;

    if (!out || n == 0)
        return -1;

    ptm = gmtime(&sec);
    if (!ptm)
        return -1;

    snprintf(ts, sizeof ts, "%04d%02d%02d%02d%02d%06.3f",
             ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
             ptm->tm_hour, ptm->tm_min, (double)ptm->tm_sec + frac);
    for (int k = 0; ts[k] != '\0'; k++)
        if (ts[k] == ',')
            ts[k] = '.';

    w = snprintf(out, n, "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 %c M\n",
                 type, mod, inst, seq,
                 sta ? sta : "", chan ? chan : "", net ? net : "",
                 loc ? loc : "", ts,
                 (phase == 'S' || phase == 's') ? 'S' : 'P');
    return (w > 0 && (size_t)w < n) ? 0 : -1;
}
