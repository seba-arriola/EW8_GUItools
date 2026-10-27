/* Picadas: formato byte-identico al GTK, parseo, eco propio por seq,
 * PickReplaceWindow y el guard de HOLD. */
#include <math.h>
#include <string.h>

#include "csntvp_util.h"

#include "tvq_test.h"

static int near(double a, double b)
{
    return fabs(a - b) < 0.002;
}

int main(void)
{
    char msg[TVQ_PICK_MSG_MAX];
    CsntvpPickMsg pm;
    TvqConfig *cfg = tvq_t_cfg("one.sta", "AAA HHZ XX -- 1.0\n");
    char err[512];
    TvqCore *c;
    TvqStationInfo si;
    int rc;

    /* Un .d real resuelve el id: el core lo lleva desde el arranque. */
    cfg->module_id = 175;

    /* --- 1. formato: mismo texto que csntvp_gui.c:326 -------------------- */
    /* 1700000000.25 = 2023-11-14T22:13:20.250Z */
    rc = tvq_pick_msg_format(msg, sizeof msg, 50, 175, 0, 7, "AAA", "HHZ", "XX",
                             "--", 'P', 1700000000.25);
    T_CHECK(rc == 0, "formatea la picada");
    T_CHECK(strcmp(msg, "50 175 0 7 AAA.HHZ.XX.-- ?0 20231114221320.250 0 0 0 "
                        "P M\n") == 0,
            "mensaje byte-identico al del GTK");

    rc = tvq_pick_msg_format(msg, sizeof msg, 50, 175, 0, 7, "AAA", "HHZ", "XX",
                             "--", 'S', 1700000000.25);
    T_CHECK(rc == 0 && strstr(msg, " S M\n") != NULL, "fase S en el token 11");

    /* --- 2. ida y vuelta con el parser del GTK -------------------------- */
    T_CHECK(csntvp_pick_parse(msg, &pm) == 1, "el parser del GTK la entiende");
    T_CHECK(strcmp(pm.sta, "AAA") == 0 && strcmp(pm.chan, "HHZ") == 0 &&
                strcmp(pm.net, "XX") == 0 && strcmp(pm.loc, "--") == 0,
            "SCNL preservado");
    T_CHECK(pm.phase == 'S' && pm.origin == 'M', "fase/origen preservados");
    T_CHECK(pm.mod == 175 && pm.inst == 0 && pm.seq == 7, "mod/inst/seq");
    T_CHECK(near(pm.t, 1700000000.25), "tiempo preservado");

    /* --- 3. eco propio: upsert por seq, sin duplicar --------------------- */
    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0,
            "core con 1 estacion");

    tvq_core_set_hold(c, 1);
    T_CHECK(tvq_core_place_pick(c, 0, 1000.0, 'P') == 1, "coloca la picada en HOLD");
    T_CHECK(tvq_core_station_info(c, 0, &si) == 1 && si.n_picks == 1,
            "1 picada activa");

    tvq_core_last_pick(c, msg, sizeof msg);
    T_CHECK(strstr(msg, "P M\n") != NULL, "la ultima picada es P manual");
    T_CHECK(strstr(msg, "AAA.HHZ.XX.--") != NULL, "SCNL de la estacion");

    /* eco de la MISMA picada (mod/inst propios = 175/0) */
    {
        char echo[TVQ_PICK_MSG_MAX];

        tvq_pick_msg_format(echo, sizeof echo, 50, 175, 0, 0, "AAA", "HHZ", "XX",
                            "--", 'P', 1000.4);
        T_CHECK(tvq_core_ingest_pick(c, echo, (int)strlen(echo)) == 1,
                "eco propio aceptado");
        T_CHECK(tvq_core_station_info(c, 0, &si) == 1 && si.n_picks == 1,
                "el eco no duplica la picada");
    }

    /* --- 4. PickReplaceWindow ------------------------------------------- */
    T_CHECK(tvq_core_place_pick(c, 0, 1003.0, 'S') == 1, "coloca una S cercana");
    T_CHECK(tvq_core_station_info(c, 0, &si) == 1 && si.n_picks == 1,
            "la picada previa dentro de la ventana se reemplaza");

    cfg->pick_replace_secs = 0.0;
    T_CHECK(tvq_core_place_pick(c, 0, 1100.0, 'P') == 1, "coloca lejos");
    T_CHECK(tvq_core_station_info(c, 0, &si) == 1 && si.n_picks == 2,
            "con PickReplaceWindow 0 conviven");

    /* --- 5. guardas del picado ------------------------------------------ */
    tvq_core_set_hold(c, 0);
    T_CHECK(tvq_core_place_pick(c, 0, 1200.0, 'P') == 0,
            "sin HOLD no se pica");
    tvq_core_set_hold(c, 1);
    T_CHECK(tvq_core_place_pick(c, 5, 1200.0, 'P') == 0, "fila invalida no pica");
    T_CHECK(tvq_core_place_pick(c, 0, 1200.0, 'X') == 0, "fase invalida no pica");
    T_CHECK(tvq_core_place_pick(c, 0, 1200.0, '\0') == 0,
            "P3: sin fase armada no pica");

    /* --- 6. el mensaje publicado lleva el mod del .d (175) -------------- */
    tvq_core_set_hold(c, 0);
    tvq_core_set_hold(c, 1);
    T_CHECK(tvq_core_place_pick(c, 0, 1300.0, 'P') == 1, "pica con mod resuelto");
    tvq_core_last_pick(c, msg, sizeof msg);
    T_CHECK(strstr(msg, " 175 0 ") != NULL, "mod 175 en el mensaje");

    /* --- 7. el modo es persistente: picar no desarma (P7) --------------- */
    tvq_core_set_pick_phase(c, 'S');
    T_CHECK(tvq_core_place_pick(c, 0, 1400.0, tvq_core_pick_phase(c)) == 1,
            "pica con la fase armada");
    T_CHECK(tvq_core_pick_phase(c) == 'S', "P7: picar no desarma el modo");

    tvq_core_free(c);
    tvq_config_free(cfg);

    printf(tvq_t_fail ? "FALLAS: test_pick\n" : "OK: test_pick\n");
    return tvq_t_fail ? 1 : 0;
}
