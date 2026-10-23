/* C14: log propio del modulo (clave `LogFile` del .d).
 *
 * Fija las tres propiedades que se pagarian caro:
 *   - sin `EW_LOG` escribible el arranque NO muere (`logit_init` haria `exit`)
 *     y el modulo se queda solo en consola;
 *   - el fichero se llama `ew_controllerQT_<fecha>.log` (nombre del PROCESO):
 *     es el literal que busca `diag.sh log ew_controllerQT`, no el `.d`;
 *   - un segundo init es inerte: `logit_init` con logflag 0 re-`fclose`a el
 *     FILE* y el segundo `fclose` aborta con `free(): double free`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
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

/* Arranque sin log escribible, en un HIJO: si la guarda faltase, `logit_init`
 * haria `exit()` y el estado NO seria 42 (salia 255 sin EW_LOG y 0 si no puede
 * abrir el fichero); los dos se distinguen del 42 que pone este codigo. */
static void expect_console_only(const char *ewlog, const char *what)
{
    pid_t pid;
    int st = 0;

    if (ewlog)
        setenv("EW_LOG", ewlog, 1);
    else
        unsetenv("EW_LOG");

    pid = fork();
    if (pid == 0) {
        const int flag = ecq_log_init(1);
        ecq_logf("esto no debe escribirse\n");
        _exit(flag == 0 ? 42 : 9);
    }
    CHECK(pid > 0, "no se pudo hacer fork");
    if (pid <= 0)
        return;
    CHECK(waitpid(pid, &st, 0) == pid, "waitpid fallo");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 42, what);
}

static void check_startup_guards(void)
{
    expect_console_only(NULL,
                        "sin EW_LOG el arranque debe quedar en consola, no morir");
    expect_console_only("/no/existe/ecq_logit",
                        "sin EW_LOG escribible debe quedar en consola, no morir");
}

/* Primer init del proceso padre: crea el fichero y escribe. */
static void check_creates_own_file(void)
{
    char path[600], buf[4096], stamp[16];
    const time_t now = time(NULL);
    struct tm tm;
    FILE *f;
    size_t n;

    snprintf(g_dir, sizeof g_dir, "/tmp/ecq_logit_XXXXXX");
    if (!mkdtemp(g_dir)) {
        printf("FAIL: no se pudo crear el directorio temporal\n");
        g_fail = 1;
        return;
    }
    setenv("EW_LOG", g_dir, 1);

    CHECK(ecq_log_init(1) == 1,
          "con EW_LOG escribible, LogFile 1 debe abrir el fichero");
    ecq_logf("prueba-logit %d\n", 7);

    gmtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%Y%m%d", &tm);
    snprintf(path, sizeof path, "%s/ew_controllerQT_%s.log", g_dir, stamp);

    f = fopen(path, "rb");
    CHECK(f != NULL,
          "no se creo ew_controllerQT_<fecha>.log (nombre del proceso, no del .d)");
    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = '\0';
        fclose(f);
        CHECK(strstr(buf, "startup at UTC_") != NULL,
              "falta el banner que escribe logit_init");
        CHECK(strstr(buf, "prueba-logit 7") != NULL,
              "ecq_logf no llego al fichero");
    }
}

/* Un 2.º init no debe llamar a logit_init: con logflag 0 re-fclosea el FILE*
 * (logit_common.c no lo pone a NULL) y el siguiente fclose aborta. */
static void check_reinit_is_inert(void)
{
    CHECK(ecq_log_init(0) == 1, "un 2.º init no debe cambiar el flag aplicado");
    setenv("EW_LOG", "/no/existe/ecq_logit", 1);
    CHECK(ecq_log_init(1) == 1, "un 2.º init no debe reabrir ni apagar nada");
}

int main(void)
{
    char cmd[400];

    check_startup_guards();
    check_creates_own_file();
    check_reinit_is_inert();

    if (g_dir[0]) {
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
        if (system(cmd) != 0)
            printf("nota: no se pudo limpiar %s\n", g_dir);
    }

    if (g_fail == 0)
        printf("OK: log propio por LogFile, con nombre del proceso y sin EW_LOG fatal\n");
    return g_fail;
}
