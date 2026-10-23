/* C13: resolucion del log por prefijo/mtime y recorte desde borde de linea. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

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

static void touch(const char *name, const char *body, time_t mtime)
{
    char path[600];
    struct utimbuf ut;
    FILE *f;
    size_t i;

    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    f = fopen(path, "wb");
    if (!f) {
        g_fail = 1;
        return;
    }
    if (body)
        fputs(body, f);
    else
        for (i = 0; i < 200; i++)
            fprintf(f, "linea %04u de relleno\n", (unsigned)i);
    fclose(f);

    ut.actime = mtime;
    ut.modtime = mtime;
    utime(path, &ut);
}

int main(void)
{
    char out[600], err[512];
    char path[600];
    const time_t now = time(NULL);

    snprintf(g_dir, sizeof g_dir, "/tmp/ecq_log_XXXXXX");
    if (!mkdtemp(g_dir)) {
        printf("FAIL: no se pudo crear el directorio temporal\n");
        return 1;
    }

    touch("probe_1.log", "viejo\n", now - 100);
    touch("probe_2.log", "nuevo\n", now);
    touch("probe_notes.txt", "no es log\n", now + 10);
    touch("startstop_9.log", "startstop\n", now + 20);

    /* El mas reciente entre los <prefijo>_*.log. */
    CHECK(ecq_log_path(g_dir, "probe", "probe", out, sizeof out, err, sizeof err) == 0,
          "no se encontro el log de probe");
    CHECK(strstr(out, "probe_2.log") != NULL, "no se eligio el log de mayor mtime");

    /* `config` vacio => se usa el nombre. */
    CHECK(ecq_log_path(g_dir, "probe", "", out, sizeof out, err, sizeof err) == 0,
          "sin config deberia usar el nombre");
    CHECK(strstr(out, "probe_2.log") != NULL, "sin config eligio otro fichero");

    /* startstop es literal aunque el .d se llame startstop_unix.d. */
    CHECK(ecq_log_path(g_dir, "startstop", "startstop_unix", out, sizeof out,
                       err, sizeof err) == 0,
          "no se encontro el log de startstop");
    CHECK(strstr(out, "startstop_9.log") != NULL, "startstop deberia ser literal");

    /* El prefijo se compara SIN distinguir mayusculas: el log propio del panel
     * es `ew_controllerQT_*.log` (nombre del proceso) mientras el prefijo sale
     * del .d, `ew_controllerqt`. */
    touch("Upper_Mod_1.log", "mayus\n", now + 5);
    CHECK(ecq_log_path(g_dir, "upper_mod", "upper_mod", out, sizeof out,
                       err, sizeof err) == 0,
          "el prefijo deberia casar sin distinguir mayusculas");
    CHECK(strstr(out, "Upper_Mod_1.log") != NULL,
          "no se eligio el log escrito con otras mayusculas");

    /* Directorio inaccesible / sin ficheros: mensajes exactos del GTK. */
    CHECK(ecq_log_path("/no/existe/ecq", "probe", "probe", out, sizeof out,
                       err, sizeof err) == -1, "un LogDir inexistente deberia fallar");
    CHECK(strncmp(err, "(LogDir not accessible: ", 24) == 0,
          "el mensaje de LogDir inaccesible no es el del GTK");
    CHECK(ecq_log_path(g_dir, "no_hay", "no_hay", out, sizeof out,
                       err, sizeof err) == -1, "sin ficheros deberia fallar");
    CHECK(strcmp(err, "(no log file)") == 0, "el mensaje sin fichero no es el del GTK");

    /* Tail: 40 bytes desde un borde de linea. */
    snprintf(path, sizeof path, "%s/probe_2.log", g_dir);
    touch("probe_2.log", NULL, now);
    {
        long len = 0;
        char *tail = ecq_log_tail(path, 40, &len);
        CHECK(tail != NULL, "ecq_log_tail devolvio NULL con un fichero valido");
        if (tail) {
            CHECK(len > 0 && (size_t)len == strlen(tail), "len no coincide con el texto");
            CHECK(strncmp(tail, "linea 0", 7) == 0,
                  "el recorte no empieza en un borde de linea");
            /* 40 B caen a media linea 198: se descarta y queda solo la 199. */
            CHECK(strcmp(tail, "linea 0199 de relleno\n") == 0,
                  "el recorte no es una linea completa");
            ecq_free(tail);
        }

        /* Con sitio para dos lineas, las dos enteras. */
        tail = ecq_log_tail(path, 60, &len);
        CHECK(tail != NULL, "ecq_log_tail(60) devolvio NULL");
        if (tail) {
            CHECK(strcmp(tail, "linea 0198 de relleno\nlinea 0199 de relleno\n") == 0,
                  "el recorte de 60 B no trae dos lineas completas");
            ecq_free(tail);
        }
    }
    CHECK(ecq_log_tail("/no/existe.log", 40, NULL) == NULL,
          "un fichero ausente deberia dar NULL");

    {
        char cmd[400];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
        if (system(cmd) != 0)
            printf("nota: no se pudo limpiar %s\n", g_dir);
    }

    if (g_fail == 0)
        printf("OK: log por prefijo/mtime y tail desde borde de linea\n");
    return g_fail;
}
