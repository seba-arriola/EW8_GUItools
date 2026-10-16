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

int ewgui_origin_id(char *out, size_t n, const char *event_id, int mod)
{
    if (!out || n == 0 || !event_id || !*event_id)
        return -1;
    return fits(snprintf(out, n, "%s:%d", event_id, mod), n) ? 0 : -1;
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

const char *ewgui_mod_algorithm(int mod)
{
    if (mod == 162) return "csnloc";
    if (mod == 163) return "HYPOINVERSE";
    if (mod == 164) return "NLLoc";
    return "unknown";
}

const char *ewgui_mod_status(int mod, int manual)
{
    if (manual) return "REVIEWED";
    if (mod == 162) return "PRELIMINARY";
    return "REVISED";
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
