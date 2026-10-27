/* Limpieza de la vista: expulsa solo lo que supera la antiguedad, compacta el
 * array en orden y nunca doble-libera (las estaciones movidas se ponen a cero). */
#include <string.h>

#include "tvq_test.h"

int main(void)
{
    TvqConfig *cfg = tvq_t_cfg(
        "three.sta", "AAA HHZ XX -- 1.0\nBBB HHZ XX -- 1.0\nCCC HHZ XX -- 1.0\n");
    char err[512];
    TvqCore *c;
    int32_t samples[100];
    char buf[8192];
    int len;
    TvqStationInfo si;

    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0, "core con 3");
    T_CHECK(tvq_core_nstations(c) == 3, "n_sta=3");

    /* Solo AAA recibe datos: BBB y CCC "no estan llegando". */
    for (int k = 0; k < 100; k++)
        samples[k] = k;
    len = tvq_t_trace(buf, sizeof buf, "AAA", "HHZ", "XX", "--", "i4", 100.0,
                      tvq_now_epoch() - 1.0, 100, samples);
    T_CHECK(tvq_core_ingest_trace(c, buf, len) == 1, "AAA con datos");

    T_CHECK(tvq_core_clean(c, 0.0) == 0, "clean(0) no quita nada");
    T_CHECK(tvq_core_nstations(c) == 3, "clean(0) deja las 3");

    T_CHECK(tvq_core_clean(c, 3600.0) == 2, "clean(1 h) quita las 2 sin datos");
    T_CHECK(tvq_core_nstations(c) == 1, "queda 1 estacion");
    T_CHECK(tvq_core_station_info(c, 0, &si) == 1 && strcmp(si.sta, "AAA") == 0,
            "la que queda es AAA (y conserva el orden)");
    T_CHECK(si.rate > 99.9, "la que queda conserva sus ondas");

    /* Reload: vuelve la lista del .sta. */
    T_CHECK(tvq_core_reload_stations(c) == 0, "reload del StaFile");
    T_CHECK(tvq_core_nstations(c) == 3, "reload restaura las 3");
    T_CHECK(tvq_core_station_info(c, 2, &si) == 1 && strcmp(si.sta, "CCC") == 0,
            "reload respeta el orden del fichero");
    T_CHECK(si.n_picks == 0, "reload deja las picadas a cero");

    /* Tras el reload nadie tiene datos: clean las quita todas. */
    T_CHECK(tvq_core_clean(c, 60.0) == 3, "clean quita tambien las que nunca llegaron");
    T_CHECK(tvq_core_nstations(c) == 0, "vista vacia");
    T_CHECK(tvq_core_clean(c, 60.0) == 0, "clean sobre vista vacia es inocuo");

    /* Dejar una vista vacia y liberar no debe reventar (doble free). */
    tvq_core_free(c);

    /* Segundo ciclo: limpiar una estacion intermedia conserva la ultima. */
    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0, "core nuevo");
    len = tvq_t_trace(buf, sizeof buf, "AAA", "HHZ", "XX", "--", "i4", 100.0,
                      tvq_now_epoch() - 1.0, 100, samples);
    (void)tvq_core_ingest_trace(c, buf, len);
    len = tvq_t_trace(buf, sizeof buf, "CCC", "HHZ", "XX", "--", "i4", 100.0,
                      tvq_now_epoch() - 1.0, 100, samples);
    (void)tvq_core_ingest_trace(c, buf, len);
    T_CHECK(tvq_core_clean(c, 3600.0) == 1, "clean quita solo la intermedia");
    T_CHECK(tvq_core_nstations(c) == 2, "quedan 2");
    T_CHECK(tvq_core_station_info(c, 0, &si) == 1 && strcmp(si.sta, "AAA") == 0,
            "orden: AAA primero");
    T_CHECK(tvq_core_station_info(c, 1, &si) == 1 && strcmp(si.sta, "CCC") == 0,
            "orden: CCC segundo (compactado)");

    tvq_core_free(c);
    tvq_config_free(cfg);

    printf(tvq_t_fail ? "FALLAS: test_clean\n" : "OK: test_clean\n");
    return tvq_t_fail ? 1 : 0;
}
