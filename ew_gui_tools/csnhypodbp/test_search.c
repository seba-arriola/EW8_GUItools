/*
 * Test headless del nucleo puro de busqueda hipocentral (criterio C2):
 * parseo de fechas, rangos opcionales, un solo lado, errores y limite.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_search.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static void fields_clear(const char **f)
{
    for (int i = 0; i < CSNHYPODBP_SEARCH_FIELDS; i++)
        f[i] = "";
}

int main(void)
{
    const char *f[CSNHYPODBP_SEARCH_FIELDS];
    CsnhypodbpSearch s;
    char err[128];

    /* 1) sin fechas -> error */
    fields_clear(f);
    CHECK(csnhypodbp_search_build(f, 0, &s, err, sizeof err) == 0,
          "sin fechas -> error");
    CHECK(strstr(err, "obligatorio") != NULL, "mensaje de fecha obligatoria");

    /* 2) solo fechas -> OK, defaults */
    fields_clear(f);
    f[CSNHYPODBP_SEARCH_TMIN] = "2030-01-01";
    f[CSNHYPODBP_SEARCH_TMAX] = "2030-01-02 03:04:05";
    CHECK(csnhypodbp_search_build(f, 0, &s, err, sizeof err) == 1, "solo fechas -> OK");
    CHECK(strcmp(s.time_min, "2030-01-01 00:00:00+00") == 0, "tmin normalizado");
    CHECK(strcmp(s.time_max, "2030-01-02 03:04:05+00") == 0, "tmax normalizado");
    CHECK(!s.have_lat && !s.have_lon && !s.have_depth && !s.have_mag,
          "sin rangos opcionales");
    CHECK(s.limit == CSNHYPODBP_SEARCH_DEFAULT_LIMIT, "limite por defecto");

    /* 3) max_events explicito */
    CHECK(csnhypodbp_search_build(f, 250, &s, err, sizeof err) == 1 && s.limit == 250,
          "limite de config");

    /* 4) fecha invalida (mes 13) */
    f[CSNHYPODBP_SEARCH_TMIN] = "2030-13-01";
    CHECK(csnhypodbp_search_build(f, 0, &s, err, sizeof err) == 0, "mes 13 -> error");
    f[CSNHYPODBP_SEARCH_TMIN] = "2030-01-01";

    /* 5) falta la fecha hasta */
    f[CSNHYPODBP_SEARCH_TMAX] = "";
    CHECK(csnhypodbp_search_build(f, 0, &s, err, sizeof err) == 0, "falta hasta -> error");
    f[CSNHYPODBP_SEARCH_TMAX] = "2030-01-02";

    /* 6) lat min > max */
    f[CSNHYPODBP_SEARCH_LATMIN] = "-30";
    f[CSNHYPODBP_SEARCH_LATMAX] = "-34";
    CHECK(csnhypodbp_search_build(f, 0, &s, err, sizeof err) == 0, "lat min>max -> error");

    /* 7) lat a un solo lado (solo max) */
    f[CSNHYPODBP_SEARCH_LATMIN] = "";
    f[CSNHYPODBP_SEARCH_LATMAX] = "-30";
    CHECK(csnhypodbp_search_build(f, 0, &s, err, sizeof err) == 1, "lat solo max -> OK");
    CHECK(s.have_lat && fabs(s.lat_min + 90.0) < 1e-9 && fabs(s.lat_max + 30.0) < 1e-9,
          "lat completada con cota natural");

    /* 8) magnitud a un solo lado (solo min) */
    fields_clear(f);
    f[CSNHYPODBP_SEARCH_TMIN] = "2030-01-01";
    f[CSNHYPODBP_SEARCH_TMAX] = "2030-12-31";
    f[CSNHYPODBP_SEARCH_MAGMIN] = "5.5";
    CHECK(csnhypodbp_search_build(f, 0, &s, err, sizeof err) == 1, "mag solo min -> OK");
    CHECK(s.have_mag && fabs(s.mag_min - 5.5) < 1e-9 && s.mag_max > 1e5,
          "mag completada con cota natural");

    /* 9) valor no numerico */
    f[CSNHYPODBP_SEARCH_MAGMIN] = "abc";
    CHECK(csnhypodbp_search_build(f, 0, &s, err, sizeof err) == 0, "valor no numerico -> error");

    /* 10) todos los rangos validos */
    fields_clear(f);
    f[CSNHYPODBP_SEARCH_TMIN] = "2030-01-01 00:00:00";
    f[CSNHYPODBP_SEARCH_TMAX] = "2030-12-31 23:59:59";
    f[CSNHYPODBP_SEARCH_LATMIN] = "-40";
    f[CSNHYPODBP_SEARCH_LATMAX] = "-10";
    f[CSNHYPODBP_SEARCH_LONMIN] = "-80";
    f[CSNHYPODBP_SEARCH_LONMAX] = "-60";
    f[CSNHYPODBP_SEARCH_DEPMIN] = "5";
    f[CSNHYPODBP_SEARCH_DEPMAX] = "200";
    f[CSNHYPODBP_SEARCH_MAGMIN] = "3";
    f[CSNHYPODBP_SEARCH_MAGMAX] = "7";
    CHECK(csnhypodbp_search_build(f, 500, &s, err, sizeof err) == 1, "todos los rangos -> OK");
    CHECK(s.have_lat && s.have_lon && s.have_depth && s.have_mag && s.limit == 500,
          "todos los rangos presentes");
    CHECK(fabs(s.depth_min - 5.0) < 1e-9 && fabs(s.depth_max - 200.0) < 1e-9,
          "profundidad parseada");

    if (fails) {
        printf("FALLARON %d comprobaciones\n", fails);
        return 1;
    }
    /* --- fmt_utc: fecha y hora juntas, ISO UTC --- */
    {
        char buf[32];
        csnhypodbp_search_fmt_utc(1600000000, buf, sizeof buf);
        CHECK(strcmp(buf, "2020-09-13 12:26:40") == 0, "fmt_utc epoch conocido");
        csnhypodbp_search_fmt_utc(0, buf, sizeof buf);
        CHECK(strcmp(buf, "1970-01-01 00:00:00") == 0, "fmt_utc epoch 0");
        csnhypodbp_search_fmt_utc(1600000000 + 86400, buf, sizeof buf);
        CHECK(strcmp(buf, "2020-09-14 12:26:40") == 0, "fmt_utc cambio de dia");
    }

    /* --- set_date: conserva la hora, y 00:00:00 si no la traia --- */
    {
        char buf[64];
        const char *fields[CSNHYPODBP_SEARCH_FIELDS];
        CsnhypodbpSearch s;
        char err[128];

        buf[0] = '\0';
        CHECK(csnhypodbp_search_set_date(buf, sizeof buf, 2026, 10, 11) == 1,
              "set_date escribe");
        CHECK(strcmp(buf, "2026-10-11 00:00:00") == 0, "set_date vacio -> 00:00:00");

        snprintf(buf, sizeof buf, "2026-01-05");
        csnhypodbp_search_set_date(buf, sizeof buf, 2026, 10, 11);
        CHECK(strcmp(buf, "2026-10-11 00:00:00") == 0, "set_date solo fecha");

        snprintf(buf, sizeof buf, "2026-01-05 14:30:45");
        csnhypodbp_search_set_date(buf, sizeof buf, 2026, 10, 11);
        CHECK(strcmp(buf, "2026-10-11 14:30:45") == 0, "set_date conserva la hora");

        snprintf(buf, sizeof buf, "basura");
        csnhypodbp_search_set_date(buf, sizeof buf, 2026, 10, 11);
        CHECK(strcmp(buf, "2026-10-11 00:00:00") == 0, "set_date con basura");

        /* Lo que escribe el calendario lo acepta el buscador. */
        for (int i = 0; i < CSNHYPODBP_SEARCH_FIELDS; i++) fields[i] = "";
        fields[0] = buf;   /* TMIN */
        fields[1] = buf;   /* TMAX */
        CHECK(csnhypodbp_search_build(fields, 10, &s, err, sizeof err) == 1,
              "set_date produce un campo valido para search_build");
    }

    printf("ALL SEARCH TESTS PASSED\n");
    return 0;
}
