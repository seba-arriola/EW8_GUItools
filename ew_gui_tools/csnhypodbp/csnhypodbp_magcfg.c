/*
 * csnhypodbp_magcfg.c - Lector de los parametros de magnitud del .d de csnmags
 * (ver csnhypodbp_magcfg.h). Parser minimo `Clave valor...`: solo las claves que
 * la GUI usa para dibujar; el resto se ignora.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csnhypodbp_magcfg.h"

void csnhypodbp_magcfg_defaults(CsnhypodbpMagCfg *c)
{
    if (!c) return;
    c->ml_ta = 10.0;  c->ml_tb = 45.0;
    c->mwp_start = 0.0; c->mwp_t0 = 95.0;
    c->mb_window = 30.0;
    c->ms_window = 400.0;
    c->wa_period = 0.8; c->wa_damp = 0.7; c->wa_gain = 2080.0;
    c->mb_f1 = 0.5;   c->mb_f2 = 2.0;
    c->ms_f1 = 0.045; c->ms_f2 = 0.056;
    /* Puertas de distancia: identicas a csnmags/mag_config.c. */
    c->ml_min_delta = 0.0;  c->ml_max_delta = 8.0;   c->ml_max_dist_km = 600.0;
    c->ml_dist_type = 1;
    c->mwp_min_delta = 3.0; c->mwp_max_delta = 90.0;
    c->mb_min_delta = 5.0;  c->mb_max_delta = 105.0;
    c->ms_min_delta = 20.0; c->ms_max_delta = 160.0;
    /* Corte de profundidad: solo Ms lo trae (60 km, el mismo veto de csnmags);
       `Ms_DeepCorr != 0` lo desactiva. */
    c->ml_max_depth = 0.0; c->mwp_max_depth = 0.0; c->mb_max_depth = 0.0;
    c->ms_max_depth = 60.0;
    c->ml_min_depth = 0.0; c->mwp_min_depth = 0.0; c->mb_min_depth = 0.0;
    c->ms_min_depth = 0.0;
    c->ms_deep_corr = 0;
}

/* Primer numero del resto de la linea; 1 si lo hay. */
static int num1(const char *v, double *out)
{
    double x;
    if (!v || sscanf(v, "%lf", &x) != 1) return 0;
    *out = x;
    return 1;
}

/* Los n primeros numeros del resto de la linea; 1 si estan todos. */
static int numn(const char *v, double *out, int n)
{
    const char *p = v;
    int i;
    if (!v) return 0;
    for (i = 0; i < n; i++) {
        char *end;
        double x = strtod(p, &end);
        if (end == p) return 0;
        out[i] = x;
        p = end;
    }
    return 1;
}

int csnhypodbp_magcfg_load(CsnhypodbpMagCfg *c, const char *path)
{
    FILE *f;
    char line[512];

    if (!c || !path || !path[0]) return -1;
    f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *key, *val, *end;
        double t[3];

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;
        key = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
        val = p;
        while (*val == ' ' || *val == '\t') val++;
        end = val + strlen(val);
        while (end > val && (end[-1] == '\n' || end[-1] == '\r' ||
                             end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';

        if (strcmp(key, "WoodAndersonCoefs") == 0) {
            if (numn(val, t, 3)) {
                c->wa_period = t[0]; c->wa_damp = t[1]; c->wa_gain = t[2];
            }
        } else if (strcmp(key, "Ml_TA") == 0) {
            num1(val, &c->ml_ta);
        } else if (strcmp(key, "Ml_TB") == 0) {
            num1(val, &c->ml_tb);
        } else if (strcmp(key, "Mwp_StartOff") == 0) {
            num1(val, &c->mwp_start);
        } else if (strcmp(key, "Mwp_T0") == 0) {
            num1(val, &c->mwp_t0);
        } else if (strcmp(key, "Mb_Window") == 0) {
            num1(val, &c->mb_window);
        } else if (strcmp(key, "Ms_Window") == 0) {
            num1(val, &c->ms_window);
        } else if (strcmp(key, "Mb_Band") == 0) {
            if (numn(val, t, 2)) { c->mb_f1 = t[0]; c->mb_f2 = t[1]; }
        } else if (strcmp(key, "Ms_Band") == 0) {
            if (numn(val, t, 2)) { c->ms_f1 = t[0]; c->ms_f2 = t[1]; }
        } else if (strcmp(key, "Ml_MinDelta") == 0) {
            num1(val, &c->ml_min_delta);
        } else if (strcmp(key, "Ml_MaxDelta") == 0) {
            num1(val, &c->ml_max_delta);
        } else if (strcmp(key, "Ml_MaxDist") == 0) {
            num1(val, &c->ml_max_dist_km);
        } else if (strcmp(key, "Ml_DistType") == 0) {
            double v;
            if (num1(val, &v)) c->ml_dist_type = (v == 0.0) ? 0 : 1;
        } else if (strcmp(key, "Mwp_MinDelta") == 0) {
            num1(val, &c->mwp_min_delta);
        } else if (strcmp(key, "Mwp_MaxDelta") == 0) {
            num1(val, &c->mwp_max_delta);
        } else if (strcmp(key, "Mb_MinDelta") == 0) {
            num1(val, &c->mb_min_delta);
        } else if (strcmp(key, "Mb_MaxDelta") == 0) {
            num1(val, &c->mb_max_delta);
        } else if (strcmp(key, "Ms_MinDelta") == 0) {
            num1(val, &c->ms_min_delta);
        } else if (strcmp(key, "Ms_MaxDelta") == 0) {
            num1(val, &c->ms_max_delta);
        } else if (strcmp(key, "Ml_MaxDepth") == 0) {
            num1(val, &c->ml_max_depth);
        } else if (strcmp(key, "Mwp_MaxDepth") == 0) {
            num1(val, &c->mwp_max_depth);
        } else if (strcmp(key, "Mb_MaxDepth") == 0) {
            num1(val, &c->mb_max_depth);
        } else if (strcmp(key, "Ms_MaxDepth") == 0) {
            num1(val, &c->ms_max_depth);
        } else if (strcmp(key, "Ml_MinDepth") == 0) {
            num1(val, &c->ml_min_depth);
        } else if (strcmp(key, "Mwp_MinDepth") == 0) {
            num1(val, &c->mwp_min_depth);
        } else if (strcmp(key, "Mb_MinDepth") == 0) {
            num1(val, &c->mb_min_depth);
        } else if (strcmp(key, "Ms_MinDepth") == 0) {
            num1(val, &c->ms_min_depth);
        } else if (strcmp(key, "Ms_DeepCorr") == 0) {
            double v;
            if (num1(val, &v)) c->ms_deep_corr = (v == 0.0) ? 0 : 1;
        }
    }
    fclose(f);
    return 0;
}

void csnhypodbp_magcfg_window(const CsnhypodbpMagCfg *c, int kind,
                              double *t0, double *t1)
{
    CsnhypodbpMagCfg d;
    double a = 0.0, b = 0.0;

    if (!c) { csnhypodbp_magcfg_defaults(&d); c = &d; }
    switch (kind) {
    case CSNHYPODBP_MAG_ML:  a = c->ml_ta;  b = c->ml_tb; break;
    case CSNHYPODBP_MAG_MWP: a = c->mwp_start; b = c->mwp_start + c->mwp_t0; break;
    case CSNHYPODBP_MAG_MB:  b = c->mb_window; break;
    case CSNHYPODBP_MAG_MS:  b = c->ms_window; break;
    default: break;
    }
    if (t0) *t0 = a;
    if (t1) *t1 = b;
}

const char *csnhypodbp_magcfg_name(int kind)
{
    switch (kind) {
    case CSNHYPODBP_MAG_ML:  return "ML";
    case CSNHYPODBP_MAG_MWP: return "Mwp";
    case CSNHYPODBP_MAG_MB:  return "Mb";
    case CSNHYPODBP_MAG_MS:  return "Ms";
    default: return "-";
    }
}

/* Limites vigentes de la puerta de `kind` (grados; `hi_km` solo en ML). */
void csnhypodbp_magcfg_gate_limits(const CsnhypodbpMagCfg *c, int kind,
                                   double *lo, double *hi, double *hi_km)
{
    CsnhypodbpMagCfg d;

    *lo = *hi = *hi_km = 0.0;
    if (!c) { csnhypodbp_magcfg_defaults(&d); c = &d; }
    switch (kind) {
    case CSNHYPODBP_MAG_ML:
        *lo = c->ml_min_delta; *hi = c->ml_max_delta; *hi_km = c->ml_max_dist_km;
        break;
    case CSNHYPODBP_MAG_MWP: *lo = c->mwp_min_delta; *hi = c->mwp_max_delta; break;
    case CSNHYPODBP_MAG_MB:  *lo = c->mb_min_delta;  *hi = c->mb_max_delta;  break;
    case CSNHYPODBP_MAG_MS:  *lo = c->ms_min_delta;  *hi = c->ms_max_delta;  break;
    default: break;   /* magnitud desconocida: sin puerta (0,0) */
    }
}

void csnhypodbp_magcfg_apply_override(const CsnhypodbpMagCfg *c, int kind,
                                      const CsnhypodbpMagGateOv *ov,
                                      CsnhypodbpMagCfg *out)
{
    CsnhypodbpMagCfg d;

    if (!out) return;
    if (!c) { csnhypodbp_magcfg_defaults(&d); c = &d; }
    *out = *c;
    if (!ov || !ov->on) return;
    switch (kind) {
    case CSNHYPODBP_MAG_ML:
        out->ml_min_delta = ov->min_delta;
        out->ml_max_delta = ov->max_delta;
        if (ov->max_dist_km > 0.0) out->ml_max_dist_km = ov->max_dist_km;
        out->ml_min_depth = ov->min_depth;
        out->ml_max_depth = ov->max_depth;
        break;
    case CSNHYPODBP_MAG_MWP:
        out->mwp_min_delta = ov->min_delta; out->mwp_max_delta = ov->max_delta;
        out->mwp_min_depth = ov->min_depth;
        out->mwp_max_depth = ov->max_depth;
        break;
    case CSNHYPODBP_MAG_MB:
        out->mb_min_delta = ov->min_delta;  out->mb_max_delta = ov->max_delta;
        out->mb_min_depth = ov->min_depth;
        out->mb_max_depth = ov->max_depth;
        break;
    case CSNHYPODBP_MAG_MS:
        out->ms_min_delta = ov->min_delta;  out->ms_max_delta = ov->max_delta;
        out->ms_min_depth = ov->min_depth;
        out->ms_max_depth = ov->max_depth;
        out->ms_deep_corr = 0;   /* el override manda sobre Ms_DeepCorr */
        break;
    default: break;
    }
}

void csnhypodbp_magcfg_gate(const CsnhypodbpMagCfg *c, int kind,
                            double dist_deg, double dist_km, int *in_range)
{
    double lo, hi, hi_km;

    if (!in_range) return;
    *in_range = 1;
    csnhypodbp_magcfg_gate_limits(c, kind, &lo, &hi, &hi_km);
    if (hi <= 0.0 && hi_km <= 0.0) return;              /* sin puerta */
    if (!isfinite(dist_deg) || dist_deg < 0.0) return;  /* distancia desconocida */
    if (dist_deg < lo || (hi > 0.0 && dist_deg > hi)) { *in_range = 0; return; }
    /* ML tiene ademas un corte en km (Ml_MaxDist). */
    if (hi_km > 0.0 && isfinite(dist_km) && dist_km >= 0.0 && dist_km > hi_km)
        *in_range = 0;
}

double csnhypodbp_magcfg_max_depth(const CsnhypodbpMagCfg *c, int kind)
{
    CsnhypodbpMagCfg d;

    if (!c) { csnhypodbp_magcfg_defaults(&d); c = &d; }
    switch (kind) {
    case CSNHYPODBP_MAG_ML:  return c->ml_max_depth;
    case CSNHYPODBP_MAG_MWP: return c->mwp_max_depth;
    case CSNHYPODBP_MAG_MB:  return c->mb_max_depth;
    /* Con `Ms_DeepCorr != 0` csnmags no veta: la GUI tampoco. */
    case CSNHYPODBP_MAG_MS:  return c->ms_deep_corr ? 0.0 : c->ms_max_depth;
    default: return 0.0;
    }
}

double csnhypodbp_magcfg_min_depth(const CsnhypodbpMagCfg *c, int kind)
{
    CsnhypodbpMagCfg d;

    if (!c) { csnhypodbp_magcfg_defaults(&d); c = &d; }
    switch (kind) {
    case CSNHYPODBP_MAG_ML:  return c->ml_min_depth;
    case CSNHYPODBP_MAG_MWP: return c->mwp_min_depth;
    case CSNHYPODBP_MAG_MB:  return c->mb_min_depth;
    case CSNHYPODBP_MAG_MS:  return c->ms_min_depth;
    default: return 0.0;
    }
}

int csnhypodbp_magcfg_depth_ok(const CsnhypodbpMagCfg *c, int kind, double depth_km)
{
    double lo = csnhypodbp_magcfg_min_depth(c, kind);
    double hi = csnhypodbp_magcfg_max_depth(c, kind);

    if (!isfinite(depth_km) || depth_km < 0.0) return 1;  /* profundidad desconocida */
    if (lo > 0.0 && depth_km < lo) return 0;              /* por debajo del piso */
    if (hi > 0.0 && depth_km > hi) return 0;              /* por encima del techo */
    return 1;
}

void csnhypodbp_magcfg_gate_text(const CsnhypodbpMagCfg *c, int kind,
                                 char *out, size_t n)
{
    double lo, hi, hi_km;

    if (!out || n == 0) return;
    out[0] = '\0';
    csnhypodbp_magcfg_gate_limits(c, kind, &lo, &hi, &hi_km);
    if (hi <= 0.0 && hi_km <= 0.0) return;
    if (hi_km > 0.0)
        snprintf(out, n, "%.0f-%.0f deg y <=%.0f km", lo, hi, hi_km);
    else
        snprintf(out, n, "%.0f-%.0f deg", lo, hi);
}

/* --- Cajas de la barra de overrides --------------------------------------- */

/* Rango y decimales de cada caja: los del `GtkSpinButton` que la barra usaba
 * (rango, paso y `set_digits`), para que el cambio a `GtkEntry` no mueva ni un
 * limite. El orden es el de `CSNHYPODBP_MAG_OV_*`. */
static const struct {
    double lo, hi;
    int    digits;
} MAG_OV_FIELD[CSNHYPODBP_MAG_OV_FIELD_N] = {
    {   0.0,  180.0, 1 },   /* min_delta   [deg] */
    {   0.0,  180.0, 1 },   /* max_delta   [deg] */
    {   0.0, 2000.0, 0 },   /* max_dist_km [km]  */
    {   0.0,  700.0, 0 },   /* min_depth   [km]  */
    {   0.0,  700.0, 0 }    /* max_depth   [km]  */
};

/* Redondeo al mas cercano con `digits` decimales (`.5` hacia arriba en valor
 * absoluto, `round()`): el spin mostraba el valor redondeado y aqui se escribe a
 * mano, asi que un "12.56" en un campo de 1 decimal vale 12.6. */
static double mag_ov_round(double v, int digits)
{
    double f = 1.0;
    int i;

    for (i = 0; i < digits; i++) f *= 10.0;
    return round(v * f) / f;
}

int csnhypodbp_mag_ov_field_limits(int field, double *lo, double *hi, int *digits)
{
    if (field < 0 || field >= CSNHYPODBP_MAG_OV_FIELD_N) return -1;
    if (lo)     *lo = MAG_OV_FIELD[field].lo;
    if (hi)     *hi = MAG_OV_FIELD[field].hi;
    if (digits) *digits = MAG_OV_FIELD[field].digits;
    return 0;
}

int csnhypodbp_mag_ov_fmt(char *out, size_t n, int field, double v)
{
    int digits, len;

    if (!out || n == 0 || field < 0 || field >= CSNHYPODBP_MAG_OV_FIELD_N)
        return -1;
    out[0] = '\0';
    if (!isfinite(v)) return -1;
    digits = MAG_OV_FIELD[field].digits;
    /* `%.0f` de una profundidad con decimales redondearia sin avisar: se recorta
     * el valor al decimal del campo antes de formatear. */
    v = mag_ov_round(v, digits);
    len = snprintf(out, n, "%.*f", digits, v);
    return (len < 0 || (size_t)len >= n) ? -1 : 0;
}

int csnhypodbp_mag_ov_width_chars(int field)
{
    double hi = 0.0;
    char wid[24];

    /* El ancho sigue al texto canonico (no a una constante a mano): si cambia el
     * formato del campo, cambia la caja. */
    if (csnhypodbp_mag_ov_field_limits(field, NULL, &hi, NULL) != 0) return -1;
    if (csnhypodbp_mag_ov_fmt(wid, sizeof wid, field, hi) != 0) return -1;
    return (int)strlen(wid) + CSNHYPODBP_MAG_OV_WIDTH_MARGIN;
}

/* Parseo manual e independiente de `LC_NUMERIC`: `strtod` honra el locale y en
 * este entorno suele ser es_CL, donde "12.5" se corta en el punto (AGENTS §7).
 * Gramatica: espacios, signo, digitos, [. ,] digitos, espacios. Cualquier otra
 * cosa (incluido un resto sin consumir) es invalido. */
static int mag_ov_parse_number(const char *s, double *out)
{
    const char *p = s;
    double v = 0.0, scale = 0.1;
    int ndig = 0, neg = 0;

    if (!s) return -1;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10.0 + (double)(*p - '0'); p++; ndig++; }
    if (*p == '.' || *p == ',') {
        p++;
        while (*p >= '0' && *p <= '9') {
            v += (double)(*p - '0') * scale; scale *= 0.1; p++; ndig++;
        }
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (ndig == 0 || *p != '\0') return -1;      /* vacio, basura o resto */
    *out = neg ? -v : v;
    return 0;
}

int csnhypodbp_mag_ov_parse(const char *txt, int field, double *out)
{
    double v = 0.0;

    if (field < 0 || field >= CSNHYPODBP_MAG_OV_FIELD_N) return -1;
    if (mag_ov_parse_number(txt, &v) != 0) return -1;
    v = mag_ov_round(v, MAG_OV_FIELD[field].digits);
    if (v < MAG_OV_FIELD[field].lo) v = MAG_OV_FIELD[field].lo;
    if (v > MAG_OV_FIELD[field].hi) v = MAG_OV_FIELD[field].hi;
    if (out) *out = v;
    return 0;
}

int csnhypodbp_mag_ov_from_text(CsnhypodbpMagGateOv *ov,
                                const char *txt[CSNHYPODBP_MAG_OV_FIELD_N])
{
    double v[CSNHYPODBP_MAG_OV_FIELD_N];
    int i;

    if (!ov || !txt) return -1;
    for (i = 0; i < CSNHYPODBP_MAG_OV_FIELD_N; i++) {
        if (csnhypodbp_mag_ov_parse(txt[i] ? txt[i] : "", i, &v[i]) != 0)
            return i + 1;            /* 1..N: el primer campo invalido, ov intacto */
    }
    ov->on = 1;
    ov->min_delta   = v[CSNHYPODBP_MAG_OV_MIN_DELTA];
    ov->max_delta   = v[CSNHYPODBP_MAG_OV_MAX_DELTA];
    ov->max_dist_km = v[CSNHYPODBP_MAG_OV_MAX_DIST_KM];
    ov->min_depth   = v[CSNHYPODBP_MAG_OV_MIN_DEPTH];
    ov->max_depth   = v[CSNHYPODBP_MAG_OV_MAX_DEPTH];
    return 0;
}

void csnhypodbp_mag_ov_texts(const CsnhypodbpMagGateOv *ov,
                             char out[CSNHYPODBP_MAG_OV_FIELD_N][24])
{
    if (!ov || !out) return;
    csnhypodbp_mag_ov_fmt(out[CSNHYPODBP_MAG_OV_MIN_DELTA], 24,
                          CSNHYPODBP_MAG_OV_MIN_DELTA, ov->min_delta);
    csnhypodbp_mag_ov_fmt(out[CSNHYPODBP_MAG_OV_MAX_DELTA], 24,
                          CSNHYPODBP_MAG_OV_MAX_DELTA, ov->max_delta);
    csnhypodbp_mag_ov_fmt(out[CSNHYPODBP_MAG_OV_MAX_DIST_KM], 24,
                          CSNHYPODBP_MAG_OV_MAX_DIST_KM, ov->max_dist_km);
    csnhypodbp_mag_ov_fmt(out[CSNHYPODBP_MAG_OV_MIN_DEPTH], 24,
                          CSNHYPODBP_MAG_OV_MIN_DEPTH, ov->min_depth);
    csnhypodbp_mag_ov_fmt(out[CSNHYPODBP_MAG_OV_MAX_DEPTH], 24,
                          CSNHYPODBP_MAG_OV_MAX_DEPTH, ov->max_depth);
}
