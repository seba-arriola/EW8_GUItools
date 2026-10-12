/*
 * Nucleo puro de los perfiles de profundidad (ver csnhypodbp_profile.h).
 * Sin GTK ni Earthworm; testeable headless.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csnhypodbp_profile.h"

/* --- utilidades de texto --- */

/* Siguiente token separado por espacios/tabs; 0 si no hay mas. */
static int next_token(char **p, char *tok, size_t n)
{
    char *s = *p;
    size_t len = 0;

    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == '\0') {
        *p = s;
        return 0;
    }
    while (*s && !isspace((unsigned char)*s)) {
        if (len + 1 < n)
            tok[len] = *s;
        len++;
        s++;
    }
    tok[len < n ? len : n - 1] = '\0';
    *p = s;
    return 1;
}

/* Token completo -> double; 0 si no es un numero valido. */
static int token_num(const char *tok, double *out)
{
    char *end = NULL;
    double v = strtod(tok, &end);

    if (end == tok || (end && *end != '\0'))
        return 0;
    *out = v;
    return 1;
}

/* Coeficientes de la recta en forma general A*lon + B*lat + C = 0. */
static void line_abc(const CsnhypodbpProfile *p, double *A, double *B, double *C)
{
    *A = p->lat1 - p->lat2;
    *B = p->lon2 - p->lon1;
    *C = p->lon1 * p->lat2 - p->lon2 * p->lat1;
}

/* --- perfiles --- */

int csnhypodbp_profile_add(CsnhypodbpProfiles *ps, const char *name,
                           double lon1, double lat1, double lon2, double lat2,
                           const char *slab_file, const char *topo_file,
                           char *err, size_t errn)
{
    CsnhypodbpProfile *p;

    if (!ps) return 0;
    if (ps->n >= CSNHYPODBP_MAX_PROFILES) {
        if (err) snprintf(err, errn, "demasiados perfiles (max %d)",
                          CSNHYPODBP_MAX_PROFILES);
        return 0;
    }
    if (!name || name[0] == '\0') {
        if (err) snprintf(err, errn, "perfil sin nombre");
        return 0;
    }
    /* Extremos distintos: si no, no hay recta. */
    if (lon1 == lon2 && lat1 == lat2) {
        if (err) snprintf(err, errn, "perfil %s: extremos iguales", name);
        return 0;
    }

    p = &ps->p[ps->n];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->lon1 = lon1; p->lat1 = lat1; p->lon2 = lon2; p->lat2 = lat2;
    /* lat = a*lon + b; para una recta vertical queda a=0,b=0 (no se usa para
       dibujar: la recta se pinta con los extremos). */
    if (lon2 != lon1) {
        p->a = (lat2 - lat1) / (lon2 - lon1);
        p->b = lat1 - p->a * lon1;
    }
    if (slab_file && slab_file[0])
        snprintf(p->slab_file, sizeof(p->slab_file), "%s", slab_file);
    if (topo_file && topo_file[0])
        snprintf(p->topo_file, sizeof(p->topo_file), "%s", topo_file);
    ps->n++;
    return 1;
}

int csnhypodbp_profile_add_tokens(const char *name, char *rest,
                                  CsnhypodbpProfiles *ps,
                                  char *err, size_t errn)
{
    static const char *names[4] = { "lon1", "lat1", "lon2", "lat2" };
    char tok[CSNHYPODBP_PROFILE_PATH_MAX];
    char slab[CSNHYPODBP_PROFILE_PATH_MAX] = "", topo[CSNHYPODBP_PROFILE_PATH_MAX] = "";
    double v[4];
    const char *what = NULL;
    char *p = rest;
    int i;

    if (!ps) return 0;
    if (!name || name[0] == '\0') {
        if (err) snprintf(err, errn, "falta el nombre");
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (!next_token(&p, tok, sizeof(tok)) || !token_num(tok, &v[i])) {
            what = names[i];
            break;
        }
    }
    if (what) {
        if (err) snprintf(err, errn, "%s invalido", what);
        return 0;
    }
    if (next_token(&p, tok, sizeof(tok)))
        snprintf(slab, sizeof(slab), "%s", tok);
    if (next_token(&p, tok, sizeof(tok)))
        snprintf(topo, sizeof(topo), "%s", tok);
    if (next_token(&p, tok, sizeof(tok))) {
        if (err) snprintf(err, errn, "tokens de mas");
        return 0;
    }
    return csnhypodbp_profile_add(ps, name, v[0], v[1], v[2], v[3],
                                  slab, topo, err, errn);
}

double csnhypodbp_profile_dist(const CsnhypodbpProfile *p, double lat, double lon)
{
    double A, B, C, den;

    if (!p) return -1.0;
    line_abc(p, &A, &B, &C);
    den = sqrt(A * A + B * B);
    if (den <= 0.0) return -1.0;
    return fabs(A * lon + B * lat + C) / den;
}

int csnhypodbp_profile_nearest(const CsnhypodbpProfiles *ps, double lat, double lon)
{
    int i, best = -1;
    double best_d = 0.0;

    if (!ps || ps->n <= 0) return -1;
    for (i = 0; i < ps->n; i++) {
        double d = csnhypodbp_profile_dist(&ps->p[i], lat, lon);
        if (d < 0.0) continue;
        if (best < 0 || d < best_d) { best = i; best_d = d; }
    }
    return best;
}

void csnhypodbp_profiles_default_files(CsnhypodbpProfiles *ps, const char *dir)
{
    int i;

    if (!ps) return;
    for (i = 0; i < ps->n; i++) {
        CsnhypodbpProfile *p = &ps->p[i];
        char buf[CSNHYPODBP_PROFILE_PATH_MAX];
        if (p->slab_file[0] == '\0') {
            if (dir && dir[0])
                snprintf(buf, sizeof(buf), "%s/slab%s.tmp", dir, p->name);
            else
                snprintf(buf, sizeof(buf), "slab%s.tmp", p->name);
            snprintf(p->slab_file, sizeof(p->slab_file), "%s", buf);
        }
        if (p->topo_file[0] == '\0') {
            if (dir && dir[0])
                snprintf(buf, sizeof(buf), "%s/topo%s.tmp", dir, p->name);
            else
                snprintf(buf, sizeof(buf), "topo%s.tmp", p->name);
            snprintf(p->topo_file, sizeof(p->topo_file), "%s", buf);
        }
    }
}

int csnhypodbp_profiles_parse(const char *path, CsnhypodbpProfiles *out,
                              char *err, size_t errn)
{
    FILE *f;
    char line[1024];
    int lineno = 0, nread = 0;

    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!path) {
        if (err) snprintf(err, errn, "sin archivo de perfiles");
        return -1;
    }
    f = fopen(path, "r");
    if (!f) {
        if (err) snprintf(err, errn, "no se pudo abrir %s", path);
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *p = line, tok[CSNHYPODBP_PROFILE_PATH_MAX];
        char name[CSNHYPODBP_PROFILE_NAME_MAX];
        int i;

        lineno++;
        for (i = 0; line[i]; i++)
            if (line[i] == '\n' || line[i] == '\r') { line[i] = '\0'; break; }

        if (!next_token(&p, tok, sizeof(tok))) continue;      /* vacia */
        if (tok[0] == '#') continue;                          /* comentario */
        if (strcmp(tok, "Profile") != 0) {
            if (err) snprintf(err, errn, "%s:%d: se esperaba Profile", path, lineno);
            fclose(f);
            return -1;
        }
        if (!next_token(&p, name, sizeof(name)) || name[0] == '\0') {
            if (err) snprintf(err, errn, "%s:%d: falta el nombre", path, lineno);
            fclose(f);
            return -1;
        }
        /* p apunta al resto de la linea: lon1 lat1 lon2 lat2 [slab] [topo] */
        if (!csnhypodbp_profile_add_tokens(name, p, out, err, errn)) {
            if (err && err[0]) {
                char msg[256];
                snprintf(msg, sizeof(msg), "%s:%d: %s", path, lineno, err);
                snprintf(err, errn, "%s", msg);
            }
            fclose(f);
            return -1;
        }
        nread++;
    }
    fclose(f);
    return nread;
}

/* --- polilineas de slab/topo --- */

void csnhypodbp_profile_line_free(CsnhypodbpProfileLine *l)
{
    if (!l) return;
    free(l->lon);
    free(l->val);
    memset(l, 0, sizeof(*l));
    l->lon_min = 1.0;
    l->lon_max = -1.0;
}

int csnhypodbp_profile_line_read(const char *path, int lon_col, int val_col,
                                 CsnhypodbpProfileLine *out, char *err, size_t errn)
{
    FILE *f;
    char line[4096];
    int cap = 1024;
    int need = (lon_col > val_col ? lon_col : val_col);

    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->lon_min = 1.0;      /* sentinel: sin datos finitos */
    out->lon_max = -1.0;
    if (lon_col < 1 || val_col < 1) {
        if (err) snprintf(err, errn, "columnas invalidas (%d,%d)", lon_col, val_col);
        return -1;
    }
    f = fopen(path, "r");
    if (!f) {
        if (err) snprintf(err, errn, "no se pudo abrir %s", path);
        return -1;
    }
    out->lon = malloc((size_t)cap * sizeof(double));
    out->val = malloc((size_t)cap * sizeof(double));
    if (!out->lon || !out->val) {
        if (err) snprintf(err, errn, "%s: sin memoria", path);
        csnhypodbp_profile_line_free(out);
        fclose(f);
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p = line, tok[128];
        double lon = 0.0, val = 0.0;
        int col = 0, have = 0;
        int i;

        for (i = 0; line[i]; i++)
            if (line[i] == '\n' || line[i] == '\r') { line[i] = '\0'; break; }
        /* Salta lineas vacias y comentarios. */
        for (i = 0; line[i] && isspace((unsigned char)line[i]); i++) {}
        if (line[i] == '\0' || line[i] == '#') continue;

        while (next_token(&p, tok, sizeof(tok))) {
            col++;
            if (col == lon_col) {
                if (!token_num(tok, &lon) || !isfinite(lon)) { have = 0; break; }
                have |= 1;
            } else if (col == val_col) {
                /* NaN es valido: es un hueco. */
                if (!token_num(tok, &val)) { have = 0; break; }
                have |= 2;
            }
            if (col >= need) break;
        }
        if (have != 3) continue;          /* fila corta o no numerica: se salta */

        if (out->n >= CSNHYPODBP_PROFILE_MAX_PTS) { out->truncated = 1; break; }
        if (out->n >= cap) {
            int ncap = cap * 2;
            double *nl, *nv;
            if (ncap > CSNHYPODBP_PROFILE_MAX_PTS) ncap = CSNHYPODBP_PROFILE_MAX_PTS;
            nl = realloc(out->lon, (size_t)ncap * sizeof(double));
            nv = realloc(out->val, (size_t)ncap * sizeof(double));
            if (!nl || !nv) {
                free(nl ? nl : out->lon);
                free(nv ? nv : out->val);
                out->lon = out->val = NULL;
                if (err) snprintf(err, errn, "%s: sin memoria", path);
                csnhypodbp_profile_line_free(out);
                fclose(f);
                return -1;
            }
            out->lon = nl; out->val = nv; cap = ncap;
        }
        out->lon[out->n] = lon;
        out->val[out->n] = val;
        out->n++;
        if (isfinite(val)) {
            if (out->lon_min > out->lon_max) {
                out->lon_min = out->lon_max = lon;
                out->val_min = out->val_max = val;
            } else {
                if (lon < out->lon_min) out->lon_min = lon;
                if (lon > out->lon_max) out->lon_max = lon;
                if (val < out->val_min) out->val_min = val;
                if (val > out->val_max) out->val_max = val;
            }
        }
    }
    fclose(f);
    if (out->n <= 0) {
        if (err) snprintf(err, errn, "%s: sin filas validas", path);
        csnhypodbp_profile_line_free(out);
        return -1;
    }
    return 0;
}
