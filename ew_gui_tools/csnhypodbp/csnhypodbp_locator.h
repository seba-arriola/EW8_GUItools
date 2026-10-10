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

#endif /* CSNHYPODBP_LOCATOR_H */
