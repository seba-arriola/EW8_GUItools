#include <stdio.h>
#include <stdlib.h>

/*
 * gen_hypos.c - genera un hypos.jsonl mínimo (schema del csnloc offline) que
 * casa con el tank de gen_tank.c.
 */

#define T0 1700000000.0

int main(int argc, char **argv)
{
    FILE *f;
    const char *path = (argc > 1) ? argv[1] : "test/tmp/hypos.jsonl";
    f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fprintf(f,
            "{\"event\":1,\"id\":1,\"version\":1,\"t0\":%.3f,"
            "\"lat\":-33.2000,\"lon\":-71.2000,\"depth_km\":10.0,\"nphases\":2,"
            "\"phases\":["
            "{\"sta\":\"TST1\",\"net\":\"C1\",\"chan\":\"HHZ\",\"loc\":\"--\","
            "\"phase\":\"P\",\"t_epoch\":%.3f,\"residual\":0.1,\"weight\":0},"
            "{\"sta\":\"TST2\",\"net\":\"C1\",\"chan\":\"HHZ\",\"loc\":\"--\","
            "\"phase\":\"P\",\"t_epoch\":%.3f,\"residual\":-0.1,\"weight\":0}"
            "]}\n",
            T0, T0 + 5.0, T0 + 8.0);
    fclose(f);
    return 0;
}
