#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ewgui/ctrl.h"

int main(void)
{
    char buf[] =
        "Hostname-OS: testhost\n"
        "Start time (UTC): 2026-10-07 00:00:00\n"
        "Current time (UTC): 2026-10-07 00:01:00\n"
        "Disk space avail: 1000 MB\n"
        "Stopstart Version: v8.0b1\n"
        "name/key/size: CONTROL_RING / 1060 / 1000\n"
        "Process  Process\n"
        "-------\n"
        "csntvp 1234 Alive  run_working_v8/params/csntvp.d\n"
        "startstop 1000 Alive  startstop_unix.d\n";

    EwCtrlStatus st;
    memset(&st, 0, sizeof(st));
    ewgui_ctrl_parse_status(buf, &st);

    assert(st.nmods == 2);
    assert(st.nrings == 1);
    assert(strcmp(st.hostname, "testhost") == 0);
    assert(strcmp(st.version, "v8.0b1") == 0);
    assert(strcmp(st.disk, "1000 MB") == 0);
    assert(strcmp(st.rings[0].name, "CONTROL_RING") == 0);
    assert(st.rings[0].key == 1060);
    assert(st.rings[0].size == 1000);
    assert(strcmp(st.mods[0].name, "csntvp") == 0);
    assert(st.mods[0].pid == 1234);
    assert(strcmp(st.mods[0].status, "Alive") == 0);
    assert(strcmp(st.mods[0].cfgfile, "csntvp.d") == 0);
    assert(strcmp(st.mods[0].config, "csntvp") == 0);
    assert(strcmp(st.mods[1].name, "startstop") == 0);
    assert(strcmp(st.mods[1].cfgfile, "startstop_unix.d") == 0);

    /* helpers */
    {
        char line[] = "Disk space avail: 42 MB";
        char out[64];
        ewgui_ctrl_campo_despues(line, "Disk space avail", out, sizeof(out));
        assert(strcmp(out, "42 MB") == 0);
    }
    {
        char d[] = "run_working_v8/params/csntvp.d";
        char c[64];
        ewgui_ctrl_extraer_config(d, c, sizeof(c));
        assert(strcmp(c, "csntvp") == 0);
        ewgui_ctrl_extraer_cfgfile(d, c, sizeof(c));
        assert(strcmp(c, "csntvp.d") == 0);
    }

    /* ewgui_ctrl_sync_plan: NONE / VALUES / STRUCTURE */
    {
        EwCtrlStatus a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        a.nmods = b.nmods = 2;
        a.nrings = b.nrings = 1;
        snprintf(a.mods[0].name, sizeof(a.mods[0].name), "csntvp");
        a.mods[0].pid = 11; strcpy(a.mods[0].status, "Alive"); strcpy(a.mods[0].detalle, "x");
        snprintf(a.mods[1].name, sizeof(a.mods[1].name), "startstop");
        a.mods[1].pid = 22; strcpy(a.mods[1].status, "Alive");
        snprintf(a.rings[0].name, sizeof(a.rings[0].name), "CONTROL_RING");
        a.rings[0].key = 1060; a.rings[0].size = 1000;
        b = a;

        /* sólo cambian campos volátiles -> NONE */
        strcpy(a.curtime, "t1"); strcpy(a.disk, "100 MB");
        strcpy(b.curtime, "t2"); strcpy(b.disk, "90 MB");
        assert(ewgui_ctrl_sync_plan(&a, &b) == EWCTRL_SYNC_NONE);

        /* cambia un valor visible -> VALUES */
        strcpy(b.mods[0].status, "Stop");
        assert(ewgui_ctrl_sync_plan(&a, &b) == EWCTRL_SYNC_VALUES);

        /* cambia la estructura (pid) -> STRUCTURE */
        b = a;
        b.mods[1].pid = 99;
        assert(ewgui_ctrl_sync_plan(&a, &b) == EWCTRL_SYNC_STRUCTURE);

        /* cambia el número de módulos -> STRUCTURE */
        b = a;
        b.nmods = 1;
        assert(ewgui_ctrl_sync_plan(&a, &b) == EWCTRL_SYNC_STRUCTURE);

        /* cambia un ring -> VALUES */
        b = a;
        b.rings[0].size = 2000;
        assert(ewgui_ctrl_sync_plan(&a, &b) == EWCTRL_SYNC_VALUES);
    }

    printf("ALL CTRL TESTS PASSED\n");
    return 0;
}
