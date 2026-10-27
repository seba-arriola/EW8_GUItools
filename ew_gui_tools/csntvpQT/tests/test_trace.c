/* Trazas: buffer circular por (estacion, canal), indice absoluto, huecos y las
 * guardas del port (estacion no listada, TRACE2 truncado, i2). */
#include <string.h>

#include "tvq_test.h"

int main(void)
{
    TvqConfig *cfg = tvq_t_cfg("one.sta", "AAA HHZ XX -- 1.0\n");
    char err[512];
    TvqCore *c;
    int32_t samples[100];
    char buf[8192];
    int len;
    int v = 0;
    TvqStationInfo si;

    for (int k = 0; k < 100; k++)
        samples[k] = 1000 + k;

    c = tvq_core_new(cfg, err, sizeof err);
    T_CHECK(c != NULL, "core nuevo");
    T_CHECK(c && tvq_core_load_stations(c, err, sizeof err) == 0,
            "carga 1 estacion");
    T_CHECK(tvq_core_nstations(c) == 1, "n_sta=1");

    /* --- paquete base: 100 muestras a 100 Hz desde t=1000 ---------------- */
    len = tvq_t_trace(buf, sizeof buf, "AAA", "HHZ", "XX", "--", "i4", 100.0,
                      1000.0, 100, samples);
    T_CHECK(len > 0, "construye TRACE2 i4");
    T_CHECK(tvq_core_ingest_trace(c, buf, len) == 1, "ingesta TRACE2");

    T_CHECK(tvq_core_station_info(c, 0, &si) == 1, "station_info");
    T_CHECK(si.rate > 99.9 && si.rate < 100.1, "rate 100 Hz");
    T_CHECK(si.cap == 120L * 60L * 100L, "buffer de 120 min por tasa");

    T_CHECK(tvq_core_sample(c, 0, 100000LL, &v) == 1 && v == 1000,
            "primera muestra");
    T_CHECK(tvq_core_sample(c, 0, 100099LL, &v) == 1 && v == 1099,
            "ultima muestra");

    /* --- hueco de 5 s: relleno INT_MAX, lo viejo intacto ----------------- */
    len = tvq_t_trace(buf, sizeof buf, "AAA", "HHZ", "XX", "--", "i4", 100.0,
                      1005.0, 100, samples);
    T_CHECK(tvq_core_ingest_trace(c, buf, len) == 1, "ingesta tras hueco");
    T_CHECK(tvq_core_sample(c, 0, 100150LL, &v) == 0,
            "hueco entre paquetes = sin dato");
    T_CHECK(tvq_core_sample(c, 0, 100050LL, &v) == 1 && v == 1050,
            "lo anterior al hueco no se pisa");
    T_CHECK(tvq_core_sample(c, 0, 100500LL, &v) == 1 && v == 1000,
            "el paquete nuevo entra en su indice absoluto");

    /* --- estacion no listada -------------------------------------------- */
    len = tvq_t_trace(buf, sizeof buf, "ZZZ", "HHZ", "XX", "--", "i4", 100.0,
                      1000.0, 10, samples);
    T_CHECK(tvq_core_ingest_trace(c, buf, len) == 0,
            "estacion no listada se ignora");
    T_CHECK(tvq_core_nstations(c) == 1, "no crea pistas fantasma");

    /* --- i2 -------------------------------------------------------------- */
    len = tvq_t_trace(buf, sizeof buf, "AAA", "HHZ", "XX", "--", "i2", 100.0,
                      1020.0, 10, samples);
    T_CHECK(tvq_core_ingest_trace(c, buf, len) == 1, "i2 aceptado");
    T_CHECK(tvq_core_sample(c, 0, 102003LL, &v) == 1 && v == 1003,
            "i2 decodificado");

    /* --- fuera de orden: no retrocede el ultimo indice ------------------- */
    for (int k = 0; k < 100; k++)
        samples[k] = 7000 + k;
    len = tvq_t_trace(buf, sizeof buf, "AAA", "HHZ", "XX", "--", "i4", 100.0,
                      1020.5, 100, samples);
    T_CHECK(tvq_core_ingest_trace(c, buf, len) == 1, "paquete solapado aceptado");
    T_CHECK(tvq_core_sample(c, 0, 102055LL, &v) == 1 && v == 7005,
            "el solape escribe en su indice");
    T_CHECK(tvq_core_station_info(c, 0, &si) == 1 && si.t_last > 1020.9,
            "t_last no retrocede");

    /* --- TRACE2 truncado: nsamp grande, payload corto -------------------- */
    len = tvq_t_trace(buf, sizeof buf, "AAA", "HHZ", "XX", "--", "i4", 100.0,
                      1030.0, 100, samples);
    ((TRACE2_HEADER *)buf)->nsamp = 5000;
    T_CHECK(tvq_core_ingest_trace(c, buf, len) == 1,
            "TRACE2 con nsamp mentiroso no aborta");
    T_CHECK(tvq_core_sample(c, 0, 103099LL, &v) == 1,
            "escribe hasta la ultima muestra que trae");
    T_CHECK(tvq_core_sample(c, 0, 103500LL, &v) == 0,
            "no lee mas alla del payload");

    /* --- mensajes invalidos --------------------------------------------- */
    T_CHECK(tvq_core_ingest_trace(c, buf, 10) == 0, "mensaje corto ignorado");

    tvq_core_free(c);
    tvq_config_free(cfg);

    printf(tvq_t_fail ? "FALLAS: test_trace\n" : "OK: test_trace\n");
    return tvq_t_fail ? 1 : 0;
}
