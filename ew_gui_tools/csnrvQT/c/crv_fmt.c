/* Formatos de la vista, identicos a los `snprintf` de csnrv.c:
 *   origin time  -> "<HH:MM:SS UTC>\n<DD/MM/YYYY>"  (csnrv.c:495-499)
 *   coordenadas  -> "Lat: %.3f  Lon: %.3f"          (csnrv.c:501-503)
 *   profundidad  -> "Depth: %.0f km"                (csnrv.c:504-506)
 *   transcurrido -> "HH:MM:SS" (negativo = 0)       (csnrv.c:520-532)
 * y el `iso_epoch` del seed de BD (csnrv.c:842-859).
 *
 * Vive en C puro para que `test_state.c` los fije sin abrir una ventana.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "crv_capi.h"

void crv_fmt_origin(double t0, char *out, size_t n)
{
    time_t rawtime;
    struct tm tm;

    if (!out || n == 0)
        return;
    out[0] = '\0';
    if (t0 <= 0.0)
        return;

    rawtime = (time_t)(t0 + 0.5);
    if (!gmtime_r(&rawtime, &tm))
        return;
    snprintf(out, n, "%02d:%02d:%02d UTC\n%02d/%02d/%04d",
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
}

void crv_fmt_coords(double lat, double lon, char *out, size_t n)
{
    if (!out || n == 0)
        return;
    snprintf(out, n, "Lat: %.3f  Lon: %.3f", lat, lon);
}

void crv_fmt_depth(double depth_km, char *out, size_t n)
{
    if (!out || n == 0)
        return;
    snprintf(out, n, "Depth: %.0f km", depth_km);
}

void crv_fmt_elapsed(time_t now, double t0, int has_data, char *out, size_t n)
{
    long diff;
    int hours, minutes, seconds;

    if (!out || n == 0)
        return;
    if (!has_data || t0 <= 0.0) {
        snprintf(out, n, "--:--:--");
        return;
    }
    diff = (long)difftime(now, (time_t)t0);
    if (diff < 0)
        diff = 0;
    hours = (int)(diff / 3600);
    minutes = (int)((diff % 3600) / 60);
    seconds = (int)(diff % 60);
    snprintf(out, n, "%02d:%02d:%02d", hours, minutes, seconds);
}

double crv_iso_epoch(const char *s)
{
    int y, mo, d, h, mi;
    double sec;
    struct tm tm;

    if (!s || sscanf(s, "%4d-%2d-%2d %2d:%2d:%lf",
                     &y, &mo, &d, &h, &mi, &sec) != 6)
        return 0.0;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = (int)sec;
    return (double)timegm(&tm) + (sec - (int)sec);
}
