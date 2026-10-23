/* Parseo minimo del TYPE_HYP2000ARC: los mismos offsets que csnrv.c:441-466.
 *
 * t0 son 14 digitos YYYYMMDDHHMMSS + 2 de centesimas; lat/lon van en
 * grados+minutos/100 con su letra (S/W niegan); depth en centesimas de km;
 * qid en 136..145 y version en 178..181 (fallback al caracter 161 cuando el
 * ARC viejo no trae el campo).
 *
 * `timegm` en vez de `setenv("TZ","GMT")`: mismo resultado UTC sin cambiar el
 * entorno del proceso (el GTK lo hacia en cada mensaje).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crv_capi.h"

/* Copia `n` caracteres del mensaje a `buf` (NUL al final). `len` es el largo
 * REAL del mensaje; `buf` solo tiene que caber n+1. */
static int num_at(const char *msg, size_t len, size_t off, size_t n,
                  char *buf, size_t buflen)
{
    if (n + 1 > buflen || off + n > len)
        return -1;
    memcpy(buf, msg + off, n);
    buf[n] = '\0';
    return 0;
}

int crv_arc_parse(const char *msg, size_t len, CrvArc *out)
{
    char buf[24];
    char tmp[8];
    struct tm t;
    double otime;
    double lat, lon, depth;
    int qid, qver;

    if (!msg || !out)
        return -1;
    /* Necesitamos hasta 182 (version); si no, el fallback de 161. */
    if (len < 36 || len < 146)
        return -1;

    /* --- t0: 14 digitos + centesimas ------------------------------------ */
    if (num_at(msg, len, 0, 14, buf, sizeof(buf)) != 0)
        return -1;
    memset(&t, 0, sizeof(t));
    memcpy(tmp, buf, 4);      tmp[4] = '\0'; t.tm_year = atoi(tmp) - 1900;
    memcpy(tmp, buf + 4, 2);  tmp[2] = '\0'; t.tm_mon  = atoi(tmp) - 1;
    memcpy(tmp, buf + 6, 2);  tmp[2] = '\0'; t.tm_mday = atoi(tmp);
    memcpy(tmp, buf + 8, 2);  tmp[2] = '\0'; t.tm_hour = atoi(tmp);
    memcpy(tmp, buf + 10, 2); tmp[2] = '\0'; t.tm_min  = atoi(tmp);
    memcpy(tmp, buf + 12, 2); tmp[2] = '\0'; t.tm_sec  = atoi(tmp);
    otime = (double)timegm(&t);

    if (num_at(msg, len, 14, 2, buf, sizeof(buf)) == 0)
        otime += atof(buf) / 100.0;

    /* --- latitud: GGMMM + dir ------------------------------------------- */
    if (num_at(msg, len, 16, 2, buf, sizeof(buf)) != 0)
        return -1;
    lat = atof(buf);
    if (num_at(msg, len, 19, 4, buf, sizeof(buf)) != 0)
        return -1;
    lat += (atof(buf) / 100.0) / 60.0;
    if (msg[18] == 'S')
        lat = -lat;

    /* --- longitud: GGGMMM + dir ----------------------------------------- */
    if (num_at(msg, len, 23, 3, buf, sizeof(buf)) != 0)
        return -1;
    lon = atof(buf);
    if (num_at(msg, len, 27, 4, buf, sizeof(buf)) != 0)
        return -1;
    lon += (atof(buf) / 100.0) / 60.0;
    if (msg[26] == 'W')
        lon = -lon;

    /* --- profundidad (centesimas de km) --------------------------------- */
    if (num_at(msg, len, 31, 5, buf, sizeof(buf)) != 0)
        return -1;
    depth = atof(buf) / 100.0;

    /* --- qid y version --------------------------------------------------- */
    if (num_at(msg, len, 136, 10, buf, sizeof(buf)) != 0)
        return -1;
    qid = atoi(buf);

    qver = 0;
    if (len >= 182 && num_at(msg, len, 178, 4, buf, sizeof(buf)) == 0)
        qver = atoi(buf);
    if (qver == 0 && len >= 162) {
        buf[0] = msg[161];
        buf[1] = '\0';
        qver = atoi(buf);
    }
    if (qid <= 0)
        return -1;

    out->t0 = otime;
    out->lat = lat;
    out->lon = lon;
    out->depth_km = depth;
    out->qid = qid;
    out->version = qver;
    return 0;
}
