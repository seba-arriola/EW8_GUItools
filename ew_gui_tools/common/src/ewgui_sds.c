/*
 * Ruta de archivo SDS de SeisComP (nucleo sin GTK; ver include/ewgui/sds.h).
 * Mismo layout que `mseedarchplayer/mseedfilename.c` (formato SCP).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ewgui/sds.h"

/* El SDS de SeisComP escribe el location vacio cuando no hay; EarthWorm usa el
 * literal "--" (LOC_NULL_STRING, trace_buf.h). Aqui se traduce: nulo, vacio o
 * solo-espacios -> ""; los locations reales (00, 10, 04, ...) pasan literales. */
static const char *sds_loc(const char *loc)
{
    const char *p;

    if (!loc)
        return "";
    for (p = loc; *p == ' ' || *p == '\t'; p++)
        ;
    if (*p == '\0')
        return "";
    if (p[0] == '-' && p[1] == '-' && p[2] == '\0')
        return "";
    return loc;
}

int ewgui_sds_path(char *out, size_t outsz, const char *root,
                   const char *net, const char *sta, const char *loc,
                   const char *chan, double t)
{
    time_t tt;
    struct tm tm;
    int year, doy, n;

    if (!out || outsz == 0 || !root || !net || !sta || !chan)
        return -1;

    tt = (time_t)floor(t);
    if (!gmtime_r(&tt, &tm))
        return -1;

    year = tm.tm_year + 1900;
    doy  = tm.tm_yday + 1;

    loc = sds_loc(loc);

    n = snprintf(out, outsz, "%s/%04d/%s/%s/%s.D/%s.%s.%s.%s.D.%04d.%03d",
                 root, year, net, sta, chan, net, sta,
                 loc, chan, year, doy);
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    return 0;
}
