#include "csnwphase.h"

#include <stdio.h>
#include <string.h>

/*
 * Captura de la salida COMPLETA del motor en un solo texto: es lo que muestra
 * el visor de la GUI y lo que el usuario guarda con `Save…`.
 *
 * Se excluyen a propósito `fort.15*` (volcado muestra a muestra: MB de números)
 * y `SYNTH/` (SAC sintéticos binarios); el run dir completo se empaqueta aparte.
 */

static long dump_file(FILE *out, const char *path, const char *label)
{
    FILE *in = fopen(path, "r");
    char buf[4096];
    size_t n;
    long total = 0;
    int last = '\n';

    fprintf(out, "\n===== %s =====\n", label);

    if (!in) {
        fprintf(out, "(ausente)\n");
        return 0;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
        last = (unsigned char)buf[n - 1];
        total += (long)n;
    }
    fclose(in);

    if (last != '\n')
        fputc('\n', out);

    return total;
}

int csnwphase_log_write(const char *rundir, const char *path)
{
    static const char *files[] = {
        "i_master",
        "CMTSOLUTION",
        "WCMTSOLUTION",
        "WCMTSOLUTION.noth",
        "xy_WCMTSOLUTION",
        "xyz_WCMTSOLUTION",
        "ts_WCMTSOLUTION",
        "LOG/gs_o_wpinversion.log",
        "LOG/wpinversion.noth.log",
        "LOG/wpinversion.log",
        "o_wpinversion",
        "grid_search_ts_out",
        "grid_search_xy_out",
        "grid_search_xyz_out",
    };
    FILE *out;
    char p[1024];
    long total = 0;
    int i;

    if (!rundir || !path)
        return -1;

    out = fopen(path, "w");
    if (!out)
        return -1;

    fprintf(out, "# Inversion de fase W: salida completa del motor\n");
    fprintf(out, "# run dir: %s\n", rundir);
    fprintf(out, "# (los comandos exactos los agrega csnwphase_run)\n");

    for (i = 0; i < (int)(sizeof(files) / sizeof(files[0])); i++) {
        snprintf(p, sizeof(p), "%s/%s", rundir, files[i]);
        total += dump_file(out, p, files[i]);
    }

    if (fclose(out) != 0)
        return -1;

    if (total > 0x7fffffffL)
        return -1;

    return (int)total;
}
