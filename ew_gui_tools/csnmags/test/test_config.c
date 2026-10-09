#include "csnmags.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* test_config.c - lectura de csnmags.d y defaults. */

int main(void)
{
    const char *cf = "test/tmp_csnmags.d";
    FILE *f = fopen(cf, "w");
    MagConfig c;

    fprintf(f, "MyModuleId MOD_MAGNITUDES\n");
    fprintf(f, "StaFile estaciones_107.txt\n");
    fprintf(f, "ResponseDir responses\n");
    fprintf(f, "WoodAndersonCoefs 0.8 0.7 2080\n");
    fprintf(f, "Ml_Coeffs 1.11 0.00189 -2.09\n");
    fprintf(f, "Mwp_T0 95.0\n");
    fprintf(f, "Mb_QTable calib/mb_Q.tab\n");
    fprintf(f, "Ms_Band 0.045 0.056\n");
    fprintf(f, "GlobalGrid grids/global.grid\n");
    fprintf(f, "LocalGrid grids/tarapaca.grid\n");
    fclose(f);

    assert(mag_config_load(cf, &c) == 0);
    assert(strcmp(c.my_mod_id, "MOD_MAGNITUDES") == 0);
    assert(strcmp(c.sta_file, "estaciones_107.txt") == 0);
    assert(c.wa_gain > 2000.0 && c.wa_gain < 2100.0);
    assert(c.mwp_t0 > 94.0 && c.mwp_t0 < 96.0);
    assert(c.ms_band_lo > 0.04 && c.ms_band_hi > c.ms_band_lo);
    assert(c.n_grids == 2 && c.grids[0].level == 0 && c.grids[1].level == 2);
    /* defaults no sobreescritos */
    assert(c.ml_max_dist > 500.0);
    assert(c.mb.min_delta > 4.9 && c.mb.max_delta < 105.1);
    remove(cf);

    /* falta StaFile -> falla */
    {
        FILE *g = fopen(cf, "w");
        fprintf(g, "MyModuleId MOD_MAGNITUDES\n");
        fclose(g);
        assert(mag_config_load(cf, &c) == -1);
        remove(cf);
    }

    printf("test_config OK\n");
    return 0;
}
