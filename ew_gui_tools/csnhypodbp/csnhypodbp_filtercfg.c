#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "csnhypodbp_filtercfg.h"

#define MAX_TOK 8

static const char *type_name(int type)
{
    switch (type) {
    case 1:  return "HP";
    case 2:  return "LP";
    case 3:  return "BP";
    default: return "?";
    }
}

static int type_from(const char *s)
{
    if (!s)
        return 0;
    if (!strcasecmp(s, "HP")) return 1;
    if (!strcasecmp(s, "LP")) return 2;
    if (!strcasecmp(s, "BP")) return 3;
    return 0;
}

static int ci_equal(const char *a, const char *b)
{
    return a && b && strcasecmp(a, b) == 0;
}

static char norm_key(char k)
{
    return (char)tolower((unsigned char)k);
}

static int key_valid(char k)
{
    unsigned char u = (unsigned char)k;
    if (u < 33 || u > 126)
        return 0;
    if (k == 'p' || k == 'P' || k == 's' || k == 'S')
        return 0;   /* reservadas por el picado de fases */
    return 1;
}

static void set_err(char *err, size_t errn, const char *msg)
{
    if (err && errn)
        snprintf(err, errn, "%s", msg);
}

static int split(char *buf, char *tok[MAX_TOK])
{
    int n = 0;
    char *save = NULL;
    for (char *p = strtok_r(buf, " \t\r\n", &save); p;
         p = strtok_r(NULL, " \t\r\n", &save)) {
        if (p[0] == '#')
            break;                       /* comentario de cola */
        if (n < MAX_TOK)
            tok[n++] = p;
    }
    return n;
}

static int key_conflict(const CsnhypodbpFilterCfg *c, char key,
                        int ignore_menu, int ignore_raw, int ignore_stalta,
                        const CsnhypodbpFilterPreset *skip)
{
    if (!ignore_menu && key == c->menu_key)
        return 1;
    if (!ignore_raw && key == c->raw_key)
        return 1;
    if (!ignore_stalta && c->stalta_declared && key == c->stalta_key)
        return 1;
    for (int i = 0; i < c->nf; i++) {
        if (&c->f[i] == skip)
            continue;
        if (c->f[i].key == key)
            return 1;
    }
    return 0;
}

static int add_preset(CsnhypodbpFilterCfg *c, const char *name, char key,
                      int type, double f1, double f2, int order,
                      char *err, size_t errn)
{
    key = norm_key(key);
    if (!key_valid(key)) {
        set_err(err, errn, "Filter: tecla invalida o reservada (p/s)");
        return -1;
    }

    int slot = -1;
    CsnhypodbpFilterPreset *skip = NULL;
    for (int i = 0; i < c->nf; i++) {
        if (c->f[i].key == key) {
            if (c->f[i].from_file) {
                set_err(err, errn, "Filter: tecla duplicada en el .d");
                return -1;
            }
            slot = i;             /* reemplaza un default */
            skip = &c->f[i];
            break;
        }
    }
    if (slot < 0) {
        if (c->nf >= CSNHYPODBP_MAX_FILTERS) {
            set_err(err, errn, "Filter: demasiados presets");
            return -1;
        }
        slot = c->nf;
    }

    if (strlen(name) >= sizeof(((CsnhypodbpFilterPreset *)0)->name)) {
        set_err(err, errn, "Filter: nombre demasiado largo (max 15)");
        return -1;
    }
    if (key_conflict(c, key, 0, 0, 0, skip)) {
        set_err(err, errn, "Filter: tecla ya usada por FilterMenu/FilterRaw/StaLta");
        return -1;
    }
    if (slot == c->nf)
        c->nf++;

    CsnhypodbpFilterPreset *p = &c->f[slot];
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->key = key;
    p->type = type;
    p->f1 = f1;
    p->f2 = f2;
    p->order = order;
    p->from_file = 1;
    return 0;
}

static int parse_double(const char *s, double *out)
{
    if (!s || !*s)
        return -1;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || (end && *end != '\0') || !isfinite(v))
        return -1;
    *out = v;
    return 0;
}

static int parse_int(const char *s, int *out)
{
    if (!s || !*s)
        return -1;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || (end && *end != '\0'))
        return -1;
    *out = (int)v;
    return 0;
}

static int add_filter(CsnhypodbpFilterCfg *c, const char *rest,
                      char *err, size_t errn)
{
    char buf[256];
    char *tok[MAX_TOK];

    snprintf(buf, sizeof(buf), "%s", rest ? rest : "");
    int ntok = split(buf, tok);
    if (ntok < 4) {
        set_err(err, errn, "Filter: faltan argumentos");
        return -1;
    }

    int idx, type;
    const char *name;
    if (type_from(tok[0])) {
        type = type_from(tok[0]);
        name = type_name(type);
        idx = 1;
    } else if (ntok >= 5 && type_from(tok[1])) {
        type = type_from(tok[1]);
        name = tok[0];
        idx = 2;
    } else {
        set_err(err, errn, "Filter: tipo invalido (HP/LP/BP)");
        return -1;
    }

    if (strlen(tok[idx]) != 1) {
        set_err(err, errn, "Filter: la tecla debe ser un solo caracter");
        return -1;
    }
    char key = tok[idx][0];

    double f1 = 0.0, f2 = 0.0;
    int order = 0;

    if (type == 3) {
        if (ntok < idx + 4) {
            set_err(err, errn, "Filter BP: faltan f1, f2 u orden");
            return -1;
        }
        if (ntok > idx + 4) {
            set_err(err, errn, "Filter BP: argumentos de mas");
            return -1;
        }
        if (parse_double(tok[idx + 1], &f1) || parse_double(tok[idx + 2], &f2) ||
            parse_int(tok[idx + 3], &order)) {
            set_err(err, errn, "Filter BP: argumento no numerico");
            return -1;
        }
        if (!(f1 > 0.0) || !(f2 > f1)) {
            set_err(err, errn, "Filter BP: se exige 0 < f1 < f2");
            return -1;
        }
    } else {
        if (ntok < idx + 3) {
            set_err(err, errn, "Filter: faltan f1 u orden");
            return -1;
        }
        if (ntok > idx + 3) {
            set_err(err, errn, "Filter: argumentos de mas");
            return -1;
        }
        if (parse_double(tok[idx + 1], &f1) || parse_int(tok[idx + 2], &order)) {
            set_err(err, errn, "Filter: argumento no numerico");
            return -1;
        }
        if (!(f1 > 0.0)) {
            set_err(err, errn, "Filter: f1 debe ser > 0");
            return -1;
        }
    }

    if (order != 2 && order != 4) {
        set_err(err, errn, "Filter: el orden debe ser 2 o 4");
        return -1;
    }

    return add_preset(c, name, key, type, f1, f2, order, err, errn);
}

static int add_stalta(CsnhypodbpFilterCfg *c, const char *rest,
                      char *err, size_t errn)
{
    char buf[256];
    char *tok[MAX_TOK];

    snprintf(buf, sizeof(buf), "%s", rest ? rest : "");
    int ntok = split(buf, tok);
    if (ntok < 5) {
        set_err(err, errn, "StaLta: faltan argumentos");
        return -1;
    }
    if (ntok > 6) {
        set_err(err, errn, "StaLta: argumentos de mas");
        return -1;
    }
    if (strlen(tok[0]) != 1) {
        set_err(err, errn, "StaLta: la tecla debe ser un solo caracter");
        return -1;
    }

    double sta, lta, on, off, corr;
    if (parse_double(tok[1], &sta) || parse_double(tok[2], &lta) ||
        parse_double(tok[3], &on) || parse_double(tok[4], &off)) {
        set_err(err, errn, "StaLta: argumento no numerico");
        return -1;
    }
    if (!(sta > 0.0) || !(lta >= sta) || sta > 3600.0 || lta > 3600.0) {
        set_err(err, errn, "StaLta: se exige 0 < sta <= lta <= 3600 s");
        return -1;
    }
    if (!(on > 0.0) || !(off >= 0.0)) {
        set_err(err, errn, "StaLta: se exige on > 0 y off >= 0");
        return -1;
    }
    corr = -sta / 2.0;
    if (ntok >= 6 && parse_double(tok[5], &corr)) {
        set_err(err, errn, "StaLta: corr no numerico");
        return -1;
    }

    char key = norm_key(tok[0][0]);
    if (!key_valid(key)) {
        set_err(err, errn, "StaLta: tecla invalida o reservada (p/s)");
        return -1;
    }
    if (key_conflict(c, key, 0, 0, 1, NULL)) {
        set_err(err, errn, "StaLta: tecla ya usada por FilterMenu/FilterRaw/Filter");
        return -1;
    }
    c->stalta_declared = 1;
    c->stalta_key = key;
    c->stalta_sta = sta;
    c->stalta_lta = lta;
    c->stalta_on_thr = on;
    c->stalta_off_thr = off;
    c->stalta_corr = corr;

    return 0;
}

void csnhypodbp_filtercfg_defaults(CsnhypodbpFilterCfg *c)
{
    if (!c)
        return;
    memset(c, 0, sizeof(*c));

    c->menu_key = 'f';
    c->raw_key = '0';
    c->phase_zero = 1;

    char err[128];
    /* Recomendados por rango de distancia: tecla propia, sin automatismo. */
    add_preset(c, "P_local",       '1', 3, 1.0,  20.0, 2, err, sizeof(err));
    add_preset(c, "P_regional",    '2', 3, 0.7,  10.0, 2, err, sizeof(err));
    add_preset(c, "P_teleseismic", '3', 3, 0.5,   2.0, 2, err, sizeof(err));
    add_preset(c, "S_local",       '4', 3, 0.5,   8.0, 2, err, sizeof(err));
    add_preset(c, "HP",            'r', 1, 1.0,   0.0, 2, err, sizeof(err));
    add_preset(c, "LP",            'k', 2, 0.1,   0.0, 2, err, sizeof(err));
    add_preset(c, "BP",            'd', 3, 0.001, 12.0, 4, err, sizeof(err));

    /* Los defaults son reemplazables por el .d. */
    for (int i = 0; i < c->nf; i++)
        c->f[i].from_file = 0;

    c->stalta_declared = 1;
    c->stalta_on = 0;
    c->stalta_key = 't';
    c->stalta_sta = 0.5;
    c->stalta_lta = 10.0;
    c->stalta_on_thr = 3.0;
    c->stalta_off_thr = 1.5;
    c->stalta_corr = -0.25;
}

int csnhypodbp_filtercfg_add(CsnhypodbpFilterCfg *c, const char *kind,
                             const char *rest, char *err, size_t errn)
{
    if (!c || !kind) {
        set_err(err, errn, "config de filtros nula");
        return -1;
    }

    if (ci_equal(kind, "Filter")) {
        return add_filter(c, rest, err, errn);
    }

    if (ci_equal(kind, "FilterMenu") || ci_equal(kind, "FilterRaw")) {
        char buf[64];
        char *tok[MAX_TOK];
        snprintf(buf, sizeof(buf), "%s", rest ? rest : "");
        if (split(buf, tok) != 1 || strlen(tok[0]) != 1) {
            set_err(err, errn, "FilterMenu/FilterRaw: falta la tecla (o sobran argumentos)");
            return -1;
        }
        char key = norm_key(tok[0][0]);
        int is_menu = ci_equal(kind, "FilterMenu");
        if (!key_valid(key)) {
            set_err(err, errn, is_menu ? "FilterMenu: tecla invalida o reservada"
                                       : "FilterRaw: tecla invalida o reservada");
            return -1;
        }
        if (key_conflict(c, key, is_menu, !is_menu, 0, NULL)) {
            set_err(err, errn, is_menu ? "FilterMenu: tecla ya usada"
                                       : "FilterRaw: tecla ya usada");
            return -1;
        }
        if (is_menu)
            c->menu_key = key;
        else
            c->raw_key = key;
        return 0;
    }

    if (ci_equal(kind, "FilterPhase")) {
        char buf[64];
        char *tok[MAX_TOK];
        snprintf(buf, sizeof(buf), "%s", rest ? rest : "");
        if (split(buf, tok) != 1) {
            set_err(err, errn, "FilterPhase: se espera zero|causal");
            return -1;
        }
        if (ci_equal(tok[0], "zero"))
            c->phase_zero = 1;
        else if (ci_equal(tok[0], "causal"))
            c->phase_zero = 0;
        else {
            set_err(err, errn, "FilterPhase: se espera zero o causal");
            return -1;
        }
        return 0;
    }

    if (ci_equal(kind, "StaLta")) {
        return add_stalta(c, rest, err, errn);
    }

    set_err(err, errn, "clave de filtro desconocida");
    return -1;
}

int csnhypodbp_filtercfg_find_key(const CsnhypodbpFilterCfg *c, char key)
{
    if (!c || !key)
        return -1;
    key = norm_key(key);
    for (int i = 0; i < c->nf; i++)
        if (c->f[i].key == key)
            return i;
    return -1;
}

int csnhypodbp_filtercfg_key_action(const CsnhypodbpFilterCfg *c, int active,
                                    char key)
{
    if (!c || !key)
        return CSNHYPODBP_FILTER_KEY_NONE;

    key = norm_key(key);
    if (key == c->menu_key)
        return CSNHYPODBP_FILTER_KEY_MENU;
    if (key == c->raw_key)
        return CSNHYPODBP_FILTER_KEY_RAW;

    int idx = csnhypodbp_filtercfg_find_key(c, key);
    if (idx < 0)
        return CSNHYPODBP_FILTER_KEY_NONE;
    return (idx == active) ? CSNHYPODBP_FILTER_KEY_RAW : idx;
}

static void fmt_freq(double f, char *out, size_t n)
{
    if (!(f > 0.0))          snprintf(out, n, "0");
    else if (f >= 10.0)      snprintf(out, n, "%.0f", f);
    else if (f >= 1.0)       snprintf(out, n, "%.1f", f);
    else if (f >= 0.1)       snprintf(out, n, "%.2f", f);
    else                     snprintf(out, n, "%.3f", f);
}

static void fmt_params(int type, double f1, double f2, int order, char key,
                       const char *name, char *buf, size_t n)
{
    char a[24], b[24], keytxt[16];
    fmt_freq(f1, a, sizeof(a));
    fmt_freq(f2, b, sizeof(b));

    const char *tn = type_name(type);
    int named = name && name[0] && strcmp(name, tn) != 0;
    const char *prefix = named ? name : "";
    const char *sep = named ? " " : "";

    if (key)
        snprintf(keytxt, sizeof(keytxt), " (%c)", key);
    else
        snprintf(keytxt, sizeof(keytxt), " (manual)");

    if (type == 3)
        snprintf(buf, n, "%s%sBP %s-%s Hz · n=%d%s",
                 prefix, sep, a, b, order, keytxt);
    else
        snprintf(buf, n, "%s%s%s %s Hz · n=%d%s",
                 prefix, sep, tn, a, order, keytxt);
}

static void fmt_preset(const CsnhypodbpFilterPreset *p, char *buf, size_t n)
{
    fmt_params(p->type, p->f1, p->f2, p->order, p->key, p->name, buf, n);
}

void csnhypodbp_filtercfg_format_preset(const CsnhypodbpFilterPreset *p,
                                        char *buf, size_t n)
{
    if (!p || !buf || n == 0)
        return;
    fmt_preset(p, buf, n);
}

void csnhypodbp_filtercfg_format_manual(int type, double f1, double f2, int order,
                                        char *buf, size_t n)
{
    if (!buf || n == 0)
        return;
    fmt_params(type, f1, f2, order, 0, NULL, buf, n);
}

void csnhypodbp_filtercfg_format_active(const CsnhypodbpFilterCfg *c, int idx,
                                        int manual_type, double manual_f1,
                                        double manual_f2, int manual_order,
                                        char *buf, size_t n)
{
    if (!c || !buf || n == 0)
        return;

    if (idx == -2)
        fmt_params(manual_type, manual_f1, manual_f2, manual_order, 0, NULL, buf, n);
    else if (idx < 0 || idx >= c->nf)
        snprintf(buf, n, "Raw (%c)", c->raw_key);
    else
        fmt_preset(&c->f[idx], buf, n);

    if (c->stalta_on) {
        size_t used = strlen(buf);
        if (used < n)
            snprintf(buf + used, n - used, " + STA/LTA %g/%g s",
                     c->stalta_sta, c->stalta_lta);
    }
}

void csnhypodbp_filtercfg_help_text(const CsnhypodbpFilterCfg *c,
                                    char *buf, size_t n)
{
    if (!c || !buf || n == 0)
        return;

    size_t used = 0;
    int r = snprintf(buf, n, "Filtros de la vista de ondas (sub-pestaña picking):\n");
    if (r < 0) {
        buf[0] = '\0';
        return;
    }
    used = (size_t)r;

    for (int i = 0; i < c->nf && used < n; i++) {
        char label[160];
        fmt_preset(&c->f[i], label, sizeof(label));
        r = snprintf(buf + used, n - used, "  %c  %s\n", c->f[i].key, label);
        if (r < 0)
            break;
        used += (size_t)r;
    }

    if (used < n) {
        r = snprintf(buf + used, n - used,
                     "  %c  vuelve a la traza cruda (Raw)\n"
                     "  %c  abre la ventana Filters…\n",
                     c->raw_key, c->menu_key);
        if (r > 0)
            used += (size_t)r;
    }

    if (c->stalta_declared && used < n) {
        snprintf(buf + used, n - used,
                 "  %c  STA/LTA %g/%g s (on %g, off %g) %s\n",
                 c->stalta_key, c->stalta_sta, c->stalta_lta,
                 c->stalta_on_thr, c->stalta_off_thr,
                 c->stalta_on ? "[activo]" : "[apagado]");
    }
}
