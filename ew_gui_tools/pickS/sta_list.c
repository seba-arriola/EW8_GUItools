/******************************************************************************
 * sta_list.c — lectura de pickS.sta                                          *
 *                                                                            *
 * Formato (una línea por estación 3C):                                       *
 *   PickFlag Pin Sta Net Loc ChanE ChanN ChanZ                               *
 * Ej:                                                                        *
 *   1 1 FAR1 C -- HHE HHN HHZ                                                *
 ******************************************************************************/

#include "pickS.h"

int PickS_ReadStaList(const char *file, PickS_Station *out, int maxsta)
{
    FILE *f;
    char  line[512];
    int   n = 0;

    f = fopen(file, "r");
    if (!f) {
        fprintf(stderr, "pickS: no se pudo abrir %s\n", file);
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        char sta[PICKS_SCNL], net[PICKS_SCNL], loc[PICKS_SCNL];
        char ce[PICKS_SCNL], cn[PICKS_SCNL], cz[PICKS_SCNL];
        int  flag, pin;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;

        if (sscanf(p, "%d %d %15s %15s %15s %15s %15s %15s",
                   &flag, &pin, sta, net, loc, ce, cn, cz) != 8) {
            fprintf(stderr, "pickS: linea invalida en %s: %s", file, line);
            continue;
        }
        if (flag == 0) continue;                 /* 0 = no picar */
        if (n >= maxsta) {
            fprintf(stderr, "pickS: demasiadas estaciones (max %d)\n", maxsta);
            break;
        }

        memset(&out[n], 0, sizeof(out[n]));
        out[n].pickflag = flag;
        out[n].pin      = pin;
        snprintf(out[n].sta, PICKS_SCNL, "%s", sta);
        snprintf(out[n].net, PICKS_SCNL, "%s", net);
        snprintf(out[n].loc, PICKS_SCNL, "%s", loc);
        snprintf(out[n].chan[PICKS_COMP_E], PICKS_SCNL, "%s", ce);
        snprintf(out[n].chan[PICKS_COMP_N], PICKS_SCNL, "%s", cn);
        snprintf(out[n].chan[PICKS_COMP_Z], PICKS_SCNL, "%s", cz);
        n++;
    }
    fclose(f);
    return n;
}

/* Devuelve el indice de estación en *out y la componente en *comp_out.
 * -1 si no se encuentra. */
int PickS_FindStation(const PickS_Station *list, int n,
                      const char *sta, const char *chan, const char *net,
                      const char *loc, int *comp_out)
{
    int i, c;

    for (i = 0; i < n; i++) {
        if (strcmp(list[i].sta, sta) != 0) continue;
        if (net && net[0] && strcmp(list[i].net, net) != 0) continue;
        if (loc && loc[0] && strcmp(list[i].loc, loc) != 0) continue;
        for (c = 0; c < PICKS_MAX_COMP; c++) {
            if (strcmp(list[i].chan[c], chan) == 0) {
                if (comp_out) *comp_out = c;
                return i;
            }
        }
    }
    return -1;
}
