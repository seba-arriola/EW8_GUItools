#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "csnmagsutils.h"

STATION_META Estaciones[MAX_STATIONS];
int NumEstaciones = 0;
double dSPFResp_table[40];
double dSPDist_table[160];

int CargarEstaciones(char *archivo, int debug) {
    FILE *fp = fopen(archivo, "r");
    if (!fp) return -1;
    char linea[256]; int count = 0;
    while (fgets(linea, sizeof(linea), fp) != NULL && count < MAX_STATIONS) {
        if (linea[0] == '#' || strlen(linea) < 10) continue; 
        char loc_temp[10]; 
        if (sscanf(linea, "%s %s %s %s %lf %lf %lf %lf", Estaciones[count].sta, Estaciones[count].net, 
                   Estaciones[count].comp, loc_temp, &Estaciones[count].lat, &Estaciones[count].lon, 
                   &Estaciones[count].elev, &Estaciones[count].gain) == 8) {
            if (strcmp(loc_temp, "--") == 0) strcpy(Estaciones[count].loc, "--");
            else { strncpy(Estaciones[count].loc, loc_temp, 2); Estaciones[count].loc[2] = '\0'; }
            count++;
        }
    }
    fclose(fp); NumEstaciones = count;
    if (debug) logit("t", "Cargadas %d estaciones desde %s\n", NumEstaciones, archivo);
    return 0;
}

int CargarCurvaSPF(char *archivo, int debug) {
    FILE *fp = fopen(archivo, "r");
    if (!fp) return -1;
    char linea[256]; int count = 0;
    while (fgets(linea, sizeof(linea), fp) != NULL && count < 40) {
        if (linea[0] == '#') continue;
        if (sscanf(linea, "%lf", &dSPFResp_table[count]) == 1) count++;
    }
    fclose(fp); 
    if (debug) logit("t", "Cargados %d valores SPF desde %s\n", count, archivo);
    return 0;
}

int CargarTablaSPDist(char *archivo, int debug) {
    FILE *fp = fopen(archivo, "r");
    if (!fp) return -1;
    char linea[256]; int count = 0;
    while (fgets(linea, sizeof(linea), fp) != NULL && count < 160) {
        if (linea[0] == '#') continue;
        if (sscanf(linea, "%lf", &dSPDist_table[count]) == 1) count++;
    }
    fclose(fp); 
    if (debug) logit("t", "Cargados %d valores S-P desde %s\n", count, archivo);
    return 0;
}

int ObtenerMetadatosEstacion(char *sta, char *net, char *comp, char *loc, double *lat, double *lon, double *gain) {
    for (int i = 0; i < NumEstaciones; i++) {
        if (!strcmp(Estaciones[i].sta, sta) && !strcmp(Estaciones[i].net, net) && !strcmp(Estaciones[i].comp, comp)) {
            if (!strcmp(Estaciones[i].loc, loc) || (!strcmp(Estaciones[i].loc, "--") && strlen(loc) == 0)) {
                *lat = Estaciones[i].lat; *lon = Estaciones[i].lon; *gain = Estaciones[i].gain;
                return 1; 
            }
        }
    }
    return 0; 
}

double CalcularDistanciaGrados(double lat1, double lon1, double lat2, double lon2) {
    double rlat1 = lat1 * PI / 180.0, rlat2 = lat2 * PI / 180.0;
    double dlon = (lon2 - lon1) * PI / 180.0, dlat = (lat2 - lat1) * PI / 180.0;
    double a = sin(dlat/2.0)*sin(dlat/2.0) + cos(rlat1)*cos(rlat2)*sin(dlon/2.0)*sin(dlon/2.0);
    return 2.0 * atan2(sqrt(a), sqrt(1.0-a)) * 180.0 / PI; 
}

time_t ConvertToEpoch(char *t_str) {
    struct tm t; char tmp[5]; memset(&t, 0, sizeof(struct tm));
    strncpy(tmp, t_str, 4); tmp[4]=0; t.tm_year = atoi(tmp) - 1900;
    strncpy(tmp, t_str+4, 2); tmp[2]=0; t.tm_mon = atoi(tmp) - 1;
    strncpy(tmp, t_str+6, 2); tmp[2]=0; t.tm_mday = atoi(tmp);
    strncpy(tmp, t_str+8, 2); tmp[2]=0; t.tm_hour = atoi(tmp);
    strncpy(tmp, t_str+10, 2); tmp[2]=0; t.tm_min = atoi(tmp);
    strncpy(tmp, t_str+12, 2); tmp[2]=0; t.tm_sec = atoi(tmp);
    char *tz = getenv("TZ"); setenv("TZ", "", 1); tzset();
    time_t epoch = mktime(&t);
    if (tz) setenv("TZ", tz, 1); else unsetenv("TZ"); tzset();
    return epoch; 
}

/* ========================================================================= */
/* FUNCIONES MATEMATICAS PROPIAS (heredadas de rutinas de magnitud)                          */
/* ========================================================================= */

int detrend(double int_len, double dt, double Z_in[], long ncount, double prior_motion, double vZ[], double Z_out[], int order, double S_to_N) {
    double x, z, sum1 = 0., sum2 = 0., sum3 = 0., sum4 = 0., sum5 = 0., sum6 = 0., z1 = 0., z2 = 0., z3 = 0., x1, x2, x3;
    double slope, slope2, curv, cube, denom, error_linear, error_quadratic, rms_signal_amp = 0.;
    long n, ntop; int top, result = 0;
  
    for (n=0; n<ncount; n++) Z_out[n] = Z_in[n] - Z_in[0]; ncount -= 2;
    ntop = (int)(int_len / dt); if (ntop > ncount) ntop = ncount;
    top = (int)(20. / dt); if (top > ntop) top = ntop;
    
    for (n=0; n<top; n++) rms_signal_amp += (vZ[n] * vZ[n]);
    if (top > 0) rms_signal_amp = sqrt(rms_signal_amp / (double)top);
  
    if (prior_motion == 0.0 || (rms_signal_amp / prior_motion) < S_to_N) return -1;

    if (order == 1) {
        for (n=0; n<ntop; n++) { x = (double)n; sum1 += (Z_out[n] * x); sum2 += (x * x); }
        slope = (sum2 != 0.0) ? (sum1 / sum2) : 0.0;
        for (n=0; n<ncount; n++) Z_out[n] -= (slope * (double)n); 
        result = 1; x = (double)ntop; error_linear = sqrt(slope*slope * x*x / 3.);
    
        sum1 = 0.; sum2 = 0.; sum3 = 0.; sum4 = 0.; sum5 = 0.;
        for (n=0; n<ntop; n++) {
            x = (double)n; z = Z_in[n] - Z_in[0];
            sum1 += (z * x); sum2 += (x * x); sum3 += (x * x * x);
            sum4 += (z * x * x); sum5 += (x * x * x * x);
        }
        
        double denom_quad = (sum2 * sum5 - sum3 * sum3);
        if (denom_quad != 0.0) {
            slope2 = (sum1 * sum5 - sum4 * sum3) / denom_quad;
            curv = (sum2 * sum4 - sum1 * sum3) / denom_quad;
        } else { slope2 = 0.0; curv = 0.0; }
        
        error_quadratic = slope2 * slope2 / 3.;
        error_quadratic += (curv * slope2 * x / 2.);
        error_quadratic += (curv * curv * x * x / 5.);
        if (error_quadratic > 0) error_quadratic = sqrt(error_quadratic) * x; else error_quadratic = 0;
    
        if (rms_signal_amp > 0) { error_linear = error_linear / rms_signal_amp; error_quadratic = error_quadratic / rms_signal_amp; }
        if ((rms_signal_amp/prior_motion) < 3.5) return -2;
    }
    
    if (order == 2) {
        for (n=0; n<ntop; n++) {
            x = (double)n; sum1 += (Z_out[n] * x); sum2 += (x * x);
            sum3 += (x * x * x); sum4 += (Z_out[n] * x * x); sum5 += (x * x * x * x);
        }
        double denom_quad = (sum2 * sum5 - sum3 * sum3);
        if (denom_quad != 0.0) { slope = (sum1 * sum5 - sum4 * sum3) / denom_quad; curv = (sum2 * sum4 - sum1 * sum3) / denom_quad; } 
        else { slope = 0.0; curv = 0.0; }
        for (n=0; n<ncount; n++) { x = (double)n; Z_out[n] -= ((slope + curv*x) * x); }   
        result = 2;
    }

    if (order == 3) {
        for (n=0; n<ntop; n++) {
            x1 = (double)n; x2 = x1 * x1; x3 = x2 * x1;
            z1 += (Z_out[n] * x1); z2 += (Z_out[n] * x2); z3 += (Z_out[n] * x3);
            sum2 += x2; sum3 += x3; sum4 += (x2 * x2); sum5 += (x3 * x2); sum6 += (x3 * x3);
        }
        denom = (sum4*sum6 - sum5*sum5) * (sum3*sum4 - sum2*sum5) - (sum3*sum5 - sum4*sum4) * (sum5*sum4 - sum6*sum3);
        if (denom != 0.0) {
            slope = ((sum4*sum6 - sum5*sum5) * (sum4*z2 - sum5*z1) - (sum3*sum5 - sum4*sum4) * (sum5*z3 - sum6*z2)) / denom;
            double denom_curv = (sum3*sum5 - sum4*sum4);
            if (denom_curv != 0.0) { curv = (sum5*z1 - sum4*z2 - slope * (sum5*sum2 - sum3*sum4)) / denom_curv; } else curv = 0.0;
            if (sum4 != 0.0) cube = (z1 - slope*sum2 - curv*sum3) / sum4; else cube = 0.0;
        } else { slope = 0.0; curv = 0.0; cube = 0.0; }
        for (n=0; n<ncount; n++) { x = (double)n; Z_out[n] -= ((slope + curv*x + cube*x*x) * x); }
        result = 3;
    }
    return result;
}            

double wavelet_decomp(double int_len, double Z_in[], long ncount, double dt) {
    int i, k, l, m, n, ntop, n_remaining, ic, division_counter;
    static double x[MAXMWPARRAY], x_next[MAXMWPARRAY], w[MAXMWPARRAY], decomp_mag[MAXMWPARRAY];
    double y, z, avg_count = 0., avg = 0., two = 2., average[100], xform[2][100], error[2][500];
    double xmin, tau, a, fc, wi, sum0, sum1, sum2, sum3, weight[100];
    double smooth[4], nonsmooth[4], smooth2[6] = {.332671, .806891, .459877, -.135011, -.085441, .035226}, nonsmooth2[6];
    
    ncount -= 2; ntop = (int)(int_len / dt); if (ntop > ncount-2) ntop = ncount - 2; if (ntop > MAXMWPARRAY) ntop = MAXMWPARRAY;
    for (n=0; n<100; n++) weight[n] = (n==0) ? 1. : 2. * weight[n-1];
    for (n=0; n<ntop; n++) { w[n] = 0.0; x[n] = Z_in[n]; x_next[n] = Z_in[n]; }
    
    smooth[0] = (1. + sqrt(3.0)) / (4. * sqrt(2.)); smooth[1] = (3. + sqrt(3.0)) / (4. * sqrt(2.));
    smooth[2] = (3. - sqrt(3.0)) / (4. * sqrt(2.)); smooth[3] = (1. - sqrt(3.0)) / (4. * sqrt(2.));
    z = 1.; for (n = 0; n < 6; n++) { nonsmooth2[n] = z * smooth2[5 - n]; if (n < 4) nonsmooth[n] = z * smooth[3 - n]; z *= -1.; }

    n_remaining = ntop; division_counter = 0;
    while (n_remaining >= 5) {
        k = 0; m = 0; avg = 0.; avg_count = 0.;
        for (n=0; n<n_remaining; n+=2) {
            x_next[k] = 0.0; w[m] = 0.0;
            for (l=0; l<6; l++) {
                int idx = (n+l < n_remaining) ? (n+l) : (n+l - n_remaining);
                x_next[k] += smooth2[l] * x[idx]; w[m] += nonsmooth2[l] * x[idx];
            }
            avg += (w[m] * w[m]); decomp_mag[k] = w[m] * w[m]; avg_count += 1.; m++; k++;
        }
        average[division_counter] = (avg_count > 0.5) ? sqrt(avg / avg_count) : 0.0;
        n_remaining = k; division_counter++; for (n=0; n<k; n++) x[n] = x_next[n];
    }
    if (average[division_counter-1] < 1.e-50 && division_counter > 0) division_counter--;
    y = 1. / (two * dt); for (n=1; n<division_counter; n++) y /= two;
    for (n=division_counter-1; n>=0; n--) { xform[0][division_counter-n-1] = y; xform[1][n] = average[division_counter-n-1]; y *= two; }

    for (int index=0; index<500; index++) {
        fc = .005 + .001*(double)index; ic = 0;
        while (xform[0][ic]<fc && ic<division_counter) ic++;
        sum0 = sum1 = sum2 = sum3 = 0.;
        for (i = 0; i < ic; i++) { sum0 += weight[i]; sum1 += weight[i] * xform[1][i]; }
        for (i = ic; i < division_counter-1; i++) { wi = xform[0][i]; sum2 += weight[i] * xform[1][i] / wi; sum3 += weight[i] / (wi * wi); }
        double denom_a = (sum0 + fc*fc*sum3); a = (denom_a != 0.0) ? (sum1 + fc*sum2) / denom_a : -1.0;
        if (a < 0.0 || fc <= xform[0][0]) error[1][index] = 2.e+20; 
        else {
            z = 0.; for (i=0; i<ic; i++) z += weight[i] * ((xform[1][i] - a) * (xform[1][i] - a));
            for (i=ic; i<division_counter-1; i++) { wi = xform[0][i]; y = (wi != 0.0) ? xform[1][i] - a*fc/wi : 0.0; z += weight[i] * (y*y); }
            error[0][index] = a; error[1][index] = z;
        }
    }
    xmin = 1.e+50; for (i=0; i<500; i++) { if (error[1][i] < xmin) { ic = i; xmin = error[1][i]; } }
    fc = .005 + .001 * (double)ic; a = error[0][ic];
    for (i=0; i<division_counter; i++) {
        wi = xform[0][i];
        if (wi < fc) z = a; else { z = (wi != 0.0) ? a * fc * fc / (wi * wi) : a; }
    }
    tau = (fc != 0.0) ? 2.5 / fc : 200.0;
    if (tau < 5.) tau = 5.; else if (tau > 200.) tau = 200.;
    if (tau > (dt * (double)ncount)) tau = dt * (double)ncount;
    return tau;
}

int integrate(double *h1, double *h2, int *n1, int *n2, long ncount, double Z_in[], double dt, double int_len) {
    double max1 = -1.e+10, max2 = -1.e+10, extrema[2][1000], test;
    static double zT[MAXMWPARRAY]; int n_max = 0, array_top = (int)(int_len / dt), n_start = (int)(2.5/dt); 
    if (array_top > ncount) array_top = (int)ncount; if (array_top > MAXMWPARRAY) array_top = MAXMWPARRAY;
    *n1 = 0; *n2 = 0; *h1 = 0.; *h2 = 0.; zT[0] = 0.;
    for (int n=1; n<array_top; n++) zT[n] = zT[n-1] + 0.5*dt*(Z_in[n]+Z_in[n-1]);
    for (int n=n_start; n<array_top-1; n++) {
        if ((zT[n] > zT[n-1] && zT[n] > zT[n+1] && zT[n] > 0.0) || (zT[n] < zT[n-1] && zT[n] < zT[n+1] && zT[n] < 0.0)) {
            extrema[0][n_max] = zT[n] * zT[n]; extrema[1][n_max] = (float)n; n_max++;
            if (n_max > 999) return -1;
        }
    }
    if (n_max < 1) return -1;
    if (n_max == 1) { *n1 = (int)(.01 + extrema[1][0]); *h1 = zT[*n1]; return 1; }
    for (int n=0; n<n_max; n++) { if (extrema[0][n] > max1) { max1 = extrema[0][n]; *n1 = (int)(.01 + extrema[1][n]); *h1 = zT[*n1]; } }
    for (int n=0; n<n_max; n++) {
        int i = (int)(.01 + extrema[1][n]);
        if (extrema[0][n] > max2 && i != *n1) {
            test = (zT[*n1] != 0.0) ? zT[i] / zT[*n1] : 0.0;
            if (test < 0.0) { max2 = extrema[0][n]; *n2 = i; }
        }
    }
    if (*n2 > 0) { *h2 = zT[*n2]; return 1; }                                              
    for (int n=0; n<n_max; n++) {
        int i = (int)(0.01 + extrema[1][n]);
        if (extrema[0][n] > max2 && i != *n1) {
            test = (zT[*n1] != 0.0) ? zT[i] / zT[*n1] : 0.0;
            if (test > 0.2) { max2 = extrema[0][n]; *n2 = i; }
        }
    }
    *h2 = zT[*n2];
    if (*n1 > *n2) { *h1 = zT[*n2]; *n1 = *n2; }
    *h2 = 0.0; *n2 = 0; return 1;
}

double ComputeMwpMag(double dMaxIntDisp, double dDelta) {
    double dDeltaT = (dDelta == 0.0) ? 0.1 : dDelta;
    double V_p = (160.0 * dDeltaT + 7900.0);
    double dMoment = 4.0 * PI * 3400.0 * V_p * V_p * V_p * dDeltaT * 111194.9 * dMaxIntDisp;
    return (dMoment > 0.0) ? (1.0 / 1.5) * (log10(dMoment) - 9.1) : 0.0;
}

double MbMlGroundMotion(double dSens, long lPer, long lAmp) {
    if (dSens == 0.0 || lPer < 1 || lPer > 40) return 0.0;
    return ((double)lAmp * 1.e9) / (2.0 * PI * dSens * dSPFResp_table[lPer - 1] * (1.0 / ((double)lPer / 10.0)));
}

double ComputeMlMag(double dMlAmp, double dMlPer, double dDelta) {
    double dMlPerT = (dMlPer < 0.1) ? 1.0 : ((dMlPer > 3.0) ? 30.0 : dMlPer * 10.0);
    double dDeltaT = (dDelta <= 0.0) ? 0.5 : dDelta;
    double dAmp = (dMlAmp <= 1.0) ? 1.0 : dMlAmp;
    if (dDeltaT < 1.65) return log10(dAmp / (dMlPerT / 10.0)) - 0.066 + 0.8 * log10(dDeltaT * dDeltaT);
    return log10(dAmp / (dMlPerT / 10.0)) - 0.364 + 1.5 * log10(dDeltaT * dDeltaT);
}
