/* Test de DBSCAN_Cluster: dos grupos separados + ruido. */
#include <stdio.h>
#include "../csnloc.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

int main(void)
{
    /* 2D: grupo A en (0,0),(0.1,0.1),(0.2,0); grupo B en (10,10),(10.1,10.1),
       (10.2,10); ruido en (50,50). */
    double X[] = {
        0.0, 0.0,  0.1, 0.1,  0.2, 0.0,
        10.0, 10.0, 10.1, 10.1, 10.2, 10.0,
        50.0, 50.0
    };
    int labels[7];
    int ncl = DBSCAN_Cluster(X, 7, 2, 1.0, 2, labels);
    int i;

    CHECK(ncl == 2, "detecta exactamente 2 clusters");
    CHECK(labels[0] == labels[1] && labels[1] == labels[2], "grupo A junto");
    CHECK(labels[3] == labels[4] && labels[4] == labels[5], "grupo B junto");
    CHECK(labels[0] != labels[3], "A y B separados");
    CHECK(labels[6] == -1, "punto aislado = ruido");
    (void)i;

    if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_dbscan\n");
    return 0;
}
