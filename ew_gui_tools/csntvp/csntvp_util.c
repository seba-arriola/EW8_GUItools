#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "csntvp_util.h"

int csntvp_pick_parse(const char *msg, CsntvpPickMsg *out)
{
    if (!msg || !out)
        return 0;
    memset(out, 0, sizeof(*out));

    int type, mod, inst, seq;
    char scnl[64], fmwt[16], ts[32];
    if (sscanf(msg, "%d %d %d %d %63s %15s %31s",
               &type, &mod, &inst, &seq, scnl, fmwt, ts) < 7)
        return 0;
    (void)type;
    (void)fmwt;   /* first-motion + peso: no se muestran en la etiqueta */

    out->mod  = mod;
    out->inst = inst;
    out->seq  = seq;

    if (sscanf(scnl, "%15[^.].%15[^.].%15[^.].%15s",
               out->sta, out->chan, out->net, out->loc) < 3)
        return 0;

    for (int k = 0; ts[k]; k++)
        if (ts[k] == ',') ts[k] = '.';

    int py, pmo, pd, phh, pmn;
    double psec;
    if (sscanf(ts, "%4d%2d%2d%2d%2d%lf", &py, &pmo, &pd, &phh, &pmn, &psec) != 6)
        return 0;

    struct tm pt = {0};
    pt.tm_year = py - 1900;
    pt.tm_mon  = pmo - 1;
    pt.tm_mday = pd;
    pt.tm_hour = phh;
    pt.tm_min  = pmn;
    pt.tm_sec  = (int)psec;

    char *otz = getenv("TZ");
    setenv("TZ", "GMT", 1);
    tzset();
    out->t = (double)mktime(&pt) + (psec - (int)psec);
    if (otz) setenv("TZ", otz, 1);
    else     unsetenv("TZ");
    tzset();

    /* Tokens 11 (fase) y 12 (origen), opcionales. */
    char phase_tok[8] = "", origin_tok[8] = "";
    sscanf(msg, "%*d %*d %*d %*d %*63s %*15s %*31s %*d %*d %*d %7s %7s",
           phase_tok, origin_tok);
    out->phase  = (phase_tok[0] == 'S' || phase_tok[0] == 's') ? 'S' : 'P';
    out->origin = (origin_tok[0] == 'M' || origin_tok[0] == 'm') ? 'M' : 'A';
    return 1;
}

void csntvp_pick_label(char phase, char origin, char *buf, size_t n)
{
    if (!buf || n == 0)
        return;
    char ph = (phase  == 'S' || phase  == 's') ? 'S' : 'P';
    char og = (origin == 'M' || origin == 'm') ? 'M' : 'A';
    snprintf(buf, n, "%c(%c)", ph, og);
}

int csntvp_parse_hexcolor(const char *s, double rgb[3])
{
    unsigned int r, g, b;
    if (!s || s[0] == '\0')
        return -1;
    while (*s == '#')
        s++;
    if (sscanf(s, "%2x%2x%2x", &r, &g, &b) != 3)
        return -1;
    rgb[0] = (double)r / 255.0;
    rgb[1] = (double)g / 255.0;
    rgb[2] = (double)b / 255.0;
    return 0;
}
