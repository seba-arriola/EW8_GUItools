/*
 * Test headless de la caja `depth` + `Fix` de la barra de picking: parseo y
 * validacion del numero (locale-independente) y formateo. Sin GTK.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_fixdepth.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static int parses(const char *txt, double want)
{
    double km = -1.0;
    return csnhypodbp_fixdepth_parse(txt, &km) == 1 && fabs(km - want) < 1e-9;
}

static int rejects(const char *txt)
{
    double km = 123.0;
    return csnhypodbp_fixdepth_parse(txt, &km) == 0 && km == -1.0;
}

int main(void)
{
    char buf[32];

    CHECK(parses("12.5", 12.5), "acepta punto decimal");
    CHECK(parses("12,5", 12.5), "acepta coma decimal");
    CHECK(parses(" 0 ", 0.0), "acepta 0 con espacios");
    CHECK(parses("1000", 1000.0), "acepta el maximo");
    CHECK(parses("+3.5", 3.5), "acepta signo +");
    CHECK(parses(".5", 0.5), "acepta .5");
    CHECK(parses("33", 33.0), "acepta entero");
    CHECK(parses("33.000", 33.0), "acepta ceros decimales");

    CHECK(rejects(""), "rechaza vacio");
    CHECK(rejects("   "), "rechaza solo espacios");
    CHECK(rejects("-1"), "rechaza negativo");
    CHECK(rejects("1e2"), "rechaza exponente");
    CHECK(rejects("abc"), "rechaza texto");
    CHECK(rejects("1.2.3"), "rechaza dos separadores");
    CHECK(rejects("12."), "rechaza punto sin decimales");
    CHECK(rejects("1001"), "rechaza fuera de rango");
    CHECK(rejects("nan"), "rechaza nan");
    CHECK(rejects("inf"), "rechaza inf");
    CHECK(rejects("12 km"), "rechaza texto sobrante");
    CHECK(rejects(NULL), "rechaza NULL");

    csnhypodbp_fixdepth_format(12.34, buf, sizeof buf);
    CHECK(strcmp(buf, "12.3") == 0, "formatea a 1 decimal");
    csnhypodbp_fixdepth_format(0.0, buf, sizeof buf);
    CHECK(strcmp(buf, "0.0") == 0, "formatea 0");
    csnhypodbp_fixdepth_format(-1.0, buf, sizeof buf);
    CHECK(buf[0] == '\0', "km invalido -> vacio");
    csnhypodbp_fixdepth_format(NAN, buf, sizeof buf);
    CHECK(buf[0] == '\0', "NAN -> vacio");

    if (fails > 0) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_fixdepth\n");
    return 0;
}
