/******************************************************************************
 * test_data.c                                                                *
 *                                                                            *
 * Verifica los parsers de datos de csnstaevdisp sin abrir la GUI:            *
 *   - LoadStations()      sobre el station file (lat/lon/elev)               *
 *   - LoadQuakeHistory()  sobre csnhypodbp_hist.txt                          *
 *                                                                            *
 * Se enlaza contra csnstaevdisp.c compilado con -Dmain=... para no duplicar  *
 * el parser. Uso: test_data <stations> <quakes>                             *
 ******************************************************************************/
#include <stdio.h>
#include <string.h>

extern void LoadStations(void);
extern void LoadQuakeHistory(void);
extern int  NumEstaciones;
extern int  NumQuakes;
extern char StaFile[256];
extern char QuakeFile[256];

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: %s <stations> <quakes>\n", argv[0]);
        return 2;
    }

    strncpy(StaFile, argv[1], 255);
    strncpy(QuakeFile, argv[2], 255);

    LoadStations();
    LoadQuakeHistory();

    printf("estaciones=%d quakes=%d\n", NumEstaciones, NumQuakes);

    if (NumEstaciones < 1) { printf("FALLO: no se cargaron estaciones\n"); return 1; }
    if (NumQuakes < 1)     { printf("FALLO: no se cargaron sismos\n");     return 1; }

    printf("OK test_data\n");
    return 0;
}
