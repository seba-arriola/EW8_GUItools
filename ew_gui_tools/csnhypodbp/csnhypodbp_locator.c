#include "csnhypodbp_locator.h"

#include <stdio.h>
#include <string.h>

int csnhypodbp_locator_add(CsnhypodbpLocator *list, int *n, int cap,
                           const char *name, const char *bin, const char *cfg)
{
    if (!list || !n || *n < 0 || *n >= cap || !name || !name[0]) return -1;
    snprintf(list[*n].name, sizeof(list[*n].name), "%s", name);
    snprintf(list[*n].bin, sizeof(list[*n].bin), "%s", bin ? bin : "");
    snprintf(list[*n].cfg, sizeof(list[*n].cfg), "%s", cfg ? cfg : "");
    (*n)++;
    return 0;
}

int csnhypodbp_locator_find(const CsnhypodbpLocator *list, int n, const char *name)
{
    int i;
    if (!list || !name) return -1;
    for (i = 0; i < n; i++)
        if (strcmp(list[i].name, name) == 0) return i;
    return -1;
}

int csnhypodbp_locator_resolve(const CsnhypodbpLocator *l,
                               const char *ew_home, const char *ew_version,
                               const char *ew_params, char *bin_out, size_t bn,
                               char *cfg_out, size_t cn)
{
    if (!l) return -1;
    if (bin_out && bn) {
        if (strchr(l->bin, '/') || !ew_home)
            snprintf(bin_out, bn, "%s", l->bin);
        else if (ew_version)
            snprintf(bin_out, bn, "%s/%s/bin/%s", ew_home, ew_version, l->bin);
        else
            snprintf(bin_out, bn, "%s/bin/%s", ew_home, l->bin);
    }
    if (cfg_out && cn) {
        if (strchr(l->cfg, '/') || !ew_params)
            snprintf(cfg_out, cn, "%s", l->cfg);
        else
            snprintf(cfg_out, cn, "%s/%s", ew_params, l->cfg);
    }
    return 0;
}

int csnhypodbp_locator_mod(const CsnhypodbpLocator *l)
{
    const char *b;

    if (!l || !l->bin[0]) return 0;
    b = strrchr(l->bin, '/');
    b = b ? b + 1 : l->bin;
    if (strcmp(b, "csnloc") == 0) return 162;
    if (strcmp(b, "hyp2000_ring") == 0) return 163;
    if (strcmp(b, "nlloc_ring") == 0) return 164;
    return 0;
}
