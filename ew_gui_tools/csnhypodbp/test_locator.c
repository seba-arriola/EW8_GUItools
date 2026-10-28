/*
 * test_locator - parseo/resolucion de localizadores offline (headless).
 * `locator <name> <bin> <cfg>`; bin/cfg sin '/' se resuelven contra
 * $EW_HOME/$EW_VERSION/bin y $EW_PARAMS.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_locator.h"

int main(void)
{
    CsnhypodbpLocator list[8];
    int n = 0;
    char b[512], c[512];

    assert(csnhypodbp_locator_add(list, &n, 8, "csnloc", "csnloc", "csnloc.d") == 0);
    assert(csnhypodbp_locator_add(list, &n, 8, "nlloc_ring", "nlloc_ring", "nlloc_ring.d") == 0);
    assert(csnhypodbp_locator_add(list, &n, 8, "hyp2000_ring", "hyp2000_ring", "hyp2000_ring.d") == 0);
    assert(n == 3);
    assert(csnhypodbp_locator_find(list, n, "nlloc_ring") == 1);
    assert(csnhypodbp_locator_find(list, n, "noexiste") == -1);

    /* nombres vacios o fuera de capacidad se rechazan */
    assert(csnhypodbp_locator_add(list, &n, 8, "", "x", "y") == -1);
    { CsnhypodbpLocator cap[1]; int cn = 0; assert(csnhypodbp_locator_add(cap, &cn, 1, "a", "b", "c") == 0); assert(csnhypodbp_locator_add(cap, &cn, 1, "d", "e", "f") == -1); }

    assert(csnhypodbp_locator_resolve(&list[0], "/opt/ew", "8.0", "/params",
                                      b, sizeof(b), c, sizeof(c)) == 0);
    assert(strcmp(b, "/opt/ew/8.0/bin/csnloc") == 0);
    assert(strcmp(c, "/params/csnloc.d") == 0);

    /* rutas explicitas (con '/') no se prefijan */
    {
        CsnhypodbpLocator e;
        memset(&e, 0, sizeof(e));
        snprintf(e.name, sizeof(e.name), "x");
        snprintf(e.bin, sizeof(e.bin), "/abs/bin");
        snprintf(e.cfg, sizeof(e.cfg), "/abs/cfg.d");
        csnhypodbp_locator_resolve(&e, "/opt/ew", "8.0", "/params",
                                   b, sizeof(b), c, sizeof(c));
        assert(strcmp(b, "/abs/bin") == 0);
        assert(strcmp(c, "/abs/cfg.d") == 0);
    }

    /* live_usage: solo hyp2000_ring conserva el historial de iteraciones. */
    list[0].live_usage = 0;   /* csnloc */
    list[1].live_usage = 0;   /* nlloc_ring */
    list[2].live_usage = 1;   /* hyp2000_ring */
    assert(csnhypodbp_locator_is_live_mod(list, n, 163) == 1);
    assert(csnhypodbp_locator_is_live_mod(list, n, 162) == 0);
    assert(csnhypodbp_locator_is_live_mod(list, n, 164) == 0);
    assert(csnhypodbp_locator_is_live_mod(list, n, 0) == 0);
    assert(csnhypodbp_locator_is_live_mod(NULL, 0, 163) == 0);

    printf("test_locator OK\n");
    return 0;
}
