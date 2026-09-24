/******************************************************************************
 * ew2glass3.c                                                                *
 * *
 * Módulo Earthworm nativo para conectar el anillo de picks (PICK_RING) con   *
 * el asociador GLASS3 a través de Apache Kafka.                              *
 * *
 * Reemplaza la necesidad de ew2openapi y scripts en Python.                  *
 * Lee mensajes TYPE_PICK_SCNL, los formatea a JSON (DetectionFormats) y      *
 * los inyecta en el topic configurado de Kafka.                              *
 * Traduce el peso (0-4) a probabilidad (1.0 - 0.1).                          *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <math.h>          /* Agregado para la función floor() */

#include <earthworm.h>
#include <transport.h>
#include <trace_buf.h>
#include <kom.h>
#include <librdkafka/rdkafka.h>
#include <json-c/json.h>

/* Parche de compatibilidad para Earthworm 8.0 */
#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif

#define MAX_STR 256
#define MAX_JSON_SIZE 2048

/* Variables globales de configuración */
char MyModName[MAX_STR] = "MOD_EW2GLASS3";
char InRingName[MAX_STR] = "PICK_RING";
long InKey;
int  HeartbeatInt = 30;
int  LogFile = 1;
int  Debug = 0;
char KafkaBrokers[MAX_STR] = "localhost:9092";
char KafkaTopic[MAX_STR] = "glass3_input_topic";
char AgencyID[MAX_STR] = "CL";
char Author[MAX_STR] = "ew2glass3";

/* Variables de Earthworm */
unsigned char MyInstId;
unsigned char MyModId;
unsigned char TypeHeartBeat;
unsigned char TypeError;
unsigned char TypePickSCNL;
SHM_INFO      InRegion;
pid_t         MyPid;

/* Variables de Kafka */
rd_kafka_t *rk = NULL;
rd_kafka_topic_t *rkt = NULL;

/* Prototipos */
int  ReadConfig(char *configfile);
void Lookup(void);
void Status(unsigned char type, short ierr, char *note);
void ConvertAndSendPick(char *msg);
double WeightToProbability(int weight);
void DeliveryReportCallback(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque);
void SigHandler(int sig);

/*-------------------------------------------------------------------------
 * main()
 *-------------------------------------------------------------------------*/
int main(int argc, char **argv) {
    MSG_LOGO  getlogo;
    MSG_LOGO  msglogo;
    long      msgSize;
    char      msg[MAX_JSON_SIZE];
    int       res;
    time_t    timeNow;
    time_t    timeLastBeat = 0;
    char      errText[256];
    rd_kafka_conf_t *conf;

    /* Verificar argumentos */
    if (argc != 2) {
        fprintf(stderr, "Uso: ew2glass3 <configfile>\n");
        exit(1);
    }

    /* Inicializar logging */
    logit_init(argv[1], 0, 256, 1);

    /* Leer configuración */
    if (ReadConfig(argv[1]) < 0) {
        logit("e", "ew2glass3: Error leyendo archivo de configuración %s\n", argv[1]);
        exit(1);
    }

    /* Obtener IDs de Earthworm */
    Lookup();

    /* Re-inicializar logging con el ID del módulo correcto */
    logit_init(argv[1], MyModId, 256, LogFile);
    logit("t", "ew2glass3: Inicializando...\n");

    MyPid = getpid();

    /* Instalar manejadores de señales (para apagar Kafka limpiamente si Startstop manda TERM) */
    signal(SIGINT, SigHandler);
    signal(SIGTERM, SigHandler);

    /* -------------------------------------------------------------
     * Configuración de Kafka (Producer)
     * ------------------------------------------------------------- */
    conf = rd_kafka_conf_new();
    if (rd_kafka_conf_set(conf, "bootstrap.servers", KafkaBrokers, errText, sizeof(errText)) != RD_KAFKA_CONF_OK) {
        logit("e", "ew2glass3: Error en config Kafka (brokers): %s\n", errText);
        exit(1);
    }

    /* Callback para confirmar que los mensajes llegaron (opcional pero recomendado) */
    rd_kafka_conf_set_dr_msg_cb(conf, DeliveryReportCallback);

    /* Crear la instancia de productor Kafka */
    rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errText, sizeof(errText));
    if (!rk) {
        logit("e", "ew2glass3: Fallo al crear el productor Kafka: %s\n", errText);
        exit(1);
    }

    /* Crear objeto Topic (el destino) */
    rkt = rd_kafka_topic_new(rk, KafkaTopic, NULL);
    if (!rkt) {
        logit("e", "ew2glass3: Fallo al crear topic Kafka: %s\n", rd_kafka_err2str(rd_kafka_last_error()));
        rd_kafka_destroy(rk);
        exit(1);
    }

    logit("t", "ew2glass3: Conectado a Kafka Brokers: %s, Topic: %s\n", KafkaBrokers, KafkaTopic);
    logit("t", "ew2glass3: Metadatos cargados desde archivo .d -> AgencyID: %s, Author: %s\n", AgencyID, Author);

    /* Conectarse al anillo de entrada */
    tport_attach(&InRegion, InKey);
    logit("t", "ew2glass3: Conectado a anillo de entrada de memoria compartida: %ld\n", InKey);

    /* Definir qué mensajes queremos escuchar (TYPE_PICK_SCNL) */
    getlogo.instid = INST_WILDCARD;
    getlogo.mod    = MOD_WILDCARD;
    getlogo.type   = TypePickSCNL;

    /* Flush del anillo inicial (no procesar picks viejos) */
    while (tport_getmsg(&InRegion, &getlogo, 1, &msglogo, &msgSize, msg, sizeof(msg) - 1) != GET_NONE);

    /* Loop principal */
    while (tport_getflag(&InRegion) != TERMINATE && tport_getflag(&InRegion) != MyPid) {

        /* Enviar latido (Heartbeat) */
        time(&timeNow);
        if (timeNow - timeLastBeat >= HeartbeatInt) {
            timeLastBeat = timeNow;
            Status(TypeHeartBeat, 0, "");
        }

        /* Poll de eventos de Kafka (para llamar al DeliveryReportCallback y liberar memoria) */
        rd_kafka_poll(rk, 0);

        /* Buscar mensajes en el anillo */
        res = tport_getmsg(&InRegion, &getlogo, 1, &msglogo, &msgSize, msg, sizeof(msg) - 1);

        if (res == GET_NONE) {
            sleep_ew(50); /* Dormir 50ms para no quemar CPU */
            continue;
        }

        if (res == GET_TOOBIG) {
            sprintf(errText, "Mensaje recuperado demasiado grande (%ld bytes)", msgSize);
            Status(TypeError, 1, errText);
            continue;
        }

        if (res == GET_MISS || res == GET_NOTRACK) {
            Status(TypeError, 2, "Mensaje(s) perdidos o error de tracking.");
        }

        /* Asegurar terminación nula */
        msg[msgSize] = '\0';

        /* ¡Llegó un pick! Formatearlo a JSON y enviarlo a Kafka */
        if (msglogo.type == TypePickSCNL) {
            ConvertAndSendPick(msg);
        }
    }

    /* Limpieza final */
    logit("t", "ew2glass3: Terminando, vaciando cola de Kafka...\n");
    tport_detach(&InRegion);
    
    /* Esperar hasta 10 segundos para enviar los mensajes que queden en la cola */
    rd_kafka_flush(rk, 10000); 
    
    rd_kafka_topic_destroy(rkt);
    rd_kafka_destroy(rk);

    logit("t", "ew2glass3: Modulo terminado.\n");
    return 0;
}

/*-------------------------------------------------------------------------
 * ConvertAndSendPick()
 * Toma un string TYPE_PICK_SCNL, extrae datos, crea JSON y lo envia a Kafka.
 * Formato pick_scnl:
 * type mod inst seq S.C.N.L fm_wt time amp1 amp2 amp3
 * Ej: 8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0
 *-------------------------------------------------------------------------*/
void ConvertAndSendPick(char *msg) {
    int type, mod, inst, seq;
    char scnl[64], fmwt[4], t_str[32];
    char sta[16], chan[16], net[16], loc[16], phase[8];
    int weight = 4;
    char time_iso[32];

    /* Parsear el mensaje TYPE_PICK_SCNL estándar de Earthworm */
    int parsed = sscanf(msg, "%d %d %d %d %63s %3s %31s", 
                        &type, &mod, &inst, &seq, scnl, fmwt, t_str);
    
    if (parsed < 7) {
        if (Debug) logit("e", "ew2glass3: Error parseando pick (campos insuficientes): %s\n", msg);
        return;
    }

    /* Desglosar S.C.N.L (Estacion.Canal.Red.Localizacion) */
    int scnl_parsed = sscanf(scnl, "%15[^.].%15[^.].%15[^.].%15s", sta, chan, net, loc);
    if (scnl_parsed < 3) {
        if (Debug) logit("e", "ew2glass3: Error parseando formato SCNL: %s\n", scnl);
        return;
    }
    if (scnl_parsed == 3 || strcmp(loc, "--") == 0) {
        strcpy(loc, "");
    }

    /* Obtener el peso (Weight) del string fmwt (Ej: "U1", "?2", "D3") */
    if (strlen(fmwt) >= 2) {
        weight = fmwt[1] - '0';
        if (weight < 0 || weight > 4) weight = 4;
    }

    /* Fase por defecto (En Earthworm los picks primarios siempre se asumen "P") */
    strcpy(phase, "P");

    /* Formatear el tiempo a ISO 8601 (GLASS3 DetectionFormats) */
    /* Entrada cruda:  20260309163736.490 */
    /* Salida ISO:     2026-03-09T16:37:36.490Z */
    if (strlen(t_str) >= 18) {
        snprintf(time_iso, sizeof(time_iso), "%.4s-%.2s-%.2sT%.2s:%.2s:%.2s.%sZ",
                 t_str, t_str + 4, t_str + 6, t_str + 8, t_str + 10, t_str + 12, t_str + 15);
    } else {
        if (Debug) logit("e", "ew2glass3: Formato de tiempo invalido: %s\n", t_str);
        return;
    }

    /* Convertir peso de EW (0=Excelente, 4=Malo) a Probabilidad Bayesiana para GLASS3 */
    double phase_prob = WeightToProbability(weight);

    /* Generar ID Unico y persistente (formato: EW_INSTID_MODID_SEQNUM) 
     * ¡ESTO ES VITAL PARA QUE GLASS3TOEW PUEDA LEER EL PICK_ID ORIGINAL LUEGO! */
    char pick_id[64];
    sprintf(pick_id, "%d_%d_%d", inst, mod, seq);

    /* -------------------------------------------------------------
     * CONSTRUCCION DEL JSON (Librería json-c)
     * Esqueleto estricto DetectionFormats
     * ------------------------------------------------------------- */
    struct json_object *jobj = json_object_new_object();
    struct json_object *jsite = json_object_new_object();
    struct json_object *jsource = json_object_new_object();
    struct json_object *jclass = json_object_new_object();

    /* Objeto "Site" */
    json_object_object_add(jsite, "Station", json_object_new_string(sta));
    json_object_object_add(jsite, "Channel", json_object_new_string(chan));
    json_object_object_add(jsite, "Network", json_object_new_string(net));
    json_object_object_add(jsite, "Location", json_object_new_string(loc));

    /* Objeto "Source" */
    json_object_object_add(jsource, "AgencyID", json_object_new_string(AgencyID));
    json_object_object_add(jsource, "Author", json_object_new_string(Author));

    /* Objeto "ClassificationInfo" (ESENCIAL PARA EL BAYES DE GLASS3) */
    json_object_object_add(jclass, "Phase", json_object_new_string(phase));
    json_object_object_add(jclass, "PhaseProbability", json_object_new_double(phase_prob));

    /* Objeto Raíz */
    json_object_object_add(jobj, "Type", json_object_new_string("Pick"));
    json_object_object_add(jobj, "ID", json_object_new_string(pick_id));
    json_object_object_add(jobj, "Site", jsite);
    json_object_object_add(jobj, "Source", jsource);
    json_object_object_add(jobj, "Time", json_object_new_string(time_iso));
    json_object_object_add(jobj, "Phase", json_object_new_string(phase));
    json_object_object_add(jobj, "ClassificationInfo", jclass);

    /* Extraer el string JSON final */
    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PLAIN);
    
    if (Debug) {
        logit("e", "ew2glass3: Enviando -> %s\n", json_string);
    }

    /* -------------------------------------------------------------
     * ENVIO A KAFKA
     * ------------------------------------------------------------- */
retry:
    if (rd_kafka_produce(
            rkt,
            RD_KAFKA_PARTITION_UA, /* Partición automática */
            RD_KAFKA_MSG_F_COPY,   /* Copiar el payload internamente (librdkafka gestiona la memoria) */
            (void *)json_string, strlen(json_string),
            NULL, 0,               /* Clave de enrutamiento opcional */
            NULL) == -1) {
                
        fprintf(stderr, "%% Fallo la inyeccion del mensaje: %s\n", rd_kafka_err2str(rd_kafka_last_error()));
        
        /* Si la cola interna de Kafka está llena, vaciamos con poll y reintentamos */
        if (rd_kafka_last_error() == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
            rd_kafka_poll(rk, 1000);
            goto retry;
        }
    } else {
        /* Produce fue exitoso y el mensaje está encolado para enviarse */
        rd_kafka_poll(rk, 0); /* Sirve callbacks */
    }

    /* Liberar el objeto JSON de la memoria */
    json_object_put(jobj);
}

/*-------------------------------------------------------------------------
 * WeightToProbability()
 * Convierte el peso del picker nativo a probabilidad
 *-------------------------------------------------------------------------*/
double WeightToProbability(int weight) {
    switch(weight) {
        case 0: return 1.0;
        case 1: return 0.8;
        case 2: return 0.5;
        case 3: return 0.2;
        default: return 0.1;
    }
}

/*-------------------------------------------------------------------------
 * DeliveryReportCallback()
 * Función llamada por Kafka asíncronamente cuando el mensaje es entregado
 * (o falla definitivamente tras reintentos).
 *-------------------------------------------------------------------------*/
void DeliveryReportCallback(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque) {
    if (rkmessage->err) {
        logit("e", "ew2glass3: Error en la entrega del mensaje a Kafka: %s\n", rd_kafka_err2str(rkmessage->err));
    }
    /* Si Debug está en 2 o más, podría imprimir acuses de recibo exitosos */
}

/*-------------------------------------------------------------------------
 * SigHandler()
 * Para atrapar Ctrl+C o señales de apagado
 *-------------------------------------------------------------------------*/
void SigHandler(int sig) {
    /* Al recibir TERM, Earthworm setea el flag de InRegion, así que el while saldrá 
       naturalmente. Aquí no hacemos mucho más que avisar. */
    logit("t", "ew2glass3: Recibida señal de apagado (%d)\n", sig);
}

/*-------------------------------------------------------------------------
 * Status()
 * Construye y envía latidos y mensajes de error al anillo
 *-------------------------------------------------------------------------*/
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
        logit("et", "ew2glass3: Error: %s\n", note);
    }

    size = strlen(msg);
    if (tport_putmsg(&InRegion, &logo, size, msg) != PUT_OK) {
        logit("et", "ew2glass3: Error enviando mensaje de estado/latido.\n");
    }
}

/*-------------------------------------------------------------------------
 * Lookup()
 * Busca los identificadores numéricos en los archivos de EW
 *-------------------------------------------------------------------------*/
void Lookup(void) {
    if (GetLocalInst(&MyInstId) != 0) {
        fprintf(stderr, "ew2glass3: Error obteniendo MyInstId.\n");
        exit(-1);
    }
    if (GetModId(MyModName, &MyModId) != 0) {
        fprintf(stderr, "ew2glass3: Módulo inválido <%s>.\n", MyModName);
        exit(-1);
    }
    if (GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0) {
        fprintf(stderr, "ew2glass3: Tipo inválido <TYPE_HEARTBEAT>.\n");
        exit(-1);
    }
    if (GetType("TYPE_ERROR", &TypeError) != 0) {
        fprintf(stderr, "ew2glass3: Tipo inválido <TYPE_ERROR>.\n");
        exit(-1);
    }
    if (GetType("TYPE_PICK_SCNL", &TypePickSCNL) != 0) {
        fprintf(stderr, "ew2glass3: Tipo inválido <TYPE_PICK_SCNL>.\n");
        exit(-1);
    }
}

/*-------------------------------------------------------------------------
 * ReadConfig()
 * Parsea el archivo de configuración .d usando kom.c
 *-------------------------------------------------------------------------*/
int ReadConfig(char *configfile) {
    int     ncommand;     /* Numero de comandos requeridos (puedes ajustar) */
    char    init[10];     /* Flags para los requeridos */
    int     nmiss;
    char    *com;
    char    *str;
    int     i;

    ncommand = 4;
    for (i = 0; i < ncommand; i++) init[i] = 0;

    if (!k_open(configfile)) {
        fprintf(stderr, "ew2glass3: Error abriendo archivo comando <%s>.\n", configfile);
        return -1;
    }

    while (k_rd()) {
        com = k_str();
        if (!com) continue;
        if (com[0] == '#') continue;

        if (k_its("MyModuleId")) {
            str = k_str();
            if (str) strcpy(MyModName, str);
            init[0] = 1;
        } else if (k_its("InRing")) {
            str = k_str();
            if (str) {
                strcpy(InRingName, str);
                if ((InKey = GetKey(str)) == -1) {
                    fprintf(stderr, "ew2glass3: Anillo de entrada invalido <%s>\n", str);
                    return -1;
                }
            }
            init[1] = 1;
        } else if (k_its("HeartBeatInt")) {
            HeartbeatInt = k_int();
            init[2] = 1;
        } else if (k_its("LogFile")) {
            LogFile = k_int();
            init[3] = 1;
        } else if (k_its("Debug")) {
            Debug = k_int();
        } else if (k_its("KafkaBrokers")) {
            str = k_str();
            if (str) strcpy(KafkaBrokers, str);
        } else if (k_its("KafkaTopic")) {
            str = k_str();
            if (str) strcpy(KafkaTopic, str);
        } else if (k_its("AgencyID")) {
            str = k_str();
            if (str) strcpy(AgencyID, str);
        } else if (k_its("Author")) {
            str = k_str();
            if (str) strcpy(Author, str);
        } else {
            fprintf(stderr, "ew2glass3: Comando desconocido <%s> en <%s>\n", com, configfile);
            continue;
        }

        if (k_err()) {
            fprintf(stderr, "ew2glass3: Comando mal formado en <%s>.\n", configfile);
            return -1;
        }
    }
    nmiss = 0;
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;

    if (nmiss) {
        fprintf(stderr, "ew2glass3: ERROR, faltan comandos requeridos en <%s>.\n", configfile);
        return -1;
    }

    k_close();
    return 0;
}
