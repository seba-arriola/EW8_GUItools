/* test_aic.c — el AIC recupera un onset de varianza conocido. */
#include "pickS.h"

int main(void)
{
    int    n = 300, i, onset = 150;
    double v[300], got = -1.0;
    unsigned int seed = 12345u;

    for (i = 0; i < n; i++) {
        double amp = (i < onset) ? 1.0 : 20.0;
        seed = seed * 1103515245u + 12345u;
        v[i] = amp * ((double)((seed >> 16) & 0x7fff) / 16384.0 - 1.0);
    }

    if (PickS_Aic_Onset(v, n, onset + 10, 40, &got) != 0) {
        printf("FAIL: AIC devolvio error\n"); return 1;
    }
    printf("onset real=%d estimado=%.0f\n", onset, got);
    if (fabs(got - (double)onset) > 6.0) {
        printf("FAIL: AIC fuera de tolerancia\n"); return 1;
    }
    printf("test_aic OK\n");
    return 0;
}
