/*
 * test_locator - parseo/resolucion de localizadores offline (headless).
 * `locator <bin> <cfg> ...`; bin/cfg sin '/' se resuelven contra
 * $EW_HOME/$EW_VERSION/bin y $EW_PARAMS.
 *
 * El mod NO sale del nombre del binario: se lee `MyModuleId` del .d y se
 * resuelve con GetModId (aqui, un stub). Se usan ids sinteticos (910..912),
 * no los de produccion.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_locator.h"

static int stub_getmodid(const char *symbol, unsigned char *id)
{
    if (strcmp(symbol, "MOD_TEST") != 0) return -1;
    *id = 199;
    return 0;
}

static char *stub_getmodname(unsigned char mod)
{
    switch (mod) {
    case 162: return "MOD_CSNLOC";
    case 163: return "MOD_HYP2000_RING";
    case 164: return "MOD_NLLOC_RING";
    case 172: return "MOD_CSNHYPODBPQT";
    case 200: return "SIN_PREFIJO";
    case 201: return "MOD_";
    case 202: return "mod_csnloc";
    default:  return NULL;
    }
}

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

    /* El mod es un campo resuelto, no una funcion del nombre del binario. */
    list[0].mod = 910;
    list[1].mod = 911;
    list[2].mod = 912;
    assert(csnhypodbp_locator_mod(&list[0]) == 910);
    assert(csnhypodbp_locator_mod(&list[1]) == 911);
    assert(csnhypodbp_locator_mod(&list[2]) == 912);
    assert(csnhypodbp_locator_mod(NULL) == 0);
    assert(csnhypodbp_locator_name_for_mod(list, n, 912) == list[2].name);
    assert(csnhypodbp_locator_name_for_mod(list, n, 999) == NULL);

    list[0].live_usage = 0;
    list[1].live_usage = 0;
    list[2].live_usage = 1;
    assert(csnhypodbp_locator_is_live_mod(list, n, 912) == 1);
    assert(csnhypodbp_locator_is_live_mod(list, n, 911) == 0);
    assert(csnhypodbp_locator_is_live_mod(list, n, 0) == 0);
    assert(csnhypodbp_locator_is_live_mod(NULL, 0, 912) == 0);

    /* MyModuleId del .d + get_modid -> mod (sin ids escritos a mano). */
    {
        const char *path = "/tmp/csnhypodbp_test_locator.d";
        CsnhypodbpLocator r[1];
        char sym[64], err[256];
        FILE *fp = fopen(path, "w");

        assert(fp != NULL);
        fputs("# fixture\nMyModuleId MOD_TEST\n", fp);
        fclose(fp);
        memset(r, 0, sizeof(r));
        snprintf(r[0].name, sizeof(r[0].name), "test_locator");
        snprintf(r[0].cfg, sizeof(r[0].cfg), "%s", path);
        r[0].live_usage = 1;
        assert(csnhypodbp_locator_read_module(&r[0], sym, sizeof(sym),
                                              err, sizeof(err)) == 0);
        assert(strcmp(sym, "MOD_TEST") == 0);
        assert(csnhypodbp_locator_resolve_mods(r, 1, stub_getmodid) == 0);
        assert(r[0].mod == 199);
        assert(strcmp(csnhypodbp_locator_name_for_mod(r, 1, 199),
                      "test_locator") == 0);
        assert(csnhypodbp_locator_is_live_mod(r, 1, 199) == 1);
        remove(path);
    }

    /* Etiqueta uniforme: GetModIdName + normalizacion, SIN tabla de locators. */
    {
        char label[64], tiny[6], one[1] = { 'x' };

        assert(strcmp(csnhypodbp_mod_label(162, stub_getmodname, label,
                                           sizeof(label)), "csnloc") == 0);
        assert(strcmp(csnhypodbp_mod_label(163, stub_getmodname, label,
                                           sizeof(label)), "hyp2000_ring") == 0);
        assert(strcmp(csnhypodbp_mod_label(164, stub_getmodname, label,
                                           sizeof(label)), "nlloc_ring") == 0);
        assert(strcmp(csnhypodbp_mod_label(172, stub_getmodname, label,
                                           sizeof(label)), "csnhypodbpqt") == 0);
        assert(strcmp(csnhypodbp_mod_label(200, stub_getmodname, label,
                                           sizeof(label)), "sin_prefijo") == 0);
        assert(strcmp(csnhypodbp_mod_label(202, stub_getmodname, label,
                                           sizeof(label)), "csnloc") == 0);
        assert(strcmp(csnhypodbp_mod_label(201, stub_getmodname, label,
                                           sizeof(label)), "") == 0);
        assert(strcmp(csnhypodbp_mod_label(999, stub_getmodname, label,
                                           sizeof(label)), "") == 0);
        assert(strcmp(csnhypodbp_mod_label(0, stub_getmodname, label,
                                           sizeof(label)), "") == 0);
        assert(strcmp(csnhypodbp_mod_label(300, stub_getmodname, label,
                                           sizeof(label)), "") == 0);
        assert(strcmp(csnhypodbp_mod_label(162, NULL, label,
                                           sizeof(label)), "") == 0);

        assert(strcmp(csnhypodbp_mod_label(162, stub_getmodname, tiny,
                                           sizeof(tiny)), "csnlo") == 0);
        assert(strcmp(csnhypodbp_mod_label(162, stub_getmodname, one,
                                           sizeof(one)), "") == 0);
        assert(one[0] == '\0');
        assert(strcmp(csnhypodbp_mod_label(162, stub_getmodname, label, 0),
                      "") == 0);
        assert(strcmp(csnhypodbp_mod_label(162, stub_getmodname, NULL, 64),
                      "") == 0);
    }

    printf("test_locator OK\n");
    return 0;
}
