/* C2: log propio (clave LogFile) con nombre del PROCESO, no del .d. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "sev_capi.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);             \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

static char g_dir[256];

/* Sin EW_LOG escribible `logit_init` haria exit(-1)/exit(0). Se prueba en un
 * hijo para distinguir el 42 que pone este codigo. */
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
        const int flag = sev_log_init("x.d", 1);
        sev_logf("no debe escribirse\n");
        _exit(flag == 0 ? 42 : 9);
    }
    CHECK(pid > 0, "fork");
    if (pid <= 0)
        return;
    CHECK(waitpid(pid, &st, 0) == pid, "waitpid");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 42, what);
}

int main(void)
{
    char path[700], buf[4096], stamp[16], cmd[400];
    const time_t now = time(NULL);
    struct tm tm;
    FILE *f;
    size_t n;

    expect_console_only(NULL, "sin EW_LOG debe quedar en consola, no morir");
    expect_console_only("/no/existe/sev_logit",
                        "sin EW_LOG escribible debe quedar en consola");

    snprintf(g_dir, sizeof g_dir, "/tmp/sev_logit_XXXXXX");
    if (!mkdtemp(g_dir)) {
        printf("FAIL: mkdtemp\n");
        return 1;
    }
    setenv("EW_LOG", g_dir, 1);
    CHECK(sev_log_init("csnstaevdisp.d", 1) == 1, "LogFile 1 abre el fichero");
    sev_logf("prueba-logit %d\n", 7);

    gmtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%Y%m%d", &tm);
    snprintf(path, sizeof path, "%s/csnstaevdispQT_%s.log", g_dir, stamp);

    f = fopen(path, "rb");
    CHECK(f != NULL, "no se creo csnstaevdispQT_<fecha>.log");
    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = '\0';
        fclose(f);
        CHECK(strstr(buf, "startup at UTC_") != NULL, "banner de logit_init");
        CHECK(strstr(buf, "prueba-logit 7") != NULL, "sev_logf escribe");
    }

    CHECK(sev_log_init("csnstaevdisp.d", 0) == 1,
          "un 2.º init no cambia el flag aplicado");
    setenv("EW_LOG", "/no/existe/sev_logit", 1);
    CHECK(sev_log_init("csnstaevdisp.d", 1) == 1,
          "un 2.º init no reabre ni apaga");

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
    if (system(cmd) != 0)
        printf("nota: no se pudo limpiar %s\n", g_dir);

    if (g_fail == 0)
        printf("OK: log propio por LogFile, nombre del proceso y sin EW_LOG fatal\n");
    return g_fail;
}
