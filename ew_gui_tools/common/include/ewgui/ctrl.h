#ifndef EWGUI_CTRL_H
#define EWGUI_CTRL_H

#include <stddef.h>

#include <transport.h>

/*
 * Protocolo del CONTROL_RING (startstop) y parseo del TYPE_STATUS.
 * Sin GTK. Extraído de ew_controller.
 */

typedef struct {
    char name[32];       /* nombre del módulo */
    int  pid;
    char status[16];     /* Alive / Stop / Dead / ... */
    char detalle[192];   /* resto de la línea de estado (argumentos) */
    char config[64];     /* nombre base del archivo de config (log) */
    char cfgfile[64];    /* nombre del .d */
} EwCtrlMod;

typedef struct {
    char name[64];
    int  key;
    int  size;
} EwCtrlRing;

#define EW_CTRL_MAX_ROWS 256

typedef struct {
    EwCtrlMod  mods[EW_CTRL_MAX_ROWS];
    int        nmods;
    EwCtrlRing rings[EW_CTRL_MAX_ROWS];
    int        nrings;
    char hostname[128];
    char starttime[64];
    char curtime[64];
    char disk[64];
    char version[64];
} EwCtrlStatus;

/* Repuebla `st` a partir de un mensaje TYPE_STATUS de startstop. */
void ewgui_ctrl_parse_status(char *buf, EwCtrlStatus *st);

/* Compara dos EwCtrlStatus para decidir el trabajo de refresco de la UI.
   Sólo mira los campos que se muestran (mods[]/rings[]); ignora los volátiles
   (curtime, disk, ...) para no refrescar sin motivo. */
typedef enum {
    EWCTRL_SYNC_NONE = 0,   /* nada visible cambió -> no tocar widgets       */
    EWCTRL_SYNC_VALUES,     /* misma estructura; actualizar filas in situ     */
    EWCTRL_SYNC_STRUCTURE   /* cambió conjunto/orden -> reconstruir filas     */
} EwCtrlSync;

EwCtrlSync ewgui_ctrl_sync_plan(const EwCtrlStatus *prev, const EwCtrlStatus *next);

/* Helpers de parseo (expuestos para tests). */
void ewgui_ctrl_campo_despues(char *line, const char *label, char *out, size_t n);
void ewgui_ctrl_extraer_config(char *detalle, char *config, size_t n);
void ewgui_ctrl_extraer_cfgfile(char *detalle, char *cfgfile, size_t n);

/* Identidad de un módulo: el PID. El NOMBRE no identifica: startstop_unix.d puede
   tener dos `Process` con el mismo nombre (las dos instancias de csnmags, con
   csnmags.d y csnmags_raw.d), y resolver por nombre apunta siempre a la primera.
   Índice en st->mods, o -1 si pid <= 0 o no está. */
int  ewgui_ctrl_idx_por_pid(const EwCtrlStatus *st, int pid);

/* Rellena cfgfile con "<name>.d" acotado a n. El sufijo ".d" se reserva siempre
   (se recorta el nombre antes que perderlo: identifica el archivo de config);
   nunca escribe más de n bytes y siempre termina en NUL. */
void ewgui_ctrl_cfgfile_por_nombre(const char *name, char *cfgfile, size_t n);

/* Etiqueta de un módulo para los selectores: el nombre, y SÓLO cuando ese nombre
   está repetido en st, "nombre (cfgfile)" (o "nombre (pid N)" si el .d tampoco
   distingue). Acotada a n. */
void ewgui_ctrl_etiqueta_modulo(const EwCtrlStatus *st, int idx, char *out, size_t n);

/* Envía un mensaje al CONTROL_RING (payload + '\n'). 0 si OK, -1 si falla. */
int  ewgui_ctrl_send(SHM_INFO *region, unsigned char instid, unsigned char modid,
                     unsigned char type, const char *payload);

#endif /* EWGUI_CTRL_H */
