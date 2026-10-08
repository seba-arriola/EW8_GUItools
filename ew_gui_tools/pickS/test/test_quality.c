/* test_quality.c — mapeo métricas -> weight 0-4 monótono. */
#include "pickS.h"

int main(void)
{
    PickS_Params p;
    PickS_Metrics good = { 100.0, 100.0, 1.0, 0.9, 90.0, 5.0 };
    PickS_Metrics bad  = { 0.5, 0.5, 0.0, 0.0, 0.0, 0.1 };
    PickS_Metrics mid  = { 5.0, 5.0, 0.7, 0.5, 90.0, 1.0 };
    int wg = -1, wb = -1, wm = -1;

    PickS_SetDefaults(&p);
    PickS_Quality_Weight(&p, &good, &wg);
    PickS_Quality_Weight(&p, &bad, &wb);
    PickS_Quality_Weight(&p, &mid, &wm);
    printf("good=%d mid=%d bad=%d\n", wg, wm, wb);

    if (wg != 0) { printf("FAIL: bueno deberia ser 0\n"); return 1; }
    if (wb != 4) { printf("FAIL: malo deberia ser 4\n"); return 1; }
    if (!(wg <= wm && wm <= wb)) { printf("FAIL: no monotono\n"); return 1; }
    printf("test_quality OK\n");
    return 0;
}
