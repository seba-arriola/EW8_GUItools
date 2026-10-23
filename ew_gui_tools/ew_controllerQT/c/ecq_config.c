/* Lectura del .d de ew_controllerQT (MyModuleId/Ring/HeartBeatInt/LogFile/
 * PollInt/LogDir), con la MISMA tabla de claves que ew_controller_main.c:102-111
 * y la misma prioridad de LogDir (ew_controller_main.c:120-129):
 *
 *     $EW_LOG  >  LogDir del .d  >  "logs"
 *
 * La ultima rama evita el "(LogDir not accessible: )" que salia con el valor
 * vacio: un visor de logs sin directorio no tiene nada que ensenar.
 */
#include <stdio.h>
#include <stdlib.h>

#include "ewgui/config.h"

#include "ecq_capi.h"

/* LogDir del .d, tal cual; el efectivo vive en cfg->log_dir. */
#define ECQ_LOG_DIR_RAW 512

EcqConfig *ecq_config_load(const char *dpath, char *err, size_t errlen)
{
    EcqConfig *cfg;
    char logdir_raw[ECQ_LOG_DIR_RAW] = "";
    EwKeySpec spec[] = {
        { "MyModuleId",   EW_KEY_STR, NULL, 0, 1 },
        { "Ring",         EW_KEY_STR, NULL, 0, 1 },
        { "HeartBeatInt", EW_KEY_INT, NULL, 0, 1 },
        { "LogFile",      EW_KEY_INT, NULL, 0, 1 },
        { "PollInt",      EW_KEY_INT, NULL, 0, 1 },
        { "LogDir",       EW_KEY_STR, logdir_raw, sizeof(logdir_raw), 0 },
    };

    if (err && errlen)
        err[0] = '\0';
    if (!dpath || !*dpath) {
        if (err)
            snprintf(err, errlen, "falta el archivo de configuracion (.d)");
        return NULL;
    }

    cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        if (err)
            snprintf(err, errlen, "sin memoria");
        return NULL;
    }
    snprintf(cfg->dpath, sizeof(cfg->dpath), "%s", dpath);

    spec[0].out = cfg->my_module;
    spec[0].size = sizeof(cfg->my_module);
    spec[1].out = cfg->ring;
    spec[1].size = sizeof(cfg->ring);
    spec[2].out = &cfg->heart_beat_int;
    spec[3].out = &cfg->log_file;
    spec[4].out = &cfg->poll_int;

    if (ewgui_config_load(dpath, spec, sizeof(spec) / sizeof(spec[0])) != 0) {
        if (err)
            snprintf(err, errlen, "error leyendo la configuracion <%s>", dpath);
        free(cfg);
        return NULL;
    }

    /* PollInt <= 0 haria un bucle de peticiones sin espera. */
    if (cfg->poll_int <= 0)
        cfg->poll_int = 5;
    if (cfg->heart_beat_int <= 0)
        cfg->heart_beat_int = 30;

    /* LogDir efectivo (misma prioridad que el modulo GTK). */
    {
        const char *ewlog = getenv("EW_LOG");
        if (ewlog && *ewlog)
            snprintf(cfg->log_dir, sizeof(cfg->log_dir), "%s", ewlog);
        else if (logdir_raw[0])
            snprintf(cfg->log_dir, sizeof(cfg->log_dir), "%s", logdir_raw);
        else
            snprintf(cfg->log_dir, sizeof(cfg->log_dir), "logs");
    }

    return cfg;
}

void ecq_config_free(EcqConfig *cfg)
{
    free(cfg);
}

void ecq_print_config(const EcqConfig *cfg, FILE *out)
{
    if (!cfg)
        return;
    if (!out)
        out = stdout;
    fprintf(out, "MyModuleId      %s\n", cfg->my_module);
    fprintf(out, "Ring            %s\n", cfg->ring);
    fprintf(out, "HeartBeatInt    %d\n", cfg->heart_beat_int);
    fprintf(out, "LogFile         %d\n", cfg->log_file);
    fprintf(out, "PollInt         %d\n", cfg->poll_int);
    fprintf(out, "LogDir          %s\n", cfg->log_dir);
}
