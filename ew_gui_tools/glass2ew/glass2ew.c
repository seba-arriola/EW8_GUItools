/******************************************************************************
 * glass2ew.c                                                                 *
 * *
 * Módulo Earthworm nativo que actúa como el puente definitivo de regreso.    *
 * Se suscribe a Kafka, lee los eventos JSON de GLASS3, y construye           *
 * internamente el formato Hypoinverse-2000 (Archive).                        *
 * *
 * FIX: Calculo matematico manual del RMS extrayendo los residuales por fase  *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <signal.h>

#include <earthworm.h>
#include <transport.h>  
#include <kom.h>        
#include <librdkafka/rdkafka.h>
#include <json-c/json.h>

#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif

#define MAX_STR 256

/* Variables globales de configuracion */
char MyModName[MAX_STR] = "MOD_GLASS2EW";
char OutRingName[MAX_STR] = "HYPO_RING";
long OutKey = 0; 
int  HeartbeatInt = 30;
int  LogFile = 1;
int  Debug = 1;
char KafkaBrokers[MAX_STR] = "localhost:9092";
char KafkaTopic[MAX_STR] = "glass_locations";
char KafkaGroupId[MAX_STR] = "ew_hypo_bridge";

unsigned char MyInstId;
unsigned char MyModId;
unsigned char TypeHeartBeat;
unsigned char TypeError;
unsigned char TypeHyp2000Arc; 

SHM_INFO      OutRegion;
pid_t         MyPid;
int           Terminate = 0;

int  ReadConfig(char *configfile);
void Lookup(void);
void Status(unsigned char type, short ierr, char *note);
void ProcessGlass3EventAndRing(const char *json_payload);
void SigHandler(int sig);

int main(int argc, char **argv) {
    time_t timeNow, timeLastBeat = 0;
    char errText[512];
    rd_kafka_t *rk;
    rd_kafka_conf_t *conf;
    rd_kafka_topic_partition_list_t *subscription;
    rd_kafka_resp_err_t err;

    if (argc != 2) { 
        fprintf(stderr, "Uso: glass2ew <configfile>\n"); 
        exit(1); 
    }

    if (ReadConfig(argv[1]) < 0) { 
        fprintf(stderr, ">> FATAL: Error crítico leyendo archivo config %s\n", argv[1]); 
        exit(1); 
    }

    Lookup();

    logit_init(argv[1], MyModId, 256, LogFile);
    logit("t", "glass2ew: Iniciando inyeccion directa a %s...\n", OutRingName);

    MyPid = getpid();
    signal(SIGINT, SigHandler);
    signal(SIGTERM, SigHandler);

    conf = rd_kafka_conf_new();
    rd_kafka_conf_set(conf, "bootstrap.servers", KafkaBrokers, errText, sizeof(errText));
    rd_kafka_conf_set(conf, "group.id", KafkaGroupId, errText, sizeof(errText));
    rd_kafka_conf_set(conf, "auto.offset.reset", "latest", errText, sizeof(errText));

    rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errText, sizeof(errText));
    if (!rk) {
        fprintf(stderr, ">> FATAL: Falla iniciando Kafka: %s\n", errText);
        exit(1);
    }

    rd_kafka_poll_set_consumer(rk);
    subscription = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_list_add(subscription, KafkaTopic, RD_KAFKA_PARTITION_UA);
    err = rd_kafka_subscribe(rk, subscription);
    if (err) {
        fprintf(stderr, ">> FATAL: Falla en subscripcion a topico Kafka: %s\n", rd_kafka_err2str(err));
        exit(1);
    }
    rd_kafka_topic_partition_list_destroy(subscription);

    if (OutKey <= 0) {
        fprintf(stderr, ">> FATAL: Llave OutKey de OutRing es <= 0. Falta configuracion de Anillo.\n");
        exit(1);
    }
    tport_attach(&OutRegion, OutKey);

    while (!Terminate && tport_getflag(&OutRegion) != TERMINATE && tport_getflag(&OutRegion) != MyPid) {
        time(&timeNow);
        if (timeNow - timeLastBeat >= HeartbeatInt) {
            timeLastBeat = timeNow;
            Status(TypeHeartBeat, 0, "");
        }
        rd_kafka_message_t *rkmessage = rd_kafka_consumer_poll(rk, 500);
        if (rkmessage) {
            if (!rkmessage->err) {
                char *payload = strndup((const char *)rkmessage->payload, rkmessage->len);
                if (payload) {
                    ProcessGlass3EventAndRing(payload);
                    free(payload);
                }
            }
            rd_kafka_message_destroy(rkmessage);
        }
    }
    tport_detach(&OutRegion);
    rd_kafka_consumer_close(rk);
    rd_kafka_destroy(rk);
    return 0;
}

void pad_string(char *dest, const char *src, int length) {
    int i;
    int srclen = src ? strlen(src) : 0;
    for (i = 0; i < length; i++) {
        if (i < srclen) dest[i] = src[i];
        else dest[i] = ' ';
    }
}

void ProcessGlass3EventAndRing(const char *json_payload) {
    struct json_object *parsed_json, *type_obj, *lat_obj, *lon_obj, *depth_obj, *time_obj, *data_array = NULL;
    
    parsed_json = json_tokener_parse(json_payload);
    if (!parsed_json) return;

    if (!json_object_object_get_ex(parsed_json, "Type", &type_obj) && 
        !json_object_object_get_ex(parsed_json, "Cmd", &type_obj)) {
        json_object_put(parsed_json); return;
    }
    
    const char *msg_type = json_object_get_string(type_obj);
    if (strcmp(msg_type, "Event") != 0 && strcmp(msg_type, "Hypo") != 0 && strcmp(msg_type, "Detection") != 0) {
        json_object_put(parsed_json); return;
    }

    double lat = 0.0, lon = 0.0, depth = 0.0;
    const char *o_time_str = "";
    struct json_object *hypo_obj = NULL;
    
    json_object_object_get_ex(parsed_json, "Hypocenter", &hypo_obj);
    
    if (json_object_object_get_ex(parsed_json, "Latitude", &lat_obj)) lat = json_object_get_double(lat_obj);
    else if (hypo_obj && json_object_object_get_ex(hypo_obj, "Latitude", &lat_obj)) lat = json_object_get_double(lat_obj);

    if (json_object_object_get_ex(parsed_json, "Longitude", &lon_obj)) lon = json_object_get_double(lon_obj);
    else if (hypo_obj && json_object_object_get_ex(hypo_obj, "Longitude", &lon_obj)) lon = json_object_get_double(lon_obj);

    if (json_object_object_get_ex(parsed_json, "Depth", &depth_obj)) depth = json_object_get_double(depth_obj);
    else if (hypo_obj && json_object_object_get_ex(hypo_obj, "Depth", &depth_obj)) depth = json_object_get_double(depth_obj);

    if (json_object_object_get_ex(parsed_json, "Time", &time_obj)) o_time_str = json_object_get_string(time_obj);
    else if (hypo_obj && json_object_object_get_ex(hypo_obj, "Time", &time_obj)) o_time_str = json_object_get_string(time_obj);

    if (o_time_str == NULL || strlen(o_time_str) < 18) { json_object_put(parsed_json); return; }

    /* ========================================================================= */
    /* EXTRACCION DE ESTADISTICAS Y CALCULO MANUAL DE RMS                        */
    /* ========================================================================= */
    int nps = 0, gap = 0, dmin = 0, rms_100 = 0;
    struct json_object *tmp_obj, *prop_obj = NULL;

    if (!json_object_object_get_ex(parsed_json, "Data", &data_array)) {
        json_object_object_get_ex(parsed_json, "Phases", &data_array);
    }
    if (data_array) {
        nps = json_object_array_length(data_array);
    }

    json_object_object_get_ex(parsed_json, "Properties", &prop_obj);
    if (!prop_obj) json_object_object_get_ex(parsed_json, "Bayes", &prop_obj);

    struct json_object *search_objs[] = { parsed_json, hypo_obj, prop_obj };
    for(int i=0; i<3; i++) {
        if (!search_objs[i]) continue;
        if (nps == 0 && json_object_object_get_ex(search_objs[i], "NumPhases", &tmp_obj)) nps = json_object_get_int(tmp_obj);
        if (nps == 0 && json_object_object_get_ex(search_objs[i], "PhaseCount", &tmp_obj)) nps = json_object_get_int(tmp_obj);
        
        if (gap == 0 && json_object_object_get_ex(search_objs[i], "Gap", &tmp_obj)) gap = (int)json_object_get_double(tmp_obj);
        if (gap == 0 && json_object_object_get_ex(search_objs[i], "AzimuthalGap", &tmp_obj)) gap = (int)json_object_get_double(tmp_obj);
        
        if (dmin == 0 && json_object_object_get_ex(search_objs[i], "MinimumDistance", &tmp_obj)) dmin = (int)(json_object_get_double(tmp_obj) * 111.19);
        
        if (rms_100 == 0 && json_object_object_get_ex(search_objs[i], "StandardDeviation", &tmp_obj)) rms_100 = (int)(json_object_get_double(tmp_obj) * 100.0);
        if (rms_100 == 0 && json_object_object_get_ex(search_objs[i], "TimeResidual", &tmp_obj)) rms_100 = (int)(json_object_get_double(tmp_obj) * 100.0);
        if (rms_100 == 0 && json_object_object_get_ex(search_objs[i], "RMS", &tmp_obj)) rms_100 = (int)(json_object_get_double(tmp_obj) * 100.0);
        if (rms_100 == 0 && json_object_object_get_ex(search_objs[i], "Fit", &tmp_obj)) rms_100 = (int)(json_object_get_double(tmp_obj) * 100.0);
    }

    /* SI EL RMS ES 0, PROCEDEMOS AL CALCULO MATEMATICO DEL ROOT-MEAN-SQUARE DESDE LAS FASES */
    if (rms_100 == 0 && data_array) {
        double sum_sq_res = 0.0;
        int count_res = 0;
        for (int i = 0; i < nps; i++) {
            struct json_object *pick_obj = json_object_array_get_idx(data_array, i);
            struct json_object *assoc_obj, *res_obj;
            if (json_object_object_get_ex(pick_obj, "AssociationInfo", &assoc_obj)) {
                if (json_object_object_get_ex(assoc_obj, "Residual", &res_obj)) {
                    double r = json_object_get_double(res_obj);
                    sum_sq_res += (r * r); /* Elevar al cuadrado y sumar */
                    count_res++;
                }
            }
        }
        if (count_res > 0) {
            double calc_rms = sqrt(sum_sq_res / count_res); /* Calcular la raiz de la media */
            rms_100 = (int)(calc_rms * 100.0 + 0.5);
            if (Debug) printf(">> CALCULADO RMS MATEMATICO DESDE %d FASES: %.2f\n", count_res, calc_rms);
        }
    }

    int y=0, m=0, d=0, h=0, mn=0;
    double sec = 0.0;
    sscanf(o_time_str, "%4d-%2d-%2dT%2d:%2d:%lf", &y, &m, &d, &h, &mn, &sec);

    char arc_msg[65536] = ""; 
    char line[200];
    char temp[256];
    
    unsigned long event_id_num = 0;
    struct json_object *id_obj;
    const char *id_str_raw = "N/A";
    
    if (json_object_object_get_ex(parsed_json, "ID", &id_obj)) {
        const char *id_str = json_object_get_string(id_obj);
        id_str_raw = id_str;
        for (int k = 0; id_str[k] != '\0'; k++) {
            if (id_str[k] >= '0' && id_str[k] <= '9') {
                event_id_num = event_id_num * 10 + (id_str[k] - '0');
            }
        }
    }
    
    static unsigned long fallback_id = 1;
    if (event_id_num == 0) {
        event_id_num = fallback_id++;
    }

    char temp_id[20];
    unsigned long final_id = event_id_num % 2147000000UL;
    snprintf(temp_id, sizeof(temp_id), "%010lu", final_id);
    
    if (Debug) {
        printf("\n================ INYECTANDO A ANILLO =================\n");
        printf("1. ID Original desde JSON (Glass3): '%s'\n", id_str_raw);
        printf("2. String 10-char inyectado (136):  '%s'\n", temp_id);
        printf("3. Estads: Ph=%d Gap=%d Dmin=%d RMS_100=%d\n", nps, gap, dmin, rms_100);
        printf("======================================================\n");
    }

    char lat_dir = (lat >= 0) ? 'N' : 'S';
    int lat_deg = abs((int)lat);
    int lat_min_100 = (int)((fabs(lat) - lat_deg) * 60.0 * 100.0 + 0.5);

    char lon_dir = (lon >= 0) ? 'E' : 'W';
    int lon_deg = abs((int)lon);
    int lon_min_100 = (int)((fabs(lon) - lon_deg) * 60.0 * 100.0 + 0.5);

    int z_100 = (int)(depth * 100.0 + 0.5);
    int s_100 = (int)(sec * 100.0 + 0.5);

    memset(line, ' ', 162);
    line[162] = '\n';
    line[163] = '\0';
    
    snprintf(temp, sizeof(temp), "%04d%02d%02d%02d%02d%04d%02d%c%04d%03d%c%04d%05d",
             y, m, d, h, mn, s_100, lat_deg, lat_dir, lat_min_100, lon_deg, lon_dir, lon_min_100, z_100);
    memcpy(line, temp, strlen(temp));
    
    char ext_info[20];
    snprintf(ext_info, sizeof(ext_info), "%3d%3d%3d%4d", nps, gap, dmin, rms_100);
    memcpy(line + 39, ext_info, 13);
    
    memcpy(line + 136, temp_id, 10);
    strcat(arc_msg, line);

    memset(line, ' ', 162);
    line[0] = '$'; line[1] = '1';
    line[162] = '\n'; line[163] = '\0';
    strcat(arc_msg, line);

    if (data_array) {
        int num_picks = json_object_array_length(data_array);
        for (int i = 0; i < num_picks; i++) {
            struct json_object *pick_obj = json_object_array_get_idx(data_array, i);
            struct json_object *site_obj, *sta_obj, *chan_obj, *net_obj, *loc_obj, *ptime_obj;
            
            if (json_object_object_get_ex(pick_obj, "Site", &site_obj) &&
                json_object_object_get_ex(pick_obj, "Time", &ptime_obj)) {
                
                const char *sta="", *chan="", *net="", *loc="";
                if (json_object_object_get_ex(site_obj, "Station", &sta_obj)) sta = json_object_get_string(sta_obj);
                if (json_object_object_get_ex(site_obj, "Channel", &chan_obj)) chan = json_object_get_string(chan_obj);
                if (json_object_object_get_ex(site_obj, "Network", &net_obj)) net = json_object_get_string(net_obj);
                if (json_object_object_get_ex(site_obj, "Location", &loc_obj)) loc = json_object_get_string(loc_obj);
                
                const char *loc_str = (strlen(loc) > 0) ? loc : "--";

                int py=0, pm=0, pd=0, ph=0, pmn=0;
                double psec = 0.0;
                sscanf(json_object_get_string(ptime_obj), "%4d-%2d-%2dT%2d:%2d:%lf", &py, &pm, &pd, &ph, &pmn, &psec);

                memset(line, ' ', 114);
                line[114] = '\n';
                line[115] = '\0';

                pad_string(&line[0], sta, 5);      
                pad_string(&line[5], net, 2);      
                line[8] = ' ';                     
                pad_string(&line[9], chan, 3);     
                line[13] = ' ';                    
                line[14] = 'P';                    
                line[15] = ' ';                    
                line[16] = '0';                    

                snprintf(temp, sizeof(temp), "%04d%02d%02d%02d%02d%05.2f", py, pm, pd, ph, pmn, psec);
                memcpy(&line[17], temp, 17);

                pad_string(&line[111], loc_str, 2);

                if (strlen(arc_msg) + strlen(line) + 120 < sizeof(arc_msg) - 200) {
                    strcat(arc_msg, line);
                    
                    memset(line, ' ', 114);
                    line[114] = '\n'; line[115] = '\0';
                    pad_string(&line[0], sta, 5);
                    pad_string(&line[5], net, 2);
                    line[8] = ' ';
                    pad_string(&line[9], chan, 3);
                    line[104] = '$';               
                    line[105] = '1';               
                    pad_string(&line[111], loc_str, 2); 
                    strcat(arc_msg, line);
                } else {
                    break;
                }
            }
        }
    }

    memset(line, ' ', 114);
    line[114] = '\n';
    line[115] = '\0';
    strcat(arc_msg, line); 

    MSG_LOGO logo;
    logo.instid = MyInstId;
    logo.mod    = MyModId;
    logo.type   = TypeHyp2000Arc;

    if (tport_putmsg(&OutRegion, &logo, strlen(arc_msg), arc_msg) != PUT_OK) {
        logit("et", "glass2ew: [ERROR CRITICO] Fallo enviando ARC al anillo.\n");
    } else {
        logit("t", "glass2ew: [EXITO] Sismo ID %lu en anillo %s.\n", final_id, OutRingName);
    }
    
    json_object_put(parsed_json);
}

void SigHandler(int sig) { Terminate = 1; }
void Status(unsigned char type, short ierr, char *note) {
    MSG_LOGO    logo;
    char        msg[256];
    long        size;
    time_t      t;

    logo.instid = MyInstId;
    logo.mod    = MyModId;
    logo.type   = type;

    time(&t);
    
    if (type == TypeHeartBeat) {
        sprintf(msg, "%ld %d\n", (long) t, MyPid);
    } else if (type == TypeError) {
        sprintf(msg, "%ld %hd %s\n", (long) t, ierr, note);
        logit("et", "glass2ew: Error: %s\n", note);
    }

    size = strlen(msg);
    tport_putmsg(&OutRegion, &logo, size, msg);
}

void Lookup(void) {
    if (GetLocalInst(&MyInstId) != 0) { fprintf(stderr, ">> FATAL: Falla GetLocalInst\n"); exit(-1); }
    if (GetModId(MyModName, &MyModId) != 0) { fprintf(stderr, ">> FATAL: Módulo inválido <%s>\n", MyModName); exit(-1); }
    if (GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0) { fprintf(stderr, ">> FATAL: Tipo inválido <TYPE_HEARTBEAT>\n"); exit(-1); }
    if (GetType("TYPE_ERROR", &TypeError) != 0) { fprintf(stderr, ">> FATAL: Tipo inválido <TYPE_ERROR>\n"); exit(-1); }
    if (GetType("TYPE_HYP2000ARC", &TypeHyp2000Arc) != 0) { fprintf(stderr, ">> FATAL: Tipo inválido <TYPE_HYP2000ARC>\n"); exit(-1); }
}

int ReadConfig(char *configfile) {
    int ncommand = 2; char init[10] = {0}; char *com, *str; int nmiss = 0, i;

    if (!k_open(configfile)) { fprintf(stderr, ">> Error abriendo config %s\n", configfile); return -1; }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if (k_its("MyModuleId")) { str = k_str(); if (str) strcpy(MyModName, str); init[0] = 1; }
        else if (k_its("OutRing")) { 
            str = k_str(); 
            if (str) { 
                strcpy(OutRingName, str); 
                if ((OutKey = GetKey(str)) == -1) {
                    fprintf(stderr, ">> Error: Anillo invalido <%s>\n", str);
                    return -1;
                }
            }
            init[1] = 1; 
        }
        else if (k_its("HeartBeatInt")) HeartbeatInt = k_int();
        else if (k_its("LogFile")) LogFile = k_int();
        else if (k_its("Debug")) Debug = k_int();
        else if (k_its("KafkaBrokers")) { str = k_str(); if (str) strcpy(KafkaBrokers, str); }
        else if (k_its("KafkaTopic")) { str = k_str(); if (str) strcpy(KafkaTopic, str); }
        else if (k_its("KafkaGroupId")) { str = k_str(); if (str) strcpy(KafkaGroupId, str); }
        else continue;
        if (k_err()) { fprintf(stderr, ">> Error procesando comando en config\n"); return -1; }
    }
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close();
    if (nmiss > 0) fprintf(stderr, ">> Error: Faltan comandos requeridos en config\n");
    return (nmiss > 0) ? -1 : 0;
}
