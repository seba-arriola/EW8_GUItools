#include "csnhypodbp_fixdepth.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int csnhypodbp_fixdepth_parse(const char *txt, double *km)
{
    char buf[64];
    const char *s;
    char *p, *end;
    size_t n;
    int seen_sep = 0, digits = 0, frac_digits = 0;

    if (km) *km = -1.0;
    if (!txt) return 0;

    n = strlen(txt);
    while (n > 0 && isspace((unsigned char)txt[n - 1])) n--;
    s = txt;
    while (n > 0 && isspace((unsigned char)s[0])) { s++; n--; }
    if (n == 0 || n >= sizeof(buf)) return 0;
    memcpy(buf, s, n);
    buf[n] = '\0';

    p = buf;
    if (*p == '+') p++;
    if (!*p) return 0;
    for (; *p; p++) {
        if (*p == '.' || *p == ',') {
            if (seen_sep) return 0;
            seen_sep = 1;
            *p = '.';
        } else if (isdigit((unsigned char)*p)) {
            digits++;
            if (seen_sep) frac_digits++;
        } else {
            return 0;
        }
    }
    if (digits == 0) return 0;
    if (seen_sep && frac_digits == 0) return 0;   /* "12." no es un numero */

    /* Conversion manual: strtod depende del locale (con es_CL "12.5" se corta
       en el punto) y aqui el texto puede venir con punto o con coma. */
    {
        double v = 0.0, scale = 1.0;
        int in_frac = 0;

        end = buf;
        if (*end == '+') end++;
        for (; *end; end++) {
            if (*end == '.') { in_frac = 1; continue; }
            if (!in_frac) {
                v = v * 10.0 + (double)(*end - '0');
                if (v > CSNHYPODBP_FIXDEPTH_MAX_KM) return 0;
            } else {
                scale *= 10.0;
                if (scale <= 1e15)
                    v += (double)(*end - '0') / scale;
                if (v > CSNHYPODBP_FIXDEPTH_MAX_KM) return 0;
            }
        }
        if (v < CSNHYPODBP_FIXDEPTH_MIN_KM || v > CSNHYPODBP_FIXDEPTH_MAX_KM) return 0;
        if (km) *km = v;
    }
    return 1;
}

void csnhypodbp_fixdepth_format(double km, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (!(km >= 0.0) || !isfinite(km)) {
        out[0] = '\0';
        return;
    }
    snprintf(out, n, "%.1f", km);
}
