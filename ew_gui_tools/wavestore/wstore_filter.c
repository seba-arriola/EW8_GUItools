#include "wstore_filter.h"

#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WF_NFIELDS 4
#define WF_FIELD   32      /* largo maximo de un campo del patron */
#define WF_LINE    512     /* largo maximo de una linea del archivo */

/* Campos en el orden del archivo: STA NET CHAN LOC. */
typedef struct {
    char f[WF_NFIELDS][WF_FIELD];
} WFilterPat;

struct WStoreFilter {
    WFilterPat *pat;
    int         n;
};

/* Recorta espacios de los extremos (in puede ser NULL). */
static void wf_trim(const char *in, char *out, size_t n)
{
    size_t i = 0;

    if (!in) { out[0] = '\0'; return; }
    while (*in == ' ' || *in == '\t') in++;
    while (*in && i + 1 < n) out[i++] = *in++;
    out[i] = '\0';
    while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t')) out[--i] = '\0';
}

/* Loc nula de EarthWorm: "--" (o vacio) => vacio. */
static void wf_norm_loc(const char *in, char *out, size_t n)
{
    wf_trim(in, out, n);
    if (out[0] == '-' && out[1] == '-' && out[2] == '\0') out[0] = '\0';
}

static int wf_match(const char *pattern, const char *val)
{
    char v[WF_FIELD];

    wf_trim(val, v, sizeof(v));
    return fnmatch(pattern, v, 0) == 0;
}

/* Siguiente token separado por espacios/tabs; avanza *p. NULL si no hay mas. */
static char *wf_token(char **p)
{
    char *s = *p;
    char *t;

    while (*s && (unsigned char)*s <= ' ') s++;
    if (!*s) { *p = s; return NULL; }
    t = s;
    while (*t && (unsigned char)*t > ' ') t++;
    if (*t) { *t = '\0'; t++; }
    *p = t;
    return s;
}

int wstore_filter_load(const char *path, WStoreFilter **out)
{
    WStoreFilter *f;
    char          line[WF_LINE];
    FILE         *fp;
    int           lineno = 0;

    if (!out) return -1;
    *out = NULL;
    if (!path || !path[0]) return 0;   /* sin archivo => sin filtro */

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "wstore_filter: no se pudo abrir '%s'\n", path);
        return -1;
    }
    f = (WStoreFilter *)calloc(1, sizeof(*f));
    if (!f) { fclose(fp); return -1; }
    f->pat = (WFilterPat *)calloc(WSTORE_FILTER_MAX, sizeof(*f->pat));
    if (!f->pat) { free(f); fclose(fp); return -1; }

    while (fgets(line, sizeof(line), fp)) {
        WFilterPat cur;
        char      *p, *tok;
        int        nf = 0;

        lineno++;
        if (!strchr(line, '\n') && !feof(fp)) {
            fprintf(stderr, "wstore_filter: %s:%d: linea demasiado larga (max %d)\n",
                    path, lineno, WF_LINE - 1);
            goto bad;
        }
        p = strchr(line, '#');   /* comentario: al inicio o al final */
        if (p) *p = '\0';

        memset(&cur, 0, sizeof(cur));
        p = line;
        while ((tok = wf_token(&p)) != NULL) {
            if (nf >= WF_NFIELDS) {
                fprintf(stderr, "wstore_filter: %s:%d: se esperan 4 campos (STA NET CHAN LOC)\n",
                        path, lineno);
                goto bad;
            }
            if (strlen(tok) >= WF_FIELD) {
                fprintf(stderr, "wstore_filter: %s:%d: campo demasiado largo (max %d)\n",
                        path, lineno, WF_FIELD - 1);
                goto bad;
            }
            snprintf(cur.f[nf], WF_FIELD, "%s", tok);
            nf++;
        }
        if (nf == 0) continue;
        if (nf < WF_NFIELDS) {
            fprintf(stderr, "wstore_filter: %s:%d: se esperan 4 campos (STA NET CHAN LOC), hay %d\n",
                    path, lineno, nf);
            goto bad;
        }
        if (f->n >= WSTORE_FILTER_MAX) {
            fprintf(stderr, "wstore_filter: %s:%d: demasiados patrones (max %d)\n",
                    path, lineno, WSTORE_FILTER_MAX);
            goto bad;
        }
        wf_norm_loc(cur.f[3], cur.f[3], WF_FIELD);
        f->pat[f->n++] = cur;
    }

    fclose(fp);
    if (f->n == 0)
        fprintf(stderr, "wstore_filter: '%s' sin patrones: se guarda todo\n", path);
    *out = f;
    return 0;

bad:
    fclose(fp);
    wstore_filter_free(f);
    return -1;
}

int wstore_filter_accept(const WStoreFilter *f, const char *sta, const char *net,
                         const char *chan, const char *loc)
{
    char l[WF_FIELD];
    int  i;

    if (!f || f->n <= 0) return 1;
    wf_norm_loc(loc, l, sizeof(l));
    for (i = 0; i < f->n; i++) {
        const WFilterPat *p = &f->pat[i];
        if (wf_match(p->f[0], sta) && wf_match(p->f[1], net) &&
            wf_match(p->f[2], chan) && wf_match(p->f[3], l))
            return 1;
    }
    return 0;
}

int wstore_filter_count(const WStoreFilter *f)
{
    return (f && f->n > 0) ? f->n : 0;
}

void wstore_filter_free(WStoreFilter *f)
{
    if (!f) return;
    free(f->pat);
    free(f);
}
