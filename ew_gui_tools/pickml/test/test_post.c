/* test_post.c — umbral, picos, ventana de validez, weight y dedup. */

#include "../pickml.h"

static int fails;

#define CHECK(c) do { \
    if (!(c)) { fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
} while (0)

int main(void)
{
    float  curve[600];
    PmPick pk[8];
    int    n, weight, i;
    double wprob[4] = {0.90, 0.70, 0.50, 0.30};

    memset(curve, 0, sizeof(curve));

    /* un lóbulo CONTIGUO sobre el umbral con el máximo en i=300
     * (t0 + 3.0 s a 100 Hz) */
    for (i = 290; i <= 310; i++) curve[i] = 0.35f;
    curve[300] = 0.9f;

    n = pm_post_peaks(curve, 600, 100.0, 100.0, 0.30, 0.0, 0.0, 'P', pk, 8);
    CHECK(n == 1);
    CHECK(fabs(pk[0].t - 103.0) < 1e-9);
    CHECK(pk[0].phase == 'P');
    CHECK(pk[0].prob > 0.89f);

    /* segundo lóbulo separado -> dos picks */
    for (i = 395; i <= 405; i++) curve[i] = 0.40f;
    curve[400] = 0.7f;
    n = pm_post_peaks(curve, 600, 100.0, 100.0, 0.30, 0.0, 0.0, 'P', pk, 8);
    CHECK(n == 2);
    CHECK(fabs(pk[1].t - 104.0) < 1e-9);

    /* MinPosSec descarta el primer lóbulo (posición 3 s < 3.5 s) */
    n = pm_post_peaks(curve, 600, 100.0, 100.0, 0.30, 3.5, 0.0, 'P', pk, 8);
    CHECK(n == 1);
    CHECK(fabs(pk[0].t - 104.0) < 1e-9);

    /* MaxPosSec acota el final */
    n = pm_post_peaks(curve, 600, 100.0, 100.0, 0.30, 0.0, 2.5, 'P', pk, 8);
    CHECK(n == 0);

    /* curva sin nada sobre el umbral */
    memset(curve, 0, sizeof(curve));
    curve[10] = 0.29f;
    n = pm_post_peaks(curve, 600, 0.0, 100.0, 0.30, 0.0, 0.0, 'S', pk, 8);
    CHECK(n == 0);

    /* weight: 0 el mejor; por debajo de W3 se descarta */
    CHECK(pm_prob_weight(0.95, wprob, &weight) == 1 && weight == 0);
    CHECK(pm_prob_weight(0.75, wprob, &weight) == 1 && weight == 1);
    CHECK(pm_prob_weight(0.55, wprob, &weight) == 1 && weight == 2);
    CHECK(pm_prob_weight(0.35, wprob, &weight) == 1 && weight == 3);
    CHECK(pm_prob_weight(0.10, wprob, &weight) == 0);

    /* dedup: mismo pick dentro de la ventana se descarta; distinta fase no */
    {
        PmDedup d;
        memset(&d, 0, sizeof(d));
        CHECK(pm_dedup_accept(&d, 100.0, 'P', 2.0) == 1);
        CHECK(pm_dedup_accept(&d, 101.0, 'P', 2.0) == 0);
        CHECK(pm_dedup_accept(&d, 101.0, 'S', 2.0) == 1);
        CHECK(pm_dedup_accept(&d, 103.5, 'P', 2.0) == 1);
    }

    /* compuerta S-P: un arribo no puede ser P y S a la vez */
    {
        PmPick pp[2], sp[3];
        int ns;

        pp[0].t = 100.0; pp[0].phase = 'P'; pp[0].prob = 0.50f;
        pp[1].t = 120.0; pp[1].phase = 'P'; pp[1].prob = 0.40f;

        sp[0].t = 100.3;  sp[0].phase = 'S'; sp[0].prob = 0.39f;  /* pegada al P */
        sp[1].t = 112.0;  sp[1].phase = 'S'; sp[1].prob = 0.35f;  /* legítima */
        sp[2].t = 121.0;  sp[2].phase = 'S'; sp[2].prob = 0.31f;  /* pegada al 2.º P */

        ns = pm_gate_s_vs_p(sp, 3, pp, 2, 2.0);
        CHECK(ns == 1);
        CHECK(fabs(sp[0].t - 112.0) < 1e-9);

        /* con minsp 0 no filtra nada */
        sp[0].t = 100.3; sp[1].t = 112.0; sp[2].t = 121.0;
        CHECK(pm_gate_s_vs_p(sp, 3, pp, 2, 0.0) == 3);
        /* sin P, no filtra */
        CHECK(pm_gate_s_vs_p(sp, 3, pp, 0, 2.0) == 3);
    }

    if (fails) { fprintf(stderr, "test_post: %d fallos\n", fails); return 1; }
    printf("test_post: OK\n");
    return 0;
}
