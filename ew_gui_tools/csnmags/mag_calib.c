#include "csnmags.h"

#include <string.h>

/*
 * mag_calib.c - Regionalización de la calibración de magnitudes.
 *
 * `calib_map.txt` declara regiones (bbox) y las tablas que las caracterizan:
 *   <nombre> <latmin> <latmax> <lonmin> <lonmax> <loga0> <q> <stacorr>
 * Los ficheros son rutas relativas al directorio de trabajo ($EW_PARAMS).
 * Las tablas por defecto se cargan de Ml_LogA0Default y Mb_QTable.
 */

int mag_calib_load(const MagConfig *cfg, MagCalib *c)
{
    char  line[512];
    FILE *f;

    memset(c, 0, sizeof(*c));

    /* Tables por defecto */
    tab1d_load("calib/ml_loga0_default.tab", &c->loga0_global);
    if (cfg->mb_q_table[0]) tab1d_load(cfg->mb_q_table, &c->q_global);

    if (!cfg->calib_file[0]) return 0;
    f = fopen(cfg->calib_file, "r");
    if (!f) return 0;

    while (fgets(line, sizeof(line), f)) {
        RegionCalib *r;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (c->n_reg >= 32) break;
        r = &c->regs[c->n_reg];
        memset(r, 0, sizeof(*r));
        if (sscanf(line, "%63s %lf %lf %lf %lf %255s %255s %255s",
                   r->name, &r->latmin, &r->latmax, &r->lonmin, &r->lonmax,
                   r->loga0, r->qtab, r->stacorr) < 5) continue;
        c->n_reg++;
    }
    fclose(f);
    return 0;
}

const RegionCalib *mag_calib_for(const MagCalib *c, double lat, double lon)
{
    int i;
    if (!c) return NULL;
    for (i = 0; i < c->n_reg; i++) {
        const RegionCalib *r = &c->regs[i];
        if (lat >= r->latmin && lat <= r->latmax &&
            lon >= r->lonmin && lon <= r->lonmax)
            return r;
    }
    return NULL;
}

void mag_calib_free(MagCalib *c)
{
    if (c) memset(c, 0, sizeof(*c));
}
