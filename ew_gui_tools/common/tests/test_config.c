#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ewgui/config.h"

int main(void)
{
    char tmpl[] = "/tmp/opencode/ewgui_cfg_XXXXXX";
    const char *content =
        "# archivo de prueba\n"
        "MyModuleId  MOD_CSNTVP\n"
        "HeartBeatInt 15\n"
        "InitialZoom 8.0\n"
        "waveformsColor #00FF00\n";
    char mod[32] = {0};
    int hb = 0;
    double zoom = 0.0;
    double wcolor[3] = {0, 0, 0};
    EwKeySpec spec[] = {
        { "MyModuleId", EW_KEY_STR, mod, sizeof(mod), 1 },
        { "HeartBeatInt", EW_KEY_INT, &hb, 0, 1 },
        { "InitialZoom", EW_KEY_DBL, &zoom, 0, 0 },
        { "waveformsColor", EW_KEY_COLOR, wcolor, 0, 0 },
    };
    char missing[32] = {0};
    EwKeySpec spec_missing[] = {
        { "NoExiste", EW_KEY_STR, missing, sizeof(missing), 1 },
    };
    double rgb[3];
    int fd;

    /* --- color hex --- */
    assert(ewgui_parse_hex_color("#FF8000", rgb) == 0);
    assert(rgb[0] > 0.99 && rgb[1] > 0.49 && rgb[1] < 0.51 && rgb[2] < 0.01);
    assert(ewgui_parse_hex_color("00FF00", rgb) == 0);
    assert(rgb[1] > 0.99);
    assert(ewgui_parse_hex_color("#12345", rgb) == -1);
    assert(ewgui_parse_hex_color("#zzzzzz", rgb) == -1);

    /* --- carga de .d --- */
    fd = mkstemp(tmpl);
    assert(fd >= 0);
    assert(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
    close(fd);

    assert(ewgui_config_load(tmpl, spec, 4) == 0);
    assert(strcmp(mod, "MOD_CSNTVP") == 0);
    assert(hb == 15);
    assert(zoom > 7.99 && zoom < 8.01);
    assert(wcolor[1] > 0.99 && wcolor[0] < 0.01 && wcolor[2] < 0.01);

    /* --- clave obligatoria ausente --- */
    assert(ewgui_config_load(tmpl, spec_missing, 1) == -1);

    /* --- archivo inexistente --- */
    assert(ewgui_config_load("/tmp/opencode/no_existe_ewgui.d", spec, 4) == -1);

    unlink(tmpl);
    return 0;
}
