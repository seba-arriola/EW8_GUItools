/* Editor del .d de un modulo (pestana Configuration).
 *
 * Envuelve `ewgui/cfgfile.h` (C puro de common/, la misma que usa el modulo
 * GTK) para no reimplementar el parseo ni el `.bak` + rename atomico. Solo
 * expone las lineas clave/valor (kind == 0): los comentarios y las lineas en
 * blanco se reescriben intactos.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/cfgfile.h"

#include "ecq_capi.h"

struct EcqCfg {
    EwCfgFile file;
    int      *var_line;   /* var -> indice de linea en file.lines */
    int       nvars;
    int       dirty;
    char      path[512];
};

EcqCfg *ecq_cfg_open(const char *cfgfile, char *err, size_t errlen)
{
    EcqCfg *cfg;
    int i;

    if (err && errlen)
        err[0] = '\0';
    if (!cfgfile || !*cfgfile) {
        if (err)
            snprintf(err, errlen, "no hay archivo de configuracion seleccionado");
        return NULL;
    }

    cfg = (EcqCfg *)calloc(1, sizeof(*cfg));
    if (!cfg) {
        if (err)
            snprintf(err, errlen, "sin memoria");
        return NULL;
    }

    ewgui_cfgfile_resolve(cfgfile, cfg->path, sizeof(cfg->path));
    if (ewgui_cfgfile_read(cfg->path, &cfg->file) != 0) {
        if (err)
            snprintf(err, errlen, "Could not read the config file: %s", cfg->path);
        free(cfg);
        return NULL;
    }

    for (i = 0; i < cfg->file.nlines; i++) {
        if (cfg->file.lines[i].kind == 0)
            cfg->nvars++;
    }
    cfg->var_line = (int *)calloc((size_t)(cfg->nvars > 0 ? cfg->nvars : 1), sizeof(int));
    if (!cfg->var_line) {
        ewgui_cfgfile_free(&cfg->file);
        free(cfg);
        if (err)
            snprintf(err, errlen, "sin memoria");
        return NULL;
    }
    cfg->nvars = 0;
    for (i = 0; i < cfg->file.nlines; i++) {
        if (cfg->file.lines[i].kind == 0)
            cfg->var_line[cfg->nvars++] = i;
    }

    return cfg;
}

void ecq_cfg_free(EcqCfg *cfg)
{
    if (!cfg)
        return;
    ewgui_cfgfile_free(&cfg->file);
    free(cfg->var_line);
    free(cfg);
}

int ecq_cfg_nvars(const EcqCfg *cfg)
{
    return cfg ? cfg->nvars : 0;
}

const char *ecq_cfg_key(const EcqCfg *cfg, int var)
{
    if (!cfg || var < 0 || var >= cfg->nvars)
        return "";
    return cfg->file.lines[cfg->var_line[var]].name;
}

const char *ecq_cfg_value(const EcqCfg *cfg, int var)
{
    if (!cfg || var < 0 || var >= cfg->nvars)
        return "";
    return cfg->file.lines[cfg->var_line[var]].value;
}

int ecq_cfg_set(EcqCfg *cfg, int var, const char *value)
{
    EwCfgLine *l;
    char *dup;

    if (!cfg || var < 0 || var >= cfg->nvars || !value)
        return -1;
    l = &cfg->file.lines[cfg->var_line[var]];
    dup = strdup(value);
    if (!dup)
        return -1;
    free(l->value);
    l->value = dup;
    cfg->dirty = 1;
    return 0;
}

int ecq_cfg_dirty(const EcqCfg *cfg)
{
    return cfg ? cfg->dirty : 0;
}

const char *ecq_cfg_path(const EcqCfg *cfg)
{
    return cfg ? cfg->path : "";
}

int ecq_cfg_save(EcqCfg *cfg, char *msg, size_t msglen)
{
    if (msg && msglen)
        msg[0] = '\0';
    if (!cfg)
        return -1;

    if (ewgui_cfgfile_write(&cfg->file) != 0) {
        if (msg)
            snprintf(msg, msglen, "ERROR saving %s", cfg->path);
        return -1;
    }
    cfg->dirty = 0;
    if (msg)
        snprintf(msg, msglen, "Saved to %s (backup .bak)", cfg->path);
    return 0;
}
