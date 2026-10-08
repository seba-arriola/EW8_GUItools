/******************************************************************************
 * report.c — formatea una línea TYPE_PICK_SCNL con el token de fase.          *
 *                                                                            *
 * Formato (idéntico a pick_FP + 11º campo de fase):                          *
 *   type mod inst seq STA.CHAN.NET.LOC fmwt YYYYMMDDhhmmss.mmm amp 0 0 S      *
 ******************************************************************************/

#include "pickS.h"
#include <time.h>

int PickS_Report_Format(char *buf, size_t n, int modid, int instid, int seq,
                        const char *sta, const char *chan, const char *net,
                        const char *loc, double t_epoch, char fm, int weight,
                        long amp, char phase)
{
    time_t    tt = (time_t)floor(t_epoch);
    struct tm tmv;
    int       ms;

    ms = (int)floor((t_epoch - (double)tt) * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; tt += 1; }
    if (ms < 0) ms = 0;

    gmtime_r(&tt, &tmv);

    return snprintf(buf, n,
        "%d %d %d %d %s.%s.%s.%s %c%d %04d%02d%02d%02d%02d%02d.%03d "
        "%ld 0 0 %c\n",
        8, modid, instid, seq,
        sta, chan, net, loc,
        fm, weight,
        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms,
        amp, phase);
}
