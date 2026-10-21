/* test_report.c — la línea TYPE_PICK_SCNL tiene el formato que parsea csnloc. */

#include "../pickml.h"

static int fails;

#define CHECK(c) do { \
    if (!(c)) { fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
} while (0)

int main(void)
{
    char buf[256];
    const char *want =
        "8 156 1 7 FAR1.HHZ.C.-- ?2 20231114221320.123 0 0 0 P A\n";
    int n;

    n = pm_report_format(buf, sizeof(buf), 156, 1, 7,
                         "FAR1", "HHZ", "C", "--",
                         1700000000.123, 2, 'P');
    CHECK(n > 0);
    if (strcmp(buf, want) != 0) {
        fprintf(stderr, "FALLO report:\n  got  <%s>\n  want <%s>\n", buf, want);
        fails++;
    }

    /* fase S y weight 0 */
    n = pm_report_format(buf, sizeof(buf), 156, 2, 8,
                         "FAR1", "HHN", "C", "--",
                         1700000000.5, 0, 'S');
    CHECK(strstr(buf, "FAR1.HHN.C.-- ?0 20231114221320.500 0 0 0 S A") != NULL);

    /* redondeo de milisegundos que cruza el segundo */
    n = pm_report_format(buf, sizeof(buf), 156, 1, 9,
                         "FAR1", "HHZ", "C", "--",
                         1700000000.9997, 1, 'P');
    CHECK(strstr(buf, "20231114221321.000") != NULL);

    if (fails) { fprintf(stderr, "test_report: %d fallos\n", fails); return 1; }
    printf("test_report: OK\n");
    return 0;
}
