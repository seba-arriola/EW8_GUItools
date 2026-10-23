/* Resolucion y recorte del log de un modulo (pestana Logs).
 *
 * Misma regla que `ew_controller_gui.c:349-384`:
 *   - el prefijo sale del campo `config` del TYPE_STATUS (basename sin
 *     extension), o del nombre si viene vacio; `startstop` se queda literal
 *     porque su .d es `startstop_unix.d` y su log empieza por `startstop_`;
 *   - de los `<prefijo>_*.log` del LogDir gana el de mayor mtime; el prefijo se
 *     compara SIN distinguir mayusculas, porque el log propio del panel se
 *     llama `ew_controllerQT_*.log` (nombre del proceso) mientras el prefijo
 *     sale del .d (`ew_controllerqt`);
 *   - sin directorio o sin fichero, el motivo se devuelve en `err` para que la
 *     UI lo pinte tal cual ("(LogDir not accessible: X)" / "(no log file)").
 *
 * `tail_file()` del GTK (ew_controller_gui.c:736-754) lee los ultimos N bytes y
 * empieza en un borde de linea; aqui igual.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "ecq_capi.h"

static void base_prefix(const char *name, const char *config, char *out, size_t n)
{
    const char *base = (config && *config) ? config : name;
    const char *slash;

    if (name && strcmp(name, "startstop") == 0)
        base = "startstop";
    if (!base)
        base = "";
    /* basename por si el campo trae ruta. */
    slash = strrchr(base, '/');
    if (slash)
        base = slash + 1;
    snprintf(out, n, "%s_", base);
}

int ecq_log_path(const char *logdir, const char *name, const char *config,
                 char *out, size_t outlen, char *err, size_t errlen)
{
    DIR *d;
    struct dirent *e;
    struct stat st;
    char prefix[160];
    char best[600];
    time_t best_t = 0;
    int found = 0;

    if (out && outlen)
        out[0] = '\0';
    if (err && errlen)
        err[0] = '\0';

    if (!logdir || !*logdir) {
        if (err)
            snprintf(err, errlen, "(LogDir not accessible: )");
        return -1;
    }
    d = opendir(logdir);
    if (!d) {
        if (err)
            snprintf(err, errlen, "(LogDir not accessible: %s)", logdir);
        return -1;
    }

    base_prefix(name, config, prefix, sizeof(prefix));

    while ((e = readdir(d)) != NULL) {
        char path[600];
        const size_t plen = strlen(prefix);

        if (strncasecmp(e->d_name, prefix, plen) != 0)
            continue;
        if (!strstr(e->d_name, ".log"))
            continue;
        snprintf(path, sizeof(path), "%s/%s", logdir, e->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        /* >= para que dos ficheros del mismo segundo resuelvan al ultimo leido
         * (mismo criterio que el scan del GTK). */
        if (!found || st.st_mtime >= best_t) {
            best_t = st.st_mtime;
            snprintf(best, sizeof(best), "%s", path);
            found = 1;
        }
    }
    closedir(d);

    if (!found) {
        if (err)
            snprintf(err, errlen, "(no log file)");
        return -1;
    }
    if (out)
        snprintf(out, outlen, "%s", best);
    return 0;
}

char *ecq_log_tail(const char *path, long maxbytes, long *len)
{
    FILE *f;
    long size, start, n;
    char *buf;

    if (len)
        *len = 0;
    if (!path || !*path)
        return NULL;
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0)
        size = 0;
    start = (maxbytes > 0 && size > maxbytes) ? size - maxbytes : 0;
    if (fseek(f, start, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    n = size - start;
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    n = (long)fread(buf, 1, (size_t)n, f);
    fclose(f);

    /* Empezar en borde de linea: se descarta la primera linea partida. */
    if (start > 0 && n > 0) {
        char *nl = (char *)memchr(buf, '\n', (size_t)n);
        if (nl) {
            n -= (long)(nl + 1 - buf);
            memmove(buf, nl + 1, (size_t)n);
        } else {
            n = 0;
        }
    }
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}

void ecq_free(void *p)
{
    free(p);
}
