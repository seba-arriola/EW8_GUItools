#ifndef CSNHYPODBP_LOCATOR_H
#define CSNHYPODBP_LOCATOR_H

#include <stddef.h>

/*
 * csnhypodbp_locator.c - Localizadores declarados en csnhypodbpqt.d:
 *   locator <bin> <cfg> <live_usage> <time_recalc_live> <N_retry>
 * El nombre es el basename de <bin>. Si <bin>/<cfg> no contienen '/', se
 * resuelven contra $EW_HOME/$EW_VERSION/bin y $EW_PARAMS respectivamente.
 *
 * El `mod` NO se escribe en ningun sitio: se resuelve del `MyModuleId` del .d
 * del locator y del `Module <SIMBOLO> <id>` de earthworm.d (GetModId). Asi un
 * operador puede cambiar el id en earthworm.d sin recompilar nada.
 */

typedef struct {
    char name[32];              /* basename(bin) */
    char bin[256];
    char cfg[256];
    int  mod;                   /* resuelto de MyModuleId + GetModId; 0 = desconocido */
    int  live_usage;            /* 0/1 */
    int  time_recalc_live;      /* s */
    int  n_retry;               /* intentos sin picadas nuevas */
} CsnhypodbpLocator;

/* Resuelve un simbolo de earthworm.d a su id (GetModId en produccion, stub en
 * tests). Devuelve 0 si OK. */
typedef int (*CsnhypodbpGetModId)(const char *symbol, unsigned char *id);

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

/* Puro: abre el .d del locator y extrae el simbolo de `MyModuleId`. */
int csnhypodbp_locator_read_module(const CsnhypodbpLocator *l,
                                   char *symbol, size_t n,
                                   char *err, size_t errn);

/* Resuelve l->mod para cada locator (MyModuleId + get_modid). Devuelve cuantos
 * fallaron; los que fallan quedan con mod=0 (nunca un id por defecto). */
int csnhypodbp_locator_resolve_mods(CsnhypodbpLocator *list, int n,
                                    CsnhypodbpGetModId get_modid);

/* Modulo EarthWorm del localizador (l->mod); 0 si no se pudo resolver. */
int csnhypodbp_locator_mod(const CsnhypodbpLocator *l);

/* Nombre del locator cuyo mod resuelto es `mod`; NULL si no esta en la tabla. */
const char *csnhypodbp_locator_name_for_mod(const CsnhypodbpLocator *list,
                                            int n, int mod);

/* 1 si `mod` es el de un localizador declarado con live_usage=1. Solo esos
 * escriben en HYPO_RING_REF y conservan el historial de iteraciones. */
int csnhypodbp_locator_is_live_mod(const CsnhypodbpLocator *list, int n, int mod);

#endif /* CSNHYPODBP_LOCATOR_H */
