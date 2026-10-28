#ifndef CSNHYPODBP_LOCATOR_H
#define CSNHYPODBP_LOCATOR_H

#include <stddef.h>

/*
 * csnhypodbp_locator.c - Localizadores declarados en csnhypodbpqt.d:
 *   locator <bin> <cfg> <live_usage> <time_recalc_live> <N_retry>
 * El nombre es el basename de <bin>. Si <bin>/<cfg> no contienen '/', se
 * resuelven contra $EW_HOME/$EW_VERSION/bin y $EW_PARAMS respectivamente.
 */

typedef struct {
    char name[32];              /* basename(bin) */
    char bin[256];
    char cfg[256];
    int  live_usage;            /* 0/1 */
    int  time_recalc_live;      /* s */
    int  n_retry;               /* intentos sin picadas nuevas */
} CsnhypodbpLocator;

int csnhypodbp_locator_add(CsnhypodbpLocator *list, int *n, int cap,
                           const char *name, const char *bin, const char *cfg);
/* Alta desde la tabla nueva: nombre = basename(bin). */
int csnhypodbp_locator_add_full(CsnhypodbpLocator *list, int *n, int cap,
                                const char *bin, const char *cfg,
                                int live_usage, int time_recalc_live,
                                int n_retry);
/* Carga las lineas `locator <bin> <cfg> <live> <recalc> <retry>` de un .d. */
int csnhypodbp_locator_load_file(const char *path, CsnhypodbpLocator *list,
                                 int *n, int cap);
/* Ruta por convencion: $EW_PARAMS/csnhypodbpqt.d (o ./csnhypodbpqt.d). */
int csnhypodbp_locator_default_file(char *out, size_t n);
/* Carga la tabla por convencion si aun no hay locators. */
int csnhypodbp_locator_load_default(CsnhypodbpLocator *list, int *n, int cap);
int csnhypodbp_locator_find(const CsnhypodbpLocator *list, int n, const char *name);
int csnhypodbp_locator_resolve(const CsnhypodbpLocator *l,
                               const char *ew_home, const char *ew_version,
                               const char *ew_params, char *bin_out, size_t bn,
                               char *cfg_out, size_t cn);

/* Modulo EarthWorm del binario del localizador (162 csnloc, 163 hyp2000_ring,
 * 164 nlloc_ring; 0 si no se reconoce). El ARC no lleva `mod`: viaja en el logo
 * del anillo y, offline, hay que ponerlo desde el localizador elegido. */
int csnhypodbp_locator_mod(const CsnhypodbpLocator *l);

/* 1 si `mod` es el de un localizador declarado con live_usage=1. Solo esos
 * escriben en HYPO_RING_REF y conservan el historial de iteraciones. */
int csnhypodbp_locator_is_live_mod(const CsnhypodbpLocator *list, int n, int mod);

#endif /* CSNHYPODBP_LOCATOR_H */
