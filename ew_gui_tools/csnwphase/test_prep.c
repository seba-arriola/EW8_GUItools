#include "csnwphase.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Verifica que i_master y CMTSOLUTION son BYTE A BYTE iguales a los que
 * produce el generador del CSN. Los fixtures se generan con el Python original
 * (tools/gen_fixtures.sh -> tests/data/); si faltan, corre `make fixtures`.
 *
 * También verifica la tabla de parámetros (misma fuente de verdad que i_master).
 */

#define DATA_DIR "tests/data"

static char *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    if (len)
        *len = n;
    return buf;
}

/* Compara y, si difieren, muestra ambas versiones con marcas de fin de línea. */
static void diff_or_die(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) {
        printf("  %s: identico al fixture (%zu bytes)\n", what, strlen(want));
        return;
    }

    fprintf(stderr, "FALLO: %s NO coincide con el generador del CSN\n", what);
    fprintf(stderr, "--- nuestro ---\n%s--- fixture ---\n%s", got, want);
    assert(0 && "fixture mismatch");
}

static void fixture_test(const char *fixture, const char *got, const char *what)
{
    char path[512];
    char *want;
    const char *dir = getenv("CSNWPHASE_DATA_DIR");

    if (!dir || !dir[0])
        dir = DATA_DIR;
    snprintf(path, sizeof(path), "%s/%s", dir, fixture);
    want = read_file(path, NULL);
    if (!want) {
        fprintf(stderr,
                "FALTA el fixture %s: genera los fixtures con "
                "`make -C ew_gui_tools/csnwphase fixtures`\n", path);
        assert(0 && "fixture ausente");
    }
    diff_or_die(what, got, want);
    free(want);
}

static void fill_input(CsnwphaseInput *in)
{
    memset(in, 0, sizeof(*in));
    in->year = 2015;
    in->month = 9;
    in->day = 16;
    in->hour = 22;
    in->minute = 54;
    in->second = 32.0;
    in->latitude = -31.573;
    in->longitude = -71.674;
    in->depth_km = 25.0;
    in->magnitude = 8.3;
    snprintf(in->location, sizeof(in->location), "Chile");
    snprintf(in->event_id, sizeof(in->event_id), "E1-20150916225432");
    snprintf(in->origin_id, sizeof(in->origin_id), "E1-20150916225432:162");
}

int main(void)
{
    CsnwphaseCfg cfg;
    CsnwphaseInput in;
    CsnwphaseParamRow rows[CSNWPHASE_MAX_PARAMS];
    char i_master[8192], cmt[8192];
    char err[256];
    int i, n;

    printf("csnwphase: tests de preparacion del run dir\n");

    csnwphase_cfg_defaults(&cfg);
    fill_input(&in);

    /* El fixture se generó con seed="-" (SEED sólo lo usa extract.csh). */
    snprintf(cfg.seed_ref, sizeof(cfg.seed_ref), "-");

    assert(csnwphase_i_master_text(&cfg, &in, i_master, sizeof(i_master)) > 0);
    assert(csnwphase_cmt_text(&cfg, &in, cmt, sizeof(cmt)) > 0);

    fixture_test("i_master_illapel.txt", i_master, "i_master");
    fixture_test("cmt_illapel.txt", cmt, "CMTSOLUTION");

    /* La banda de i_master debe coincidir con la que anuncia la tabla. */
    {
        const char *line = strstr(i_master, "filt_cf1:");
        const char *v = strstr(i_master, "filt_cf2:");
        assert(line && v);
        assert(strstr(line, "0.001") != NULL);   /* mag 8.3 -> 0.001 - 0.005 */
        assert(strstr(v, "0.005") != NULL);
    }

    /* Tabla de parámetros: cubre hipocentro, banda, ventanas y screening. */
    assert(csnwphase_params_build(&cfg, &in, rows, CSNWPHASE_MAX_PARAMS, &n) == 0);
    assert(n >= 18);

    {
        int have_band = 0, have_win = 0, have_th = 0, have_gf = 0;
        for (i = 0; i < n; i++) {
            if (strcmp(rows[i].name, "banda de inversion [Hz]") == 0) {
                have_band = 1;
                assert(strcmp(rows[i].value, "0.001 - 0.005") == 0);
            }
            if (strcmp(rows[i].name, "ventana de ondas [s]") == 0) {
                have_win = 1;
                assert(strcmp(rows[i].value, "t0-300 / t0+900") == 0);
            }
            if (strcmp(rows[i].name, "screening RMS (-th)") == 0) {
                have_th = 1;
                assert(strcmp(rows[i].value, "5 3 1") == 0);
            }
            if (strcmp(rows[i].name, "GF_PATH (base de Green)") == 0)
                have_gf = 1;
        }
        assert(have_band && have_win && have_th && have_gf);
    }

    /* La tabla es lo que se ejecuta: los valores vienen de i_master. */
    assert(strstr(i_master, "DMIN:       5.0") != NULL);
    assert(strstr(i_master, "DMAX:       50.0") != NULL);
    assert(strstr(i_master, "GFDIR:   ./GF") != NULL);
    assert(strstr(i_master, "IDEC_2:  2 280  0.1") != NULL);
    assert(strstr(i_master, "IDEC_3:  0.001  0.1  100  0.03") != NULL);
    assert(strstr(i_master, "WP_WIN:  0. 15.0 12.0") != NULL);

    /* prep_write deja los dos archivos en el run dir. */
    assert(csnwphase_prep_write(&cfg, &in, "/tmp/csnwphase_prep", err,
                                sizeof(err)) == 0);
    {
        char *a = read_file("/tmp/csnwphase_prep/i_master", NULL);
        char *b = read_file("/tmp/csnwphase_prep/CMTSOLUTION", NULL);
        assert(a && b);
        assert(strcmp(a, i_master) == 0);
        assert(strcmp(b, cmt) == 0);
        free(a);
        free(b);
    }

    /* AC4: el run dir se crea con TODOS sus padres. El panel `Mww` usa
       `MwwWorkDir/<id>-<fecha>`, que cuelga de un directorio que puede no
       existir en la primera corrida: con un mkdir de un solo nivel la
       preparacion fallaba y el panel se quedaba "sin resultado" sin decir por que. */
    {
        char *c;

        assert(csnwphase_prep_write(&cfg, &in,
                                    "/tmp/csnwphase_prep_parent/aaa/bbb/run",
                                    err, sizeof(err)) == 0);
        c = read_file("/tmp/csnwphase_prep_parent/aaa/bbb/run/i_master", NULL);
        assert(c);
        free(c);
    }

    /* Un run dir imposible (colgando de un fichero) falla CON mensaje. */
    assert(csnwphase_prep_write(&cfg, &in, "/tmp/csnwphase_prep/i_master/x",
                                err, sizeof(err)) != 0);
    assert(err[0] != '\0');

    printf("ALL CSNWPHASE PREP TESTS PASSED\n");
    return 0;
}
