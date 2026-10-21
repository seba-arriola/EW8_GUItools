/* test_sta.c — lectura de pickml.sta y resolución estación/componente. */

#include "../pickml.h"
#include <unistd.h>

static int fails;

#define CHECK(c) do { \
    if (!(c)) { fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
} while (0)

int main(void)
{
    char path[64];
    FILE *f;
    PmStation st[8];
    int n, comp;

    snprintf(path, sizeof(path), "/tmp/pickml_sta_%d.sta", (int)getpid());
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("# comentario\n"
          "1 1 FAR1 C -- HHE HHN HHZ\n"
          "1 2 GO01 C1 00 HHE HHN HHZ\n"
          "0 3 NOPE C -- HHE HHN HHZ\n"
          "1 4 PAYG IU 10 BHE BHN BHZ\n", f);
    fclose(f);

    n = pm_sta_read(path, st, 8);
    CHECK(n == 3);                        /* la línea con flag 0 se ignora */

    CHECK(!strcmp(st[0].sta, "FAR1"));
    CHECK(!strcmp(st[0].chan[PM_COMP_Z], "HHZ"));
    CHECK(!strcmp(st[0].chan[PM_COMP_N], "HHN"));
    CHECK(!strcmp(st[0].chan[PM_COMP_E], "HHE"));

    comp = -1;
    CHECK(pm_sta_find(st, n, "FAR1", "HHN", "C", "--", &comp) == 0);
    CHECK(comp == PM_COMP_N);

    comp = -1;
    CHECK(pm_sta_find(st, n, "GO01", "HHZ", "C1", "00", &comp) == 1);
    CHECK(comp == PM_COMP_Z);

    /* red distinta: no casa */
    CHECK(pm_sta_find(st, n, "GO01", "HHZ", "C", "00", &comp) < 0);
    /* localización distinta: no casa */
    CHECK(pm_sta_find(st, n, "GO01", "HHZ", "C1", "--", &comp) < 0);
    /* canal fuera de la lista: no casa */
    CHECK(pm_sta_find(st, n, "FAR1", "BHZ", "C", "--", &comp) < 0);
    /* estación inexistente */
    CHECK(pm_sta_find(st, n, "XXXX", "HHZ", "C", "--", &comp) < 0);

    /* canal de reporte por fase */
    {
        PmConfig c;
        pm_config_defaults(&c);
        CHECK(pm_sta_chan_for_phase(&st[0], 'P', &c) == PM_COMP_Z);
        CHECK(pm_sta_chan_for_phase(&st[0], 'S', &c) == PM_COMP_N);
        strcpy(c.SReportChan, "E");
        CHECK(pm_sta_chan_for_phase(&st[0], 'S', &c) == PM_COMP_E);
    }

    remove(path);

    if (fails) { fprintf(stderr, "test_sta: %d fallos\n", fails); return 1; }
    printf("test_sta: OK\n");
    return 0;
}
