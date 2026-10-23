/* Fixture de TYPE_HYP2000ARC para los tests C.
 *
 * Los offsets son los del ARC (csnrv.c:441-466): t0 0..13 + centesimas 14..15,
 * lat 16..22, lon 23..30, depth 31..35, qid 136..145, version 178..181 y el
 * fallback de 1 digito en 161.
 */
#ifndef CRV_ARC_FIXTURE_H
#define CRV_ARC_FIXTURE_H

#include <stdio.h>
#include <string.h>

static void arc_put(char *buf, size_t off, const char *s)
{
    memcpy(buf + off, s, strlen(s));
}

/* Devuelve el largo util (300). Los huecos quedan a espacios, como el ARC. */
static size_t arc_build(char *buf, const char *t0_14, const char *cent,
                        const char *lat_deg, char lat_dir, const char *lat_min,
                        const char *lon_deg, char lon_dir, const char *lon_min,
                        const char *depth5, const char *qid,
                        const char *ver4, char ver1)
{
    memset(buf, ' ', 300);
    arc_put(buf, 0, t0_14);
    arc_put(buf, 14, cent);
    arc_put(buf, 16, lat_deg);
    buf[18] = lat_dir;
    arc_put(buf, 19, lat_min);
    arc_put(buf, 23, lon_deg);
    buf[26] = lon_dir;
    arc_put(buf, 27, lon_min);
    arc_put(buf, 31, depth5);
    arc_put(buf, 136, qid);
    arc_put(buf, 178, ver4);
    buf[161] = ver1;
    buf[300 - 1] = '\0';
    return 300;
}

#endif /* CRV_ARC_FIXTURE_H */
