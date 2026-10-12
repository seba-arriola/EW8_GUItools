/*
 * Nucleo puro del formulario de busqueda hipocentral (ver csnhypodbp_search.h).
 * Sin GTK ni Earthworm; testeable headless.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csnhypodbp_search.h"

/* Cotas naturales para completar un rango a un solo lado. */
#define LAT_LO (-90.0)
#define LAT_HI (90.0)
#define LON_LO (-180.0)
#define LON_HI (180.0)
#define BIG_LO (-1.0e6)
#define BIG_HI (1.0e6)

static const char *trim(const char *s, char *buf, size_t n)
{
    if (!s) {
        buf[0] = '\0';
        return buf;
    }
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        len--;
    if (len >= n)
        len = n - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    return buf;
}

/* "YYYY-MM-DD" o "YYYY-MM-DD HH:MM:SS" -> "YYYY-MM-DD HH:MM:SS+00". */
static int parse_time(const char *s, char *out, size_t outsz)
{
    int Y, M, D, h = 0, mi = 0, se = 0;
    size_t len = strlen(s);

    if (len == 10) {
        if (sscanf(s, "%4d-%2d-%2d", &Y, &M, &D) != 3)
            return 0;
        if (s[4] != '-' || s[7] != '-')
            return 0;
    } else if (len == 19) {
        if (sscanf(s, "%4d-%2d-%2d %2d:%2d:%2d", &Y, &M, &D, &h, &mi, &se) != 6)
            return 0;
        if (s[4] != '-' || s[7] != '-' || s[10] != ' ' ||
            s[13] != ':' || s[16] != ':')
            return 0;
    } else {
        return 0;
    }

    if (Y < 1900 || Y > 2999 || M < 1 || M > 12 || D < 1 || D > 31 ||
        h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60)
        return 0;

    snprintf(out, outsz, "%04d-%02d-%02d %02d:%02d:%02d+00",
             Y, M, D, h, mi, se);
    return 1;
}

static int parse_num(const char *s, double *out)
{
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0')
        return 0;
    *out = v;
    return 1;
}

/* Parsea un par min/max opcional. Devuelve 1 OK, 0 error (mensaje en err). */
static int range_pair(const char *name, const char *smin, const char *smax,
                      double def_min, double def_max,
                      int *have, double *out_min, double *out_max,
                      char *err, size_t errsz)
{
    char b1[64], b2[64];
    const char *a = trim(smin, b1, sizeof b1);
    const char *c = trim(smax, b2, sizeof b2);

    if (!*a && !*c) {
        *have = 0;
        return 1;
    }

    double lo = def_min, hi = def_max;
    if (*a && !parse_num(a, &lo)) {
        snprintf(err, errsz, "%s: valor invalido '%s'", name, a);
        return 0;
    }
    if (*c && !parse_num(c, &hi)) {
        snprintf(err, errsz, "%s: valor invalido '%s'", name, c);
        return 0;
    }
    if (lo > hi) {
        snprintf(err, errsz, "%s: minimo > maximo", name);
        return 0;
    }

    *have = 1;
    *out_min = lo;
    *out_max = hi;
    return 1;
}

void csnhypodbp_search_fmt_utc(time_t t, char *out, size_t n)
{
    struct tm *tm = gmtime(&t);

    if (!tm) {
        snprintf(out, n, "--");
        return;
    }
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

int csnhypodbp_search_set_date(char *entry, size_t n, int y, int m, int d)
{
    char hora[16] = "00:00:00";
    int hh = 0, mi = 0, se = 0;

    if (!entry || n == 0) return 0;
    /* Si el campo ya traia hora ("YYYY-MM-DD HH:MM:SS"), se conserva. */
    if (strlen(entry) >= 19 && entry[10] == ' ' &&
        sscanf(entry + 11, "%2d:%2d:%2d", &hh, &mi, &se) == 3 &&
        hh >= 0 && hh <= 23 && mi >= 0 && mi <= 59 && se >= 0 && se <= 60)
        snprintf(hora, sizeof(hora), "%02d:%02d:%02d", hh, mi, se);
    snprintf(entry, n, "%04d-%02d-%02d %s", y, m, d, hora);
    return 1;
}

int csnhypodbp_search_build(const char *const fields[CSNHYPODBP_SEARCH_FIELDS],
                            int max_events, CsnhypodbpSearch *out,
                            char *err, size_t errsz)
{
    if (!fields || !out)
        return 0;

    memset(out, 0, sizeof *out);
    if (err && errsz)
        err[0] = '\0';

#define FAIL(msg) do { if (err && errsz) snprintf(err, errsz, "%s", msg); \
                       return 0; } while (0)

    char b[CSNHYPODBP_SEARCH_TIME_LEN * 2];
    const char *t;

    t = trim(fields[CSNHYPODBP_SEARCH_TMIN], b, sizeof b);
    if (!*t)
        FAIL("tiempo origen desde: obligatorio");
    if (!parse_time(t, out->time_min, sizeof out->time_min))
        FAIL("tiempo origen desde: formato YYYY-MM-DD[ HH:MM:SS]");

    t = trim(fields[CSNHYPODBP_SEARCH_TMAX], b, sizeof b);
    if (!*t)
        FAIL("tiempo origen hasta: obligatorio");
    if (!parse_time(t, out->time_max, sizeof out->time_max))
        FAIL("tiempo origen hasta: formato YYYY-MM-DD[ HH:MM:SS]");

    if (!range_pair("latitud", fields[CSNHYPODBP_SEARCH_LATMIN],
                    fields[CSNHYPODBP_SEARCH_LATMAX], LAT_LO, LAT_HI,
                    &out->have_lat, &out->lat_min, &out->lat_max, err, errsz))
        return 0;
    if (!range_pair("longitud", fields[CSNHYPODBP_SEARCH_LONMIN],
                    fields[CSNHYPODBP_SEARCH_LONMAX], LON_LO, LON_HI,
                    &out->have_lon, &out->lon_min, &out->lon_max, err, errsz))
        return 0;
    if (!range_pair("profundidad", fields[CSNHYPODBP_SEARCH_DEPMIN],
                    fields[CSNHYPODBP_SEARCH_DEPMAX], BIG_LO, BIG_HI,
                    &out->have_depth, &out->depth_min, &out->depth_max,
                    err, errsz))
        return 0;
    if (!range_pair("magnitud", fields[CSNHYPODBP_SEARCH_MAGMIN],
                    fields[CSNHYPODBP_SEARCH_MAGMAX], BIG_LO, BIG_HI,
                    &out->have_mag, &out->mag_min, &out->mag_max, err, errsz))
        return 0;

    out->limit = max_events > 0 ? max_events : CSNHYPODBP_SEARCH_DEFAULT_LIMIT;
    return 1;

#undef FAIL
}
