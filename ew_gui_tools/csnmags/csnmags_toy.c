#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <earthworm.h>
#include <transport.h>
#include <kom.h>
#include <trace_buf.h>
#include <ws_clientII.h>
#include <rw_mag.h>
#include <socket_ew.h>

#include "csnmagsutils.h"

/* --- CONSTANTES RESTAURADAS --- */
#define MAX_STR 256
#define MAX_TRACE_BUF 1000000 
#define MAX_WS_RETRIES 2      
/* ------------------------------ */

/* Variables de configuracion */
char MyModName[MAX_STR], RingName[MAX_STR], WsIP[MAX_STR], WsPort[MAX_STR];
char StaFile[MAX_STR], SpFilterFile[MAX_STR], SpDistFile[MAX_STR];   
long OutKey;
int  HeartbeatInt, LogFile, WsTimeout, Debug = 0;
double WindowDuration;

/* Variables Earthworm */
unsigned char MyInstId, MyModId, TypeHeartBeat, TypeError, TypeHyp2000Arc, TypeMagnitude;
SHM_INFO Region;
pid_t    MyPid;
WS_MENU_QUEUE_REC ws_menu; 
int      Terminate = 0;    
time_t   timeLastBeat = 0; 

/* Estructura y Arreglo para la Cola de Estaciones Pendientes */
#define MAX_PENDING 500       
#define MAX_EVENTS 100        
#define CALC_TYPE_MWP 1
#define CALC_TYPE_ML  2

typedef struct {
    int active, calc_type;             
    char event_id[20], origin_time_str[15];
    char sta[6], net[3], comp[4], loc[3];
    double dDelta, dSens, p_starttime, endtime;         
    time_t insertion_time;  
} PENDING_STATION;
PENDING_STATION Pendientes[MAX_PENDING];

typedef struct { char sta[6]; double mag; } STA_MAG;
typedef struct {
    int active, num_ml, used_ml, num_mwp, used_mwp;
    char event_id[20], pref_type[10];
    time_t origin_time, last_update;
    STA_MAG ml_data[MAX_STATIONS], mwp_data[MAX_STATIONS];
    double avg_ml, avg_mwp, pref_mag;
} EVENT_STATE;
EVENT_STATE Eventos[MAX_EVENTS];

/* Prototipos Internos */
int ReadConfig(char *configfile);
void Lookup(void);
void Status(unsigned char type, short ierr, char *note);
void ProcessArcMessage(char *msg);
int ConnectToWaveServer(void);
void AgregarPendiente(const char *evid, const char *otime, const char *sta, const char *net, const char *comp, const char *loc, double dDelta, double dSens, double p_st, double endt, int type);
void RevisarPendientes(void);
void ProcesarEstacionMwp(const char *evid, const char *sta, const char *net, const char *comp, const char *loc, double dDelta, double dSens, double p_st, double endt);
void ProcesarEstacionMl(const char *evid, const char *sta, const char *net, const char *comp, const char *loc, double dDelta, double dSens, double p_st, double endt);
void RegistrarMagnitudEstacion(const char *evid, const char *sta, double mag, int type);
void ActualizarMagnitudRed(EVENT_STATE *ev);

int main(int argc, char **argv) {
    MSG_LOGO  getlogo[1], reclogo;
    long      recsize;
    char      msg[65536];
    int       res;
    time_t    timeNow, timeLastCheck = 0;

    if (argc != 2) { fprintf(stderr, "Uso: csnmags_toy <configfile>\n"); exit(1); }
    if (ReadConfig(argv[1]) < 0) { fprintf(stderr, "csnmags_toy: Error en %s\n", argv[1]); exit(1); }

    Lookup();
    logit_init(argv[1], MyModId, 256, LogFile);
    SocketSysInit();
    setWsClient_ewDebug(0); 
    
    logit("t", "csnmags_toy: Iniciando Motor CSNmags (Filtros + Promediador) V2...\n");

    if (CargarEstaciones(StaFile, Debug) < 0) logit("e", "ADVERTENCIA - No se pudo cargar %s\n", StaFile);
    if (CargarCurvaSPF(SpFilterFile, Debug) < 0) logit("e", "ERROR - No se pudo cargar %s\n", SpFilterFile);
    if (CargarTablaSPDist(SpDistFile, Debug) < 0) logit("e", "ERROR - No se pudo cargar %s\n", SpDistFile);

    for (int i = 0; i < MAX_PENDING; i++) Pendientes[i].active = 0;
    for (int i = 0; i < MAX_EVENTS; i++) Eventos[i].active = 0;

    MyPid = getpid();
    tport_attach(&Region, OutKey);
    getlogo[0].instid = 0; getlogo[0].mod = 0; getlogo[0].type = TypeHyp2000Arc;
    ws_menu.head = NULL; ws_menu.tail = NULL;
    
    while (ConnectToWaveServer() != 0 && !Terminate) sleep_ew(5000);
    logit("t", "csnmags_toy: Listo. Esperando sismos en el anillo...\n");

    while (tport_getflag(&Region) != TERMINATE && tport_getflag(&Region) != MyPid && !Terminate) {
        time(&timeNow);
        if (timeNow - timeLastBeat >= HeartbeatInt) {
            timeLastBeat = timeNow;
            Status(TypeHeartBeat, 0, "");
        }
        if (timeNow - timeLastCheck >= 15) {
            timeLastCheck = timeNow;
            RevisarPendientes();
        }
        res = tport_getmsg(&Region, getlogo, 1, &reclogo, &recsize, msg, sizeof(msg) - 1);
        if (res == GET_OK || res == GET_MISS) {
            msg[recsize] = '\0';
            ProcessArcMessage(msg);
        } else {
            sleep_ew(500); 
        }
    }
    wsKillMenu(&ws_menu);
    tport_detach(&Region);
    return 0;
}

int ConnectToWaveServer(void) {
    if (ws_menu.head != NULL) { wsKillMenu(&ws_menu); ws_menu.head = NULL; ws_menu.tail = NULL; }
    if (wsAppendMenu(WsIP, WsPort, &ws_menu, WsTimeout) != WS_ERR_NONE) return -1;
    return 0;
}

void RegistrarMagnitudEstacion(const char *event_id, const char *sta, double mag, int calc_type) {
    EVENT_STATE *ev = NULL;
    time_t current_time; time(&current_time);

    for (int i = 0; i < MAX_EVENTS; i++) {
        if (Eventos[i].active && strcmp(Eventos[i].event_id, event_id) == 0) { ev = &Eventos[i]; break; }
    }
    if (!ev) {
        int oldest_idx = 0; time_t oldest_time = current_time;
        for (int i = 0; i < MAX_EVENTS; i++) {
            if (!Eventos[i].active) { ev = &Eventos[i]; break; }
            if (Eventos[i].last_update < oldest_time) { oldest_time = Eventos[i].last_update; oldest_idx = i; }
        }
        if (!ev) ev = &Eventos[oldest_idx];
        
        ev->active = 1; strcpy(ev->event_id, event_id); ev->origin_time = current_time;
        ev->num_ml = 0; ev->num_mwp = 0; ev->avg_ml = 0.0; ev->used_ml = 0;
        ev->avg_mwp = 0.0; ev->used_mwp = 0; strcpy(ev->pref_type, "N/A"); ev->pref_mag = 0.0;
    }
    ev->last_update = current_time;

    int found = 0;
    if (calc_type == CALC_TYPE_ML) {
        for (int j = 0; j < ev->num_ml; j++) {
            if (strcmp(ev->ml_data[j].sta, sta) == 0) { ev->ml_data[j].mag = mag; found = 1; break; }
        }
        if (!found && ev->num_ml < MAX_STATIONS) {
            strncpy(ev->ml_data[ev->num_ml].sta, sta, 5); ev->ml_data[ev->num_ml].sta[5] = '\0';
            ev->ml_data[ev->num_ml].mag = mag; ev->num_ml++;
        }
    } else {
        for (int j = 0; j < ev->num_mwp; j++) {
            if (strcmp(ev->mwp_data[j].sta, sta) == 0) { ev->mwp_data[j].mag = mag; found = 1; break; }
        }
        if (!found && ev->num_mwp < MAX_STATIONS) {
            strncpy(ev->mwp_data[ev->num_mwp].sta, sta, 5); ev->mwp_data[ev->num_mwp].sta[5] = '\0';
            ev->mwp_data[ev->num_mwp].mag = mag; ev->num_mwp++;
        }
    }
    ActualizarMagnitudRed(ev);
}

void ActualizarMagnitudRed(EVENT_STATE *ev) {
    double sum, raw_avg, sd; int count, used;

    if (ev->num_ml > 0) {
        sum = 0.0; count = 0;
        for (int i = 0; i < ev->num_ml; i++) { sum += ev->ml_data[i].mag; count++; }
        raw_avg = sum / count; sum = 0.0; used = 0;
        for (int i = 0; i < ev->num_ml; i++) {
            if (fabs(ev->ml_data[i].mag - raw_avg) <= 0.6) { sum += ev->ml_data[i].mag; used++; }
        }
        ev->avg_ml = (used >= 2) ? (sum / used) : raw_avg;
        ev->used_ml = (used >= 2) ? used : count;
    }

    if (ev->num_mwp > 0) {
        sum = 0.0; count = 0;
        for (int i = 0; i < ev->num_mwp; i++) { sum += ev->mwp_data[i].mag; count++; }
        raw_avg = sum / count; sd = 0.0;
        if (count > 2) {
            for (int i = 0; i < ev->num_mwp; i++) sd += pow(ev->mwp_data[i].mag - raw_avg, 2);
            sd = sqrt(sd / count);
        }
        sum = 0.0; used = 0;
        for (int i = 0; i < ev->num_mwp; i++) {
            if (count <= 2 || fabs(ev->mwp_data[i].mag - raw_avg) <= sd) { sum += ev->mwp_data[i].mag; used++; }
        }
        ev->avg_mwp = (used > 0) ? (sum / used) : 0.0;
        ev->used_mwp = used;
    }

    ev->pref_mag = 0.0; strcpy(ev->pref_type, "None");
    if (ev->used_mwp >= 3 && ev->avg_mwp > 5.5) { ev->pref_mag = ev->avg_mwp; strcpy(ev->pref_type, "Mwp"); }
    else if (ev->used_mwp >= 3 && ev->avg_mwp > 0.0 && (ev->avg_ml == 0.0 || ev->avg_ml > 5.0)) { ev->pref_mag = ev->avg_mwp; strcpy(ev->pref_type, "Mwp"); }
    else if (ev->used_ml > 0) { ev->pref_mag = ev->avg_ml; strcpy(ev->pref_type, "ML"); }

    logit("t", "CSNmags_Red: [ID %s] ML=%.2f (%d est) | MWp=%.2f (%d est) -> PREF: %s %.2f\n", 
          ev->event_id, ev->avg_ml, ev->used_ml, ev->avg_mwp, ev->used_mwp, ev->pref_type, ev->pref_mag);

    char mag_out_buf[1024]; MSG_LOGO logo_mag; MAG_INFO mag;
    logo_mag.instid = MyInstId; logo_mag.mod = MyModId; logo_mag.type = TypeMagnitude;

    if (ev->used_ml > 0) {
        memset(&mag, 0, sizeof(MAG_INFO));
        sprintf(mag.qid, "%s", ev->event_id); strcpy(mag.szmagtype, "ML"); strcpy(mag.algorithm, "CSNNet");
        mag.mag = ev->avg_ml; mag.error = 0.1; mag.quality = 1.0; mag.nstations = ev->used_ml;
        if (wr_mag(&mag, mag_out_buf, sizeof(mag_out_buf)) == 0) tport_putmsg(&Region, &logo_mag, strlen(mag_out_buf), mag_out_buf);
    }
    if (ev->used_mwp > 0) {
        memset(&mag, 0, sizeof(MAG_INFO));
        sprintf(mag.qid, "%s", ev->event_id); strcpy(mag.szmagtype, "Mwp"); strcpy(mag.algorithm, "CSNNet");
        mag.mag = ev->avg_mwp; mag.error = 0.1; mag.quality = 1.0; mag.nstations = ev->used_mwp;
        if (wr_mag(&mag, mag_out_buf, sizeof(mag_out_buf)) == 0) tport_putmsg(&Region, &logo_mag, strlen(mag_out_buf), mag_out_buf);
    }
}

void AgregarPendiente(const char *evid, const char *otime, const char *sta, const char *net, const char *comp, const char *loc, double dDelta, double dSens, double p_st, double endt, int type) {
    for (int i = 0; i < MAX_PENDING; i++) {
        if (Pendientes[i].active && !strcmp(Pendientes[i].event_id, evid) && !strcmp(Pendientes[i].sta, sta) && !strcmp(Pendientes[i].comp, comp) && Pendientes[i].calc_type == type) {
            Pendientes[i].dDelta = dDelta; Pendientes[i].p_starttime = p_st; Pendientes[i].endtime = endt; return;
        }
    }
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!Pendientes[i].active) {
            Pendientes[i].active = 1; strcpy(Pendientes[i].event_id, evid); strcpy(Pendientes[i].origin_time_str, otime);
            strcpy(Pendientes[i].sta, sta); strcpy(Pendientes[i].net, net); strcpy(Pendientes[i].comp, comp); strcpy(Pendientes[i].loc, loc);
            Pendientes[i].dDelta = dDelta; Pendientes[i].dSens = dSens; Pendientes[i].p_starttime = p_st;
            Pendientes[i].endtime = endt; Pendientes[i].calc_type = type; time(&Pendientes[i].insertion_time); return;
        }
    }
}

void RevisarPendientes(void) {
    time_t current_time; time(&current_time);
    for (int e = 0; e < MAX_EVENTS; e++) { if (Eventos[e].active && (current_time - Eventos[e].last_update > 86400)) Eventos[e].active = 0; }
    for (int i = 0; i < MAX_PENDING; i++) {
        if (Pendientes[i].active) {
            if ((double)current_time >= Pendientes[i].endtime + 5.0) {
                if (Pendientes[i].calc_type == CALC_TYPE_MWP) ProcesarEstacionMwp(Pendientes[i].event_id, Pendientes[i].sta, Pendientes[i].net, Pendientes[i].comp, Pendientes[i].loc, Pendientes[i].dDelta, Pendientes[i].dSens, Pendientes[i].p_starttime, Pendientes[i].endtime);
                else ProcesarEstacionMl(Pendientes[i].event_id, Pendientes[i].sta, Pendientes[i].net, Pendientes[i].comp, Pendientes[i].loc, Pendientes[i].dDelta, Pendientes[i].dSens, Pendientes[i].p_starttime, Pendientes[i].endtime);
                Pendientes[i].active = 0; 
            } else if (current_time - Pendientes[i].insertion_time > 1800) Pendientes[i].active = 0;
        }
    }
}

void ProcessArcMessage(char *msg) {
    char event_id[20] = {0}, origin_time_str[15] = {0};
    strncpy(event_id, msg + 136, 10);
    char *pe; for(pe=event_id+9; pe>=event_id && *pe==' '; pe--) *pe='\0';
    strncpy(origin_time_str, msg, 14);

    char lat_deg_str[3]={0}, lat_min_str[5]={0}, lat_dir, lon_deg_str[4]={0}, lon_min_str[5]={0}, lon_dir;
    strncpy(lat_deg_str, msg+16, 2); lat_dir = msg[18]; strncpy(lat_min_str, msg+19, 4);
    strncpy(lon_deg_str, msg+23, 3); lon_dir = msg[26]; strncpy(lon_min_str, msg+27, 4);

    double eq_lat = atof(lat_deg_str) + (atof(lat_min_str)/100.0)/60.0; if (lat_dir == 'S') eq_lat = -eq_lat;
    double eq_lon = atof(lon_deg_str) + (atof(lon_min_str)/100.0)/60.0; if (lon_dir == 'W') eq_lon = -eq_lon;
    time_t origin_epoch = ConvertToEpoch(origin_time_str);

    if (Debug) logit("t", "ACTUALIZACION GLASS3 - ID: %s\n", event_id);
    
    char *line = msg; int line_count = 0;
    while (line != NULL && *line != '\0') {
        char *next_line = strchr(line, '\n');
        int len = next_line ? (next_line - line) : strlen(line);
        if (len >= 114 && len < 150 && line[0] != '$' && line_count >= 2) {
            char sta[6]={0}, net[3]={0}, comp[4]={0}, loc[3]={0}, p_time_str[18]={0};
            strncpy(sta, line + 0, 5); strncpy(net, line + 5, 2); strncpy(comp, line + 9, 3);
            strncpy(loc, line + 111, 2); strncpy(p_time_str, line + 17, 17);
            char *p;
            for(p=sta+4; p>=sta && *p==' '; p--) *p='\0'; for(p=net+1; p>=net && *p==' '; p--) *p='\0';
            for(p=comp+2; p>=comp && *p==' '; p--) *p='\0'; for(p=loc+1; p>=loc && *p==' '; p--) *p='\0';
            if (strlen(loc) == 0 || strcmp(loc, "  ") == 0) strcpy(loc, "--"); 

            double sta_lat = -33.45, sta_lon = -70.66, dSens = 1.0E9;
            if (!ObtenerMetadatosEstacion(sta, net, comp, loc, &sta_lat, &sta_lon, &dSens)) goto SIGUIENTE_LINEA; 

            double dDelta = CalcularDistanciaGrados(eq_lat, eq_lon, sta_lat, sta_lon);
            struct tm pt; char tmp[6]; memset(&pt, 0, sizeof(struct tm));
            strncpy(tmp, p_time_str, 4); tmp[4]=0; pt.tm_year = atoi(tmp) - 1900;
            strncpy(tmp, p_time_str+4, 2); tmp[2]=0; pt.tm_mon = atoi(tmp) - 1;
            strncpy(tmp, p_time_str+6, 2); tmp[2]=0; pt.tm_mday = atoi(tmp);
            strncpy(tmp, p_time_str+8, 2); pt.tm_hour = atoi(tmp);
            strncpy(tmp, p_time_str+10, 2); tmp[2]=0; pt.tm_min = atoi(tmp);
            strncpy(tmp, p_time_str+12, 5); tmp[5]=0; double p_sec = atof(tmp); pt.tm_sec = (int)p_sec;
            
            char *tz = getenv("TZ"); setenv("TZ", "", 1); tzset();
            time_t p_epoch = mktime(&pt);
            if (tz) setenv("TZ", tz, 1); else unsetenv("TZ"); tzset();
            
            double p_starttime = (double)p_epoch + (p_sec - (int)p_sec);
            if (p_starttime < (double)origin_epoch || p_starttime > (double)origin_epoch + 600.0) p_starttime = (double)origin_epoch; 

            time_t current_time; time(&current_time);

            if (dDelta >= 3.0 && dDelta <= 90.0) {
                double endt = p_starttime + WindowDuration;
                if ((endt + 5.0) - (double)current_time > 0) AgregarPendiente(event_id, origin_time_str, sta, net, comp, loc, dDelta, dSens, p_starttime, endt, CALC_TYPE_MWP);
                else ProcesarEstacionMwp(event_id, sta, net, comp, loc, dDelta, dSens, p_starttime, endt);
            }
            if (dDelta <= 4.0) {
                int j_sp = 0; for (j_sp = 0; j_sp < 160; j_sp++) { if (dSPDist_table[j_sp] >= dDelta) break; }
                double endt = p_starttime + (double)j_sp + 20.0;
                if ((endt + 5.0) - (double)current_time > 0) AgregarPendiente(event_id, origin_time_str, sta, net, comp, loc, dDelta, dSens, p_starttime, endt, CALC_TYPE_ML);
                else ProcesarEstacionMl(event_id, sta, net, comp, loc, dDelta, dSens, p_starttime, endt);
            }
        }
    SIGUIENTE_LINEA:
        if (next_line) line = next_line + 1; else break;
        line_count++;
    }
}

void ProcesarEstacionMwp(const char *event_id, const char *sta, const char *net, const char *comp, const char *loc, double dDelta, double dSens, double p_starttime, double endtime) {
    if (dDelta < 3.0 || dDelta > 90.0) return;
    char *trace_buffer = malloc(MAX_TRACE_BUF); if (!trace_buffer) return;

    TRACE_REQ req; memset(&req, 0, sizeof(TRACE_REQ));
    strcpy(req.sta, sta); strcpy(req.net, net); strcpy(req.chan, comp); strcpy(req.loc, loc);
    double noise_window = 30.0; req.reqStarttime = p_starttime - noise_window; req.reqEndtime = endtime;
    req.pBuf = trace_buffer; req.bufLen = MAX_TRACE_BUF; req.timeout = WsTimeout; req.fill = 0; 

    int ws_res = -1, retries = 0;
    while (retries < MAX_WS_RETRIES) {
        if (ws_menu.head == NULL) ConnectToWaveServer();
        ws_res = wsGetTraceBinL(&req, &ws_menu, WsTimeout);
        if (ws_res == WS_ERR_NONE) break; 
        if (ws_res == WS_ERR_BROKEN_CONNECTION || ws_res == WS_ERR_TIMEOUT || ws_res == WS_ERR_NO_CONNECTION) { retries++; wsKillMenu(&ws_menu); ws_menu.head = NULL; sleep_ew(1000); } else break; 
    }
    if (ws_res != WS_ERR_NONE) { free(trace_buffer); return; }

    double real_samprate = 0.0; long num_samples = 0; char *ptr = req.pBuf;
    while (ptr < req.pBuf + req.actLen) {
        TRACE2_HEADER *trh = (TRACE2_HEADER *)ptr;
        if (real_samprate == 0.0) real_samprate = trh->samprate;
        num_samples += trh->nsamp; ptr += sizeof(TRACE2_HEADER) + trh->nsamp * sizeof(int32_t);
    }

    if (num_samples > 0 && real_samprate > 0) {
        double *vel = calloc(num_samples, sizeof(double)), *disp_raw = calloc(num_samples, sizeof(double)), *disp_detrend = calloc(num_samples, sizeof(double));
        if (vel && disp_raw && disp_detrend) {
            ptr = req.pBuf; long idx = 0;
            while (ptr < req.pBuf + req.actLen) {
                TRACE2_HEADER *trh = (TRACE2_HEADER *)ptr; int32_t *data = (int32_t *)(ptr + sizeof(TRACE2_HEADER));
                for (int i = 0; i < trh->nsamp && idx < num_samples; i++) { vel[idx] = (double)data[i] / dSens; idx++; }
                ptr += sizeof(TRACE2_HEADER) + trh->nsamp * sizeof(int32_t);
            }
            double total_disp = 0.0;
            for (long i = 0; i < num_samples - 1; i++) { disp_raw[i] = total_disp + (1.0 / real_samprate) * 0.5 * (vel[i] + vel[i+1]); total_disp = disp_raw[i]; }

            long noise_samples = (long)(noise_window * real_samprate); if (noise_samples > num_samples) noise_samples = num_samples;
            double noise_rms = 0.0; for (long i = 0; i < noise_samples; i++) noise_rms += (vel[i] * vel[i]);
            if (noise_samples > 0) noise_rms = sqrt(noise_rms / (double)noise_samples); if (noise_rms == 0.0) noise_rms = 1e-9; 

            double *signal_start = disp_raw + noise_samples, *vel_start = vel + noise_samples; long signal_samples = num_samples - noise_samples;

            if (signal_samples > 0 && detrend(WindowDuration, 1.0 / real_samprate, signal_start, signal_samples, noise_rms, vel_start, disp_detrend, 1, 3.5) >= 0) {
                double win_len = wavelet_decomp(WindowDuration, disp_detrend, signal_samples, 1.0 / real_samprate);
                int j_sp = 0; for (j_sp = 0; j_sp < 160; j_sp++) { if (dSPDist_table[j_sp] >= dDelta) break; }
                if (win_len <= (double)j_sp || j_sp >= 159) {
                    double h1 = 0.0, h2 = 0.0; int n1 = 0, n2 = 0;
                    integrate(&h1, &h2, &n1, &n2, signal_samples, disp_detrend, 1.0 / real_samprate, win_len);
                    double mwp_real = ComputeMwpMag(fabs(h1 - h2), dDelta);
                    if (mwp_real > 0.0) {
                        if (Debug) logit("t", " -> [%s] MWp=%.2f\n", sta, mwp_real);
                        RegistrarMagnitudEstacion(event_id, sta, mwp_real, CALC_TYPE_MWP);
                    }
                }
            }
            free(vel); free(disp_raw); free(disp_detrend);
        }
    }
    free(trace_buffer);
}

void ProcesarEstacionMl(const char *event_id, const char *sta, const char *net, const char *comp, const char *loc, double dDelta, double dSens, double p_starttime, double endtime) {
    if (dDelta > 4.0) return; 
    char *trace_buffer = malloc(MAX_TRACE_BUF); if (!trace_buffer) return;

    TRACE_REQ req; memset(&req, 0, sizeof(TRACE_REQ));
    strcpy(req.sta, sta); strcpy(req.net, net); strcpy(req.chan, comp); strcpy(req.loc, loc);
    req.reqStarttime = p_starttime; req.reqEndtime = endtime; req.pBuf = trace_buffer; req.bufLen = MAX_TRACE_BUF; req.timeout = WsTimeout; req.fill = 0; 

    int ws_res = -1, retries = 0;
    while (retries < MAX_WS_RETRIES) {
        if (ws_menu.head == NULL) ConnectToWaveServer();
        ws_res = wsGetTraceBinL(&req, &ws_menu, WsTimeout);
        if (ws_res == WS_ERR_NONE) break; 
        if (ws_res == WS_ERR_BROKEN_CONNECTION || ws_res == WS_ERR_TIMEOUT || ws_res == WS_ERR_NO_CONNECTION) { retries++; wsKillMenu(&ws_menu); ws_menu.head = NULL; sleep_ew(1000); } else break; 
    }
    if (ws_res != WS_ERR_NONE) { free(trace_buffer); return; }

    double real_samprate = 0.0; long num_samples = 0; char *ptr = req.pBuf;
    while (ptr < req.pBuf + req.actLen) {
        TRACE2_HEADER *trh = (TRACE2_HEADER *)ptr;
        if (real_samprate == 0.0) real_samprate = trh->samprate;
        num_samples += trh->nsamp; ptr += sizeof(TRACE2_HEADER) + trh->nsamp * sizeof(int32_t);
    }

    if (num_samples > 0 && real_samprate > 0) {
        double *raw_data = malloc(num_samples * sizeof(double));
        if (raw_data) {
            ptr = req.pBuf; long idx = 0;
            while (ptr < req.pBuf + req.actLen) {
                TRACE2_HEADER *trh = (TRACE2_HEADER *)ptr; int32_t *data = (int32_t *)(ptr + sizeof(TRACE2_HEADER));
                for (int i = 0; i < trh->nsamp && idx < num_samples; i++) { raw_data[idx] = (double)data[i]; idx++; }
                ptr += sizeof(TRACE2_HEADER) + trh->nsamp * sizeof(int32_t);
            }

            double dc_offset = 0.0; for(long i=0; i<num_samples; i++) dc_offset += raw_data[i]; dc_offset /= (double)num_samples;
            double max_ground_motion = 0.0, best_period = 0.0, best_amp_counts = 0.0;
            int last_sign = 0; long cycle_start = 0; double current_max_amp = 0.0;

            for (long i = 0; i < num_samples; i++) {
                double val = raw_data[i] - dc_offset;
                int sign = (val >= 0.0) ? 1 : -1;
                if (i == 0) { last_sign = sign; cycle_start = 0; current_max_amp = fabs(val); }
                else {
                    if (sign != last_sign) {
                        double T = 2.0 * (double)(i - cycle_start) / real_samprate;
                        if (T >= 0.1 && T <= 4.0) {
                            long lPer = (long)(T * 10.0 + 0.5);
                            if (lPer < 3) lPer = 3; if (lPer > 30) lPer = 30; 
                            double gm = MbMlGroundMotion(dSens, lPer, (long)current_max_amp);
                            if (gm > max_ground_motion) { max_ground_motion = gm; best_period = (double)lPer / 10.0; best_amp_counts = current_max_amp; }
                        }
                        cycle_start = i; current_max_amp = fabs(val); last_sign = sign;
                    } else if (fabs(val) > current_max_amp) current_max_amp = fabs(val);
                }
            }

            if (max_ground_motion > 0.0 && best_period > 0.0) {
                double ml_real = ComputeMlMag(max_ground_motion, best_period, dDelta);
                if (ml_real > 0.0) {
                    if (Debug) logit("t", " -> [%s] ML=%.2f (Amp: %.0f cts, Gm: %.2e nm)\n", sta, ml_real, best_amp_counts, max_ground_motion);
                    RegistrarMagnitudEstacion(event_id, sta, ml_real, CALC_TYPE_ML);
                }
            }
            free(raw_data);
        }
    }
    free(trace_buffer);
}

void Lookup(void) {
    if (GetLocalInst(&MyInstId) != 0 || GetModId(MyModName, &MyModId) != 0 ||
        GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0 || GetType("TYPE_ERROR", &TypeError) != 0 ||
        GetType("TYPE_HYP2000ARC", &TypeHyp2000Arc) != 0 || GetType("TYPE_MAGNITUDE", &TypeMagnitude) != 0) {
        fprintf(stderr, "csnmags_toy: Error de Lookup. (Falta TYPE_MAGNITUDE?)\n"); exit(-1);
    }
}

void Status(unsigned char type, short ierr, char *note) {
    MSG_LOGO logo; char msg[256]; time_t t;
    logo.instid = MyInstId; logo.mod = MyModId; logo.type = type; time(&t);
    if (type == TypeHeartBeat) sprintf(msg, "%ld %d\n", (long) t, MyPid);
    else if (type == TypeError) sprintf(msg, "%ld %hd %s\n", (long) t, ierr, note);
    tport_putmsg(&Region, &logo, strlen(msg), msg);
}

int ReadConfig(char *configfile) {
    int ncommand = 6, nmiss = 0, i; char *com, *str, init[10] = {0};
    if (!k_open(configfile)) return -1;
    while (k_rd()) {
        com = k_str(); if (!com || com[0] == '#') continue;
        if (k_its("MyModuleId")) { str = k_str(); if (str) strcpy(MyModName, str); init[0] = 1; }
        else if (k_its("RingName")) { str = k_str(); if (str) { strcpy(RingName, str); if ((OutKey = GetKey(str)) == -1) return -1; } init[1] = 1; }
        else if (k_its("HeartBeatInt")) HeartbeatInt = k_int();
        else if (k_its("LogFile")) LogFile = k_int();
        else if (k_its("WsIP")) { str = k_str(); if (str) strcpy(WsIP, str); init[2] = 1; }
        else if (k_its("WsPort")) { str = k_str(); if (str) strcpy(WsPort, str); }
        else if (k_its("WsTimeout")) WsTimeout = k_int();
        else if (k_its("WindowDuration")) WindowDuration = k_val();
        else if (k_its("StaFile")) { str = k_str(); if (str) strcpy(StaFile, str); init[3] = 1; }
        else if (k_its("SpFilterFile")) { str = k_str(); if (str) strcpy(SpFilterFile, str); init[4] = 1; } 
        else if (k_its("SpDistFile")) { str = k_str(); if (str) strcpy(SpDistFile, str); init[5] = 1; } 
        else if (k_its("Debug")) Debug = k_int();
        else continue;
        if (k_err()) return -1;
    }
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close(); return (nmiss > 0) ? -1 : 0;
}
