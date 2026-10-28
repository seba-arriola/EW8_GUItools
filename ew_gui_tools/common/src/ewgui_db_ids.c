#include "ewgui/db_ids.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Ver common/include/ewgui/db_ids.h. Movido desde hypodb (hypodb_ids.c) cuando
 * la GUI pasó a ser el segundo escritor de la base (Confirm con Mww): tener una
 * sola copia es lo que evita que los ids diverjan.
 */

static int fits(int written, size_t n)
{
    return written >= 0 && (size_t)written < n;
}

int ewgui_event_id(char *out, size_t n, const char *qid, double otime)
{
    char stamp[32] = "";

    if (!out || n == 0 || !qid || !*qid)
        return -1;
    if (otime > 0.0) {
        /* Sufijo UTC: el rango de qid de csnloc se repite cada ~27,7 h, así que
           sin él dos sismos de sesiones distintas compartirían event_id. */
        time_t t = (time_t)otime;
        struct tm tmv;

        gmtime_r(&t, &tmv);
        strftime(stamp, sizeof stamp, "-%Y%m%d%H%M%S", &tmv);
    }
    return fits(snprintf(out, n, "E%s%s", qid, stamp), n) ? 0 : -1;
}

static int all_digits(const char *s)
{
    if (!s || !*s)
        return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9')
            return 0;
    return 1;
}

int ewgui_origin_id_build(char *out, size_t n, const char *event_id, int mod,
                          unsigned version)
{
    int written;

    if (!out || n == 0 || !event_id || !*event_id || mod <= 0)
        return -1;
    /* El campo de version del ARC son 4 caracteres (arc_layout.h). */
    if (version > 9999u)
        return -1;
    if (version == 0)
        written = snprintf(out, n, "%s:%d", event_id, mod);
    else
        written = snprintf(out, n, "%s:%d:%u", event_id, mod, version);
    return fits(written, n) ? 0 : -1;
}

int ewgui_origin_id_build_iter(char *out, size_t n, const char *event_id,
                               int mod, unsigned version)
{
    int written;

    if (!out || n == 0 || !event_id || !*event_id || mod <= 0)
        return -1;
    /* Una iteracion SIEMPRE lleva version: sin ella no seria una fila propia. */
    if (version == 0 || version > 9999u)
        return -1;
    written = snprintf(out, n, "%s:%d:%u:I", event_id, mod, version);
    return fits(written, n) ? 0 : -1;
}

int ewgui_origin_id(char *out, size_t n, const char *event_id, int mod)
{
    return ewgui_origin_id_build(out, n, event_id, mod, 0);
}

int ewgui_origin_id_parse(const char *id, char *event_id, size_t n,
                          int *mod, unsigned *version, int *confirmed)
{
    const char *c1, *c2;
    char mtmp[16];
    size_t elen, mlen;
    long m;
    unsigned v = 0;
    int is_iter = 0;

    if (mod)
        *mod = 0;
    if (version)
        *version = 0;
    if (confirmed)
        *confirmed = 0;
    if (event_id && n)
        event_id[0] = '\0';
    if (!id || !*id)
        return -1;
    c1 = strchr(id, ':');
    if (!c1 || c1 == id)
        return -1;
    elen = (size_t)(c1 - id);
    c2 = strchr(c1 + 1, ':');

    mlen = c2 ? (size_t)(c2 - (c1 + 1)) : strlen(c1 + 1);
    if (mlen == 0 || mlen >= sizeof mtmp)
        return -1;
    memcpy(mtmp, c1 + 1, mlen);
    mtmp[mlen] = '\0';
    if (!all_digits(mtmp))
        return -1;
    m = strtol(mtmp, NULL, 10);
    if (m <= 0 || m > 999999)
        return -1;

    if (c2) {
        const char *c3 = strchr(c2 + 1, ':');
        char vtmp[8];
        size_t vlen = c3 ? (size_t)(c3 - (c2 + 1)) : strlen(c2 + 1);

        if (vlen == 0 || vlen > 4 || vlen >= sizeof vtmp)
            return -1;
        memcpy(vtmp, c2 + 1, vlen);
        vtmp[vlen] = '\0';
        if (!all_digits(vtmp))
            return -1;
        v = (unsigned)strtoul(vtmp, NULL, 10);
        if (v == 0 || v > 9999u)
            return -1;
        /* Cuarto campo: SOLO el marcador de iteracion "I". Una confirmacion
           lleva 3 campos y nunca el marcador. */
        if (c3) {
            if (strcmp(c3 + 1, "I") != 0)
                return -1;
            is_iter = 1;
        }
    }

    if (event_id && n) {
        if (elen + 1 > n)
            return -1;
        memcpy(event_id, id, elen);
        event_id[elen] = '\0';
    }
    if (mod)
        *mod = (int)m;
    if (version)
        *version = v;
    if (confirmed)
        *confirmed = (c2 && !is_iter) ? 1 : 0;
    return 0;
}

int ewgui_magnitude_id(char *out, size_t n, const char *origin_id,
                       const char *type)
{
    if (!out || n == 0 || !origin_id || !*origin_id || !type || !*type)
        return -1;
    return fits(snprintf(out, n, "%s/mag/%s", origin_id, type), n) ? 0 : -1;
}

int ewgui_tensor_id(char *out, size_t n, const char *origin_id,
                    const char *method)
{
    if (!out || n == 0 || !origin_id || !*origin_id || !method || !*method)
        return -1;
    return fits(snprintf(out, n, "%s/tensor/%s", origin_id, method), n) ? 0 : -1;
}

int ewgui_station_magnitude_id(char *out, size_t n, const char *magnitude_id,
                               const char *channel)
{
    if (!out || n == 0 || !magnitude_id || !*magnitude_id ||
        !channel || !*channel)
        return -1;
    return fits(snprintf(out, n, "%s/%s", magnitude_id, channel), n) ? 0 : -1;
}

int ewgui_pick_id(char *out, size_t n, const char *origin_id, const char *sta,
                  const char *phase)
{
    if (!out || n == 0 || !origin_id || !*origin_id || !sta || !phase || !*phase)
        return -1;
    return fits(snprintf(out, n, "%s/pick/%s.%s", origin_id, sta, phase), n)
               ? 0 : -1;
}

int ewgui_channel_nslco(char *out, size_t n, const char *net, const char *sta,
                        const char *loc, const char *chan)
{
    if (!out || n == 0 || !sta)
        return -1;
    if (!net)
        net = "";
    if (!chan)
        chan = "";
    if (!loc || strcmp(loc, "--") == 0)
        loc = "";
    return fits(snprintf(out, n, "%s.%s.%s.%s", net, sta, loc, chan), n)
               ? 0 : -1;
}

const char *ewgui_software(void)
{
    static char cached[64];
    static int  done = 0;

    if (!done) {
        const char *v = getenv("EW_VERSION");

        snprintf(cached, sizeof cached, "%s", (v && v[0]) ? v : "ew_8.0");
        done = 1;
    }
    return cached;
}

const char *ewgui_origin_status(int manual)
{
    return manual ? "MANUAL" : "AUTOMATIC";
}

void ewgui_epoch_to_iso(double t, char *out, size_t n)
{
    time_t s = (time_t)floor(t);
    int ms = (int)lround((t - (double)s) * 1000.0);
    struct tm tm;
    char date[24];

    if (!out || n == 0)
        return;
    if (ms >= 1000) {
        ms -= 1000;
        s += 1;
    } else if (ms < 0) {
        ms = 0;
    }
    gmtime_r(&s, &tm);
    if (strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm) == 0)
        snprintf(date, sizeof date, "1970-01-01 00:00:00");
    snprintf(out, n, "%s.%03d+00", date, ms);
}
