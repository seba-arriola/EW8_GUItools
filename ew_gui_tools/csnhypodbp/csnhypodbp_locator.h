#ifndef CSNHYPODBP_LOCATOR_H
#define CSNHYPODBP_LOCATOR_H

#include <stddef.h>

/*
 * csnhypodbp_locator.c - Localizadores offline declarados en el .d:
 *   locator <name> <bin> <cfg>
 * Si <bin>/<cfg> no contienen '/', se resuelven contra $EW_HOME/$EW_VERSION/bin
 * y $EW_PARAMS respectivamente.
 */

typedef struct { char name[32]; char bin[256]; char cfg[256]; } CsnhypodbpLocator;

int csnhypodbp_locator_add(CsnhypodbpLocator *list, int *n, int cap,
                           const char *name, const char *bin, const char *cfg);
int csnhypodbp_locator_find(const CsnhypodbpLocator *list, int n, const char *name);
int csnhypodbp_locator_resolve(const CsnhypodbpLocator *l,
                               const char *ew_home, const char *ew_version,
                               const char *ew_params, char *bin_out, size_t bn,
                               char *cfg_out, size_t cn);

/* Modulo EarthWorm del binario del localizador (162 csnloc, 163 hyp2000_ring,
 * 164 nlloc_ring; 0 si no se reconoce). El ARC no lleva `mod`: viaja en el logo
 * del anillo y, offline, hay que ponerlo desde el localizador elegido. */
int csnhypodbp_locator_mod(const CsnhypodbpLocator *l);

#endif /* CSNHYPODBP_LOCATOR_H */
