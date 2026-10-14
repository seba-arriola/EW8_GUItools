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

    /* Identidad por PID: el nombre NO identifica (dos instancias de csnmags). */
    {
        EwCtrlStatus d;
        char lbl0[128], lbl1[128];
        memset(&d, 0, sizeof(d));
        d.nmods = 2;
        snprintf(d.mods[0].name, sizeof(d.mods[0].name), "csnmags");
        d.mods[0].pid = 200;
        snprintf(d.mods[0].cfgfile, sizeof(d.mods[0].cfgfile), "csnmags.d");
        snprintf(d.mods[1].name, sizeof(d.mods[1].name), "csnmags");
        d.mods[1].pid = 201;
        snprintf(d.mods[1].cfgfile, sizeof(d.mods[1].cfgfile), "csnmags_raw.d");

        assert(ewgui_ctrl_idx_por_pid(&d, 200) == 0);
        assert(ewgui_ctrl_idx_por_pid(&d, 201) == 1);   /* el 2.º, no el 1.º */
        assert(ewgui_ctrl_idx_por_pid(&d, 999) == -1);
        assert(ewgui_ctrl_idx_por_pid(&d, 0) == -1);
        assert(ewgui_ctrl_idx_por_pid(&d, -7) == -1);
        assert(ewgui_ctrl_idx_por_pid(NULL, 200) == -1);

        ewgui_ctrl_etiqueta_modulo(&d, 0, lbl0, sizeof(lbl0));
        ewgui_ctrl_etiqueta_modulo(&d, 1, lbl1, sizeof(lbl1));
        assert(strcmp(lbl0, "csnmags (csnmags.d)") == 0);
        assert(strcmp(lbl1, "csnmags (csnmags_raw.d)") == 0);

        /* nombre único -> etiqueta limpia, sin ruido */
        d.nmods = 1;
        ewgui_ctrl_etiqueta_modulo(&d, 0, lbl0, sizeof(lbl0));
        assert(strcmp(lbl0, "csnmags") == 0);

        /* mismo nombre y mismo .d -> hay que caer al pid */
        d.nmods = 2;
        snprintf(d.mods[1].cfgfile, sizeof(d.mods[1].cfgfile), "csnmags.d");
        ewgui_ctrl_etiqueta_modulo(&d, 1, lbl1, sizeof(lbl1));
        assert(strcmp(lbl1, "csnmags (pid 201)") == 0);

        /* índice fuera de rango -> vacío (no lee basura) */
        lbl0[0] = 'X'; lbl0[1] = '\0';
        ewgui_ctrl_etiqueta_modulo(&d, -1, lbl0, sizeof(lbl0));
        assert(lbl0[0] == '\0');
    }

    /* cfgfile de respaldo: acotado y conservando el sufijo ".d". */
    {
        char longname[96], normal[64], arena[80], small[8], one[1];
        int i;
        snprintf(longname, sizeof(longname),
                 "modulo_con_nombre_larguisimo_que_no_cabe_en_el_buffer_de_64_bytes");

        memset(arena, 'X', sizeof(arena));
        ewgui_ctrl_cfgfile_por_nombre(longname, arena, 64);
        assert(arena[63] == '\0');
        assert(strlen(arena) < 64);
        assert(strlen(arena) >= 2 && strcmp(arena + strlen(arena) - 2, ".d") == 0);
        for (i = 64; i < 80; i++) assert(arena[i] == 'X');   /* canario intacto */

        memset(small, 'X', sizeof(small));
        ewgui_ctrl_cfgfile_por_nombre("csntvp", small, sizeof(small));
        assert(small[sizeof(small) - 1] == '\0');
        assert(strlen(small) < sizeof(small));
        assert(strcmp(small + strlen(small) - 2, ".d") == 0);

        one[0] = 'X';
        ewgui_ctrl_cfgfile_por_nombre("x", one, sizeof(one));
        assert(one[0] == '\0');

        ewgui_ctrl_cfgfile_por_nombre("csntvp", normal, sizeof(normal));
        assert(strcmp(normal, "csntvp.d") == 0);
    }

    printf("ALL CTRL TESTS PASSED\n");
    return 0;
}
