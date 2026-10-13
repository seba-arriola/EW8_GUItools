/*
 * Test headless de los parametros de magnitud que la GUI lee del .d de csnmags
 * (ver csnhypodbp_magcfg.h): defaults de fabrica, sobrescritura por archivo y
 * ventana por defecto de cada magnitud.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_magcfg.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

#define TMP "/tmp/opencode/magcfg_test.d"

static void test_defaults(void)
{
    CsnhypodbpMagCfg c;
    double t0 = 0, t1 = 0;

    csnhypodbp_magcfg_defaults(&c);
    CHECK(fabs(c.ml_ta - 10.0) < 1e-12 && fabs(c.ml_tb - 45.0) < 1e-12,
          "defaults: Ml_TA/Ml_TB");
    CHECK(fabs(c.mwp_start) < 1e-12 && fabs(c.mwp_t0 - 95.0) < 1e-12,
          "defaults: Mwp_StartOff/Mwp_T0");
    CHECK(fabs(c.mb_window - 30.0) < 1e-12, "defaults: Mb_Window");
    CHECK(fabs(c.ms_window - 400.0) < 1e-12, "defaults: Ms_Window");
    CHECK(fabs(c.wa_period - 0.8) < 1e-12 && fabs(c.wa_damp - 0.7) < 1e-12 &&
          fabs(c.wa_gain - 2080.0) < 1e-12, "defaults: WoodAndersonCoefs");
    CHECK(fabs(c.mb_f1 - 0.5) < 1e-12 && fabs(c.mb_f2 - 2.0) < 1e-12,
          "defaults: Mb_Band");
    CHECK(fabs(c.ms_f1 - 0.045) < 1e-12 && fabs(c.ms_f2 - 0.056) < 1e-12,
          "defaults: Ms_Band");

    csnhypodbp_magcfg_window(&c, CSNHYPODBP_MAG_ML, &t0, &t1);
    CHECK(fabs(t0 - 10.0) < 1e-12 && fabs(t1 - 45.0) < 1e-12,
          "ventana ML: [P+TA, P+TB]");
    csnhypodbp_magcfg_window(&c, CSNHYPODBP_MAG_MWP, &t0, &t1);
    CHECK(fabs(t0) < 1e-12 && fabs(t1 - 95.0) < 1e-12,
          "ventana Mwp: [P+StartOff, +T0]");
    csnhypodbp_magcfg_window(&c, CSNHYPODBP_MAG_MB, &t0, &t1);
    CHECK(fabs(t0) < 1e-12 && fabs(t1 - 30.0) < 1e-12, "ventana Mb: [P, P+Window]");
    csnhypodbp_magcfg_window(&c, CSNHYPODBP_MAG_MS, &t0, &t1);
    CHECK(fabs(t0) < 1e-12 && fabs(t1 - 400.0) < 1e-12, "ventana Ms: [P, P+Window]");

    /* Sin config (NULL) no revienta y usa los defaults. */
    csnhypodbp_magcfg_window(NULL, CSNHYPODBP_MAG_ML, &t0, &t1);
    CHECK(fabs(t0 - 10.0) < 1e-12 && fabs(t1 - 45.0) < 1e-12,
          "ventana con config NULL: defaults");
    csnhypodbp_magcfg_window(&c, 9, &t0, &t1);
    CHECK(fabs(t0) < 1e-12 && fabs(t1) < 1e-12, "ventana de un indice invalido: 0..0");
}

static void test_names(void)
{
    CHECK(strcmp(csnhypodbp_magcfg_name(CSNHYPODBP_MAG_ML), "ML") == 0 &&
          strcmp(csnhypodbp_magcfg_name(CSNHYPODBP_MAG_MWP), "Mwp") == 0 &&
          strcmp(csnhypodbp_magcfg_name(CSNHYPODBP_MAG_MB), "Mb") == 0 &&
          strcmp(csnhypodbp_magcfg_name(CSNHYPODBP_MAG_MS), "Ms") == 0,
          "nombres: ML/Mwp/Mb/Ms");
    CHECK(strcmp(csnhypodbp_magcfg_name(9), "-") == 0, "nombre fuera de rango: -");
}

static void test_load(void)
{
    CsnhypodbpMagCfg c;
    FILE *f;
    double t0 = 0, t1 = 0;

    /* Formato real de csnmags.d: comentarios, blancos, valores separados por
       espacios y claves ajenas que se ignoran. */
    f = fopen(TMP, "w");
    CHECK(f != NULL, "load: escribe el .d de prueba");
    if (!f) return;
    fprintf(f, "# comentario\n\n");
    fprintf(f, "MyModuleId        MOD_MAGNITUDES\n");
    fprintf(f, "WoodAndersonCoefs 0.8 0.7 2080\n");
    fprintf(f, "Ml_TA             12.5\n");
    fprintf(f, "Ml_TB             60\n");
    fprintf(f, "Mwp_T0            120.0\n");
    fprintf(f, "Mwp_StartOff      -2.5\n");
    fprintf(f, "Mb_Window         25.5\n");
    fprintf(f, "Ms_Window         380\n");
    fprintf(f, "Mb_Band           0.7 2.5\n");
    fprintf(f, "Ms_Band           0.04 0.06\n");
    fclose(f);

    csnhypodbp_magcfg_defaults(&c);
    CHECK(csnhypodbp_magcfg_load(&c, TMP) == 0, "load: lee el archivo");
    CHECK(fabs(c.ml_ta - 12.5) < 1e-12 && fabs(c.ml_tb - 60.0) < 1e-12,
          "load: Ml_TA/Ml_TB");
    CHECK(fabs(c.mwp_start + 2.5) < 1e-12 && fabs(c.mwp_t0 - 120.0) < 1e-12,
          "load: Mwp con StartOff negativo");
    CHECK(fabs(c.mb_window - 25.5) < 1e-12, "load: Mb_Window");
    CHECK(fabs(c.ms_window - 380.0) < 1e-12, "load: Ms_Window");
    CHECK(fabs(c.mb_f1 - 0.7) < 1e-12 && fabs(c.mb_f2 - 2.5) < 1e-12, "load: Mb_Band");
    CHECK(fabs(c.ms_f1 - 0.04) < 1e-12 && fabs(c.ms_f2 - 0.06) < 1e-12, "load: Ms_Band");
    CHECK(fabs(c.wa_gain - 2080.0) < 1e-12, "load: WoodAndersonCoefs");

    csnhypodbp_magcfg_window(&c, CSNHYPODBP_MAG_MWP, &t0, &t1);
    CHECK(fabs(t0 + 2.5) < 1e-12 && fabs(t1 - 117.5) < 1e-12,
          "load: la ventana Mwp sigue al StartOff");

    /* Claves ausentes: se conserva el default. */
    f = fopen(TMP, "w");
    if (f) { fprintf(f, "Ml_TA 11.0\n"); fclose(f); }
    csnhypodbp_magcfg_defaults(&c);
    CHECK(csnhypodbp_magcfg_load(&c, TMP) == 0, "load: archivo minimo");
    CHECK(fabs(c.ml_ta - 11.0) < 1e-12, "load: sobrescribe lo presente");
    CHECK(fabs(c.ml_tb - 45.0) < 1e-12, "load: deja el default de lo ausente");

    /* Archivo inexistente: -1 y defaults intactos. */
    csnhypodbp_magcfg_defaults(&c);
    CHECK(csnhypodbp_magcfg_load(&c, "/tmp/opencode/no_existe_magcfg.d") == -1,
          "load: archivo ausente -> -1");
    CHECK(fabs(c.ml_ta - 10.0) < 1e-12, "load: sin archivo quedan los defaults");
    CHECK(csnhypodbp_magcfg_load(NULL, TMP) == -1, "load: config NULL -> -1");
    CHECK(csnhypodbp_magcfg_load(&c, NULL) == -1, "load: path NULL -> -1");

    remove(TMP);
    CHECK(1, "load: limpia el temporal");
}

static void test_gate(void)
{
    CsnhypodbpMagCfg c;
    int in = -1;
    char txt[64];

    csnhypodbp_magcfg_defaults(&c);
    /* Los defaults tienen que ser los de csnmags/mag_config.c. */
    CHECK(fabs(c.ml_min_delta - 0.0) < 1e-12 && fabs(c.ml_max_delta - 8.0) < 1e-12 &&
          fabs(c.ml_max_dist_km - 600.0) < 1e-12 && c.ml_dist_type == 1,
          "gate: defaults ML (0-8 deg, 600 km, epicentral)");
    CHECK(fabs(c.mwp_min_delta - 3.0) < 1e-12 && fabs(c.mwp_max_delta - 90.0) < 1e-12,
          "gate: defaults Mwp (3-90 deg)");
    CHECK(fabs(c.mb_min_delta - 5.0) < 1e-12 && fabs(c.mb_max_delta - 105.0) < 1e-12,
          "gate: defaults Mb (5-105 deg)");
    CHECK(fabs(c.ms_min_delta - 20.0) < 1e-12 && fabs(c.ms_max_delta - 160.0) < 1e-12,
          "gate: defaults Ms (20-160 deg)");

    /* Red regional (~4.5 deg): ML y Mwp entran; Mb y Ms no. */
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_ML, 4.5, 500.0, &in);
    CHECK(in == 1, "gate ML: 4.5 deg entra");
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MWP, 4.5, 500.0, &in);
    CHECK(in == 1, "gate Mwp: 4.5 deg entra (min 3)");
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MB, 4.5, 500.0, &in);
    CHECK(in == 0, "gate Mb: 4.5 deg queda fuera (min 5)");
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MS, 4.5, 500.0, &in);
    CHECK(in == 0, "gate Ms: 4.5 deg queda fuera (min 20)");

    /* ML tambien corta en km. */
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_ML, 7.0, 900.0, &in);
    CHECK(in == 0, "gate ML: 7 deg pero 900 km queda fuera");
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_ML, 7.0, 600.0, &in);
    CHECK(in == 1, "gate ML: 7 deg y 600 km entra (el corte es estricto)");

    /* Telesismo: 30 deg entra en Mb y en Mwp. */
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MWP, 30.0, 3300.0, &in);
    CHECK(in == 1, "gate Mwp: 30 deg entra");
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MB, 30.0, 3300.0, &in);
    CHECK(in == 1, "gate Mb: 30 deg entra");
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MS, 30.0, 3300.0, &in);
    CHECK(in == 1, "gate Ms: 30 deg entra");

    /* Distancia/magnitud desconocidas: no se oculta. */
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MS, NAN, NAN, &in);
    CHECK(in == 1, "gate: distancia desconocida -> entra");
    csnhypodbp_magcfg_gate(&c, 9, 1.0, 100.0, &in);
    CHECK(in == 1, "gate: magnitud desconocida -> sin puerta");
    csnhypodbp_magcfg_gate(NULL, CSNHYPODBP_MAG_MS, 4.5, 500.0, &in);
    CHECK(in == 0, "gate: config NULL usa los defaults");
    csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MS, 4.5, 500.0, NULL);
    CHECK(1, "gate: salida NULL no rompe");

    /* Texto de la puerta. */
    csnhypodbp_magcfg_gate_text(&c, CSNHYPODBP_MAG_ML, txt, sizeof(txt));
    CHECK(strcmp(txt, "0-8 deg y <=600 km") == 0, "gate_text ML");
    csnhypodbp_magcfg_gate_text(&c, CSNHYPODBP_MAG_MS, txt, sizeof(txt));
    CHECK(strcmp(txt, "20-160 deg") == 0, "gate_text Ms");
    csnhypodbp_magcfg_gate_text(&c, 9, txt, sizeof(txt));
    CHECK(txt[0] == '\0', "gate_text: magnitud desconocida -> vacio");

    /* Lectura de las claves del .d. */
    {
        FILE *f = fopen(TMP, "w");

        if (f) {
            fprintf(f, "Ms_MinDelta 2.0\nMs_MaxDelta 100.0\nMb_MinDelta 10.0\n"
                       "Mwp_MaxDelta 80.0\nMl_MaxDist 450.0\nMl_DistType 0\n");
            fclose(f);
        }
        csnhypodbp_magcfg_defaults(&c);
        CHECK(csnhypodbp_magcfg_load(&c, TMP) == 0, "gate: load de las puertas");
        CHECK(fabs(c.ms_min_delta - 2.0) < 1e-12 && fabs(c.ms_max_delta - 100.0) < 1e-12,
              "gate: Ms_MinDelta/Ms_MaxDelta");
        CHECK(fabs(c.mb_min_delta - 10.0) < 1e-12 && fabs(c.mwp_max_delta - 80.0) < 1e-12,
              "gate: Mb_MinDelta/Mwp_MaxDelta");
        CHECK(fabs(c.ml_max_dist_km - 450.0) < 1e-12 && c.ml_dist_type == 0,
              "gate: Ml_MaxDist/Ml_DistType");
        csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MS, 4.5, 500.0, &in);
        CHECK(in == 1, "gate: con el .d ajustado, 4.5 deg entra en Ms");
        remove(TMP);
    }

    /* Limites sueltos y override manual (barra inferior de la GUI). */
    {
        CsnhypodbpMagGateOv ov;
        CsnhypodbpMagCfg out;
        double lo = -1, hi = -1, km = -1;

        csnhypodbp_magcfg_defaults(&c);
        csnhypodbp_magcfg_gate_limits(&c, CSNHYPODBP_MAG_MS, &lo, &hi, &km);
        CHECK(fabs(lo - 20.0) < 1e-12 && fabs(hi - 160.0) < 1e-12 && fabs(km) < 1e-12,
              "gate_limits: Ms 20-160 sin corte en km");
        csnhypodbp_magcfg_gate_limits(&c, CSNHYPODBP_MAG_ML, &lo, &hi, &km);
        CHECK(fabs(lo) < 1e-12 && fabs(hi - 8.0) < 1e-12 && fabs(km - 600.0) < 1e-12,
              "gate_limits: ML con corte en km");
        csnhypodbp_magcfg_gate_limits(&c, 9, &lo, &hi, &km);
        CHECK(fabs(lo) < 1e-12 && fabs(hi) < 1e-12, "gate_limits: magnitud desconocida");

        memset(&ov, 0, sizeof(ov));
        ov.on = 1; ov.min_delta = 2.0; ov.max_delta = 100.0;
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_MS, &ov, &out);
        csnhypodbp_magcfg_gate(&out, CSNHYPODBP_MAG_MS, 4.5, 500.0, &in);
        CHECK(in == 1, "override: Ms baja a 2 deg y 4.5 entra");
        csnhypodbp_magcfg_gate(&c, CSNHYPODBP_MAG_MS, 4.5, 500.0, &in);
        CHECK(in == 0, "override: el .d original no se toca");
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_ML, &ov, &out);
        CHECK(fabs(out.ml_min_delta - 2.0) < 1e-12 && fabs(out.ml_max_delta - 100.0) < 1e-12 &&
              fabs(out.ml_max_dist_km - 600.0) < 1e-12,
              "override: en ML el corte en km se conserva si no se fija");
        ov.max_dist_km = 300.0;
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_ML, &ov, &out);
        CHECK(fabs(out.ml_max_dist_km - 300.0) < 1e-12,
              "override: el corte en km se puede bajar");
        ov.on = 0;
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_MS, &ov, &out);
        CHECK(fabs(out.ms_min_delta - 20.0) < 1e-12, "override: apagado usa el .d");
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_MS, NULL, &out);
        CHECK(fabs(out.ms_min_delta - 20.0) < 1e-12, "override: NULL usa el .d");
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_MS, &ov, NULL);
        CHECK(1, "override: salida NULL no rompe");

        /* Corte de PROFUNDIDAD (del evento, no de la estacion). */
        csnhypodbp_magcfg_defaults(&c);
        CHECK(fabs(csnhypodbp_magcfg_max_depth(&c, CSNHYPODBP_MAG_MS) - 60.0) < 1e-12,
              "depth: Ms corta a 60 km por defecto");
        CHECK(fabs(csnhypodbp_magcfg_max_depth(&c, CSNHYPODBP_MAG_ML)) < 1e-12,
              "depth: ML sin corte por defecto");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MS, 30.0) == 1,
              "depth: Ms con 30 km entra");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MS, 120.0) == 0,
              "depth: Ms con 120 km queda fuera");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_ML, 700.0) == 1,
              "depth: ML no tiene corte");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MS, NAN) == 1,
              "depth: profundidad desconocida -> entra");
        c.ms_deep_corr = 1;
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MS, 120.0) == 1,
              "depth: Ms_DeepCorr desactiva el veto");
        c.ms_deep_corr = 0;

        /* El override tambien mueve el corte de profundidad. */
        memset(&ov, 0, sizeof(ov));
        ov.on = 1; ov.min_delta = 20.0; ov.max_delta = 160.0; ov.max_depth = 300.0;
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_MS, &ov, &out);
        CHECK(csnhypodbp_magcfg_depth_ok(&out, CSNHYPODBP_MAG_MS, 120.0) == 1,
              "depth: el override sube el corte y 120 km entra");
        ov.max_depth = 0.0;
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_MS, &ov, &out);
        CHECK(csnhypodbp_magcfg_depth_ok(&out, CSNHYPODBP_MAG_MS, 700.0) == 1,
              "depth: override a 0 quita el corte");

        /* Piso de profundidad (min): el rango es [min, max]. */
        csnhypodbp_magcfg_defaults(&c);
        CHECK(fabs(csnhypodbp_magcfg_min_depth(&c, CSNHYPODBP_MAG_MB)) < 1e-12,
              "depth: Mb sin piso por defecto");
        c.mb_min_depth = 70.0;
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MB, 120.0) == 1,
              "depth: Mb con 120 km pasa el piso de 70");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MB, 30.0) == 0,
              "depth: Mb con 30 km no llega al piso de 70");
        c.mb_max_depth = 200.0;
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MB, 300.0) == 0,
              "depth: Mb con 300 km pasa el techo de 200");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MB, 120.0) == 1,
              "depth: Mb con 120 km entra en [70, 200]");
        memset(&ov, 0, sizeof(ov));
        ov.on = 1; ov.min_depth = 40.0; ov.max_depth = 100.0;
        csnhypodbp_magcfg_apply_override(&c, CSNHYPODBP_MAG_MB, &ov, &out);
        CHECK(csnhypodbp_magcfg_depth_ok(&out, CSNHYPODBP_MAG_MB, 30.0) == 0,
              "depth: el override sube el piso");
        CHECK(csnhypodbp_magcfg_depth_ok(&out, CSNHYPODBP_MAG_MB, 60.0) == 1,
              "depth: el override deja entrar 60 km");
    }
}

int main(void)
{
    test_defaults();
    test_names();
    test_load();
    test_gate();
    if (fails) { printf("%d FALLOS\n", fails); return 1; }
    printf("OK test_magcfg\n");
    return 0;
}
