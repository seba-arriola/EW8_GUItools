/* C3: carga de estaciones y rango de elevacion (gradiente de color). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sev_capi.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);             \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

static char g_dir[256];

static const char *write_file(const char *name, const char *body)
{
    static char path[600];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    f = fopen(path, "wb");
    if (f) {
        fputs(body, f);
        fclose(f);
    }
    return path;
}

int main(void)
{
    SevStation st[SEV_MAX_STATIONS];
    size_t n;
    double lo, hi;
    int i;

    snprintf(g_dir, sizeof g_dir, "/tmp/sev_sta_XXXXXX");
    if (!mkdtemp(g_dir)) {
        printf("FAIL: mkdtemp\n");
        return 1;
    }

    /* 3 estaciones; comentarios y linea vacia se ignoran. */
    n = sev_stations_load(
        write_file("a.txt",
                   "# sta net chan loc lat lon elev sens\n"
                   "\n"
                   "AAA C HHZ -- -30.0 -70.0 100.0 1.0\n"
                   "BBB C HHZ -- -33.0 -71.0 500.0 1.0\n"
                   "  CCC C HHZ -- -36.0 -72.0 2500.0 1.0\n"),
        st, SEV_MAX_STATIONS);
    CHECK(n == 3, "tres estaciones");
    CHECK(strcmp(st[0].sta, "AAA") == 0, "nombre de la primera");
    CHECK(st[0].lat == -30.0 && st[0].lon == -70.0, "lat/lon");
    CHECK(st[2].elev_m == 2500.0, "elevacion");
    sev_stations_range(st, n, &lo, &hi);
    CHECK(lo == 100.0 && hi == 2500.0, "rango de elevacion");

    /* Archivo vacio: 0 estaciones y fallback 0/1000. */
    n = sev_stations_load(write_file("empty.txt", ""), st, SEV_MAX_STATIONS);
    CHECK(n == 0, "archivo vacio");
    sev_stations_range(st, n, &lo, &hi);
    CHECK(lo == 0.0 && hi == 1000.0, "fallback sin estaciones");

    /* Una sola estacion: min == max => fallback. */
    n = sev_stations_load(
        write_file("one.txt", "AAA C HHZ -- -30.0 -70.0 500.0 1.0\n"),
        st, SEV_MAX_STATIONS);
    CHECK(n == 1, "una estacion");
    sev_stations_range(st, n, &lo, &hi);
    CHECK(lo == 0.0 && hi == 1000.0, "fallback con elevaciones iguales");

    /* Tope 512: 600 lineas se truncan en silencio. */
    {
        FILE *f;
        char p[600];
        snprintf(p, sizeof p, "%s/big.txt", g_dir);
        f = fopen(p, "wb");
        for (i = 0; f && i < 600; i++)
            fprintf(f, "S%03d C HHZ -- -30.0 -70.0 %d.0 1.0\n", i, i);
        if (f)
            fclose(f);
        n = sev_stations_load(p, st, SEV_MAX_STATIONS);
        CHECK(n == SEV_MAX_STATIONS, "tope 512");
    }

    /* Sin archivo: 0 y no aborta. */
    n = sev_stations_load("/no/existe/sev_sta.txt", st, SEV_MAX_STATIONS);
    CHECK(n == 0, "archivo inexistente no aborta");

    {
        char cmd[400];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
        if (system(cmd) != 0)
            printf("nota: no se pudo limpiar %s\n", g_dir);
    }

    if (g_fail == 0)
        printf("OK: estaciones (parser, tope 512 y rango de elevacion)\n");
    return g_fail;
}
