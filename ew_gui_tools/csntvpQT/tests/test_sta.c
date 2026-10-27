/* StaFile: formato `STA CHAN NET LOC escala`, el quirk de 4 campos del GTK y
 * el fichero real de params/ (141 estaciones, todas HHZ). */
#include <string.h>

#include "tvq_test.h"

int main(void)
{
    TvqStaEntry e;
    char err[512];
    TvqStaEntry arr[TVQ_MAX_STATIONS];
    int n = 0;
    const char *real = getenv("PARAMS");

    memset(&e, 0, sizeof e);
    T_CHECK(tvq_sta_parse_line("PAYG HHZ IU 10 0.005\n", &e) == 1,
            "linea de 5 campos");
    T_CHECK(strcmp(e.sta, "PAYG") == 0 && strcmp(e.chan, "HHZ") == 0 &&
                strcmp(e.net, "IU") == 0 && strcmp(e.loc, "10") == 0,
            "campos STA/CHAN/NET/LOC");
    T_CHECK(e.scale > 0.0049 && e.scale < 0.0051, "escala 0.005");

    memset(&e, 0, sizeof e);
    T_CHECK(tvq_sta_parse_line("ABC HHN C1 0.002", &e) == 1, "linea de 4 campos");
    T_CHECK(strcmp(e.loc, "--") == 0, "4.o campo = escala => LOC '--'");
    T_CHECK(e.scale > 0.0019 && e.scale < 0.0021, "escala desde el 4.o campo");

    T_CHECK(tvq_sta_parse_line("# comentario\n", &e) == 0, "comentario ignorado");
    T_CHECK(tvq_sta_parse_line("\n", &e) == 0, "linea vacia ignorada");
    T_CHECK(tvq_sta_parse_line("  # con espacios\n", &e) == 0,
            "comentario con espacios ignorado");
    T_CHECK(tvq_sta_parse_line("AAA HHZ\n", &e) == -1, "linea corta = malformada");

    /* Fichero real del sistema. */
    if (real && real[0]) {
        char path[1024];

        snprintf(path, sizeof path, "%s/stations_to_view.sta", real);
        T_CHECK(tvq_sta_load(path, arr, TVQ_MAX_STATIONS, &n, err, sizeof err) == 0,
                "carga params/stations_to_view.sta");
        T_CHECK(n > 100, "el fichero real trae mas de 100 estaciones");
        T_CHECK(strcmp(arr[0].sta, "PAYG") == 0, "primera estacion PAYG");
        T_CHECK(strcmp(arr[0].chan, "HHZ") == 0, "primera estacion en HHZ");
        T_CHECK(arr[0].scale > 0.0, "escala de la primera estacion");
    } else {
        printf("skip: PARAMS no definido (no se valida el fichero real)\n");
    }

    T_CHECK(tvq_sta_load("/no/existe.sta", arr, TVQ_MAX_STATIONS, &n, err,
                         sizeof err) == -1,
            "StaFile inexistente -> -1");
    T_CHECK(strstr(err, "no puedo abrir") != NULL, "error de StaFile claro");

    tvq_t_write("bad.sta", "AAA HHZ\n");
    T_CHECK(tvq_sta_load(tvq_t_path("bad.sta"), arr, TVQ_MAX_STATIONS, &n, err,
                         sizeof err) == -1,
            "linea malformada -> -1");
    T_CHECK(strstr(err, "linea 1") != NULL, "el error dice la linea");

    tvq_t_write("empty.sta", "# solo comentarios\n");
    T_CHECK(tvq_sta_load(tvq_t_path("empty.sta"), arr, TVQ_MAX_STATIONS, &n, err,
                         sizeof err) == -1,
            "fichero sin estaciones -> -1");

    printf(tvq_t_fail ? "FALLAS: test_sta\n" : "OK: test_sta\n");
    return tvq_t_fail ? 1 : 0;
}
