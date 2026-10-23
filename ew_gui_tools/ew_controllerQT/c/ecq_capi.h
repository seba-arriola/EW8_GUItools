#ifndef ECQ_CAPI_H
#define ECQ_CAPI_H

/* ew_controllerQT - frontera UNICA entre la capa C (c/) y la capa C++/Qt (ui/).
 *
 * Reglas (heredadas de csnhypodbpQT, docs/testLibQT.md R1/R2):
 *   R1. Ningun TU C++ incluye headers de EarthWorm/libpq/GTK/cairo: solo este
 *       header (y los de ewguiqt/).
 *   R2. Todo lo declarado aqui va en `extern "C"`.
 *   R3. Los handles son opacos y los libera el llamante (new/free, open/close).
 *   R4. Este header no incluye nada con Qt: se compila igual desde C.
 *
 * Hilos: la capa C es MONOHILO. La UI la conduce desde su QTimer, sin locks.
 */

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Configuracion (.d)                                                        */
/* ------------------------------------------------------------------------- */

typedef struct {
    char my_module[64];   /* MyModuleId: MOD_EW_CONTROLLERQT */
    char ring[64];        /* Ring: CONTROL_RING */
    int  heart_beat_int;  /* HeartBeatInt (s) */
    int  poll_int;        /* PollInt (s): cada cuanto se pide estado */
    int  log_file;        /* LogFile 0/1 */
    char log_dir[512];    /* LogDir EFECTIVO: $EW_LOG > .d > "logs" */
    char dpath[512];      /* .d tal como se paso por argv */
} EcqConfig;

/* NULL si el .d falta o le falta una clave obligatoria; `err` lleva el motivo. */
EcqConfig *ecq_config_load(const char *dpath, char *err, size_t errlen);
void ecq_config_free(EcqConfig *cfg);

/* --print-config: una clave por linea, en el orden del .d, sin comentarios. */
void ecq_print_config(const EcqConfig *cfg, FILE *out);

/* ------------------------------------------------------------------------- */
/* Estado del CONTROL_RING (una sola instancia; monohilo)                    */
/* ------------------------------------------------------------------------- */

typedef enum {
    ECQ_ACT_STOP = 0,
    ECQ_ACT_RESTART,
    ECQ_ACT_START,
    ECQ_ACT_RECONFIG
} EcqAction;

typedef struct {
    char name[64];
    int  pid;
    char status[16];     /* Alive / Stop / Dead / Zombie / ? */
    char details[192];   /* cola cruda de la linea de estado */
    char config[64];     /* prefijo del log (basename sin extension) */
    char cfgfile[64];    /* nombre del .d */
} EcqRow;

typedef struct {
    char name[64];
    int  key;
    int  size;
} EcqRingRow;

#define ECQ_MAX_ROWS 256

typedef struct {
    char hostname[128];
    char start[64];
    char current[64];
    char disk[64];
    char version[64];
    int  nmods;
    int  nrings;
    int  seen_status;    /* llego al menos un TYPE_STATUS */
    int  noreply;        /* ahora - ultimo status > PollInt*4 */
    unsigned seq;        /* incrementa con cada status aplicado */
    const EcqRow *mods;      /* valido hasta el siguiente ecq_read */
    const EcqRingRow *rings;
} EcqTable;

typedef struct EcqCore EcqCore;

EcqCore *ecq_core_new(const EcqConfig *cfg);
void     ecq_core_free(EcqCore *core);

/* Resuelve clave/tipos/inst/mod y adjunta el anillo. -1 si el anillo no existe
 * todavia (el llamante reintenta; startstop puede no estar arriba). */
int  ecq_ring_attach(EcqCore *core);
void ecq_ring_detach(EcqCore *core);
int  ecq_ring_attached(const EcqCore *core);

/* Latido + peticion de estado cada PollInt. 1 si toco pedir. */
int  ecq_tick(EcqCore *core, time_t now);
/* Drena el anillo. Devuelve cuantos TYPE_STATUS aplico (0 si ninguno). */
int  ecq_read(EcqCore *core, long timeout_ms);
/* Aplica un TYPE_STATUS crudo (misma ruta que ecq_read; tests y modo offline). */
int  ecq_apply_status(EcqCore *core, const char *buf);

const EcqTable *ecq_table(const EcqCore *core);
int  ecq_noreply(EcqCore *core, time_t now);

/* Etiqueta de un modulo para los selectores, como el GTK: el nombre y, solo si
 * esta repetido, "nombre (cfgfile)" o "nombre (pid N)". */
void ecq_module_label(const EcqCore *core, int idx, char *out, size_t outlen);

/* Seleccion por PID (nunca por nombre). -1 si ese pid ya no esta. */
int  ecq_select_pid(EcqCore *core, int pid);
int  ecq_selected_pid(const EcqCore *core);
/* "name (status)" del seleccionado, o "" si no hay. */
const char *ecq_target_label(const EcqCore *core);

/* Tabla de sensibilidad (ew_controller_gui.c:125-136): Stop exige Alive y no ser
 * startstop; Restart exige seleccion y no ser startstop; Start exige Stop/Dead;
 * Reconfig exige haber visto algun status y que startstop responda. */
int  ecq_can(const EcqCore *core, EcqAction action, int pid);

/* Envia la accion. START se envia como TYPE_RESTART (igual que el GTK:
 * ew_controller_gui.c:248-254). `detail` recibe el texto de la statusbar. */
int  ecq_action(EcqCore *core, EcqAction action, int pid, char *detail, size_t detlen);
const char *ecq_action_text(EcqAction action);   /* "Stop sent..." etc. */
/* Tipo y payload del mensaje de una accion (puro). START se envia como
 * TYPE_RESTART y RECONFIG sin payload. NULL si la accion no aplica. */
const char *ecq_action_message(EcqAction action, int pid, char *payload, size_t paylen);
/* Pide estado ya (boton Refresh). */
int  ecq_request_status(EcqCore *core);

/* ------------------------------------------------------------------------- */
/* Editor del .d (pestana Configuration)                                     */
/* ------------------------------------------------------------------------- */

typedef struct EcqCfg EcqCfg;

/* `cfgfile` es el nombre del .d del modulo seleccionado; se resuelve contra
 * $EW_PARAMS. Solo expone las lineas clave/valor (kind 0). */
EcqCfg *ecq_cfg_open(const char *cfgfile, char *err, size_t errlen);
void    ecq_cfg_free(EcqCfg *cfg);

int         ecq_cfg_nvars(const EcqCfg *cfg);
const char *ecq_cfg_key(const EcqCfg *cfg, int var);
const char *ecq_cfg_value(const EcqCfg *cfg, int var);
int         ecq_cfg_set(EcqCfg *cfg, int var, const char *value);   /* marca dirty */
int         ecq_cfg_dirty(const EcqCfg *cfg);
const char *ecq_cfg_path(const EcqCfg *cfg);
/* Guarda con .bak + rename atomico. `msg` = "Saved to <path> (backup .bak)" o
 * "ERROR saving <path>". 0 si OK. */
int         ecq_cfg_save(EcqCfg *cfg, char *msg, size_t msglen);

/* ------------------------------------------------------------------------- */
/* Log propio del modulo                                                     */
/* ------------------------------------------------------------------------- */

/* Inicializa `logit` sobre `$EW_LOG/ew_controllerQT_<YYYYMMDD>.log` con la
 * clave `LogFile` del .d (0 = sin disco). Devuelve el flag APLICADO: 1 si hay
 * fichero, 0 si quedo solo en consola (sin `EW_LOG` escribible `logit_init`
 * terminaria el proceso). IDEMPOTENTE: a partir de la primera llamada devuelve
 * ese mismo flag sin volver a llamar a `logit_init` (el segundo, con logflag 0,
 * re-`fclose`a el FILE* y aborta el proceso). */
int ecq_log_init(int log_file);

/* Escribe una linea al log propio (timestamp UTC, solo fichero). No hace nada
 * si `ecq_log_init` no se llamo: los tests C usan el nucleo sin log. */
void ecq_logf(const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* ------------------------------------------------------------------------- */
/* Visor de logs (pestana Logs)                                              */
/* ------------------------------------------------------------------------- */

/* Fichero `<prefix>_*.log` mas reciente de `logdir`, con el prefijo derivado
 * como en ew_controller_gui.c:349-362: `config` (basename sin extension) si
 * existe, si no `name`; `startstop` se queda literal. -1 + `err` si no hay. */
int   ecq_log_path(const char *logdir, const char *name, const char *config,
                   char *out, size_t outlen, char *err, size_t errlen);
/* Ultimos `maxbytes` desde un borde de linea. malloc'd (liberar con ecq_free). */
char *ecq_log_tail(const char *path, long maxbytes, long *len);
void  ecq_free(void *p);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ECQ_CAPI_H */
