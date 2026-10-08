/* test_sta_list.c — parseo de pickS.sta y salto de líneas flag=0. */
#include "pickS.h"

int main(void)
{
    const char *path = "/tmp/opencode/pickS_test.sta";
    FILE *f = fopen(path, "w");
    PickS_Station list[8];
    int n;

    if (!f) { printf("FAIL: no puedo escribir %s\n", path); return 1; }
    fprintf(f, "# comentario\n");
    fprintf(f, "1 1 FAR1 C -- HHE HHN HHZ\n");
    fprintf(f, "0 2 NOGO C -- HHE HHN HHZ\n");
    fprintf(f, "1 3 PAYG IU 10 HHE HHN HHZ\n");
    fclose(f);

    n = PickS_ReadStaList(path, list, 8);
    printf("estaciones activas=%d\n", n);
    if (n != 2) { printf("FAIL: se esperaban 2\n"); return 1; }
    if (strcmp(list[0].sta, "FAR1") != 0 ||
        strcmp(list[0].chan[PICKS_COMP_E], "HHE") != 0 ||
        strcmp(list[0].loc, "--") != 0) {
        printf("FAIL: campos de FAR1\n"); return 1;
    }
    if (strcmp(list[1].loc, "10") != 0) { printf("FAIL: loc PAYG\n"); return 1; }

    {
        int comp = -1;
        int idx = PickS_FindStation(list, n, "PAYG", "HHN", "IU", "10", &comp);
        if (idx != 1 || comp != PICKS_COMP_N) {
            printf("FAIL: FindStation idx=%d comp=%d\n", idx, comp); return 1;
        }
        if (PickS_FindStation(list, n, "NOPE", "HHE", "C", "--", &comp) != -1) {
            printf("FAIL: estación inexistente encontrada\n"); return 1;
        }
    }

    printf("test_sta_list OK\n");
    return 0;
}
