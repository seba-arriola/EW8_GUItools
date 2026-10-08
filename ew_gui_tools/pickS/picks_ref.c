/******************************************************************************
 * picks_ref.c — parser de líneas TYPE_PICK_SCNL y lista de picks P de guía.   *
 ******************************************************************************/

#include "pickS.h"
#include <time.h>

/* Parsea una línea de pick. Devuelve el número de campos leídos (>=7 = ok).
 * Rellena sta (nombre de estación) y t (epoch). phase: 'P' o 'S'.            */
int PickS_ParsePickLine(const char *line, char *sta, double *t, char *phase)
{
    int    type, mod, inst, seq, a1, a2, a3;
    char   scnl[64], fmwt[16], tstr[32], pstr[8];
    int    Y, Mo, D, H, Mi;
    double sec;
    int    m;
    char  *dot;

    memset(pstr, 0, sizeof(pstr));
    m = sscanf(line, "%d %d %d %d %63s %15s %31s %d %d %d %7s",
               &type, &mod, &inst, &seq, scnl, fmwt, tstr, &a1, &a2, &a3, pstr);
    if (m < 7) return m;

    dot = strchr(scnl, '.');
    if (dot) *dot = '\0';
    if (sta) { strncpy(sta, scnl, PICKS_SCNL - 1); sta[PICKS_SCNL - 1] = '\0'; }

    if (sscanf(tstr, "%4d%2d%2d%2d%2d%lf", &Y, &Mo, &D, &H, &Mi, &sec) == 6) {
        struct tm tmv;
        memset(&tmv, 0, sizeof(tmv));
        tmv.tm_year = Y - 1900;
        tmv.tm_mon  = Mo - 1;
        tmv.tm_mday = D;
        tmv.tm_hour = H;
        tmv.tm_min  = Mi;
        tmv.tm_sec  = (int)sec;
        if (t) *t = (double)timegm(&tmv) + (sec - (double)(int)sec);
    } else if (t) {
        *t = 0.0;
    }

    if (phase) *phase = (pstr[0] == 'S' || pstr[0] == 's') ? 'S' : 'P';
    return m;
}

void PickS_PickRef_Add(PickS_PickRef *list, int *n, int max,
                       const char *sta, double t)
{
    if (!list || !n || *n >= max) return;
    snprintf(list[*n].sta, PICKS_SCNL, "%s", sta);
    list[*n].t = t;
    (*n)++;
}

/* Devuelve el tiempo del pick P de `sta` más cercano por debajo de t_before y
 * dentro de [t_before-dt_max, t_before-dt_min]; -1.0 si no hay. */
double PickS_PickRef_Find(const PickS_PickRef *list, int n, const char *sta,
                          double t_before, double dt_min, double dt_max)
{
    int    i;
    double best = -1.0;

    for (i = 0; i < n; i++) {
        double dt;
        if (strcmp(list[i].sta, sta) != 0) continue;
        dt = t_before - list[i].t;
        if (dt < dt_min || dt > dt_max) continue;
        if (best < 0.0 || list[i].t > best) best = list[i].t;
    }
    return best;
}
