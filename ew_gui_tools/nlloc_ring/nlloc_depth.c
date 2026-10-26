#include "nlloc_depth.h"

#include <stdio.h>
#include <string.h>

int nlloc_locgrid_fixdepth(const char *in, char *out, size_t outn,
                           double depth_km, double spacing_km)
{
    int    nx, ny, nz;
    double x0, y0, z0, dx, dy, dz;
    char   rest[160] = "";
    int    n;

    if (!in || !out || outn == 0) return -1;
    if (!(spacing_km > 0.0)) return -1;
    if (!(depth_km >= 0.0)) return -1;

    n = sscanf(in, "LOCGRID %d %d %d %lf %lf %lf %lf %lf %lf%159[^\n]",
               &nx, &ny, &nz, &x0, &y0, &z0, &dx, &dy, &dz, rest);
    if (n < 9 || nx < 1 || ny < 1) return -1;

    snprintf(out, outn, "LOCGRID %d %d 2 %.4f %.4f %.4f %.4f %.4f %.4f%s\n",
             nx, ny, x0, y0,
             depth_km - 0.5 * spacing_km,
             dx, dy, spacing_km, rest);
    return 0;
}

int nlloc_locsearch_fixz(const char *in, char *out, size_t outn)
{
    char kind[16];
    int  nx, ny, nz;
    char rest[512] = "";

    if (!in || !out || outn == 0) return -1;

    if (sscanf(in, "LOCSEARCH %15s %d %d %d%511[^\n]",
               kind, &nx, &ny, &nz, rest) == 5 &&
        strcmp(kind, "OCT") == 0) {
        snprintf(out, outn, "LOCSEARCH %s %d %d 1%s\n", kind, nx, ny, rest);
        return 0;
    }
    snprintf(out, outn, "%s", in);
    return 0;
}

int nlloc_locsearch_set(const char *in, const char *spec, char *out, size_t outn)
{
    char   kind[16];
    int    nx, ny, nz, maxn, nsc, dens, stop;
    double minn;

    if (!in || !spec || !out || outn == 0 || !spec[0]) return -1;
    if (strncmp(in, "LOCSEARCH", 9) != 0) return -1;
    if (strchr(spec, '\n') != NULL) return -1;
    if (sscanf(spec, "%15s %d %d %d %lf %d %d %d %d",
               kind, &nx, &ny, &nz, &minn, &maxn, &nsc, &dens, &stop) != 9)
        return -1;
    if (strcmp(kind, "OCT") != 0 || nx <= 0 || ny <= 0 || nz <= 0 ||
        !(minn > 0.0) || maxn <= 0 || nsc < 0 || dens < 0 || dens > 4 ||
        (stop != 0 && stop != 1))
        return -1;
    snprintf(out, outn, "LOCSEARCH %s\n", spec);
    return 0;
}
