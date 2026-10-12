/* Comparacion de celdas de tabla por su TEXTO (lo que se ve).
 *
 * Las tablas de los modulos pintan cada celda como texto, asi que el orden se
 * define sobre ese texto:
 *   - si AMBOS valores son numeros completos, se comparan como numeros: "10" va
 *     despues de "5.2" (alfabeticamente seria al reves). Un valor con cola no
 *     numerica (p.ej. "5.2-10" = magnitud-nest) NO es numero y cae al caso
 *     alfabetico, por eso las columnas de magnitud ordenan por su propiedad
 *     numerica (`sort_prop`), no por el texto;
 *   - si no, alfabeticamente sin distinguir mayusculas (las fechas ISO
 *     "YYYY-MM-DD HH:MM:SS" ordenan solas);
 *   - un valor ausente ("", "-") no es numero y queda antes de los numeros en
 *     ascendente, o sea al final en descendente.
 *
 * Pura, sin GTK (se testea headless en tests/test_table_cmp.c). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ewgui/table_ui.h"

/* 1 si `s` es un numero completo (strtod consume todo salvo espacios). */
static int num_of(const char *s, double *out)
{
    char *end;
    double v;

    if (!s)
        return 0;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return 0;
    v = strtod(s, &end);
    if (end == s)
        return 0;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end)
        return 0;
    *out = v;
    return 1;
}

int ewgui_table_cmp_values(const char *a, const char *b)
{
    double da, db;

    if (!a)
        a = "";
    if (!b)
        b = "";
    if (num_of(a, &da) && num_of(b, &db)) {
        if (da < db)
            return -1;
        if (da > db)
            return 1;
        return 0;
    }
    return strcasecmp(a, b);
}
