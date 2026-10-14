/*
 * test_mww_error.c - tests del extractor de error del panel Mww.
 *
 * Cubre los casos del log real: la salida del motor con `ERROR`, el aviso del
 * LUT (`Non-Finite a priori GF, fc or h`, el sintoma que dejo sin resultado la
 * inversion de julio 2024), el cierre del script (`fin run.sh (rc=256)`) y que
 * una corrida correcta (rc=0) NO se reporte como error.
 */
#include "csnhypodbp_mwwerr.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static char root[256];

static void mkdir_ok(const char *path)
{
    /* Idempotente: el directorio puede existir de una corrida anterior. */
    if (mkdir(path, 0777) != 0 && errno != EEXIST)
        assert(0 && "no se pudo crear el directorio de test");
}

static void mkrun(const char *name, const char *runlog, const char *full)
{
    char path[512];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", root, name);
    mkdir_ok(path);

    if (runlog) {
        snprintf(path, sizeof path, "%s/%s/run.log", root, name);
        f = fopen(path, "w");
        assert(f);
        fputs(runlog, f);
        fclose(f);
    }
    if (full) {
        snprintf(path, sizeof path, "%s/%s/full_output.txt", root, name);
        f = fopen(path, "w");
        assert(f);
        fputs(full, f);
        fclose(f);
    }
}

static int err_of(const char *name, char *buf, size_t n)
{
    char dir[512];

    snprintf(dir, sizeof dir, "%s/%s", root, name);
    return csnhypodbp_mww_first_error(dir, buf, n);
}

int main(void)
{
    char buf[512];

    printf("csnhypodbp: test del extractor de error del panel Mww\n");

    snprintf(root, sizeof root, "/tmp/opencode/mwwerr_test");
    mkdir("/tmp/opencode", 0777);
    mkdir(root, 0777);

    /* 1. Sin ficheros: no hay nada que reportar. */
    assert(csnhypodbp_mww_first_error("/tmp/opencode/mwwerr_nada",
                                      buf, sizeof buf) == -1);

    /* 2. ERROR del motor en run.log: se extrae tal cual, sin salto de linea. */
    mkrun("a", "linea normal\nERROR: reading scr_dat_fil_list\nmas lineas\n", NULL);
    assert(err_of("a", buf, sizeof buf) == 0);
    assert(strcmp(buf, "ERROR: reading scr_dat_fil_list") == 0);

    /* 3. El caso REAL: el aviso del LUT que dejo la corrida sin resultado. */
    mkrun("b", "WARNING (make_resp_lookup_table): Non-Finite a priori GF, fc or h\n"
               "fin run.sh (rc=256)\n", NULL);
    assert(err_of("b", buf, sizeof buf) == 0);
    assert(strstr(buf, "Non-Finite") != NULL);

    /* 4. run.log vacio: se busca en full_output.txt. */
    mkrun("c", "", "===== run.log =====\nERROR: algo en la salida completa\n");
    assert(err_of("c", buf, sizeof buf) == 0);
    assert(strcmp(buf, "ERROR: algo en la salida completa") == 0);

    /* 5. Corrida correcta: el cierre NO es un error. */
    mkrun("d", "todo bien\nfin run.sh (rc=0)\n", NULL);
    assert(err_of("d", buf, sizeof buf) == -1);

    /* 6. Cierre con fallo: es lo unico que hay, y se reporta. */
    mkrun("e", "x\nfin run.sh (rc=256)\n", NULL);
    assert(err_of("e", buf, sizeof buf) == 0);
    assert(strstr(buf, "rc=256") != NULL);

    /* 8. El caso del arranque actual: el run.sh marca el paso que falla y el
       panel debe nombrar la ORDEN, no el envoltorio. */
    mkrun("f", "##### paso 5: /w/bin/rec_dec_filt coeffs_rec_lut i_master scr.dat out.dat #####\n"
               "salida normal del motor\n"
               "##### paso 5: rc=1 #####\n", NULL);
    assert(err_of("f", buf, sizeof buf) == 0);
    assert(strstr(buf, "rec_dec_filt") != NULL);
    assert(strstr(buf, "rc=1") != NULL);
    assert(strstr(buf, "#####") == NULL);

    /* 9. Un paso con rc=0 NO es un error. */
    mkrun("g", "##### paso 1: ls DATA #####\n##### paso 1: rc=0 #####\n", NULL);
    assert(err_of("g", buf, sizeof buf) == -1);

    /* 7. Argumentos invalidos. */
    assert(csnhypodbp_mww_first_error(NULL, buf, sizeof buf) == -1);
    assert(csnhypodbp_mww_first_error(root, NULL, 0) == -1);
    assert(csnhypodbp_mww_first_error("", buf, sizeof buf) == -1);

    printf("ALL CSNHYPODBP MWW ERROR TESTS PASSED\n");
    return 0;
}
