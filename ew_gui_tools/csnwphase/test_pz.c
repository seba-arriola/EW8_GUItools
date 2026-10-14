#include "csnwphase.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * Prepara un run dir mínimo con dos canales reales para que
 * tests/test_pz_engine.sh lo pase por el binario REAL del motor
 * (make_resp_lookup_table) y compruebe el id de coeffs_rec_lut.
 *
 * Uso: test_pz <dir>
 */

static const char *resp_dir(void)
{
    const char *d = getenv("CSNWPHASE_RESP_DIR");

    return (d && d[0]) ? d : "/mnt/d/resources/csnmags/responses";
}

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

int main(int argc, char **argv)
{
    CsnwphaseCfg cfg;
    CsnwphaseInput in;
    const char *dir = (argc > 1) ? argv[1] : "/tmp/csnwphase_pz";
    char data[512], path[1200], err[256];
    FILE *f;

    snprintf(data, sizeof(data), "%s/DATA", dir);
    mkdir_p(data);

    csnwphase_cfg_defaults(&cfg);
    snprintf(cfg.resp_dir, sizeof(cfg.resp_dir), "%s", resp_dir());

    /* Dos canales reales del repo (sin location -> "--" como en el motor). */
    if (csnwphase_pz_write(&cfg, data, "C1", "AC06", "BHZ", "", path,
                           sizeof(path)) != 0) {
        fprintf(stderr, "test_pz: falta %s/AC06_BHZ_C1.pz (¿respuestas no generadas?)\n",
                cfg.resp_dir);
        return 2;
    }
    printf("pz1: %s\n", path);

    if (csnwphase_pz_write(&cfg, data, "C1", "AF02", "HHN", "", path,
                           sizeof(path)) != 0) {
        fprintf(stderr, "test_pz: falta %s/AF02_HHN_C1.pz\n", cfg.resp_dir);
        return 2;
    }
    printf("pz2: %s\n", path);

    /* pz_fil_list: como `ls DATA/SAC_PZs*` del flujo real (lleva el prefijo
       DATA/; pzfile2id cuenta guiones bajos, así que el prefijo no estorba). */
    snprintf(path, sizeof(path), "%s/pz_fil_list", dir);
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "DATA/SAC_PZs_C1_AC06_BHZ_--_2000.001_2100.001\n");
    fprintf(f, "DATA/SAC_PZs_C1_AF02_HHN_--_2000.001_2100.001\n");
    assert(fclose(f) == 0);

    /* i_master: make_resp_lookup_table sólo lee IDEC_3 de aquí. */
    memset(&in, 0, sizeof(in));
    in.year = 2015;
    in.month = 9;
    in.day = 16;
    in.hour = 22;
    in.minute = 54;
    in.second = 32.0;
    in.latitude = -31.573;
    in.longitude = -71.674;
    in.depth_km = 25.0;
    in.magnitude = 8.3;
    snprintf(in.location, sizeof(in.location), "Chile");
    assert(csnwphase_prep_write(&cfg, &in, dir, err, sizeof(err)) == 0);

    printf("run dir listo: %s\n", dir);
    printf("ALL CSNWPHASE PZ PREP OK\n");
    return 0;
}
