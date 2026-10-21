/******************************************************************************
 * pickml_sta.c — lectura de pickml.sta y búsqueda de estación/componente.    *
 *                                                                            *
 * Formato (idéntico a pickS.sta):                                            *
 *   PickFlag Pin Sta Net Loc ChanE ChanN ChanZ                               *
 * Ej:                                                                        *
 *   1 1 FAR1 C -- HHE HHN HHZ                                                *
 ******************************************************************************/

#include "pickml.h"

int pm_sta_read(const char *file, PmStation *out, int maxsta)
{
    FILE *f;
    char  line[512];
    int   n = 0;

    f = fopen(file, "r");
    if (!f) {
        fprintf(stderr, "pickml: no se pudo abrir %s\n", file);
        return PM_ERR;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        char sta[PM_SCNL_LEN], net[PM_SCNL_LEN], loc[PM_SCNL_LEN];
        char ce[PM_SCNL_LEN], cn[PM_SCNL_LEN], cz[PM_SCNL_LEN];
        int  flag, pin;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;

        if (sscanf(p, "%d %d %15s %15s %15s %15s %15s %15s",
                   &flag, &pin, sta, net, loc, ce, cn, cz) != 8) {
            fprintf(stderr, "pickml: linea invalida en %s: %s", file, line);
            continue;
        }
        if (flag == 0) continue;
        if (n >= maxsta) {
            fprintf(stderr, "pickml: demasiadas estaciones (max %d)\n", maxsta);
            break;
        }

        memset(&out[n], 0, sizeof(out[n]));
        out[n].flag = flag;
        out[n].pin  = pin;
        snprintf(out[n].sta, PM_SCNL_LEN, "%s", sta);
        snprintf(out[n].net, PM_SCNL_LEN, "%s", net);
        snprintf(out[n].loc, PM_SCNL_LEN, "%s", loc);
        snprintf(out[n].chan[PM_COMP_E], PM_SCNL_LEN, "%s", ce);
        snprintf(out[n].chan[PM_COMP_N], PM_SCNL_LEN, "%s", cn);
        snprintf(out[n].chan[PM_COMP_Z], PM_SCNL_LEN, "%s", cz);
        n++;
    }
    fclose(f);
    return n;
}

int pm_sta_find(const PmStation *list, int n, const char *sta,
                const char *chan, const char *net, const char *loc,
                int *comp_out)
{
    int i, c;

    for (i = 0; i < n; i++) {
        if (strcmp(list[i].sta, sta) != 0) continue;
        if (net && net[0] && strcmp(list[i].net, net) != 0) continue;
        if (loc && loc[0] && strcmp(list[i].loc, loc) != 0) continue;
        for (c = 0; c < PM_MAX_COMP; c++) {
            if (strcmp(list[i].chan[c], chan) == 0) {
                if (comp_out) *comp_out = c;
                return i;
            }
        }
    }
    return -1;
}

/* Canal con el que se reporta una fase (P suele ir en Z; S en una horizontal).
 * Devuelve el índice de componente del .sta, o -1. */
int pm_sta_chan_for_phase(const PmStation *st, char phase, const PmConfig *c)
{
    const char *want = (phase == 'S') ? c->SReportChan : c->PReportChan;

    if (!want || !want[0]) return PM_COMP_Z;
    switch (want[0]) {
        case 'Z': case 'z': return PM_COMP_Z;
        case 'N': case 'n': return PM_COMP_N;
        case 'E': case 'e': return PM_COMP_E;
        default: return PM_COMP_Z;
    }
}
