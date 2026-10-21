/* test_window.c — geometría de ventanas: sólo se emiten ventanas completas. */

#include "../pickml.h"

static int fails;

#define CHECK(c) do { \
    if (!(c)) { fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
} while (0)

int main(void)
{
    PmWin  w;
    double t0;

    pm_win_init(&w, 1000.0, 10.0, 60.0);

    CHECK(pm_win_next(&w, 1059.0, &t0) == 0);      /* aún incompleta */
    CHECK(pm_win_next(&w, 1060.0, &t0) == 1);
    CHECK(fabs(t0 - 1000.0) < 1e-9);
    CHECK(pm_win_next(&w, 1060.0, &t0) == 0);      /* 1010+60 > 1060 */
    CHECK(pm_win_next(&w, 1070.0, &t0) == 1);
    CHECK(fabs(t0 - 1010.0) < 1e-9);

    /* sin datos no hay ventanas */
    pm_win_init(&w, 0.0, 10.0, 60.0);
    CHECK(pm_win_next(&w, -1.0, &t0) == 0);
    CHECK(pm_win_next(&w, 59.9, &t0) == 0);

    /* un solo paso = una ventana completa cada StepSec */
    pm_win_init(&w, 0.0, 60.0, 60.0);
    CHECK(pm_win_next(&w, 59.999, &t0) == 0);
    CHECK(pm_win_next(&w, 60.0, &t0) == 1);
    CHECK(fabs(t0) < 1e-9);

    if (fails) { fprintf(stderr, "test_window: %d fallos\n", fails); return 1; }
    printf("test_window: OK\n");
    return 0;
}
