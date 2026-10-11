/******************************************************************************
 * test_data.c                                                                *
 *                                                                            *
 * Verifica el parser de estaciones de csnstaevdisp sin abrir la GUI.         *
 * Los sismos ya no vienen de un archivo: se leen de la base hipocentral.     *
 *                                                                            *
 * Se enlaza contra csnstaevdisp.c compilado con -Dmain=... para no duplicar  *
 * el parser. Uso: test_data <stations>                                       *
 ******************************************************************************/
#include <stdio.h>
#include <string.h>

extern void LoadStations(void);
extern int  NumEstaciones;
extern char StaFile[256];

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: %s <stations>\n", argv[0]);
        return 2;
    }

    strncpy(StaFile, argv[1], 255);

    LoadStations();

    printf("estaciones=%d\n", NumEstaciones);

    if (NumEstaciones < 1) { printf("FALLO: no se cargaron estaciones\n"); return 1; }

    printf("OK test_data\n");
    return 0;
}
