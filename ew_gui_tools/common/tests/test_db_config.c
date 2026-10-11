#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ewgui/db.h"

static void write_file(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
    close(fd);
}

int main(void)
{
    const char *path = "/tmp/opencode/ewgui_db_cfg.env";
    const char *content =
        "# credenciales de prueba\n"
        "\n"
        "DB_HOST = 127.0.0.1\n"
        "DB_PORT=5435\n"
        "DB_NAME = \"csn_hypo\"\n"
        "DB_USER='csn'\n"
        "DB_PASSWORD=secreto\n"
        "OTRA_CLAVE=ignorada\n";
    EwGuiDbConfig cfg;

    /* Entorno limpio para que el archivo mande */
    unsetenv("DB_HOST");
    unsetenv("DB_PORT");
    unsetenv("DB_NAME");
    unsetenv("DB_USER");
    unsetenv("DB_PASSWORD");

    write_file(path, content);

    /* --- carga desde archivo (espacios, = sin espacios, comillas) --- */
    assert(ewguidb_config_load(path, &cfg) == 0);
    assert(strcmp(cfg.host, "127.0.0.1") == 0);
    assert(strcmp(cfg.port, "5435") == 0);
    assert(strcmp(cfg.name, "csn_hypo") == 0);
    assert(strcmp(cfg.user, "csn") == 0);
    assert(strcmp(cfg.password, "secreto") == 0);

    /* --- la variable de entorno tiene precedencia --- */
    setenv("DB_HOST", "envhost", 1);
    assert(ewguidb_config_load(path, &cfg) == 0);
    assert(strcmp(cfg.host, "envhost") == 0);
    assert(strcmp(cfg.name, "csn_hypo") == 0);
    unsetenv("DB_HOST");

    /* --- clave obligatoria ausente --- */
    write_file(path, "DB_HOST=127.0.0.1\nDB_PORT=5435\n");
    assert(ewguidb_config_load(path, &cfg) == -1);

    /* --- puerto no numérico --- */
    write_file(path,
               "DB_HOST=h\nDB_PORT=abc\nDB_NAME=n\nDB_USER=u\nDB_PASSWORD=p\n");
    assert(ewguidb_config_load(path, &cfg) == -1);

    /* --- archivo inexistente y sin entorno --- */
    assert(ewguidb_config_load("/tmp/opencode/no_existe_db.env", &cfg) == -1);

    unlink(path);
    return 0;
}
