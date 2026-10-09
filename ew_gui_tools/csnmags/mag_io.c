#include "csnmags.h"

#include <string.h>

/*
 * mag_io.c - Lectura del JSON de hipocentros del modo offline de csnloc
 * (un objeto por línea) y emisión de resultados. Parser mínimo, sin JSON lib.
 */

static FILE *g_f = NULL;
static char  g_line[65536];

int mag_hypos_open(const char *path)
{
    mag_hypos_close();
    g_f = fopen(path, "r");
    return g_f ? 0 : -1;
}

void mag_hypos_close(void)
{
    if (g_f) fclose(g_f);
    g_f = NULL;
}

/* Busca "key" y copia el valor string. Devuelve 1 si lo encontró. */
static int jfind_str(const char *hay, const char *key, char *out, size_t osz)
{
    char pat[64];
    const char *p, *q, *e;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(hay, pat);
    if (!p) return 0;
    q = strchr(p, ':');
    if (!q) return 0;
    q = strchr(q, '"');
    if (!q) return 0;
    e = strchr(q + 1, '"');
    if (!e) return 0;
    {
        size_t n = (size_t)(e - (q + 1));
        if (n >= osz) n = osz - 1;
        memcpy(out, q + 1, n);
        out[n] = '\0';
    }
    return 1;
}

static int jfind_num(const char *hay, const char *key, double *out)
{
    char pat[64];
    const char *p, *q;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(hay, pat);
    if (!p) return 0;
    q = strchr(p, ':');
    if (!q) return 0;
    *out = strtod(q + 1, NULL);
    return 1;
}

int mag_hypos_next(MagHypo *out)
{
    char *ph;
    double v;

    if (!g_f) return -1;
    if (!fgets(g_line, sizeof(g_line), g_f)) return 0;

    memset(out, 0, sizeof(*out));

    if (jfind_num(g_line, "id", &v)) snprintf(out->event_id, sizeof(out->event_id), "%.0f", v);
    else if (jfind_num(g_line, "event", &v)) snprintf(out->event_id, sizeof(out->event_id), "%.0f", v);

    if (jfind_num(g_line, "t0", &out->t0)) {}
    jfind_num(g_line, "lat", &out->lat);
    jfind_num(g_line, "lon", &out->lon);
    jfind_num(g_line, "depth_km", &out->depth_km);

    ph = strstr(g_line, "\"phases\"");
    if (ph) {
        const char *p = ph;
        while ((p = strchr(p, '{')) != NULL) {
            const char *e = strchr(p, '}');
            if (!e) break;
            if (out->nphases < 256) {
                char blob[2048];
                size_t n = (size_t)(e - p + 1);
                if (n >= sizeof(blob)) n = sizeof(blob) - 1;
                memcpy(blob, p, n);
                blob[n] = '\0';
                if (jfind_str(blob, "sta", out->ph[out->nphases].sta, sizeof(out->ph[0].sta))) {
                    jfind_str(blob, "net",  out->ph[out->nphases].net,  sizeof(out->ph[0].net));
                    jfind_str(blob, "chan", out->ph[out->nphases].chan, sizeof(out->ph[0].chan));
                    jfind_str(blob, "loc",  out->ph[out->nphases].loc,  sizeof(out->ph[0].loc));
                    jfind_str(blob, "phase", out->ph[out->nphases].phase, sizeof(out->ph[0].phase));
                    jfind_num(blob, "t_epoch", &out->ph[out->nphases].t_epoch);
                    out->nphases++;
                }
            }
            p = e + 1;
        }
    }
    return 1;
}
