#ifndef CSNMAGSUTILS_H
#define CSNMAGSUTILS_H

#include <time.h>
#include <stdint.h>
#include <earthworm.h>

#define MAX_STATIONS 200
#define MAXMWPARRAY 32000
#define PI 3.14159265358979323846

/* Estructura para almacenar Metadatos de Estaciones */
typedef struct {
    char sta[6], net[3], comp[4], loc[3];
    double lat, lon, elev, gain;
} STATION_META;

/* Variables globales exportadas */
extern STATION_META Estaciones[MAX_STATIONS];
extern int NumEstaciones;
extern double dSPFResp_table[40];
extern double dSPDist_table[160];

/* Prototipos de carga y busqueda de datos */
int CargarEstaciones(char *archivo, int debug);
int CargarCurvaSPF(char *archivo, int debug);
int CargarTablaSPDist(char *archivo, int debug);
int ObtenerMetadatosEstacion(char *sta, char *net, char *comp, char *loc, double *lat, double *lon, double *gain);

/* Prototipos de Geometria y Tiempo */
double CalcularDistanciaGrados(double lat1, double lon1, double lat2, double lon2);
time_t ConvertToEpoch(char *t_str);

/* Prototipos Matematicos */
int detrend(double int_len, double dt, double Z_in[], long ncount, double prior_motion, double vZ[], double Z_out[], int order, double S_to_N);
double wavelet_decomp(double int_len, double Z_in[], long ncount, double dt);
int integrate(double *h1, double *h2, int *n1, int *n2, long ncount, double Z_in[], double dt, double int_len);
double ComputeMwpMag(double dMaxIntDisp, double dDelta);
double MbMlGroundMotion(double dSens, long lPer, long lAmp);
double ComputeMlMag(double dMlAmp, double dMlPer, double dDelta);

#endif
