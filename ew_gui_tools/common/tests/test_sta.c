#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ewgui/sta.h"

int main(void)
{
    EwStation s;

    /* 8 campos */
    assert(ewgui_sta_parse_line("MT14 BHN 00 -- -30.5 -71.2 450.0 1.5", &s) == 0);
    assert(strcmp(s.sta, "MT14") == 0);
    assert(strcmp(s.net, "BHN") == 0);
    assert(strcmp(s.chan, "00") == 0);
    assert(strcmp(s.loc, "--") == 0);
    assert(s.lat > -30.51 && s.lat < -30.49);
    assert(s.lon > -71.21 && s.lon < -71.19);
    assert(s.elev_m > 449.9 && s.elev_m < 450.1);
    assert(s.sens > 1.49 && s.sens < 1.51);

    /* 7 campos (sin sens) */
    assert(ewgui_sta_parse_line("ABC BHZ 00 -- -20.0 -70.0 100.0", &s) == 0);
    assert(strcmp(s.sta, "ABC") == 0);
    assert(s.elev_m > 99.9 && s.elev_m < 100.1);

    /* insuficientes campos */
    assert(ewgui_sta_parse_line("ABC BHZ 00", &s) == -1);

    /* carga de archivo */
    {
        char tmpl[] = "/tmp/opencode/ewgui_sta_XXXXXX";
        const char *content =
            "# comentario\n"
            "\n"
            "AAA BHZ 00 -- -10.0 -70.0 10.0 1.0\n"
            "   BBB HHZ 00 -- -11.0 -71.0 20.0 1.0\n"
            "linea invalida\n"
            "CCC BHZ 00 -- -12.0 -72.0 30.0 1.0\n";
        EwStation arr[8];
        size_t n;
        int fd = mkstemp(tmpl);
        assert(fd >= 0);
        assert(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
        close(fd);

        n = ewgui_sta_load(tmpl, arr, 8);
        assert(n == 3);
        assert(strcmp(arr[0].sta, "AAA") == 0);
        assert(strcmp(arr[1].sta, "BBB") == 0);
        assert(strcmp(arr[2].sta, "CCC") == 0);
        unlink(tmpl);
    }

    printf("ALL STA TESTS PASSED\n");
    return 0;
}
