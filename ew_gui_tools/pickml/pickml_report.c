/******************************************************************************
 * pickml_report.c — formatea una línea TYPE_PICK_SCNL.                       *
 *                                                                            *
 * Formato (contrato de csnloc/pick_scln.c y pickS/report.c):                 *
 *   type mod inst seq STA.CHAN.NET.LOC fmw YYYYMMDDhhmmss.mmm amp 0 0 P|S A  *
 *                                                                            *
 * OJO con el orden: el SCNL va STA.CHAN.NET.LOC (no STA.NET.LOC.CHAN).       *
 * Se emite origen 'A' (automático) para que hypodb no lo tome como manual.    *
 ******************************************************************************/

#include "pickml.h"

int pm_report_format(char *buf, size_t n, int modid, int instid, int seq,
                     const char *sta, const char *chan, const char *net,
                     const char *loc, double t_epoch, int weight, char phase)
{
    time_t    tt = (time_t)floor(t_epoch);
    struct tm tmv;
    int       ms;

    if (!buf || n == 0) return -1;

    ms = (int)floor((t_epoch - (double)tt) * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; tt += 1; }
    if (ms < 0) ms = 0;

    gmtime_r(&tt, &tmv);

    return snprintf(buf, n,
        "%d %d %d %d %s.%s.%s.%s ?%d %04d%02d%02d%02d%02d%02d.%03d "
        "0 0 0 %c A\n",
        8, modid, instid, seq,
        sta, chan, net, loc,
        weight,
        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms,
        phase);
}
