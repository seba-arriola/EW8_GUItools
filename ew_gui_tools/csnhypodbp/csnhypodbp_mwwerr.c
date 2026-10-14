/*
 * csnhypodbp_mwwerr.c - extrae el primer error real de una corrida del panel Mww.
 *
 * Ver csnhypodbp_mwwerr.h. Logica pura (solo stdio): sin GTK, sin anillos.
 */
#include "csnhypodbp_mwwerr.h"

#include <stdio.h>
#include <string.h>

/* Marcadores de fallo reales del motor y de la orquestacion. */
static const char *markers[] = {
    "ERROR", "Error:", "error:", "FATAL", "fatal",
    "Non-Finite", "no response", "No such file", "no such file",
    "cannot", "Cannot", "abort", "rejected",
    NULL
};

static int line_is_error(const char *line)
{
    int i;

    for (i = 0; markers[i]; i++)
        if (strstr(line, markers[i]))
            return 1;
    /* Cierre de un paso del run.sh: solo es error si su rc NO es 0. */
    if (strstr(line, "##### paso ") && strstr(line, "rc=")
        && !strstr(line, "rc=0"))
        return 1;
    /* El cierre del script solo informa si FALLO (rc=0 es una corrida normal). */
    if (strstr(line, "fin run.sh") && !strstr(line, "(rc=0)"))
        return 1;
    return 0;
}

/* De "##### paso 5: <cmd> #####" saca "<cmd>"; si no tiene esa forma, copia
   la linea tal cual. Asi el panel nombra la ORDEN que fallo, no el envoltorio. */
static void step_cmd_of(const char *line, char *out, size_t n)
{
    const char *p, *q;
    size_t len;

    p = strstr(line, "##### paso ");
    if (!p)
        goto fallback;
    p += strlen("##### paso ");
    p = strchr(p, ':');          /* tras el numero de paso */
    if (!p)
        goto fallback;
    for (p++; *p == ' '; p++)
        ;
    q = strstr(p, " #####");
    len = q ? (size_t)(q - p) : strlen(p);
    if (len == 0 || len >= n)
        goto fallback;
    memcpy(out, p, len);
    out[len] = '\0';
    return;

fallback:
    snprintf(out, n, "%s", line);
}

static int scan_file(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    char line[1024], step[1024];
    int found = 0;

    if (!f)
        return -1;
    step[0] = '\0';
    while (fgets(line, sizeof line, f)) {
        size_t len;

        /* Marca de paso en curso (aun sin rc): recuerda la orden. */
        if (strstr(line, "##### paso ") && !strstr(line, "rc=")) {
            snprintf(step, sizeof step, "%s", line);
            continue;
        }
        if (!line_is_error(line))
            continue;
        len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (!len)
            continue;
        /* Si el fallo es el cierre de un paso, el motivo util es la ORDEN. */
        if (strstr(line, "##### paso ") && strstr(line, "rc=") && step[0]) {
            char cmd[1024], rc[32];
            char *sp;

            step_cmd_of(step, cmd, sizeof cmd);
            snprintf(rc, sizeof rc, "%s", strstr(line, "rc="));
            sp = strchr(rc, ' ');
            if (sp)
                *sp = '\0';
            snprintf(buf, n, "%s (%s)", cmd, rc);
            found = 1;
            break;
        }
        snprintf(buf, n, "%s", line);
        found = 1;
        break;
    }
    fclose(f);
    return found ? 0 : -1;
}

int csnhypodbp_mww_first_error(const char *rundir, char *buf, size_t n)
{
    char path[1200];

    if (!rundir || !rundir[0] || !buf || n == 0)
        return -1;
    buf[0] = '\0';

    snprintf(path, sizeof path, "%s/run.log", rundir);
    if (scan_file(path, buf, n) == 0)
        return 0;

    snprintf(path, sizeof path, "%s/full_output.txt", rundir);
    if (scan_file(path, buf, n) == 0)
        return 0;

    return -1;
}
