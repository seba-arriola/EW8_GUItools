#ifndef CSNHYPODBP_FILTERCFG_H
#define CSNHYPODBP_FILTERCFG_H

#include <stddef.h>

/*
 * Presets de filtro de la pestaña `picking` y sus atajos de teclado.
 * Lógica PURA (sin GTK): se testea headless en test_filtercfg.c.
 *
 * Gramática del .d (todas las claves son aditivas sobre los defaults; una
 * tecla existente reemplaza ese preset):
 *
 *   FilterPhase zero|causal
 *   FilterMenu  <tecla>
 *   FilterRaw   <tecla>
 *   Filter [<nombre>] <HP|LP|BP> <tecla> <f1> [<f2>] <orden>
 *   StaLta <tecla> <sta_s> <lta_s> <on> <off> [<corr_s>]
 *
 * El nombre es opcional: si el 2.º token es HP/LP/BP, el 1.º es el tipo.
 * Las teclas son un carácter ASCII imprimible, sin distinguir mayúsculas;
 * 'p' y 's' están reservadas por el picado de fases. El orden es 2 o 4.
 */

#define CSNHYPODBP_MAX_FILTERS 16

typedef struct {
    char   name[16];
    char   key;            /* 0 = sin atajo */
    int    type;           /* 1=HP, 2=LP, 3=BP */
    double f1, f2;
    int    order;
    unsigned char from_file;   /* 1 = vino del .d (no se puede duplicar) */
} CsnhypodbpFilterPreset;

typedef struct {
    CsnhypodbpFilterPreset f[CSNHYPODBP_MAX_FILTERS];
    int    nf;
    char   menu_key, raw_key;
    int    phase_zero;     /* 1 = fase cero (default), 0 = causal */
    int    stalta_declared, stalta_on;
    char   stalta_key;
    double stalta_sta, stalta_lta, stalta_on_thr, stalta_off_thr, stalta_corr;
} CsnhypodbpFilterCfg;

/* Defaults de fábrica: P_local(1), P_regional(2), P_teleseismic(3),
 * S_local(4), HP(r), LP(k), BP(d); menu f, raw 0, fase cero, StaLta(t). */
void csnhypodbp_filtercfg_defaults(CsnhypodbpFilterCfg *c);

/* Añade una línea. `kind` es la clave leída y `rest` el resto de la línea.
 * Devuelve 0, o -1 con el motivo en err. */
int csnhypodbp_filtercfg_add(CsnhypodbpFilterCfg *c, const char *kind,
                             const char *rest, char *err, size_t errn);

/* Índice del preset con esa tecla (normaliza mayúsculas), o -1. */
int csnhypodbp_filtercfg_find_key(const CsnhypodbpFilterCfg *c, char key);

/* Resultado de pulsar una tecla de filtro. */
enum {
    CSNHYPODBP_FILTER_KEY_MENU = -3,   /* abre la ventana Filters… */
    CSNHYPODBP_FILTER_KEY_NONE = -2,   /* no es tecla de filtro */
    CSNHYPODBP_FILTER_KEY_RAW  = -1    /* vuelve a Raw (o apaga el activo) */
};

/* Resuelve la tecla contra la config: devuelve un índice >= 0 (nuevo preset),
 * CSNHYPODBP_FILTER_KEY_RAW (Raw o toggle del activo) o los otros dos
 * centinelas. `active` es el índice vigente (-1 = Raw). */
int csnhypodbp_filtercfg_key_action(const CsnhypodbpFilterCfg *c, int active,
                                    char key);

/* Etiqueta de un preset (sin el sufijo de STA/LTA). */
void csnhypodbp_filtercfg_format_preset(const CsnhypodbpFilterPreset *p,
                                        char *buf, size_t n);

/* Etiqueta de parámetros sueltos (filtro manual, sin tecla). */
void csnhypodbp_filtercfg_format_manual(int type, double f1, double f2, int order,
                                        char *buf, size_t n);

/* Etiqueta del filtro activo. idx >= 0 = preset; -1 = Raw; -2 = manual (usa
 * los parámetros manuales). Añade " + STA/LTA ..." si está activo. */
void csnhypodbp_filtercfg_format_active(const CsnhypodbpFilterCfg *c, int idx,
                                        int manual_type, double manual_f1,
                                        double manual_f2, int manual_order,
                                        char *buf, size_t n);

/* Texto para ☰ → Help → Commands…, generado de la config vigente. */
void csnhypodbp_filtercfg_help_text(const CsnhypodbpFilterCfg *c,
                                    char *buf, size_t n);

#endif /* CSNHYPODBP_FILTERCFG_H */
