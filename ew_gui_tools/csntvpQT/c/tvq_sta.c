/* StaFile del visor: `STA CHAN NET LOC escala`.
 *
 * Paridad con csntvp_data.c:199-201, incluido el quirk: si la linea trae solo
 * 4 campos, el 4.o es la ESCALA y el location queda en "--" (asi se aceptan
 * los ficheros historicos sin location).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvq_capi.h"
#include "tvq_internal.h"

int tvq_sta_parse_line(const char *line, TvqStaEntry *out)
{
    char sta[20] = "", chan[20] = "", net[20] = "", loc[20] = "--";
    double scale = 0.0;
    int parsed;

    if (!line || !out)
        return 0;

    while (*line == ' ' || *line == '\t')
        line++;
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == '\0')
        return 0;

    parsed = sscanf(line, "%19s %19s %19s %19s %lf", sta, chan, net, loc, &scale);
    if (parsed == 4) {
        scale = atof(loc);
        snprintf(loc, sizeof loc, "%s", "--");
    } else if (parsed < 4) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->sta, sizeof out->sta, "%s", sta);
    snprintf(out->chan, sizeof out->chan, "%s", chan);
    snprintf(out->net, sizeof out->net, "%s", net);
    snprintf(out->loc, sizeof out->loc, "%s", loc);
    out->scale = scale;
    return 1;
}

int tvq_sta_load(const char *path, TvqStaEntry *arr, int max,
                 int *n, char *err, size_t errlen)
{
    FILE *fp;
    char line[256];
    int lineno = 0, count = 0;

    if (err && errlen)
        err[0] = '\0';
    if (!path || !arr || !n)
        return -1;

    fp = fopen(path, "r");
    if (!fp) {
        if (err)
            snprintf(err, errlen, "no puedo abrir el StaFile <%s>", path);
        return -1;
    }

    while (fgets(line, sizeof line, fp)) {
        TvqStaEntry e;
        int rc;

        lineno++;
        rc = tvq_sta_parse_line(line, &e);
        if (rc == 0)
            continue;
        if (rc < 0) {
            if (err)
                snprintf(err, errlen,
                         "<%s> linea %d: se esperan 4-5 campos "
                         "(STA CHAN NET [LOC] [escala])",
                         path, lineno);
            fclose(fp);
            return -1;
        }
        if (count < max)
            arr[count++] = e;
    }
    fclose(fp);

    if (count == 0) {
        if (err)
            snprintf(err, errlen, "<%s> no tiene estaciones", path);
        return -1;
    }
    *n = count;
    return 0;
}
