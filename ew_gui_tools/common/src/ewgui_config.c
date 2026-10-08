#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kom.h"
#include "ewgui/config.h"

int ewgui_parse_hex_color(const char *s, double rgb[3])
{
    unsigned int r, g, b;

    if (!s || !rgb)
        return -1;
    if (*s == '#')
        s++;
    if (strlen(s) != 6)
        return -1;
    if (sscanf(s, "%2x%2x%2x", &r, &g, &b) != 3)
        return -1;

    rgb[0] = (double)r / 255.0;
    rgb[1] = (double)g / 255.0;
    rgb[2] = (double)b / 255.0;
    return 0;
}

int ewgui_config_load(const char *path, const EwKeySpec *spec, size_t n)
{
    int *found;
    size_t i;
    int rc = 0;

    if (!path || !spec || n == 0)
        return -1;

    found = calloc(n, sizeof(*found));
    if (!found)
        return -1;

    if (!k_open(path)) {
        free(found);
        return -1;
    }

    while (k_rd()) {
        char *com = k_str();
        if (!com)
            continue;

        for (i = 0; i < n; i++) {
            if (!k_its((char *)spec[i].name))
                continue;

            switch (spec[i].type) {
            case EW_KEY_STR: {
                char *s = k_str();
                if (s && spec[i].out && spec[i].size)
                    snprintf((char *)spec[i].out, spec[i].size, "%s", s);
                break;
            }
            case EW_KEY_INT:
                if (spec[i].out)
                    *(int *)spec[i].out = k_int();
                break;
            case EW_KEY_DBL:
                if (spec[i].out)
                    *(double *)spec[i].out = k_val();
                break;
            case EW_KEY_LONG:
                if (spec[i].out)
                    *(long *)spec[i].out = k_long();
                break;
            case EW_KEY_COLOR: {
                char *s = k_str();
                if (s && spec[i].out)
                    ewgui_parse_hex_color(s, (double *)spec[i].out);
                break;
            }
            }
            found[i] = 1;
            break;
        }
    }

    if (k_err()) {
        fprintf(stderr, "ewgui_config: error de parseo en %s\n", path);
        k_close();
        free(found);
        return -1;
    }
    k_close();

    for (i = 0; i < n; i++) {
        if (spec[i].required && !found[i]) {
            fprintf(stderr,
                    "ewgui_config: falta la clave obligatoria '%s' en %s\n",
                    spec[i].name, path);
            rc = -1;
        }
    }

    free(found);
    return rc;
}
