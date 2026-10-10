/*
 * test_station_order - orden de canales de la pestaña Ondas (headless).
 * Regresion de channelsOrder x componentsOrder: canal externo, componente
 * interno, omitiendo los no disponibles.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_station.h"

int main(void)
{
    char out[16][8];
    char toks[8][16];
    char c[8];
    const char *avail[] = { "HHZ", "HHE", "BHZ" };
    int n, nt;

    /* Orden canal externo, componente interno; HHN/BHN no disponibles. */
    n = csnhypodbp_channel_order("HH BH HN LH", "Z N E", avail, 3, out, 16);
    assert(n == 3);
    assert(strcmp(out[0], "HHZ") == 0);
    assert(strcmp(out[1], "HHE") == 0);
    assert(strcmp(out[2], "BHZ") == 0);

    /* Orden completo cuando todo esta disponible. */
    {
        const char *all[] = { "HHZ", "HHN", "HHE", "BHZ", "BHN", "BHE" };
        n = csnhypodbp_channel_order("HH BH", "Z N E", all, 6, out, 16);
        assert(n == 6);
        assert(strcmp(out[0], "HHZ") == 0);
        assert(strcmp(out[1], "HHN") == 0);
        assert(strcmp(out[2], "HHE") == 0);
        assert(strcmp(out[3], "BHZ") == 0);
        assert(strcmp(out[4], "BHN") == 0);
        assert(strcmp(out[5], "BHE") == 0);
    }

    csnhypodbp_chan_code(c, sizeof(c), "HH", "Z");
    assert(strcmp(c, "HHZ") == 0);

    nt = csnhypodbp_split_ws("HH BH  HN, LH", toks, 8);
    assert(nt == 4);
    assert(strcmp(toks[3], "LH") == 0);

    assert(csnhypodbp_channel_order("", "Z", avail, 3, out, 16) == 0);
    assert(csnhypodbp_channel_order("HH", "", avail, 3, out, 16) == 0);

    /* needs_fetch: una fila nueva (fetched=0) obliga a descargar; si todas ya
       se intentaron, no. Regresion: antes se saltaba y no se pedian HHN/HHE. */
    {
        DEV_STATION rows[3];
        memset(rows, 0, sizeof(rows));
        snprintf(rows[0].szStation, sizeof(rows[0].szStation), "FAR1");
        snprintf(rows[1].szStation, sizeof(rows[1].szStation), "FAR1");
        snprintf(rows[2].szStation, sizeof(rows[2].szStation), "MT14");
        rows[0].fetched = 1; rows[1].fetched = 0; rows[2].fetched = 1;
        assert(csnhypodbp_station_needs_fetch(rows, 3, "FAR1") == 1); /* HHN/HHE nuevas */
        rows[1].fetched = 1;
        assert(csnhypodbp_station_needs_fetch(rows, 3, "FAR1") == 0); /* ya intentadas */
        assert(csnhypodbp_station_needs_fetch(rows, 3, "MT14") == 0);
        assert(csnhypodbp_station_needs_fetch(rows, 3, "XXXX") == 1); /* sin filas */
    }

    /* has_data: la estacion tiene datos si ALGUN canal los tiene en la ventana. */
    {
        DEV_STATION rows[3];
        int has[3] = { 0, 1, 0 };
        memset(rows, 0, sizeof(rows));
        snprintf(rows[0].szStation, sizeof(rows[0].szStation), "FAR1");
        snprintf(rows[1].szStation, sizeof(rows[1].szStation), "FAR1");
        snprintf(rows[2].szStation, sizeof(rows[2].szStation), "MT14");
        assert(csnhypodbp_station_has_data(rows, 3, has, "FAR1") == 1);
        assert(csnhypodbp_station_has_data(rows, 3, has, "MT14") == 0);
        assert(csnhypodbp_station_has_data(rows, 3, has, "XXXX") == 0);
    }

    /* has_band: solo se anaden canales de bandas ya presentes en la estacion. */
    {
        DEV_STATION rows[2];
        memset(rows, 0, sizeof(rows));
        snprintf(rows[0].szStation, sizeof(rows[0].szStation), "FAR1");
        snprintf(rows[0].szChannel, sizeof(rows[0].szChannel), "HHZ");
        snprintf(rows[1].szStation, sizeof(rows[1].szStation), "MT14");
        snprintf(rows[1].szChannel, sizeof(rows[1].szChannel), "BHZ");
        assert(csnhypodbp_station_has_band(rows, 2, "FAR1", "HHN") == 1);
        assert(csnhypodbp_station_has_band(rows, 2, "FAR1", "HHE") == 1);
        assert(csnhypodbp_station_has_band(rows, 2, "FAR1", "BHZ") == 0);
        assert(csnhypodbp_station_has_band(rows, 2, "MT14", "BHN") == 1);
        assert(csnhypodbp_station_has_band(rows, 2, "MT14", "HHZ") == 0);
        assert(csnhypodbp_station_has_band(rows, 2, "XXXX", "HHZ") == 0);
    }

    /* trace_label: canal + distancia (la estacion ya va en el titulo). */
    {
        char lab[64];
        csnhypodbp_trace_label(lab, sizeof(lab), "FAR1", "HHZ", 37.4);
        assert(strcmp(lab, "HHZ (37 km)") == 0);
        csnhypodbp_trace_label(lab, sizeof(lab), "FAR1", "HHN", 0.0);
        assert(strcmp(lab, "HHN") == 0);
    }

    /* Candidatos: todos los combos, sin filtrar disponibilidad. */
    {
        char cand[16][8];
        int nc = csnhypodbp_channel_candidates("HH BH", "Z N E", cand, 16);
        assert(nc == 6);
        assert(strcmp(cand[0], "HHZ") == 0);
        assert(strcmp(cand[5], "BHE") == 0);
    }

    printf("test_station_order OK\n");
    return 0;
}
