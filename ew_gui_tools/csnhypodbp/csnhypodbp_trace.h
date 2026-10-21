/******************************************************************************
 * csnhypodbp_trace.h                                                          *
 *                                                                             *
 * Nucleo PURO (sin GTK) de la pestana de ondas: parseo de TYPE_PICK_SCNL      *
 * (token 11 fase / token 12 origen), captura por estacion y seleccion de la    *
 * referencia P (picada o teorica). Se testea headless.                        *
 *                                                                             *
 * Regla del origen (identica a csntvp): token 12 'M'/'m' => manual; ausente o  *
 * cualquier otro => automatico 'A'. La fase: token 11 'S'/'s' => S, si no P.  *
 ******************************************************************************/
#ifndef CSNHYPODBP_TRACE_H
#define CSNHYPODBP_TRACE_H

#include <stddef.h>

/* Capacidad de la captura circular por estacion (compilada). */
#define CSNHYPODBP_CAP_MAX 64

/* Picada capturada del PICK_RING. */
typedef struct {
    char   sta[16];
    char   chan[16];
    char   phase;    /* 'P' | 'S' */
    char   origin;   /* 'A' (automatico) | 'M' (manual) */
    double t;        /* epoch (s) */
} CsnhypodbpTracePick;

/* Parseo de un mensaje TYPE_PICK_SCNL. Devuelve 1 si es valido y copia en
 * `out`; 0 si no lo es. */
int csnhypodbp_pick_parse(const char *msg, CsnhypodbpTracePick *out);

/* Captura circular por estacion. */
typedef struct {
    CsnhypodbpTracePick picks[CSNHYPODBP_CAP_MAX];
    int n;      /* cuantas hay (<= CSNHYPODBP_CAP_MAX) */
    int next;   /* indice circular de insercion    */
} CsnhypodbpPickCap;

void csnhypodbp_cap_init(CsnhypodbpPickCap *cap);
void csnhypodbp_cap_add(CsnhypodbpPickCap *cap, const CsnhypodbpTracePick *p);

/* Picada de `phase` de `sta` (ignora canal) mas cercana a t0 dentro de
 * window_sec. Devuelve 1 y copia si la encuentra; 0 si no. */
int csnhypodbp_cap_find(const CsnhypodbpPickCap *cap, const char *sta,
                        char phase, double t0, double window_sec,
                        CsnhypodbpTracePick *out);

/* Copia hasta `max_out` picadas de `sta`/`phase` dentro de window_sec.
 * Devuelve cuantas copio (<= max_out). */
int csnhypodbp_cap_find_all(const CsnhypodbpPickCap *cap, const char *sta,
                            char phase, double t0, double window_sec,
                            CsnhypodbpTracePick *out, int max_out);

/* Copia hasta `max_out` picadas de `sta` (CUALQUIER fase) dentro de
 * `window_sec` de `t0` (|t - t0| <= window). Devuelve cuantas copio. */
int csnhypodbp_cap_window(const CsnhypodbpPickCap *cap, const char *sta,
                          double t0, double window_sec,
                          CsnhypodbpTracePick *out, int max_out);

/* Referencia P: primer pick de fase P en `p` (n) si existe (devuelve 1 y
 * *ref_t = su tiempo); si no, devuelve 0 y *ref_t = theo_p. */
int csnhypodbp_ref_pick(const CsnhypodbpTracePick *p, int n,
                        double theo_p, double *ref_t);

/* --- Colores de las picadas (marcas sobre la traza) ---------------------
 * La traza se dibuja SIEMPRE negra; el origen de la picada se distingue por
 * el color de su marca. Cada fase tiene un color manual (pColor/sColor del
 * .d) y, opcionalmente, uno propio para automaticas y teoricas. Si no se
 * declaran, se DERIVAN del manual aclarandolo hacia blanco al 40 % (auto) y
 * al 70 % (teorica) en el momento de dibujar: cambiar el manual recolorea
 * tambien a las heredadas. */
#define CSNHYPODBP_LIGHTEN_AUTO 0.40
#define CSNHYPODBP_LIGHTEN_THEO 0.70

typedef enum {
    CSNHYPODBP_PICK_MANUAL = 0,
    CSNHYPODBP_PICK_AUTO   = 1,
    CSNHYPODBP_PICK_THEO   = 2
} CsnhypodbpPickOrigin;

/* Color manual (base) + overrides explicitos de auto/teorica. */
typedef struct {
    double manual[3];   /* pColor / sColor */
    int    has_auto;    /* 1 = auto_rgb es explicito (.d o menu) */
    double auto_rgb[3];
    int    has_theo;
    double theo_rgb[3];
} CsnhypodbpColorSet;

/* 'M' -> MANUAL; 'T' -> THEO; 'A', 0, ' ' o cualquier otro -> AUTO. */
CsnhypodbpPickOrigin csnhypodbp_pick_origin(char origin);

/* Aclara un canal hacia blanco: c + (1-c)*frac, con c y frac acotados a
 * [0,1]. */
double csnhypodbp_color_tint_channel(double c, double frac);
void   csnhypodbp_color_tint(const double base[3], double frac, double out[3]);

void csnhypodbp_color_set_init(CsnhypodbpColorSet *set, const double base[3]);
void csnhypodbp_color_set_override(CsnhypodbpColorSet *set,
                                   CsnhypodbpPickOrigin o, const double rgb[3]);
/* MANUAL -> manual[]; AUTO/THEO -> su override si lo hay, si no el manual
 * aclarado (40 % / 70 %). */
void csnhypodbp_color_resolve(const CsnhypodbpColorSet *set,
                              CsnhypodbpPickOrigin o, double rgb[3]);

/* "RRGGBB" mayusculas (redondeo half-up); escribe 6 digitos + NUL. */
void csnhypodbp_color_to_hex(const double rgb[3], char out[7]);

/* Color de la traza: SIEMPRE negra (0,0,0); el origen se ve en la marca. */
void csnhypodbp_trace_rgb(double rgb[3]);

/* Fase elegida por tecla: 'S'/'s' => 'S', resto => 'P'. */
char csnhypodbp_phase_from_char(char c);

#endif /* CSNHYPODBP_TRACE_H */
