/* CA7: TTModel_Predict vs tabla IASP91 (monotonia y valores plausibles). */
#include <stdio.h>
#include <math.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

int main(void)
{
    TTModel tt;
    double t, prev;
    int    i;

    if (TTModel_Init(&tt, ".", "iasp91", 0.0, 180.0, 0.5) != 0) {
        printf("FAIL: TTModel_Init (faltan iasp91.tbl/.hed en cwd?)\n");
        return 1;
    }
    CHECK(tt.nd > 0 && tt.nk > 0, "tabla TT cargada");

    /* P a 30 km de profundidad debe crecer con la distancia. */
    prev = -1.0;
    for (i = 1; i <= 20; i++) {
        double dist = i * 5.0;   /* 5..100 grados */
        if (TTModel_Predict(&tt, dist, 30.0, CSLOC_PHASE_P, &t, NULL) != 0) {
            printf("FAIL: prediccion a %.0f grados\n", dist);
            fails++;
            break;
        }
        if (prev >= 0.0 && t < prev) {
            printf("FAIL: P no monotona en %.0f grados (%.2f < %.2f)\n",
                   dist, t, prev);
            fails++;
            break;
        }
        prev = t;
    }
    CHECK(fails == 0, "P monotona creciente con distancia");

    /* Valores de referencia aproximados IASP91 (P, 0 km):
       ~ 0 grados -> 0 s ; 30 grados -> ~ 6.5 min ; 90 grados -> ~ 13 min. */
    TTModel_Predict(&tt, 1.0, 0.0, CSLOC_PHASE_P, &t, NULL);
    CHECK(t > 10.0 && t < 30.0, "P a 1 grado ~ 10-30 s");
    TTModel_Predict(&tt, 30.0, 0.0, CSLOC_PHASE_P, &t, NULL);
    CHECK(t > 330.0 && t < 420.0, "P a 30 grados ~ 330-420 s");
    TTModel_Predict(&tt, 90.0, 0.0, CSLOC_PHASE_P, &t, NULL);
    CHECK(t > 700.0 && t < 820.0, "P a 90 grados ~ 700-820 s");

    /* S debe ser mas lento que P a la misma distancia. */
    {
        double tp, ts;
        TTModel_Predict(&tt, 40.0, 20.0, CSLOC_PHASE_P, &tp, NULL);
        TTModel_Predict(&tt, 40.0, 20.0, CSLOC_PHASE_S, &ts, NULL);
        CHECK(ts > tp, "S mas lento que P a 40 grados");
    }

    TTModel_Free(&tt);
    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_tt\n");
    return 0;
}
