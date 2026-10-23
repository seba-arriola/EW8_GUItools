/* C12: editor del .d (pestana Configuration).
 *
 * Lo que hay que preservar: los comentarios, la sangria y el orden de las
 * lineas; y el `.bak` exacto del original antes de escribir. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ecq_capi.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);              \
            g_fail = 1;                                                          \
        }                                                                        \
    } while (0)

static char g_dir[256];
static char g_path[512];
static char g_orig[2048];

static const char *kBody =
    "# probe.d - fixture\n"
    "\n"
    "MyModuleId      MOD_PROBE     # id del modulo\n"
    "PollInt         5\n"
    "LogDir         \"logs\"\n"
    "  HeartBeatInt 30             # sangria y comentario\n";

static int slurp(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "rb");
    size_t got;

    if (!f)
        return -1;
    got = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[got] = '\0';
    return (int)got;
}

static void make_fixture(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/ecq_cfg_XXXXXX");
    if (!mkdtemp(g_dir)) {
        printf("FAIL: no se pudo crear el directorio temporal\n");
        g_fail = 1;
        return;
    }
    snprintf(g_path, sizeof(g_path), "%s/probe.d", g_dir);
    FILE *f = fopen(g_path, "wb");
    if (!f) {
        g_fail = 1;
        return;
    }
    fputs(kBody, f);
    fclose(f);
    slurp(g_path, g_orig, sizeof(g_orig));
}

int main(void)
{
    char err[512] = "";
    char msg[512] = "";
    EcqCfg *cfg;
    int i, mymod = -1, poll = -1;

    make_fixture();
    if (g_fail)
        return 1;

    /* EW_PARAMS hace de raiz para el nombre relativo del .d. */
    setenv("EW_PARAMS", g_dir, 1);

    cfg = ecq_cfg_open("probe.d", err, sizeof err);
    CHECK(cfg != NULL, "no se pudo abrir el .d de prueba");
    if (!cfg) {
        printf("  err=%s\n", err);
        return 1;
    }
    CHECK(ecq_cfg_nvars(cfg) == 4, "deberia haber 4 lineas clave/valor");
    CHECK(ecq_cfg_dirty(cfg) == 0, "recien abierto no esta dirty");
    CHECK(strstr(ecq_cfg_path(cfg), "probe.d") != NULL, "la ruta resuelta no nombra el .d");

    for (i = 0; i < ecq_cfg_nvars(cfg); i++) {
        if (strcmp(ecq_cfg_key(cfg, i), "MyModuleId") == 0)
            mymod = i;
        if (strcmp(ecq_cfg_key(cfg, i), "PollInt") == 0)
            poll = i;
    }
    CHECK(mymod >= 0, "no se expuso MyModuleId");
    CHECK(poll >= 0, "no se expuso PollInt");
    CHECK(strcmp(ecq_cfg_value(cfg, mymod), "MOD_PROBE") == 0, "valor de MyModuleId mal");
    CHECK(strcmp(ecq_cfg_value(cfg, poll), "5") == 0, "valor de PollInt mal");

    /* Cambiar un valor marca dirty y se refleja. */
    CHECK(ecq_cfg_set(cfg, poll, "9") == 0, "ecq_cfg_set fallo");
    CHECK(ecq_cfg_dirty(cfg) == 1, "tras set deberia estar dirty");
    CHECK(strcmp(ecq_cfg_value(cfg, poll), "9") == 0, "el valor nuevo no se lee");

    CHECK(ecq_cfg_save(cfg, msg, sizeof msg) == 0, "ecq_cfg_save fallo");
    CHECK(ecq_cfg_dirty(cfg) == 0, "tras guardar no debe quedar dirty");
    CHECK(strstr(msg, "Saved to ") && strstr(msg, "(backup .bak)"),
          "el mensaje de guardado no es el del modulo GTK");

    /* El .bak es el original byte a byte. */
    {
        char bak[520];
        char buf[2048];
        snprintf(bak, sizeof bak, "%s.bak", g_path);
        CHECK(slurp(bak, buf, sizeof buf) >= 0, "no se creo el .bak");
        CHECK(strcmp(buf, g_orig) == 0, "el .bak no es el original byte a byte");
    }

    /* El fichero nuevo conserva comentarios, sangria y orden; solo cambia el valor. */
    {
        char buf[2048];
        CHECK(slurp(g_path, buf, sizeof buf) >= 0, "no se pudo leer el .d guardado");
        CHECK(strstr(buf, "# probe.d - fixture") != NULL, "se perdio el comentario de cabecera");
        CHECK(strstr(buf, "MyModuleId      MOD_PROBE     # id del modulo") != NULL,
              "se reformateo la linea de MyModuleId");
        CHECK(strstr(buf, "PollInt         9\n") != NULL, "no se escribio el valor nuevo");
        CHECK(strstr(buf, "  HeartBeatInt 30             # sangria y comentario") != NULL,
              "se perdio la sangria/comentario de HeartBeatInt");
        CHECK(strstr(buf, "LogDir         \"logs\"") != NULL, "se perdieron las comillas");
    }

    ecq_cfg_free(cfg);

    /* Un .d inexistente: NULL y el mensaje del GTK. */
    err[0] = '\0';
    cfg = ecq_cfg_open("no-existe.d", err, sizeof err);
    CHECK(cfg == NULL, "abrir un .d inexistente deberia fallar");
    CHECK(strstr(err, "Could not read the config file:") != NULL,
          "el error deberia ser el del modulo GTK");
    if (cfg)
        ecq_cfg_free(cfg);

    /* Limpieza. */
    {
        char cmd[600];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
        if (system(cmd) != 0)
            printf("nota: no se pudo limpiar %s\n", g_dir);
    }

    if (g_fail == 0)
        printf("OK: editor del .d (valores, dirty, .bak y formato)\n");
    return g_fail;
}
