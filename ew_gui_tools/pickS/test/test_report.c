/* test_report.c — formato TYPE_PICK_SCNL con token S y reparseo. */
#include "pickS.h"

int main(void)
{
    char   buf[256], sta[PICKS_SCNL], phase;
    double t, tgot;
    int    m;

    t = 1700000000.5;
    if (PickS_Report_Format(buf, sizeof(buf), 165, 0, 7,
                            "TEST", "HHE", "C", "--", t, '?', 1, 123, 'S') <= 0) {
        printf("FAIL: format\n"); return 1;
    }
    printf("%s", buf);

    if (strncmp(buf, "8 165 0 7 TEST.HHE.C.--", 23) != 0) {
        printf("FAIL: prefijo inesperado\n"); return 1;
    }
    m = PickS_ParsePickLine(buf, sta, &tgot, &phase);
    if (m < 7) { printf("FAIL: reparseo (%d campos)\n", m); return 1; }
    if (strcmp(sta, "TEST") != 0) { printf("FAIL: estación\n"); return 1; }
    if (phase != 'S') { printf("FAIL: fase no S\n"); return 1; }
    if (fabs(tgot - t) > 0.002) {
        printf("FAIL: tiempo t=%.3f got=%.3f\n", t, tgot); return 1;
    }
    printf("test_report OK\n");
    return 0;
}
