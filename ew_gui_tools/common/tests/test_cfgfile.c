#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ewgui/cfgfile.h"

int main(void)
{
    /* parse_line */
    {
        char line[] = "  MyModuleId  MOD_X   # el id";
        EwCfgLine l;
        ewgui_cfgfile_parse_line(line, &l);
        assert(l.kind == 0);
        assert(strcmp(l.prefix, "  ") == 0);
        assert(strcmp(l.name, "MyModuleId") == 0);
        assert(strcmp(l.value, "MOD_X") == 0);
        assert(strcmp(l.comment, "# el id") == 0);
        free(l.prefix); free(l.name); free(l.gap1);
        free(l.value); free(l.gap2); free(l.comment); free(l.raw);
    }
    {
        char line[] = "# solo comentario";
        EwCfgLine l;
        ewgui_cfgfile_parse_line(line, &l);
        assert(l.kind == 1);
        free(l.prefix); free(l.raw);
    }

    /* read / edit / write / re-read */
    {
        char tmpl[] = "/tmp/opencode/ewgui_cfg_XXXXXX";
        const char *content =
            "# cabecera\n"
            "MyModuleId  MOD_X   # id\n"
            "HeartBeatInt 15\n";
        EwCfgFile cf;
        int fd = mkstemp(tmpl);
        assert(fd >= 0);
        assert(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
        close(fd);

        assert(ewgui_cfgfile_read(tmpl, &cf) == 0);
        assert(cf.nlines == 3);
        assert(cf.lines[0].kind == 1);
        assert(strcmp(cf.lines[1].name, "MyModuleId") == 0);
        assert(strcmp(cf.lines[1].value, "MOD_X") == 0);
        assert(strcmp(cf.lines[1].comment, "# id") == 0);

        /* editar el valor conservando el comentario */
        free(cf.lines[1].value);
        cf.lines[1].value = strdup("MOD_Y");
        assert(ewgui_cfgfile_write(&cf) == 0);
        ewgui_cfgfile_free(&cf);

        /* releer y comprobar */
        assert(ewgui_cfgfile_read(tmpl, &cf) == 0);
        assert(strcmp(cf.lines[1].value, "MOD_Y") == 0);
        assert(strcmp(cf.lines[1].comment, "# id") == 0);
        assert(strcmp(cf.lines[2].name, "HeartBeatInt") == 0);
        ewgui_cfgfile_free(&cf);

        unlink(tmpl);
        {
            char bak[600];
            snprintf(bak, sizeof(bak), "%s.bak", tmpl);
            unlink(bak);
        }
    }

    /* resolve */
    {
        char out[600];
        setenv("EW_PARAMS", "/tmp/params", 1);
        ewgui_cfgfile_resolve("csntvp.d", out, sizeof(out));
        assert(strcmp(out, "/tmp/params/csntvp.d") == 0);
        ewgui_cfgfile_resolve("/abs/x.d", out, sizeof(out));
        assert(strcmp(out, "/abs/x.d") == 0);
    }

    printf("ALL CFGFILE TESTS PASSED\n");
    return 0;
}
