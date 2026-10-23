/* C7: lectura del .d de ew_controllerQT y --print-config.
 *
 * Mismas claves y misma prioridad de LogDir que ew_controller_main.c:102-129.
 * Test C puro: no necesita Qt ni DISPLAY. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ecq_capi.h"

#ifndef PARAMS_DIR
#define PARAMS_DIR "."
#endif

static int g_fail = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);               \
            g_fail = 1;                                                          \
        }                                                                        \
    } while (0)

static char g_path[256];

/* Escribe un .d de prueba con las claves que se le pasen. */
static const char *write_d(const char *body)
{
    snprintf(g_path, sizeof g_path, "/tmp/ecq_cfg_XXXXXX");
    int fd = mkstemp(g_path);
    if (fd < 0)
        return NULL;
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        return NULL;
    }
    fputs(body, f);
    fclose(f);
    return g_path;
}

static void check_basic()
{
    const char *d = write_d(
        "# comentario\n"
        "MyModuleId   MOD_PROBE\n"
        "Ring         PROBE_RING\n"
        "HeartBeatInt 15\n"
        "LogFile      1\n"
        "PollInt      7\n"
        "LogDir       /var/log/probe\n");
    CHECK(d != NULL, "no se pudo crear el .d temporal");

    unsetenv("EW_LOG");
    char err[512] = "";
    EcqConfig *cfg = ecq_config_load(d, err, sizeof err);
    CHECK(cfg != NULL, "no se pudo leer un .d valido");
    if (cfg) {
        CHECK(strcmp(cfg->my_module, "MOD_PROBE") == 0, "MyModuleId mal leido");
        CHECK(strcmp(cfg->ring, "PROBE_RING") == 0, "Ring mal leido");
        CHECK(cfg->heart_beat_int == 15, "HeartBeatInt mal leido");
        CHECK(cfg->log_file == 1, "LogFile mal leido");
        CHECK(cfg->poll_int == 7, "PollInt mal leido");
        CHECK(strcmp(cfg->log_dir, "/var/log/probe") == 0, "LogDir del .d ignorado");
        ecq_config_free(cfg);
    }
    unlink(d);
}

static void check_ew_log_wins()
{
    const char *d = write_d(
        "MyModuleId   MOD_PROBE\n"
        "Ring         PROBE_RING\n"
        "HeartBeatInt 30\n"
        "LogFile      1\n"
        "PollInt      5\n"
        "LogDir       /var/log/probe\n");
    setenv("EW_LOG", "/tmp/ewlog", 1);
    char err[512] = "";
    EcqConfig *cfg = ecq_config_load(d, err, sizeof err);
    CHECK(cfg != NULL, "no se pudo leer el .d");
    if (cfg) {
        CHECK(strcmp(cfg->log_dir, "/tmp/ewlog") == 0, "EW_LOG no gana sobre el .d");
        ecq_config_free(cfg);
    }
    unsetenv("EW_LOG");
    unlink(d);
}

static void check_logdir_default()
{
    const char *d = write_d(
        "MyModuleId   MOD_PROBE\n"
        "Ring         PROBE_RING\n"
        "HeartBeatInt 30\n"
        "LogFile      1\n"
        "PollInt      5\n");
    unsetenv("EW_LOG");
    char err[512] = "";
    EcqConfig *cfg = ecq_config_load(d, err, sizeof err);
    CHECK(cfg != NULL, "no se pudo leer el .d sin LogDir");
    if (cfg) {
        CHECK(strcmp(cfg->log_dir, "logs") == 0, "sin LogDir ni EW_LOG deberia quedar \"logs\"");
        ecq_config_free(cfg);
    }
    unlink(d);
}

static void check_missing_key()
{
    const char *d = write_d(
        "MyModuleId   MOD_PROBE\n"
        "Ring         PROBE_RING\n");
    char err[512] = "";
    EcqConfig *cfg = ecq_config_load(d, err, sizeof err);
    CHECK(cfg == NULL, "un .d sin claves obligatorias deberia fallar");
    CHECK(strstr(err, d) != NULL, "el error deberia nombrar el fichero");
    if (cfg)
        ecq_config_free(cfg);
    unlink(d);

    cfg = ecq_config_load("/tmp/ecq_no_existe_12345.d", err, sizeof err);
    CHECK(cfg == NULL, "un .d inexistente deberia fallar");
    if (cfg)
        ecq_config_free(cfg);
}

static void check_print_config()
{
    char path[512];
    snprintf(path, sizeof path, "%s/ew_controllerqt.d", PARAMS_DIR);
    char err[512] = "";
    EcqConfig *cfg = ecq_config_load(path, err, sizeof err);
    CHECK(cfg != NULL, "no se pudo leer el ew_controllerqt.d del repo");
    if (!cfg)
        return;

    FILE *out = tmpfile();
    CHECK(out != NULL, "no se pudo abrir tmpfile");
    if (out) {
        ecq_print_config(cfg, out);
        rewind(out);
        char line[256];
        int n = 0;
        int saw_module = 0;
        while (fgets(line, sizeof line, out)) {
            n++;
            if (strncmp(line, "MyModuleId      MOD_EW_CONTROLLERQT", 34) == 0)
                saw_module = 1;
        }
        CHECK(n == 6, "print-config deberia sacar 6 lineas");
        CHECK(saw_module, "print-config no saca MyModuleId MOD_EW_CONTROLLERQT");
        fclose(out);
    }
    ecq_config_free(cfg);
}

int main(void)
{
    check_basic();
    check_ew_log_wins();
    check_logdir_default();
    check_missing_key();
    check_print_config();

    if (g_fail == 0)
        printf("OK: config de ew_controllerQT (claves, LogDir y print-config)\n");
    return g_fail;
}
