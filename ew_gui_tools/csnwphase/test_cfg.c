#include "csnwphase.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Tests de configuración y del guard anti-borrado de la base de Green.
 * Headless (sin GTK, sin anillos).
 */

static void mkdir_p(const char *path)
{
    char tmp[1024];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static int dir_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void test_defaults(void)
{
    CsnwphaseCfg cfg;

    csnwphase_cfg_defaults(&cfg);

    assert(strcmp(cfg.gfdir_rel, "./GF") == 0);
    assert(cfg.dmin_deg == 5.0);
    assert(cfg.dmax_deg == 50.0);
    assert(cfg.min_magnitude == 6.0);
    assert(cfg.win_pre_sec == 300 && cfg.win_post_sec == 900);
    assert(cfg.screen_th[0] == 5.0 && cfg.screen_th[1] == 3.0 &&
           cfg.screen_th[2] == 1.0);
    assert(cfg.screen_nr == 2.0);
    assert(cfg.filt_order == 4 && cfg.filt_pass == 1);
    assert(cfg.idec2_preevent == 280.0 && cfg.idec2_fend == 0.1);
    assert(cfg.idec3_nf == 100);
    assert(cfg.wpwin[1] == 15.0 && cfg.wpwin[2] == 12.0);
    printf("  defaults ok\n");
}

static void test_load(void)
{
    const char *path = "/tmp/csnwphase_test.d";
    FILE *f;
    CsnwphaseCfg cfg;

    f = fopen(path, "w");
    assert(f);
    fprintf(f, "WphaseHome /tmp/wh\n");
    fprintf(f, "WphaseBin /tmp/wh/bin\n");
    fprintf(f, "GfDir /tmp/gf\n");
    fprintf(f, "Dmin 5.0\nDmax 90.0\n");
    fprintf(f, "ScreenTh 6.0 4.0 2.0\n");
    fprintf(f, "Idec2 1 100  0.5\n");
    fprintf(f, "Idec3 0.002  0.2  50  0.05\n");
    fprintf(f, "WpWin 1. 20.0 10.0\n");
    fprintf(f, "GridSearch 0\nTimeoutSec 600\n");
    fclose(f);

    assert(csnwphase_cfg_load(&cfg, path) == 0);
    assert(strcmp(cfg.wphase_home, "/tmp/wh") == 0);
    assert(strcmp(cfg.gf_dir, "/tmp/gf") == 0);
    assert(cfg.dmax_deg == 90.0);
    assert(cfg.dmin_deg == 5.0);            /* no tocado: default */
    assert(cfg.screen_th[2] == 2.0);
    assert(cfg.idec2_idtr == 1.0 && cfg.idec2_preevent == 100.0 &&
           cfg.idec2_fend == 0.5);
    assert(cfg.idec3_fl == 0.002 && cfg.idec3_nf == 50 && cfg.idec3_tol == 0.05);
    assert(cfg.wpwin[0] == 1.0 && cfg.wpwin[1] == 20.0);
    assert(cfg.grid_search == 0 && cfg.timeout_sec == 600);
    assert(cfg.keep_tmp == 0);              /* default */
    remove(path);
    printf("  load ok\n");
}

static void test_gfdir_guard(void)
{
    CsnwphaseCfg cfg;
    char err[256];
    char base[512], run[512], gfcopy[512];
    FILE *f;

    mkdir_p("/tmp/csnwphase_guard/base/GF_0.1d");
    mkdir_p("/tmp/csnwphase_guard/run");

    /* Marcador dentro de la base: debe seguir ahí tras un guard fallido. */
    snprintf(gfcopy, sizeof(gfcopy), "/tmp/csnwphase_guard/base/GF_0.1d/H003.5");
    mkdir_p(gfcopy);

    csnwphase_cfg_defaults(&cfg);
    snprintf(cfg.gf_dir, sizeof(cfg.gf_dir), "/tmp/csnwphase_guard/base/GF_0.1d");
    snprintf(run, sizeof(run), "/tmp/csnwphase_guard/run");
    snprintf(base, sizeof(base), "%s", cfg.gf_dir);

    /* Caso bueno: GFDIR relativo dentro del run dir. */
    snprintf(cfg.gfdir_rel, sizeof(cfg.gfdir_rel), "./GF");
    assert(csnwphase_gfdir_guard(&cfg, run, err, sizeof(err)) == 0);

    /* GFDIR = la propia base (por nombre) -> rechazo. */
    snprintf(cfg.gfdir_rel, sizeof(cfg.gfdir_rel), "GF_0.1d");
    assert(csnwphase_gfdir_guard(&cfg, run, err, sizeof(err)) == -1);
    printf("  guard nombre: %s\n", err);

    /* GFDIR relativo que RESUELVE a la base -> rechazo. */
    snprintf(cfg.gfdir_rel, sizeof(cfg.gfdir_rel), "../base/GF_0.1d");
    assert(csnwphase_gfdir_guard(&cfg, run, err, sizeof(err)) == -1);
    printf("  guard resolucion: %s\n", err);

    /* GFDIR absoluto -> rechazo. */
    snprintf(cfg.gfdir_rel, sizeof(cfg.gfdir_rel), "/tmp/csnwphase_guard/base/GF_0.1d");
    assert(csnwphase_gfdir_guard(&cfg, run, err, sizeof(err)) == -1);

    /* GfDir inexistente -> rechazo. */
    snprintf(cfg.gfdir_rel, sizeof(cfg.gfdir_rel), "./GF");
    snprintf(cfg.gf_dir, sizeof(cfg.gf_dir), "/tmp/csnwphase_guard/noexiste");
    assert(csnwphase_gfdir_guard(&cfg, run, err, sizeof(err)) == -1);

    /* La base sigue intacta (nadie llamó a crea_dir). */
    assert(dir_exists(gfcopy));
    assert(dir_exists(base));

    f = fopen("/tmp/csnwphase_guard/run/i_master", "w");
    assert(f);
    fclose(f);
    printf("  guard ok (base intacta)\n");
}

int main(void)
{
    printf("csnwphase: tests de configuracion\n");
    test_defaults();
    test_load();
    test_gfdir_guard();
    printf("ALL CSNWPHASE CFG TESTS PASSED\n");
    return 0;
}
