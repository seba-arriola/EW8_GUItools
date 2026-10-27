/* Nucleo: tick, reloj de la ventana, HOLD, hit-test, vista y volcado. */
#include <math.h>
#include <string.h>

#include "tvq_test.h"

int main(void)
{
    TvqConfig *cfg = tvq_t_cfg("two.sta", "AAA HHZ XX -- 1.0\nBBB HHZ XX -- 1.0\n");
    char err[512];
    TvqCore *c;
    unsigned ch;
    double now, l0, r0, l1, r1;
    int row = -1;
    double t = 0.0;

    cfg->stations_per_screen = 2;
    cfg->time_window_min = 6;
    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0,
            "core con 2 estaciones");
    T_CHECK(tvq_core_nstations(c) == 2, "n_sta=2");

    /* --- reloj: sin datos, t_right = ahora - 2 s -------------------------- */
    now = 1700000000.0;
    ch = tvq_core_tick(c, now);
    tvq_core_view_range(c, &l0, &r0);
    T_CHECK(fabs(r0 - (now - 2.0)) < 0.01, "t_right = ahora - 2 s");
    T_CHECK(fabs((r0 - l0) - 360.0) < 0.01, "ventana de 6 min");
    T_CHECK((ch & TVQ_CH_TIME) != 0, "el primer tick avisa de cambio de tiempo");

    /* --- tick sin novedades: no anuncia cambios ------------------------- */
    ch = tvq_core_tick(c, now + 0.05);
    T_CHECK((ch & (TVQ_CH_PICKS | TVQ_CH_STATIONS)) == 0,
            "tick tranquilo no anuncia picks/estaciones");

    /* --- siembra: datos frescos ----------------------------------------- */
    T_CHECK(tvq_core_seed_fixture(c, tvq_now_epoch() - 2.0, 20) == 2,
            "siembra las 2 estaciones");
    (void)tvq_core_tick(c, tvq_now_epoch());
    T_CHECK(tvq_core_station_info(c, 0, &(TvqStationInfo){0}) == 1,
            "station_info disponible");
    T_CHECK((tvq_core_tick(c, tvq_now_epoch()) & TVQ_CH_TRACES) != 0 ||
                tvq_core_is_hold(c) == 0,
            "el tick no rompe con datos");

    /* --- HOLD congela ---------------------------------------------------- */
    tvq_core_set_hold(c, 1);
    tvq_core_view_range(c, &l0, &r0);
    (void)tvq_core_tick(c, tvq_now_epoch() + 5.0);
    tvq_core_view_range(c, &l1, &r1);
    T_CHECK(fabs(r1 - r0) < 1e-9, "HOLD congela t_right");
    T_CHECK(fabs(l1 - l0) < 1e-9, "HOLD congela t_left");

    /* --- hit-test -------------------------------------------------------- */
    /* w=400,h=200, sps=2 => alto de pista 100; y=150 => fila 1. */
    T_CHECK(tvq_core_hit(c, 300, 150, 400, 200, 0, &row, &t) == 1,
            "hit dentro de la zona de trazas");
    T_CHECK(row == 1, "hit resuelve la fila");
    T_CHECK(fabs(t - (l0 + (210.0 / 310.0) * 360.0)) < 0.01,
            "hit resuelve el tiempo");
    T_CHECK(tvq_core_hit(c, 10, 150, 400, 200, 0, &row, &t) == 0,
            "el panel izquierdo no pica");
    T_CHECK(tvq_core_hit(c, 399, 199, 400, 200, 0, &row, &t) == 1,
            "el borde derecho sigue siendo traza");

    /* con scroll el mismo y corresponde a otra fila */
    T_CHECK(tvq_core_hit(c, 300, 50, 400, 200, 100, &row, &t) == 1 && row == 1,
            "scroll_y desplaza la fila");

    /* --- vista ----------------------------------------------------------- */
    tvq_core_set_stations_per_screen(c, 4);
    T_CHECK(tvq_core_track_height(c, 200) == 50, "StationsPerScreen 4 -> pista 50");
    T_CHECK(tvq_core_canvas_height(c, 200) == 100, "canvas = n_sta * pista");

    tvq_core_set_time_window_min(c, 2);
    tvq_core_view_range(c, &l1, &r1);
    T_CHECK(fabs((r1 - l1) - 120.0) < 0.01, "TimeWindow 2 -> 120 s");

    tvq_core_set_time_window_min(c, 9999);
    T_CHECK(cfg->time_window_min == 120, "TimeWindow fuera de rango se acota");
    tvq_core_set_stations_per_screen(c, 0);
    T_CHECK(cfg->stations_per_screen == 1, "StationsPerScreen 0 se acota");

    /* --- zoom ------------------------------------------------------------ */
    tvq_core_set_hold(c, 0);
    T_CHECK(fabs(tvq_core_zoom(c) - 1.0) < 1e-9, "al soltar HOLD el zoom vuelve a 1");
    tvq_core_adjust_scale(c, 1000.0);
    T_CHECK(tvq_core_zoom(c) <= TVQ_MAX_ZOOM + 1e-9, "zoom acotado arriba");
    tvq_core_adjust_scale(c, 1e-9);
    T_CHECK(tvq_core_zoom(c) >= TVQ_MIN_ZOOM - 1e-9, "zoom acotado abajo");

    /* --- fase (P1/P2/P8): por defecto NINGUNA; p/s arman, Esc/HOLD desarman */
    T_CHECK(tvq_core_pick_phase(c) == '\0', "P1: sin fase armada por defecto");
    tvq_core_set_pick_phase(c, 'S');
    T_CHECK(tvq_core_pick_phase(c) == 'S', "P2: arma S");
    tvq_core_set_pick_phase(c, 'p');
    T_CHECK(tvq_core_pick_phase(c) == 'P', "P2: arma P (minuscula)");
    tvq_core_set_pick_phase(c, 'z');
    T_CHECK(tvq_core_pick_phase(c) == '\0', "P2: caracter invalido desarma");

    tvq_core_set_hold(c, 1);
    tvq_core_set_pick_phase(c, 'P');
    tvq_core_set_hold(c, 0);
    T_CHECK(tvq_core_pick_phase(c) == '\0', "P8: soltar HOLD desarma");

    /* --- volcado --------------------------------------------------------- */
    {
        FILE *f = tmpfile();

        if (f) {
            char buf[8192];
            size_t n;

            tvq_core_set_hold(c, 1);
            tvq_core_dump_text(c, f);
            rewind(f);
            n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = '\0';
            T_CHECK(strstr(buf, "n_sta=2") != NULL, "dump: n_sta");
            T_CHECK(strstr(buf, "hold=1") != NULL, "dump: hold");
            T_CHECK(strstr(buf, "picks_p=") != NULL, "dump: picks");
            T_CHECK(strstr(buf, "sta=AAA") != NULL, "dump: estaciones");
            T_CHECK(strstr(buf, "pick_phase=none") != NULL,
                    "P4: dump sin fase armada");
            fclose(f);
        }

        f = tmpfile();
        if (f) {
            char buf[8192];
            size_t n;

            tvq_core_set_pick_phase(c, 'P');
            tvq_core_dump_text(c, f);
            rewind(f);
            n = fread(buf, 1, sizeof buf - 1, f);
            buf[n] = '\0';
            T_CHECK(strstr(buf, "pick_phase=P") != NULL,
                    "P4: dump con fase P armada");
            fclose(f);
        }
    }

    tvq_core_free(c);
    tvq_config_free(cfg);

    printf(tvq_t_fail ? "FALLAS: test_core\n" : "OK: test_core\n");
    return tvq_t_fail ? 1 : 0;
}
