/* Test headless de csnhypodbp_tt: carga el motor IASP91 y comprueba que el
 * tiempo teorico de P es plausible y monotono en la distancia. */
#include <stdio.h>
#include <unistd.h>

#include "csnhypodbp_tt.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

static int have_tables(void)
{
    return access("iasp91.hed", R_OK) == 0 && access("iasp91.tbl", R_OK) == 0;
}

int main(void)
{
    /* Las tablas viven en csnloc/; el cwd de la prueba es csnhypodbp/. */
    if (!have_tables() && chdir("../csnloc") == 0) { /* reintenta alli */ }
    if (!have_tables()) {
        printf("test_tt: SKIP (faltan iasp91.tbl/.hed en el cwd ni en ../csnloc)\n");
        return 0;
    }
    if (csnhypodbp_tt_init("iasp91") != 0) {
        printf("FAIL: csnhypodbp_tt_init devolvio error\n");
        return 1;
    }

    double t5  = csnhypodbp_tt_p(5.0, 30.0);
    double t10 = csnhypodbp_tt_p(10.0, 30.0);
    double t5d = csnhypodbp_tt_p(5.0, 100.0);

    CHECK(t5 > 0.0, "t5 valido");
    CHECK(t10 > t5, "t10 > t5");
    CHECK(t5 > 30.0 && t5 < 120.0, "t5 en rango plausible (30-120 s)");
    CHECK(t5d > 0.0, "t5 profundo valido");

    printf("test_tt: tP(5deg,30km)=%.2f  tP(10deg,30km)=%.2f  tP(5deg,100km)=%.2f\n",
           t5, t10, t5d);
    if (fails == 0) printf("test_tt: OK\n");
    return fails ? 1 : 0;
}
