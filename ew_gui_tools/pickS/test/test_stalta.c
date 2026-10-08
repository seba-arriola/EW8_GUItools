/* test_stalta.c — la relación STA/LTA sube con un aumento de amplitud. */
#include "pickS.h"

int main(void)
{
    PickS_StaLta s;
    double fs = 100.0, r = 0.0, rmax = 0.0;
    int    i;

    PickS_StaLta_Init(&s, fs, 0.5, 5.0);

    for (i = 0; i < 1000; i++) r = PickS_StaLta_Update(&s, 0.1);
    printf("ratio ruido=%.3f\n", r);
    if (!(r < 1.5)) { printf("FAIL: ratio en ruido alto\n"); return 1; }

    for (i = 0; i < 500; i++) {
        r = PickS_StaLta_Update(&s, 1.0);
        if (r > rmax) rmax = r;
    }
    printf("ratio pico evento=%.3f\n", rmax);
    if (!(rmax > 3.0)) { printf("FAIL: ratio no sube con el evento\n"); return 1; }

    PickS_StaLta_Free(&s);
    printf("test_stalta OK\n");
    return 0;
}
