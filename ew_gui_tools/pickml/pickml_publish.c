/******************************************************************************
 * pickml_publish.c — política de publicación de picks al anillo.             *
 *                                                                            *
 * Lógica PURA: no incluye EarthWorm ni toca anillos, así que se testea       *
 * headless (test/test_publish.c). El envío con tport_putmsg vive en          *
 * pickml_main.c (run_live), que es el único punto con efecto lateral.        *
 ******************************************************************************/

#include "pickml.h"

static int same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a - 'A' + 'a' : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b - 'A' + 'a' : *b;
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

int pm_publish_phases_parse(const char *s, int *allow_p, int *allow_s)
{
    if (!s || !s[0]) return -1;

    if (same(s, "P"))  { *allow_p = 1; *allow_s = 0; return 0; }
    if (same(s, "S"))  { *allow_p = 0; *allow_s = 1; return 0; }
    if (same(s, "PS") || same(s, "SP")) { *allow_p = 1; *allow_s = 1; return 0; }
    return -1;
}

void pm_publish_phases_str(int allow_p, int allow_s, char *buf, size_t n)
{
    if (!buf || n == 0) return;
    if (allow_p && allow_s)      snprintf(buf, n, "PS");
    else if (allow_p)            snprintf(buf, n, "P");
    else if (allow_s)            snprintf(buf, n, "S");
    else                         snprintf(buf, n, "-");
}

PmPubDecision pm_publish_decide(const PmConfig *c, char phase, int weight)
{
    if (phase == 'P' && !c->PublishP) return PM_PUB_SKIP_PHASE;
    if (phase == 'S' && !c->PublishS) return PM_PUB_SKIP_PHASE;
    if (weight > c->PublishMaxWeight) return PM_PUB_SKIP_WEIGHT;
    return PM_PUB_SEND;
}
