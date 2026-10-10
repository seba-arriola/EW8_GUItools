#include "csnhypodbp_manual.h"

#include <string.h>

static CsnhypodbpManualPick g_picks[CSNHYPODBP_MANUAL_MAX];
static int g_npicks = 0;

void csnhypodbp_manual_clear(void)
{
    g_npicks = 0;
}

void csnhypodbp_manual_add(const CsnhypodbpManualPick *p)
{
    if (!p || g_npicks >= CSNHYPODBP_MANUAL_MAX) return;
    g_picks[g_npicks] = *p;
    g_npicks++;
}

int csnhypodbp_manual_remove_station(const char *sta)
{
    int i, n = 0;
    if (!sta) return 0;
    for (i = 0; i < g_npicks; i++) {
        if (strcmp(g_picks[i].sta, sta) == 0) { n++; continue; }
        g_picks[i - n] = g_picks[i];
    }
    g_npicks -= n;
    return n;
}

int csnhypodbp_manual_remove_trace(const char *sta, const char *chan)
{
    int i, n = 0;
    if (!sta || !chan) return 0;
    for (i = 0; i < g_npicks; i++) {
        if (strcmp(g_picks[i].sta, sta) == 0 && strcmp(g_picks[i].chan, chan) == 0) { n++; continue; }
        g_picks[i - n] = g_picks[i];
    }
    g_npicks -= n;
    return n;
}

int csnhypodbp_manual_count(void)
{
    return g_npicks;
}

int csnhypodbp_manual_list(CsnhypodbpManualPick *out, int max)
{
    int n = g_npicks < max ? g_npicks : max;
    if (out && n > 0) memcpy(out, g_picks, (size_t)n * sizeof(*out));
    return g_npicks;
}
