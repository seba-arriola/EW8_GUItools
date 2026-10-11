#include "hypodb_ids.h"

#include <stdio.h>
#include <string.h>

static int fits(int written, size_t n)
{
    return written >= 0 && (size_t)written < n;
}

int hypodb_event_id(char *out, size_t n, const char *qid)
{
    if (!out || n == 0 || !qid || !*qid)
        return -1;
    return fits(snprintf(out, n, "E%s", qid), n) ? 0 : -1;
}

int hypodb_origin_id(char *out, size_t n, const char *event_id, int mod)
{
    if (!out || n == 0 || !event_id || !*event_id)
        return -1;
    return fits(snprintf(out, n, "%s:%d", event_id, mod), n) ? 0 : -1;
}

int hypodb_magnitude_id(char *out, size_t n, const char *origin_id,
                        const char *type)
{
    if (!out || n == 0 || !origin_id || !*origin_id || !type || !*type)
        return -1;
    return fits(snprintf(out, n, "%s/mag/%s", origin_id, type), n) ? 0 : -1;
}

int hypodb_station_magnitude_id(char *out, size_t n, const char *magnitude_id,
                                const char *channel)
{
    if (!out || n == 0 || !magnitude_id || !*magnitude_id ||
        !channel || !*channel)
        return -1;
    return fits(snprintf(out, n, "%s/%s", magnitude_id, channel), n) ? 0 : -1;
}

int hypodb_pick_id(char *out, size_t n, const char *origin_id,
                   const char *sta, const char *phase)
{
    if (!out || n == 0 || !origin_id || !*origin_id || !sta || !phase ||
        !*phase)
        return -1;
    return fits(snprintf(out, n, "%s/pick/%s.%s", origin_id, sta, phase), n)
               ? 0 : -1;
}

int hypodb_channel_nslco(char *out, size_t n, const char *net,
                         const char *sta, const char *loc, const char *chan)
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
