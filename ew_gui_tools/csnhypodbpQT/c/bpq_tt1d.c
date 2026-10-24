/*
 * bpq_tt1d.c - Ver bpq_tt1d.h.
 *
 * Matematica del trazado (capas planas, velocidad constante):
 *
 *   Para un parametro de rayo p (slowness horizontal, s/km) cada tramo de
 *   espesor h en una capa de velocidad v aporta
 *
 *       q   = sqrt(1/v^2 - p^2)          (slowness vertical)
 *       X   = h * p / q                  (avance horizontal, km)
 *       T   = h / (v^2 * q)              (tiempo, s)
 *
 *   - Rayo DIRECTO: resolver X(p) = delta por biseccion (X crece con p) y
 *     devolver T(p). Es el caso "fuente y estacion en la misma capa" de
 *     TRVCON (tdj1 = sqrt((z-stz)^2+delta^2)/V) generalizado a N capas.
 *   - Onda REFRACTADA en el techo de la capa mm (v_ref = v[mm]): con
 *     p = 1/v_ref se suman las dos patas (bajada desde la fuente y subida a la
 *     estacion) y el tramo que corre por la interfaz:
 *         T = T_bajada + T_subida + (delta - X_bajada - X_subida) / v_ref
 *     Solo vale si delta alcanza el offset previo (si no, la onda no llega).
 *   - T = min(directo, refractadas), como el minimo de ramas de TRVCON.
 *
 * Sin correccion de curvatura ni earth-flattening: igual que HYPOINVERSE, que
 * calcula con distancias UTM planas. Por eso el llamador solo lo usa en el
 * dominio regional (ver TheoMaxDeltaKm).
 */
#include "bpq_tt1d.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BPQ_TT1D_MAX_MODELS 36      /* HYPOINVERSE LM=36 */
#define BPQ_TT1D_DEG2KM     111.195 /* misma metrica que hytra.for/HDIS */
#define BPQ_TT1D_RADDEG     57.29577951308232
#define BPQ_TT1D_PI         3.14159265358979323846
#define BPQ_TT1D_EPS        1e-9

struct BpqTt1dModel {
    char   name[64];
    int    n;                            /* capas totales (ultima = semiespacio) */
    double v[BPQ_TT1D_MAX_LAYERS];
    double top[BPQ_TT1D_MAX_LAYERS];     /* profundidad del techo de cada capa */
};

typedef struct {
    double lat, lon_pw;                  /* oeste-positivo, como el .hyp */
    double rad1, drad;
    int    mod;
} BpqTt1dNode;

struct BpqTt1dSet {
    BpqTt1dModel *mod[BPQ_TT1D_MAX_MODELS + 1];   /* indice 1..36 */
    int           nmod;
    int           mult, moddef;
    BpqTt1dNode   node[BPQ_TT1D_MAX_REGIONS];
    int           nnode;
};

static void errf(char *err, size_t errn, const char *fmt, ...)
{
    va_list ap;

    if (!err || errn == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, errn, fmt, ap);
    va_end(ap);
}

static char *trim(char *s)
{
    char *e;

    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    *e = '\0';
    return s;
}

static void unquote(char *s)
{
    size_t n = strlen(s);

    if (n >= 2 && (s[0] == '\'' || s[0] == '"') && s[n - 1] == s[0]) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static void join_path(char *out, size_t n, const char *base, const char *path)
{
    if (!path || !path[0]) {
        out[0] = '\0';
        return;
    }
    if (path[0] == '/' || !base || !base[0] || strcmp(base, ".") == 0)
        snprintf(out, n, "%s", path);
    else
        snprintf(out, n, "%s/%s", base, path);
}

/* ========================================================================== */
/* .crh                                                                       */
/* ========================================================================== */

BpqTt1dModel *bpq_tt1d_crh_load(const char *path, char *err, size_t errn)
{
    FILE *fp;
    char line[512];
    int ln = 0, have_name = 0;
    BpqTt1dModel *m;

    if (err && errn) err[0] = '\0';
    if (!path || !path[0]) {
        errf(err, errn, "ruta .crh vacia");
        return NULL;
    }
    fp = fopen(path, "r");
    if (!fp) {
        errf(err, errn, "%s: no se puede abrir", path);
        return NULL;
    }
    m = (BpqTt1dModel *)calloc(1, sizeof(*m));
    if (!m) {
        fclose(fp);
        errf(err, errn, "%s: sin memoria", path);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *s, *hash;
        double v = 0.0, d = 0.0;

        ln++;
        hash = strchr(line, '#');
        if (hash) *hash = '\0';
        s = trim(line);
        if (!*s) continue;

        if (!have_name) {
            /* La primera linea no vacia es el nombre del modelo. */
            snprintf(m->name, sizeof(m->name), "%s", s);
            have_name = 1;
            continue;
        }
        if (sscanf(s, "%lf %lf", &v, &d) != 2) {
            errf(err, errn, "%s:%d: se esperaban dos numeros (Vp profundidad)",
                 path, ln);
            goto fail;
        }
        if (m->n >= BPQ_TT1D_MAX_LAYERS) {
            errf(err, errn, "%s:%d: mas de %d capas", path, ln,
                 BPQ_TT1D_MAX_LAYERS);
            goto fail;
        }
        if (!(v > 0.0)) {
            errf(err, errn, "%s:%d: Vp no positiva (%.2f)", path, ln, v);
            goto fail;
        }
        if (m->n == 0 && fabs(d) > 1e-6) {
            errf(err, errn, "%s:%d: la primera profundidad debe ser 0 (es %.2f)",
                 path, ln, d);
            goto fail;
        }
        if (m->n > 0) {
            if (!(d > m->top[m->n - 1])) {
                errf(err, errn, "%s:%d: profundidad no creciente (%.2f tras %.2f)",
                     path, ln, d, m->top[m->n - 1]);
                goto fail;
            }
            if (v < m->v[m->n - 1] - 1e-9) {
                errf(err, errn, "%s:%d: Vp decreciente (%.2f tras %.2f)",
                     path, ln, v, m->v[m->n - 1]);
                goto fail;
            }
        }
        m->v[m->n] = v;
        m->top[m->n] = d;
        m->n++;
    }
    fclose(fp);

    if (m->n == 0) {
        errf(err, errn, "%s: sin capas (la 1.a linea es el nombre del modelo)",
             path);
        free(m);
        return NULL;
    }
    return m;

fail:
    fclose(fp);
    free(m);
    return NULL;
}

void bpq_tt1d_model_free(BpqTt1dModel *m)
{
    free(m);
}

int bpq_tt1d_model_layers(const BpqTt1dModel *m)
{
    return m ? m->n : 0;
}

const char *bpq_tt1d_model_name(const BpqTt1dModel *m)
{
    return (m && m->name[0]) ? m->name : NULL;
}

/* ========================================================================== */
/* Trazado                                                                    */
/* ========================================================================== */

/* Un tramo de la pata ascendente: espesor y velocidad. */
typedef struct {
    double h, v;
} Seg;

/* Tramos desde `depth` hasta la superficie (estacion a 0 km). */
static int up_segments(const BpqTt1dModel *m, double depth, Seg *seg, int max)
{
    int ls = 0, i, n = 0;
    double h;

    while (ls < m->n - 1 && depth > m->top[ls + 1]) ls++;
    h = depth - m->top[ls];
    if (h > BPQ_TT1D_EPS && n < max) {
        seg[n].h = h;
        seg[n].v = m->v[ls];
        n++;
    }
    for (i = ls - 1; i >= 0 && n < max; i--) {
        const double hh = m->top[i + 1] - m->top[i];

        if (hh <= BPQ_TT1D_EPS) continue;
        seg[n].h = hh;
        seg[n].v = m->v[i];
        n++;
    }
    return n;
}

/* Suma X(p) y T(p) de todos los tramos. 0 = p no es valido en alguno. */
static int ray_sums(const Seg *seg, int n, double p, double *x, double *t)
{
    double xs = 0.0, ts = 0.0;
    int i;

    for (i = 0; i < n; i++) {
        const double inv = 1.0 / seg[i].v;
        const double q2 = inv * inv - p * p;
        double q;

        if (!(q2 > 0.0)) return 0;
        q = sqrt(q2);
        xs += seg[i].h * p / q;
        ts += seg[i].h / (seg[i].v * seg[i].v * q);
    }
    *x = xs;
    *t = ts;
    return 1;
}

/* Rayo directo: invertir X(p) = delta. INFINITY si no llega. */
static double direct_time(const Seg *seg, int n, double delta)
{
    double vmax = 0.0, p_hi, p0, p1, x = 0.0, t = 0.0;
    int i;

    if (n <= 0) return INFINITY;
    if (delta <= 0.0) {
        double t0 = 0.0;

        for (i = 0; i < n; i++) t0 += seg[i].h / seg[i].v;
        return t0;
    }
    for (i = 0; i < n; i++)
        if (seg[i].h > BPQ_TT1D_EPS && seg[i].v > vmax) vmax = seg[i].v;
    if (!(vmax > 0.0)) return INFINITY;

    /* p -> 1/vmax da X -> infinito; el tope deja margen numerico. */
    p_hi = (1.0 - 1e-9) / vmax;
    if (!ray_sums(seg, n, p_hi, &x, &t) || x < delta) return INFINITY;

    p0 = 0.0;
    p1 = p_hi;
    for (i = 0; i < 80; i++) {
        const double pm = 0.5 * (p0 + p1);

        if (!ray_sums(seg, n, pm, &x, &t)) break;
        if (x < delta) p0 = pm;
        else p1 = pm;
    }
    if (!ray_sums(seg, n, 0.5 * (p0 + p1), &x, &t)) return INFINITY;
    return t;
}

/* Suma un tramo de una refractada; 0 = la velocidad de la capa no es menor que
 * la del refractor (interfaz degenerada, p. ej. 8.48/8.48 en chile_1d.crh). */
static int seg_add(double h, double v, double p, double *x, double *t)
{
    const double inv = 1.0 / v;
    const double q2 = inv * inv - p * p;
    double q;

    if (!(q2 > 0.0)) return 0;
    q = sqrt(q2);
    *x += h * p / q;
    *t += h / (v * v * q);
    return 1;
}

/* Minimo de las ondas refractadas en el techo de cada capa bajo la fuente. */
static double head_time(const BpqTt1dModel *m, double depth, double delta)
{
    int ls = 0, mm;
    double best = INFINITY;

    if (delta <= 0.0) return INFINITY;
    while (ls < m->n - 1 && depth > m->top[ls + 1]) ls++;
    if (ls >= m->n - 1) return INFINITY;     /* fuente en el semiespacio */

    for (mm = ls + 1; mm < m->n; mm++) {
        const double vr = m->v[mm];
        const double p = 1.0 / vr;
        double x = 0.0, t = 0.0;
        int i, ok = 1;

        /* Pata descendente: de la fuente al techo de la capa mm. */
        if (m->top[ls + 1] - depth > BPQ_TT1D_EPS)
            ok = seg_add(m->top[ls + 1] - depth, m->v[ls], p, &x, &t);
        for (i = ls + 1; ok && i <= mm - 1; i++)
            ok = seg_add(m->top[i + 1] - m->top[i], m->v[i], p, &x, &t);
        /* Pata ascendente: del techo de mm a la superficie. */
        for (i = mm - 1; ok && i >= 0; i--)
            ok = seg_add(m->top[i + 1] - m->top[i], m->v[i], p, &x, &t);
        if (!ok) continue;
        if (delta < x) continue;              /* la onda no alcanza esa distancia */

        t += (delta - x) / vr;
        if (t < best) best = t;
    }
    return best;
}

double bpq_tt1d_time(const BpqTt1dModel *m, double delta_km, double depth_km)
{
    Seg seg[BPQ_TT1D_MAX_LAYERS];
    double td, th, t;
    int n;

    if (!m || m->n <= 0) return NAN;
    if (!isfinite(delta_km) || !isfinite(depth_km)) return NAN;
    if (delta_km < 0.0) delta_km = 0.0;
    if (depth_km < 0.0) depth_km = 0.0;

    n = up_segments(m, depth_km, seg, BPQ_TT1D_MAX_LAYERS);
    /* Fuente en superficie y sin tramos: la P corre por la capa 1. */
    td = (n > 0) ? direct_time(seg, n, delta_km) : delta_km / m->v[0];
    th = head_time(m, depth_km, delta_km);
    t = (td < th) ? td : th;
    return isfinite(t) ? t : NAN;
}

/* ========================================================================== */
/* .hyp: bandas y seleccion                                                   */
/* ========================================================================== */

void bpq_tt1d_set_free(BpqTt1dSet *s)
{
    int i;

    if (!s) return;
    for (i = 0; i <= BPQ_TT1D_MAX_MODELS; i++) bpq_tt1d_model_free(s->mod[i]);
    free(s);
}

int bpq_tt1d_set_models(const BpqTt1dSet *s)
{
    return s ? s->nmod : 0;
}

const char *bpq_tt1d_set_model_name(const BpqTt1dSet *s, int idx)
{
    if (!s || idx < 1 || idx > BPQ_TT1D_MAX_MODELS || !s->mod[idx]) return NULL;
    return bpq_tt1d_model_name(s->mod[idx]);
}

/* Primer modelo declarado (para un .hyp sin MUL ni NOD). */
static int first_model(const BpqTt1dSet *s)
{
    int i;

    for (i = 1; i <= BPQ_TT1D_MAX_MODELS; i++)
        if (s->mod[i]) return i;
    return 0;
}

BpqTt1dSet *bpq_tt1d_hyp_load(const char *hyp_path, const char *base_dir,
                              char *err, size_t errn)
{
    FILE *fp;
    char line[1024];
    int ln = 0;
    BpqTt1dSet *s;

    if (err && errn) err[0] = '\0';
    if (!hyp_path || !hyp_path[0]) {
        errf(err, errn, "ruta .hyp vacia");
        return NULL;
    }
    fp = fopen(hyp_path, "r");
    if (!fp) {
        errf(err, errn, "%s: no se puede abrir", hyp_path);
        return NULL;
    }
    s = (BpqTt1dSet *)calloc(1, sizeof(*s));
    if (!s) {
        fclose(fp);
        errf(err, errn, "%s: sin memoria", hyp_path);
        return NULL;
    }
    s->moddef = 1;

    while (fgets(line, sizeof(line), fp)) {
        char kw[32] = "";
        char *hash;

        ln++;
        hash = strchr(line, '#');
        if (hash) *hash = '\0';
        if (sscanf(line, "%31s", kw) != 1) continue;

        if (strcmp(kw, "CRH") == 0 || strcmp(kw, "CRT") == 0) {
            char raw[640] = "", full[1024] = "";
            int idx = 0;
            BpqTt1dModel *m;
            char cerr[256] = "";

            if (sscanf(line, "%*s %d %639s", &idx, raw) != 2) {
                errf(err, errn, "%s:%d: CRH sin numero o sin archivo",
                     hyp_path, ln);
                goto fail;
            }
            unquote(raw);
            if (idx < 1 || idx > BPQ_TT1D_MAX_MODELS) {
                errf(err, errn, "%s:%d: modelo %d fuera de 1..%d", hyp_path, ln,
                     idx, BPQ_TT1D_MAX_MODELS);
                goto fail;
            }
            if (strcmp(kw, "CRT") == 0) {
                /* Modelo por tabla de gradiente: no es el de este repo. */
                errf(err, errn, "%s:%d: CRT no soportado (solo capas CRH)",
                     hyp_path, ln);
                goto fail;
            }
            join_path(full, sizeof(full), base_dir, raw);
            m = bpq_tt1d_crh_load(full, cerr, sizeof(cerr));
            if (!m) {
                errf(err, errn, "%s:%d: %s", hyp_path, ln,
                     cerr[0] ? cerr : "no se pudo leer el modelo");
                goto fail;
            }
            if (!s->mod[idx]) s->nmod++;
            bpq_tt1d_model_free(s->mod[idx]);
            s->mod[idx] = m;
        } else if (strcmp(kw, "NOD") == 0) {
            double la = 0.0, lamin = 0.0, lo = 0.0, lomin = 0.0;
            double r1 = 0.0, dr = 0.0;
            int mn = 0;

            if (sscanf(line, "%*s %lf %lf %lf %lf %lf %lf %d",
                       &la, &lamin, &lo, &lomin, &r1, &dr, &mn) != 7) {
                errf(err, errn, "%s:%d: NOD con menos de 7 campos", hyp_path, ln);
                goto fail;
            }
            if (s->nnode >= BPQ_TT1D_MAX_REGIONS) {
                errf(err, errn, "%s:%d: mas de %d nodos", hyp_path, ln,
                     BPQ_TT1D_MAX_REGIONS);
                goto fail;
            }
            s->node[s->nnode].lat = la + lamin / 60.0;
            s->node[s->nnode].lon_pw = lo + lomin / 60.0;
            s->node[s->nnode].rad1 = r1;
            s->node[s->nnode].drad = (dr < 0.1) ? 0.1 : dr;
            s->node[s->nnode].mod = mn;
            s->nnode++;
        } else if (strcmp(kw, "MUL") == 0) {
            char tf[16] = "";
            int def = 0;

            if (sscanf(line, "%*s %15s %d", tf, &def) >= 1)
                s->mult = (tf[0] == 'T' || tf[0] == 't') ? 1 : 0;
            if (def > 0) s->moddef = def;
        }
        /* El resto de tarjetas no afecta al trazado. */
    }
    fclose(fp);

    if (s->nmod == 0) {
        errf(err, errn, "%s: sin tarjetas CRH", hyp_path);
        goto fail_nofile;
    }
    if (!s->mult) s->moddef = first_model(s);
    if (s->moddef < 1 || s->moddef > BPQ_TT1D_MAX_MODELS || !s->mod[s->moddef]) {
        errf(err, errn, "%s: el modelo por defecto %d no esta declarado",
             hyp_path, s->moddef);
        goto fail_nofile;
    }
    {
        int i;

        for (i = 0; i < s->nnode; i++) {
            const int mn = s->node[i].mod;

            if (mn < 1 || mn > BPQ_TT1D_MAX_MODELS || !s->mod[mn]) {
                errf(err, errn, "%s: NOD %d usa el modelo %d, no declarado",
                     hyp_path, i + 1, mn);
                goto fail_nofile;
            }
        }
    }
    return s;

fail:
    fclose(fp);
fail_nofile:
    bpq_tt1d_set_free(s);
    return NULL;
}

static double node_hdis(const BpqTt1dNode *nd, double lat, double lon_east)
{
    const double coslat = cos(lat / BPQ_TT1D_RADDEG);
    const double dlat = lat - nd->lat;
    const double dlon = coslat * ((-lon_east) - nd->lon_pw);

    return BPQ_TT1D_DEG2KM * sqrt(dlat * dlat + dlon * dlon);
}

/* Suma un modelo a la lista (mismo indice: se acumula el peso). */
static void sel_add(int *mo, double *w, int *n, int mod, double wt)
{
    int i;

    for (i = 0; i < *n; i++) {
        if (mo[i] == mod) {
            w[i] += wt;
            return;
        }
    }
    if (*n < 4) {
        mo[*n] = mod;
        w[*n] = wt;
        (*n)++;
    }
}

static int sel_one(BpqTt1dSel *out, int mod, double wt)
{
    out->n = 1;
    out->mod[0] = mod;
    out->wt[0] = wt;
    return 1;
}

int bpq_tt1d_select(const BpqTt1dSet *s, double lat, double lon, BpqTt1dSel *out)
{
    int i, n = 0, mo[4], keep, k;
    double w[4], sum = 0.0;

    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!s || s->nmod == 0) return 0;
    if (!isfinite(lat) || !isfinite(lon)) return 0;
    if (!s->mult || s->nnode == 0) return sel_one(out, s->moddef, 1.0);

    /* Dentro de un circulo RAD1: ese modelo, sin mezcla (hytra.for:29-36). */
    for (i = 0; i < s->nnode; i++)
        if (node_hdis(&s->node[i], lat, lon) <= s->node[i].rad1)
            return sel_one(out, s->node[i].mod, 1.0);

    /* Anillo de transicion: taper coseno 1 -> 0 entre RAD1 y RAD1+DRAD. */
    for (i = 0; i < s->nnode && n < 3; i++) {
        const double d = node_hdis(&s->node[i], lat, lon);
        double wt;

        if (d >= s->node[i].rad1 + s->node[i].drad) continue;
        wt = 0.5 + 0.5 * cos(BPQ_TT1D_PI * (d - s->node[i].rad1) / s->node[i].drad);
        if (wt > 0.0) sel_add(mo, w, &n, s->node[i].mod, wt);
    }
    for (i = 0; i < n; i++) sum += w[i];
    if (n < 3 && sum < 1.0) {
        sel_add(mo, w, &n, s->moddef, 1.0 - sum);
        sum = 1.0;
    }
    if (sum <= 0.0) return sel_one(out, s->moddef, 1.0);

    /* Normaliza y descarta pesos nulos (maximo 3 modelos). */
    for (i = 0, keep = 0; i < n && keep < 3; i++) {
        if (w[i] <= 0.0) continue;
        for (k = 0; k < keep; k++)
            if (out->mod[k] == mo[i]) break;
        if (k < keep) {
            out->wt[k] += w[i] / sum;      /* no deberia: sel_add ya fusiona */
            continue;
        }
        out->mod[keep] = mo[i];
        out->wt[keep] = w[i] / sum;
        keep++;
    }
    out->n = keep;
    return keep > 0;
}

double bpq_tt1d_eval(const BpqTt1dSet *s, const BpqTt1dSel *sel,
                     double delta_km, double depth_km)
{
    double acc = 0.0, wsum = 0.0;
    int i;

    if (!s || !sel || sel->n <= 0) return NAN;
    for (i = 0; i < sel->n; i++) {
        const int idx = sel->mod[i];
        const BpqTt1dModel *m;
        double t;

        if (idx < 1 || idx > BPQ_TT1D_MAX_MODELS) continue;
        m = s->mod[idx];
        if (!m || !(sel->wt[i] > 0.0)) continue;
        t = bpq_tt1d_time(m, delta_km, depth_km);
        if (!isfinite(t)) continue;
        acc += sel->wt[i] * t;
        wsum += sel->wt[i];
    }
    return (wsum > 0.0) ? acc / wsum : NAN;
}
