/*
 * test_tt1d.c - Nucleo de tiempos de viaje P en capas (`.crh`).
 *
 * Cubre las tres ramas que implementa TRVCON, con formula cerrada:
 *   - semiespacio:        T = sqrt(d^2 + z^2) / v
 *   - vertical (d = 0):   T = sum h_i / v_i
 *   - refractada 2 capas T = 2h·sqrt(v2^2-v1^2)/(v1·v2) + d/v2
 * y los bordes que rompen un trazador mal escrito: regla de la interfaz (la
 * fuente EN la interfaz es de la capa de arriba), fuente en el semiespacio,
 * monotonia en distancia y del tiempo vertical con la profundidad, Vp iguales
 * (chile_1d.crh repite 8.48) y errores de parseo con linea.
 *
 *   make -C ew_gui_tools/csnhypodbpQT test_tt1d && ./test_tt1d
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bpq_tt1d.h"

static int g_checks, g_fail;
static char g_dir[64];
static char g_path[256];

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

#define CKNEAR(a, b, tol) do {                                                 \
    const double _a = (a), _b = (b);                                           \
    g_checks++;                                                                \
    if (!(fabs(_a - _b) <= (tol))) {                                           \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s=%.9f vs %.9f\n", __FILE__, __LINE__,   \
                #a, _a, _b);                                                   \
    }                                                                          \
} while (0)

static const char *mk(const char *name, const char *body)
{
    FILE *f;

    snprintf(g_path, sizeof(g_path), "%s/%s", g_dir, name);
    f = fopen(g_path, "w");
    if (!f) return NULL;
    fputs(body, f);
    fclose(f);
    return g_path;
}

/* Modelo de `n` capas sintetico (para el coste). */
static const char *mk_layers(int n)
{
    FILE *f;
    int i;

    snprintf(g_path, sizeof(g_path), "%s/cost.crh", g_dir);
    f = fopen(g_path, "w");
    if (!f) return NULL;
    fputs("COST\n", f);
    for (i = 1; i <= n; i++)
        fprintf(f, " %.2f %.2f\n", 4.76 + 0.20 * (i - 1), 15.0 * (i - 1));
    fclose(f);
    return g_path;
}

static double now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(void)
{
    BpqTt1dModel *m;
    char err[256] = "";
    const char *p;
    double t;

    printf("== test_tt1d ==\n");
    snprintf(g_dir, sizeof(g_dir), "/tmp/bpq_tt1d_XXXXXX");
    if (!mkdtemp(g_dir)) {
        fprintf(stderr, "FALLA no pude crear el temporal\n");
        return 1;
    }

    /* --- semiespacio: la formula exacta, via biseccion --------------------- */
    p = mk("half.crh", "HALF\n 6.00 0.0\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m != NULL);
    if (m) {
        CK(bpq_tt1d_model_layers(m) == 1);
        CK(bpq_tt1d_model_name(m) != NULL);
        CKNEAR(bpq_tt1d_time(m, 100.0, 20.0), sqrt(100.0 * 100.0 + 20.0 * 20.0) / 6.0, 1e-9);
        CKNEAR(bpq_tt1d_time(m, 0.0, 0.0), 0.0, 1e-12);
        CKNEAR(bpq_tt1d_time(m, 0.0, 30.0), 5.0, 1e-9);
        CKNEAR(bpq_tt1d_time(m, -5.0, 10.0), bpq_tt1d_time(m, 0.0, 10.0), 1e-12);
        CK(isnan(bpq_tt1d_time(m, NAN, 10.0)));
        CK(isnan(bpq_tt1d_time(NULL, 10.0, 10.0)));
        bpq_tt1d_model_free(m);
    }

    /* --- vertical puro y fuente en el semiespacio ------------------------- */
    p = mk("vert.crh", "VERT\n 5.00 0.00\n 8.00 30.00\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m != NULL && bpq_tt1d_model_layers(m) == 2);
    if (m) {
        CKNEAR(bpq_tt1d_time(m, 0.0, 30.0), 6.0, 1e-9);              /* 30/5  */
        CKNEAR(bpq_tt1d_time(m, 0.0, 40.0), 30.0 / 5.0 + 10.0 / 8.0, 1e-9);
        /* Profundidad dentro del semiespacio: finita y monotona. */
        t = bpq_tt1d_time(m, 100.0, 40.0);
        CK(isfinite(t) && t > 0.0);
        CK(bpq_tt1d_time(m, 100.0, 60.0) > t);
    }

    /* --- refractada de 2 capas: intercepto cerrado ------------------------ */
    if (m) {
        const double v1 = 5.0, v2 = 8.0, h = 30.0;
        const double intercept = 2.0 * h * sqrt(v2 * v2 - v1 * v1) / (v1 * v2);

        CKNEAR(bpq_tt1d_time(m, 300.0, 0.0), intercept + 300.0 / v2, 1e-6);
        CKNEAR(bpq_tt1d_time(m, 500.0, 0.0), intercept + 500.0 / v2, 1e-6);
        /* A distancia corta gana el directo (la capa 1 sola). */
        CKNEAR(bpq_tt1d_time(m, 10.0, 0.0), 10.0 / v1, 1e-9);
    }

    /* --- regla de la interfaz: la fuente EN la interfaz es de la capa de
     * arriba (TRVCON: z <= sum), asi que la refractada de esa interfaz todavia
     * existe; al bajar la fuente desaparece y el primer arribo solo puede
     * hacerse mas tarde. --------------------------------------------------- */
    p = mk("three.crh", "THREE\n 5.00 0.00\n 6.00 10.00\n 8.00 30.00\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m != NULL && bpq_tt1d_model_layers(m) == 3);
    if (m) {
        const double a = bpq_tt1d_time(m, 100.0, 10.0 - 1e-6);
        const double b = bpq_tt1d_time(m, 100.0, 10.0);
        const double c = bpq_tt1d_time(m, 100.0, 10.0 + 1e-6);

        CKNEAR(a, b, 1e-6);
        CK(c >= b - 1e-9);
        /* Monotonia en distancia y del tiempo VERTICAL con la profundidad. A
         * distancia fija la profundidad no es monotona: una fuente mas cerca
         * del refractor llega antes (propiedad del modelo, no del codigo). */
        {
            double prev = 0.0;
            int i, mono = 1;

            for (i = 5; i <= 500; i += 5) {
                const double d = bpq_tt1d_time(m, (double)i, 20.0);

                if (!(d > prev)) mono = 0;
                prev = d;
            }
            CK(mono);
            prev = -1.0;    /* T(0,0) = 0: el primero no puede superar a 0 */
            for (i = 0; i <= 60; i += 2) {
                const double d = bpq_tt1d_time(m, 0.0, (double)i);

                if (!(d > prev)) mono = 0;
                prev = d;
            }
            CK(mono);
        }
        bpq_tt1d_model_free(m);
    }

    /* --- Vp iguales (chile_1d.crh: 8.48 y 8.48) --------------------------- */
    p = mk("equal.crh", "EQUAL\n 5.00 0.00\n 8.48 60.00\n 8.48 70.00\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m != NULL && bpq_tt1d_model_layers(m) == 3);
    if (m) {
        t = bpq_tt1d_time(m, 300.0, 5.0);

        CK(isfinite(t) && t > 0.0);
        bpq_tt1d_model_free(m);
    }

    /* --- errores de parseo: NULL + motivo con linea ----------------------- */
    m = bpq_tt1d_crh_load("/tmp/bpq_tt1d_no_existe.crh", err, sizeof(err));
    CK(m == NULL && err[0] != '\0');

    p = mk("noname.crh", "SOLO_NOMBRE\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m == NULL && strstr(err, "sin capas") != NULL);

    p = mk("nofirst.crh", "BAD\n 5.00 10.00\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m == NULL && strstr(err, "primera profundidad") != NULL);

    p = mk("dec.crh", "BAD\n 5.00 0.00\n 4.00 10.00\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m == NULL && strstr(err, "Vp decreciente") != NULL);

    p = mk("rep.crh", "BAD\n 5.00 0.00\n 6.00 10.00\n 7.00 10.00\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m == NULL && strstr(err, "no creciente") != NULL);

    p = mk("junk.crh", "BAD\n 5.00 0.00\n basura\n");
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m == NULL && strstr(err, ":3:") != NULL);

    /* --- tope de capas ---------------------------------------------------- */
    p = mk_layers(BPQ_TT1D_MAX_LAYERS + 2);
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m == NULL && strstr(err, "capas") != NULL);

    /* --- coste: 512 consultas con 21 capas -------------------------------- */
    p = mk_layers(20);
    m = bpq_tt1d_crh_load(p, err, sizeof(err));
    CK(m != NULL && bpq_tt1d_model_layers(m) == 20);
    if (m) {
        double t0, dt;
        int i;

        t0 = now_ms();
        for (i = 0; i < 512; i++) (void)bpq_tt1d_time(m, 20.0 + i, 10.0 + (i % 40));
        dt = now_ms() - t0;
        printf("coste 512 consultas: %.3f ms\n", dt);
        CK(dt < 25.0);
        bpq_tt1d_model_free(m);
    }

    printf("== tt1d checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
