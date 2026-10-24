/*
 * test_tt1d_hyp.c - Bandas de un .hyp (CRH + NOD + MUL) y seleccion por epicentro.
 *
 * Se ejecuta con cwd = run_working_v8/params (igual que el port), asi que
 * comprueba el `hyp2000_ring.hyp` y los `.crh` REALES ademas de un fixture
 * propio en /tmp con centros y radios conocidos.
 *
 *   cd run_working_v8/params && ../../ew_gui_tools/csnhypodbpQT/test_tt1d_hyp
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_tt1d.h"

static int g_checks, g_fail;
static char g_dir[64];

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

static char *mk(char *out, size_t n, const char *name, const char *body)
{
    FILE *f;

    snprintf(out, n, "%s/%s", g_dir, name);
    f = fopen(out, "w");
    if (!f) return NULL;
    fputs(body, f);
    fclose(f);
    return out;
}

static double sum_w(const BpqTt1dSel *s)
{
    double w = 0.0;
    int i;

    for (i = 0; i < s->n; i++) w += s->wt[i];
    return w;
}

int main(void)
{
    BpqTt1dSet *set;
    BpqTt1dSel sel;
    char err[256] = "";
    char fh[256], fx[256];
    const char *p;
    double t;

    printf("== test_tt1d_hyp ==\n");
    snprintf(g_dir, sizeof(g_dir), "/tmp/bpq_tt1d_hyp_XXXXXX");
    if (!mkdtemp(g_dir)) {
        fprintf(stderr, "FALLA no pude crear el temporal\n");
        return 1;
    }

    /* --- .crh real: nombre, 20 capas y tiempo vertical de la columna ------- */
    {
        BpqTt1dModel *m = bpq_tt1d_crh_load("N30-38_1.5k.crh", err, sizeof(err));

        CK(m != NULL);
        if (m) {
            CK(bpq_tt1d_model_layers(m) == 20);
            CK(bpq_tt1d_model_name(m) != NULL &&
               strcmp(bpq_tt1d_model_name(m), "CHILE_POTIN_N30-38_1.5k") == 0);
            /* 10 km verticales: si se confundieran las columnas (Vp <-> prof)
             * el valor se iria a ~0.03 s o ~2.1 s. */
            t = bpq_tt1d_time(m, 0.0, 10.0);
            CK(t > 1.70 && t < 2.10);
            bpq_tt1d_model_free(m);
        }
    }
    /* chile_1d.crh tiene Vp iguales (8.48 en 60 y 70): debe cargar. */
    {
        BpqTt1dModel *m = bpq_tt1d_crh_load("chile_1d.crh", err, sizeof(err));

        CK(m != NULL && bpq_tt1d_model_layers(m) == 16);
        bpq_tt1d_model_free(m);
    }

    /* --- .hyp real: 6 bandas y 6 nodos ------------------------------------- */
    set = bpq_tt1d_hyp_load("hyp2000_ring.hyp", ".", err, sizeof(err));
    CK(set != NULL);
    if (set) {
        int i;

        CK(bpq_tt1d_set_models(set) == 6);
        for (i = 1; i <= 6; i++) {
            const char *n = bpq_tt1d_set_model_name(set, i);

            CK(n != NULL && strncmp(n, "CHILE_POTIN_", 12) == 0);
        }
        CK(bpq_tt1d_set_model_name(set, 0) == NULL);
        CK(bpq_tt1d_set_model_name(set, 99) == NULL);

        /* Invariantes de la seleccion con los nodos reales. */
        CK(bpq_tt1d_select(set, -30.0, -70.5, &sel) == 1);
        CK(sel.n >= 1 && sel.n <= 3);
        CKNEAR(sum_w(&sel), 1.0, 1e-9);
        for (i = 0; i < sel.n; i++) CK(sel.mod[i] >= 1 && sel.mod[i] <= 6);
        t = bpq_tt1d_eval(set, &sel, 100.0, 20.0);
        CK(isfinite(t) && t > 5.0 && t < 40.0);
        /* La mezcla cae entre el minimo y el maximo de sus bandas. */
        {
            double lo = 1e9, hi = -1e9;

            for (i = 0; i < sel.n; i++) {
                BpqTt1dSel one;
                double ti;

                memset(&one, 0, sizeof(one));
                one.n = 1;
                one.mod[0] = sel.mod[i];
                one.wt[0] = 1.0;
                ti = bpq_tt1d_eval(set, &one, 100.0, 20.0);
                if (ti < lo) lo = ti;
                if (ti > hi) hi = ti;
            }
            CK(t >= lo - 1e-9 && t <= hi + 1e-9);
        }
        /* Fuera de todos los nodos: el modelo por defecto (MUL T 3). */
        CK(bpq_tt1d_select(set, 60.0, 100.0, &sel) == 1);
        CK(sel.n == 1 && sel.mod[0] == 3);
        /* Entrada no finita: sin seleccion. */
        CK(bpq_tt1d_select(set, NAN, 0.0, &sel) == 0);
        bpq_tt1d_set_free(set);
    }

    /* --- fixture con centros/radios conocidos ------------------------------ */
    {
        const char *a;
        const char *b;
        const char *c;
        char fa[256], fb[256], fc[256];
        char body[1024];

        a = mk(fa, sizeof(fa), "theo_a.crh", "BANDA_A\n 5.00 0.00\n 8.00 30.00\n");
        b = mk(fb, sizeof(fb), "theo_b.crh", "BANDA_B\n 7.00 0.00\n 9.00 30.00\n");
        c = mk(fc, sizeof(fc), "theo_c.crh", "BANDA_C\n 6.00 0.00\n 8.50 30.00\n");
        CK(a && b && c);
        snprintf(body, sizeof(body),
                 "MUL T 3\n"
                 "CRH 1 '%s'\n"
                 "CRH 2 '%s'\n"
                 "CRH 3 '%s'\n"
                 "NOD 0.0 0.0 0.0 0.0 100.0 50.0 1\n"
                 "NOD 2.0 0.0 10.0 0.0 100.0 50.0 2\n",
                 a, b, c);
        p = mk(fh, sizeof(fh), "theo.hyp", body);
        CK(p != NULL);
        bpq_tt1d_set_free(bpq_tt1d_hyp_load("/tmp/no_existe.hyp", ".", err,
                                            sizeof(err)));
        CK(strstr(err, "no se puede abrir") != NULL);
        set = bpq_tt1d_hyp_load(p, ".", err, sizeof(err));
        CK(set != NULL);
        if (set) {
            BpqTt1dSel one, two, three, mix;
            double t1, t2, t3;

            /* Dentro del radio del nodo 1: un solo modelo. */
            CK(bpq_tt1d_select(set, 0.0, 0.0, &sel) == 1);
            CK(sel.n == 1 && sel.mod[0] == 1 && fabs(sel.wt[0] - 1.0) < 1e-12);
            /* Nodo 2 en (2N, 10W): el .hyp guarda oeste-positivo. */
            CK(bpq_tt1d_select(set, 2.0, -10.0, &sel) == 1);
            CK(sel.n == 1 && sel.mod[0] == 2);
            /* Con el signo invertido (10E) caeria fuera: default. */
            CK(bpq_tt1d_select(set, 2.0, 10.0, &sel) == 1);
            CK(sel.n == 1 && sel.mod[0] == 3);
            /* Anillo del nodo 1 a 125 km: taper coseno => mitad y mitad. */
            CK(bpq_tt1d_select(set, 125.0 / 111.195, 0.0, &sel) == 1);
            CK(sel.n == 2);
            CK(sel.mod[0] == 1 && sel.mod[1] == 3);
            CKNEAR(sel.wt[0], 0.5, 0.01);
            CKNEAR(sum_w(&sel), 1.0, 1e-9);
            /* Fuera de todo: default. */
            CK(bpq_tt1d_select(set, 60.0, 0.0, &sel) == 1);
            CK(sel.n == 1 && sel.mod[0] == 3);

            /* eval = media ponderada de los T individuales. */
            memset(&one, 0, sizeof(one));     one.n = 1;     one.mod[0] = 1; one.wt[0] = 1.0;
            memset(&two, 0, sizeof(two));     two.n = 1;     two.mod[0] = 2; two.wt[0] = 1.0;
            memset(&three, 0, sizeof(three)); three.n = 1;   three.mod[0] = 3; three.wt[0] = 1.0;
            t1 = bpq_tt1d_eval(set, &one, 200.0, 10.0);
            t2 = bpq_tt1d_eval(set, &two, 200.0, 10.0);
            t3 = bpq_tt1d_eval(set, &three, 200.0, 10.0);
            /* A es la mas lenta y B la mas rapida (5/8 vs 7/9). */
            CK(isfinite(t1) && isfinite(t2) && isfinite(t3) && t1 > t3 && t3 > t2);
            memset(&mix, 0, sizeof(mix));
            mix.n = 2;
            mix.mod[0] = 1; mix.wt[0] = 0.25;
            mix.mod[1] = 2; mix.wt[1] = 0.75;
            CKNEAR(bpq_tt1d_eval(set, &mix, 200.0, 10.0), 0.25 * t1 + 0.75 * t2, 1e-9);
            /* Sin seleccion no hay valor. */
            CK(isnan(bpq_tt1d_eval(set, NULL, 200.0, 10.0)));
            bpq_tt1d_set_free(set);
        }

        /* --- errores de .hyp (necesitan un .crh que si exista) ------------- */
        p = mk(fx, sizeof(fx), "nocrh.hyp", "MUL T 1\nNOD 0 0 0 0 100 50 1\n");
        bpq_tt1d_set_free(bpq_tt1d_hyp_load(p, ".", err, sizeof(err)));
        CK(strstr(err, "sin tarjetas CRH") != NULL);

        p = mk(fx, sizeof(fx), "crt.hyp", "CRT 1 'x.crt'\n");
        bpq_tt1d_set_free(bpq_tt1d_hyp_load(p, ".", err, sizeof(err)));
        CK(strstr(err, "CRT no soportado") != NULL);

        snprintf(body, sizeof(body),
                 "CRH 1 '%s'\nNOD 0.0 0.0 0.0 0.0 100.0 50.0 2\n", a);
        p = mk(fx, sizeof(fx), "badnod.hyp", body);
        bpq_tt1d_set_free(bpq_tt1d_hyp_load(p, ".", err, sizeof(err)));
        CK(strstr(err, "no declarado") != NULL);
    }

    printf("== tt1d_hyp checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
