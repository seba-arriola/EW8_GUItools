/*
 * Test headless de los parametros de magnitud que la GUI lee del .d de csnmags
 * (ver csnhypodbp_magcfg.h): defaults de fabrica, sobrescritura por archivo y
 * ventana por defecto de cada magnitud.
 */

#include <locale.h>
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
              "override: no toca el corte en km de ML (sigue el del .d)");
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
        CHECK(fabs(csnhypodbp_magcfg_max_depth(&c, CSNHYPODBP_MAG_ML) - 999.0) < 1e-12,
              "depth: ML sin corte por defecto (999)");
        CHECK(fabs(csnhypodbp_magcfg_max_depth(&c, CSNHYPODBP_MAG_MWP) - 999.0) < 1e-12 &&
              fabs(csnhypodbp_magcfg_max_depth(&c, CSNHYPODBP_MAG_MB) - 999.0) < 1e-12,
              "depth: Mwp/Mb sin corte por defecto (999)");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MS, 30.0) == 1,
              "depth: Ms con 30 km entra");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_MS, 120.0) == 0,
              "depth: Ms con 120 km queda fuera");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_ML, 700.0) == 1,
              "depth: ML no tiene corte");
        CHECK(csnhypodbp_magcfg_depth_ok(&c, CSNHYPODBP_MAG_ML, 999.0) == 1,
              "depth: ML con 999 km entra");
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

/* Cajas de la barra de overrides (`GtkEntry` en la GUI): limites, formato,
 * parseo (independiente de `LC_NUMERIC`) y volcado todo-o-nada. */
static void test_ov_fields(void)
{
    CsnhypodbpMagGateOv ov, back;
    char txt[CSNHYPODBP_MAG_OV_FIELD_N][24];
    const char *p[CSNHYPODBP_MAG_OV_FIELD_N];
    double lo = -1.0, hi = -1.0, v = -1.0;
    int digits = -1, i;
    const char *loc;

    CHECK(csnhypodbp_mag_ov_field_limits(CSNHYPODBP_MAG_OV_MIN_DELTA,
                                         &lo, &hi, &digits) == 0 &&
          fabs(lo) < 1e-12 && fabs(hi - 180.0) < 1e-12 && digits == 1,
          "ov: limites de la distancia minima");
    CHECK(csnhypodbp_mag_ov_field_limits(CSNHYPODBP_MAG_OV_MAX_DEPTH,
                                         &lo, &hi, &digits) == 0 &&
          fabs(hi - 999.0) < 1e-12 && digits == 0,
          "ov: limites de la profundidad maxima (999)");
    CHECK(csnhypodbp_mag_ov_field_limits(CSNHYPODBP_MAG_OV_MIN_DEPTH,
                                         &lo, &hi, &digits) == 0 &&
          fabs(hi - 700.0) < 1e-12 && digits == 0,
          "ov: limites de la profundidad minima");
    CHECK(csnhypodbp_mag_ov_field_limits(-1, NULL, NULL, NULL) == -1 &&
          csnhypodbp_mag_ov_field_limits(CSNHYPODBP_MAG_OV_FIELD_N, NULL, NULL, NULL) == -1,
          "ov: campo fuera de rango");

    /* Ancho de la caja: el del valor canonico mas el margen. En GTK4 el natural de
       un GtkEntry es 150 px fijos salvo que se acote, asi que esto es lo que
       decide el ancho visible (ver csnhypodbp_magcfg.h). */
    CHECK(csnhypodbp_mag_ov_width_chars(CSNHYPODBP_MAG_OV_MIN_DELTA) == 7 &&
          csnhypodbp_mag_ov_width_chars(CSNHYPODBP_MAG_OV_MAX_DELTA) == 7,
          "ov: ancho de las cajas de distancia (\"180.0\" + margen)");
    CHECK(csnhypodbp_mag_ov_width_chars(CSNHYPODBP_MAG_OV_MIN_DEPTH) == 5 &&
          csnhypodbp_mag_ov_width_chars(CSNHYPODBP_MAG_OV_MAX_DEPTH) == 5,
          "ov: ancho de las cajas de profundidad (\"700\"/\"999\" + margen)");
    CHECK(csnhypodbp_mag_ov_width_chars(-1) == -1 &&
          csnhypodbp_mag_ov_width_chars(CSNHYPODBP_MAG_OV_FIELD_N) == -1,
          "ov: ancho de un campo fuera de rango");
    {
        char wid[24];
        double top = 0.0;
        int ok = 1;

        for (i = 0; i < CSNHYPODBP_MAG_OV_FIELD_N; i++) {
            csnhypodbp_mag_ov_field_limits(i, NULL, &top, NULL);
            csnhypodbp_mag_ov_fmt(wid, sizeof wid, i, top);
            if (csnhypodbp_mag_ov_width_chars(i) !=
                (int)strlen(wid) + CSNHYPODBP_MAG_OV_WIDTH_MARGIN) ok = 0;
        }
        CHECK(ok, "ov: el ancho sale del valor canonico + margen");
    }

    CHECK(csnhypodbp_mag_ov_fmt(txt[0], sizeof txt[0],
                                CSNHYPODBP_MAG_OV_MIN_DELTA, 12.5) == 0 &&
          strcmp(txt[0], "12.5") == 0, "ov: fmt de 1 decimal");
    CHECK(csnhypodbp_mag_ov_fmt(txt[0], sizeof txt[0],
                                CSNHYPODBP_MAG_OV_MAX_DEPTH, 600.0) == 0 &&
          strcmp(txt[0], "600") == 0, "ov: fmt sin decimales");
    CHECK(csnhypodbp_mag_ov_fmt(txt[0], sizeof txt[0],
                                CSNHYPODBP_MAG_OV_MAX_DEPTH, 200.4) == 0 &&
          strcmp(txt[0], "200") == 0, "ov: fmt redondea al decimal del campo");
    CHECK(csnhypodbp_mag_ov_fmt(txt[0], 3, CSNHYPODBP_MAG_OV_MAX_DEPTH, 999.0) == -1,
          "ov: fmt con buffer corto");
    CHECK(csnhypodbp_mag_ov_fmt(txt[0], sizeof txt[0],
                                CSNHYPODBP_MAG_OV_MAX_DEPTH, NAN) == -1,
          "ov: fmt de un valor no finito");

    CHECK(csnhypodbp_mag_ov_parse("12.5", CSNHYPODBP_MAG_OV_MIN_DELTA, &v) == 0 &&
          fabs(v - 12.5) < 1e-9, "ov: parse de 1 decimal");
    CHECK(csnhypodbp_mag_ov_parse("  180.0 ", CSNHYPODBP_MAG_OV_MAX_DELTA, &v) == 0 &&
          fabs(v - 180.0) < 1e-9, "ov: parse con espacios alrededor");
    CHECK(csnhypodbp_mag_ov_parse("12,5", CSNHYPODBP_MAG_OV_MIN_DELTA, &v) == 0 &&
          fabs(v - 12.5) < 1e-9, "ov: parse con coma decimal");
    CHECK(csnhypodbp_mag_ov_parse("12.56", CSNHYPODBP_MAG_OV_MIN_DELTA, &v) == 0 &&
          fabs(v - 12.6) < 1e-9, "ov: parse redondea a los decimales del campo");
    CHECK(csnhypodbp_mag_ov_parse("600.4", CSNHYPODBP_MAG_OV_MAX_DEPTH, &v) == 0 &&
          fabs(v - 600.0) < 1e-9, "ov: parse trunca el decimal de un campo entero");
    CHECK(csnhypodbp_mag_ov_parse("99999", CSNHYPODBP_MAG_OV_MAX_DEPTH, &v) == 0 &&
          fabs(v - 999.0) < 1e-9, "ov: parse acota al techo del campo");
    CHECK(csnhypodbp_mag_ov_parse("-3", CSNHYPODBP_MAG_OV_MIN_DEPTH, &v) == 0 &&
          fabs(v) < 1e-9, "ov: parse acota al piso del campo");

    v = 7.5;
    CHECK(csnhypodbp_mag_ov_parse("", CSNHYPODBP_MAG_OV_MIN_DELTA, &v) == -1 &&
          fabs(v - 7.5) < 1e-9, "ov: vacio es invalido y no toca el valor");
    CHECK(csnhypodbp_mag_ov_parse("   ", CSNHYPODBP_MAG_OV_MIN_DELTA, NULL) == -1,
          "ov: solo espacios es invalido");
    CHECK(csnhypodbp_mag_ov_parse("abc", CSNHYPODBP_MAG_OV_MIN_DELTA, NULL) == -1,
          "ov: basura es invalida");
    CHECK(csnhypodbp_mag_ov_parse("1.2.3", CSNHYPODBP_MAG_OV_MIN_DELTA, NULL) == -1,
          "ov: resto sin consumir es invalido");
    CHECK(csnhypodbp_mag_ov_parse("1e3", CSNHYPODBP_MAG_OV_MIN_DELTA, NULL) == -1,
          "ov: notacion cientifica es invalida");
    CHECK(csnhypodbp_mag_ov_parse(NULL, CSNHYPODBP_MAG_OV_MIN_DELTA, NULL) == -1,
          "ov: texto NULL es invalido");
    CHECK(csnhypodbp_mag_ov_parse("1", CSNHYPODBP_MAG_OV_FIELD_N, NULL) == -1,
          "ov: campo fuera de rango al parsear");

    /* El parseo NO depende de LC_NUMERIC: con es_CL un `strtod` de C se corta en
       el punto decimal (AGENTS §7). Si el locale no esta instalado, el test sigue
       valiendo (el parser es el mismo con locale C). */
    loc = setlocale(LC_ALL, "es_CL.UTF-8");
    if (!loc) loc = setlocale(LC_ALL, "es_ES.UTF-8");
    if (!loc) loc = setlocale(LC_ALL, "de_DE.UTF-8");
    CHECK(csnhypodbp_mag_ov_parse("12.5", CSNHYPODBP_MAG_OV_MIN_DELTA, &v) == 0 &&
          fabs(v - 12.5) < 1e-9, "ov: parse de 12.5 con locale no-C");
    CHECK(csnhypodbp_mag_ov_parse("12,5", CSNHYPODBP_MAG_OV_MIN_DELTA, &v) == 0 &&
          fabs(v - 12.5) < 1e-9, "ov: parse de 12,5 con locale no-C");
    setlocale(LC_ALL, "C");     /* `fmt` usa el separador del locale: el resto
                                   de los textos esperados son con punto */

    memset(&ov, 0, sizeof ov);
    p[CSNHYPODBP_MAG_OV_MIN_DELTA]   = "2";
    p[CSNHYPODBP_MAG_OV_MAX_DELTA]   = "25.5";
    p[CSNHYPODBP_MAG_OV_MIN_DEPTH]   = "10";
    p[CSNHYPODBP_MAG_OV_MAX_DEPTH]   = "150";
    CHECK(csnhypodbp_mag_ov_from_text(&ov, p) == 0 && ov.on == 1 &&
          fabs(ov.min_delta - 2.0) < 1e-9 && fabs(ov.max_delta - 25.5) < 1e-9 &&
          fabs(ov.min_depth - 10.0) < 1e-9 &&
          fabs(ov.max_depth - 150.0) < 1e-9,
          "ov: los cuatro validos activan el override");

    csnhypodbp_mag_ov_texts(&ov, txt);
    CHECK(strcmp(txt[CSNHYPODBP_MAG_OV_MIN_DELTA], "2.0") == 0 &&
          strcmp(txt[CSNHYPODBP_MAG_OV_MAX_DELTA], "25.5") == 0 &&
          strcmp(txt[CSNHYPODBP_MAG_OV_MIN_DEPTH], "10") == 0 &&
          strcmp(txt[CSNHYPODBP_MAG_OV_MAX_DEPTH], "150") == 0,
          "ov: los textos canonicos del override");

    for (i = 0; i < CSNHYPODBP_MAG_OV_FIELD_N; i++) p[i] = txt[i];
    memset(&back, 0, sizeof back);
    CHECK(csnhypodbp_mag_ov_from_text(&back, p) == 0 &&
          fabs(back.min_delta - ov.min_delta) < 1e-9 &&
          fabs(back.max_delta - ov.max_delta) < 1e-9 &&
          fabs(back.min_depth - ov.min_depth) < 1e-9 &&
          fabs(back.max_depth - ov.max_depth) < 1e-9,
          "ov: ida y vuelta textos -> override");

    /* Un solo campo invalido invalida los cuatro (el rango que se ve es el que se
       aplica) y dice cual es, sin tocar el override. */
    p[CSNHYPODBP_MAG_OV_MIN_DEPTH] = "no";
    memset(&back, 0, sizeof back);
    back.on = 1; back.max_depth = 42.0;
    CHECK(csnhypodbp_mag_ov_from_text(&back, p) == CSNHYPODBP_MAG_OV_MIN_DEPTH + 1 &&
          back.on == 1 && fabs(back.max_depth - 42.0) < 1e-9 &&
          fabs(back.min_delta) < 1e-9,
          "ov: un campo invalido no toca el override y dice cual");
    p[CSNHYPODBP_MAG_OV_MIN_DEPTH] = "10";
    p[CSNHYPODBP_MAG_OV_MAX_DELTA] = "";
    CHECK(csnhypodbp_mag_ov_from_text(&back, p) == CSNHYPODBP_MAG_OV_MAX_DELTA + 1,
          "ov: una caja vacia es el campo invalido");
    CHECK(csnhypodbp_mag_ov_from_text(NULL, p) == -1 &&
          csnhypodbp_mag_ov_from_text(&back, NULL) == -1,
          "ov: argumentos NULL");
}

int main(void)
{
    test_defaults();
    test_names();
    test_load();
    test_gate();
    test_ov_fields();
    if (fails) { printf("%d FALLOS\n", fails); return 1; }
    printf("OK test_magcfg\n");
    return 0;
}
