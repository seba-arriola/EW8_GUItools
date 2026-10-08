/* test_ring3c.c — cobertura y alineación del buffer 3C. */
#include "pickS.h"

int main(void)
{
    PickS_Station st;
    PickS_Ring3C  r;
    double e[100], n[100], z[100];
    int32_t d[100];
    int    i;

    memset(&st, 0, sizeof(st));
    strcpy(st.sta, "TEST"); strcpy(st.net, "C"); strcpy(st.loc, "--");
    strcpy(st.chan[PICKS_COMP_E], "HHE");
    strcpy(st.chan[PICKS_COMP_N], "HHN");
    strcpy(st.chan[PICKS_COMP_Z], "HHZ");

    if (PickS_Ring3C_Init(&r, &st, 100.0, 10.0, 1.0, 10.0, 4) != 0) {
        printf("FAIL: init\n"); return 1;
    }
    for (i = 0; i < 100; i++) d[i] = i;
    PickS_Ring3C_Add(&r, PICKS_COMP_E, 1000.0, 100.0, d, 100);
    PickS_Ring3C_Add(&r, PICKS_COMP_N, 1000.0, 100.0, d, 100);
    PickS_Ring3C_Add(&r, PICKS_COMP_Z, 1000.0, 100.0, d, 100);

    if (PickS_Ring3C_Window(&r, 0, 100, e, n, z) != 0) {
        printf("FAIL: ventana cubierta devolvio error\n"); return 1;
    }
    if (PickS_Ring3C_Window(&r, 50, 100, e, n, z) == 0) {
        printf("FAIL: ventana fuera de rango aceptada\n"); return 1;
    }

    /* Solo E en el segundo bloque: N,Z sin cubrir -> error. */
    PickS_Ring3C_Add(&r, PICKS_COMP_E, 1001.0, 100.0, d, 100);
    if (PickS_Ring3C_Window(&r, 100, 100, e, n, z) != -2) {
        printf("FAIL: ventana parcial no detectada\n"); return 1;
    }
    PickS_Ring3C_Add(&r, PICKS_COMP_N, 1001.0, 100.0, d, 100);
    PickS_Ring3C_Add(&r, PICKS_COMP_Z, 1001.0, 100.0, d, 100);
    if (PickS_Ring3C_Window(&r, 100, 100, e, n, z) != 0) {
        printf("FAIL: ventana completa tras relleno\n"); return 1;
    }

    PickS_Ring3C_Free(&r);
    printf("test_ring3c OK\n");
    return 0;
}
