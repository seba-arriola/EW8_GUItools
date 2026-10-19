/*
 * csnhypodbp_magcfg.h - Parametros de magnitud que la GUI necesita para dibujar
 * la traza de vista y las franjas de ventana de la sub-pestaña `magnitudes`.
 *
 * Se leen del MISMO .d que usa csnmags (clave `MagConfigFile`), para no duplicar
 * valores: la GUI dibuja, y lo que dibuja tiene que ser lo que csnmags mide. Si
 * el archivo falta o una clave no esta, quedan los defaults de fabrica (los
 * mismos de run_working_v8/params/csnmags.d).
 */
#ifndef CSNHYPODBP_MAGCFG_H
#define CSNHYPODBP_MAGCFG_H

#include <stddef.h>

/* Orden fijo de las cuatro magnitudes (coincide con los slots del MagStore). */
typedef enum {
    CSNHYPODBP_MAG_ML = 0,
    CSNHYPODBP_MAG_MWP,
    CSNHYPODBP_MAG_MB,
    CSNHYPODBP_MAG_MS,
    CSNHYPODBP_MAG_N
} CsnhypodbpMagKind;

typedef struct {
    double ml_ta, ml_tb;                /* Ml_TA / Ml_TB (s tras la P) */
    double mwp_start, mwp_t0;           /* Mwp_StartOff / Mwp_T0 (s) */
    double mb_window;                   /* Mb_Window (s) */
    double ms_window;                   /* Ms_Window (s; csnmags aun no la tiene) */
    double wa_period, wa_damp, wa_gain; /* WoodAndersonCoefs */
    double mb_f1, mb_f2;                /* Mb_Band (Hz) */
    double ms_f1, ms_f2;                /* Ms_Band (Hz) */
    /* Puertas de distancia por magnitud: MISMAS claves y defaults que usa
     * csnmags (mag_config.c) para decidir que estaciones entran al calculo. La
     * GUI las usa para no listar estaciones que el motor va a descartar. */
    double ml_min_delta, ml_max_delta, ml_max_dist_km;  /* Ml_MinDelta/MaxDelta/MaxDist */
    int    ml_dist_type;                                /* Ml_DistType: 0 hipo, 1 epi */
    double mwp_min_delta, mwp_max_delta;                /* Mwp_MinDelta/Mwp_MaxDelta */
    double mb_min_delta, mb_max_delta;                  /* Mb_MinDelta/Mb_MaxDelta */
    double ms_min_delta, ms_max_delta;                  /* Ms_MinDelta/Ms_MaxDelta */
    /* Corte de PROFUNDIDAD por magnitud (km; 0 = sin corte). El de Ms replica el
     * veto de csnmags: con `Ms_DeepCorr 0` (default) veta eventos mas profundos
     * que 60 km. `ms_deep_corr != 0` desactiva el veto. */
    double ml_max_depth, mwp_max_depth, mb_max_depth, ms_max_depth;
    /* Corte de profundidad MINIMA por magnitud (km; 0 = sin corte). Sirve para
     * acotar el rango: p. ej. Ms solo para eventos someros, o Mb solo para
     * profundos. `ms_deep_corr != 0` desactiva solo el techo de Ms. */
    double ml_min_depth, mwp_min_depth, mb_min_depth, ms_min_depth;
    int    ms_deep_corr;
} CsnhypodbpMagCfg;

/* Defaults de fabrica (los de csnmags.d). */
void csnhypodbp_magcfg_defaults(CsnhypodbpMagCfg *c);

/* Sobrescribe lo que encuentre en `path` (formato `Clave valor...`, con `#` de
 * comentario). Devuelve 0 si se leyo y -1 si no se pudo abrir (en ese caso los
 * defaults quedan intactos). */
int csnhypodbp_magcfg_load(CsnhypodbpMagCfg *c, const char *path);

/* Ventana por defecto de una magnitud, como desplazamientos en segundos
 * respecto del ancla (la P de la estacion). */
void csnhypodbp_magcfg_window(const CsnhypodbpMagCfg *c, int kind,
                              double *t0, double *t1);

/* "ML"/"Mwp"/"Mb"/"Ms" (o "-" si el indice no vale). */
const char *csnhypodbp_magcfg_name(int kind);

/* Limites vigentes de la puerta de `kind` (grados; `max_dist_km` solo se usa en
 * ML). Magnitud desconocida -> todo 0.0. `c` NULL -> defaults. */
void csnhypodbp_magcfg_gate_limits(const CsnhypodbpMagCfg *c, int kind,
                                   double *min_delta, double *max_delta,
                                   double *max_dist_km);

/* 1 si la estacion entra en la puerta de distancia de `kind` (epicentral en
 * grados y, para ML, tambien el corte en km). Distancia desconocida (NaN o
 * negativa) -> 1: no se oculta una estacion por no conocer su distancia.
 * Magnitud desconocida o `c` NULL (se usan los defaults) -> sin puerta. */
void csnhypodbp_magcfg_gate(const CsnhypodbpMagCfg *c, int kind,
                            double dist_deg, double dist_km, int *in_range);

/* Texto de la puerta vigente de `kind`: "0-8 deg y <=600 km", "20-160 deg", ... */
void csnhypodbp_magcfg_gate_text(const CsnhypodbpMagCfg *c, int kind,
                                 char *out, size_t n);

/* Corte de profundidad vigente de `kind` en km (0 = sin corte). */
double csnhypodbp_magcfg_max_depth(const CsnhypodbpMagCfg *c, int kind);

/* Piso de profundidad vigente de `kind` en km (0 = sin piso). */
double csnhypodbp_magcfg_min_depth(const CsnhypodbpMagCfg *c, int kind);

/* 1 si un evento a `depth_km` entra en el rango [min,max] de profundidad de
 * `kind`. Profundidad desconocida -> 1 (no se descarta por no saberla). */
int csnhypodbp_magcfg_depth_ok(const CsnhypodbpMagCfg *c, int kind, double depth_km);

/* Override manual de la puerta de distancia de UNA magnitud (barra inferior de
 * la sub-pestaña `magnitudes`). `on` = 0 -> se usa la del .d. */
typedef struct {
    int    on;
    double min_delta, max_delta, max_dist_km, min_depth, max_depth;
} CsnhypodbpMagGateOv;

/* Copia `c` en `out` con el override de `kind` aplicado (si `ov->on`). El corte
 * en km solo se toca si `ov->max_dist_km > 0`. */
void csnhypodbp_magcfg_apply_override(const CsnhypodbpMagCfg *c, int kind,
                                      const CsnhypodbpMagGateOv *ov,
                                      CsnhypodbpMagCfg *out);

/* --- Cajas de la barra de overrides (sub-pestaña `magnitudes`) -------------
 * La barra usa `GtkEntry` y no `GtkSpinButton` (los botones +/- no aportaban y
 * su marco se llevaba el ancho). El rango, los decimales y el formato de cada
 * caja viven AQUI, en una sola fuente: la UI solo pinta y lee. El orden del
 * enum es el orden en que la barra crea las cajas. */
enum {
    CSNHYPODBP_MAG_OV_MIN_DELTA = 0,  /* distancia epicentral minima [deg] */
    CSNHYPODBP_MAG_OV_MAX_DELTA,      /* distancia epicentral maxima [deg] */
    CSNHYPODBP_MAG_OV_MAX_DIST_KM,    /* corte de distancia hipocentral [km] */
    CSNHYPODBP_MAG_OV_MIN_DEPTH,      /* profundidad minima del evento [km] */
    CSNHYPODBP_MAG_OV_MAX_DEPTH,      /* profundidad maxima del evento [km] */
    CSNHYPODBP_MAG_OV_FIELD_N
};

/* Caracteres de aire de cada caja, ademas de los del valor canonico del campo
 * (el usuario las queria ~70% mas angostas que los 150 px de GTK4). */
#define CSNHYPODBP_MAG_OV_WIDTH_MARGIN 2

/* Rango y decimales del campo: los mismos que tenia el `GtkSpinButton` que la
 * barra usaba antes, para que el cambio de widget no mueva ni un limite.
 * 0 = ok; -1 = `field` fuera de rango. */
int csnhypodbp_mag_ov_field_limits(int field, double *lo, double *hi, int *digits);

/* Ancho en caracteres de la caja de `field`: el del valor canonico mas largo del
 * campo (`fmt` de su tope; p. ej. "180.0") mas `CSNHYPODBP_MAG_OV_WIDTH_MARGIN`.
 * La barra lo fija como `width-chars` Y como `max-width-chars`: en GTK4
 * `width-chars` es solo un SUELO y el ancho natural de un `GtkEntry` es 150 px
 * fijos (`NAT_ENTRY_WIDTH`) si no se acota con `max-width-chars`, que es por lo
 * que las cajas se veian anchas. -1 si `field` esta fuera de rango. */
int csnhypodbp_mag_ov_width_chars(int field);

/* Texto canonico de un valor con los decimales del campo ("12.5", "600").
 * 0 = ok; -1 = buffer nulo/corto o valor no finito. */
int csnhypodbp_mag_ov_fmt(char *out, size_t n, int field, double v);

/* Parsea el texto de una caja: espacios alrededor ok; `.` o `,` como separador
 * decimal (nunca de miles); redondeo al mas cercano con los decimales del campo
 * y acotado a su rango. Vacio, basura o resto sin consumir -> -1 sin tocar
 * `*out`. Independiente de `LC_NUMERIC` (con locale es_CL un `strtod` de C se
 * corta en el punto del decimal; ver AGENTS §7). */
int csnhypodbp_mag_ov_parse(const char *txt, int field, double *out);

/* Vuelca los cinco textos en `ov` (TODO O NADA): 0 y `ov->on = 1` si los cinco
 * valen; si no, `field+1` (1..N) del PRIMER invalido y `ov` sin tocar. `txt`
 * lleva los textos en el orden de `CSNHYPODBP_MAG_OV_*`; NULL = vacio.
 * -1 si `ov` o `txt` son NULL. */
int csnhypodbp_mag_ov_from_text(CsnhypodbpMagGateOv *ov,
                                const char *txt[CSNHYPODBP_MAG_OV_FIELD_N]);

/* Los cinco textos canonicos de `ov`: lo que la barra muestra al sincronizar.
 * `out` es `[CSNHYPODBP_MAG_OV_FIELD_N][24]`. */
void csnhypodbp_mag_ov_texts(const CsnhypodbpMagGateOv *ov,
                             char out[CSNHYPODBP_MAG_OV_FIELD_N][24]);

#endif
