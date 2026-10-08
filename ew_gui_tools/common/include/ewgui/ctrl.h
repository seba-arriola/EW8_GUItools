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

/* Helpers de parseo (expuestos para tests). */
void ewgui_ctrl_campo_despues(char *line, const char *label, char *out, size_t n);
void ewgui_ctrl_extraer_config(char *detalle, char *config, size_t n);
void ewgui_ctrl_extraer_cfgfile(char *detalle, char *cfgfile, size_t n);

/* Envía un mensaje al CONTROL_RING (payload + '\n'). 0 si OK, -1 si falla. */
int  ewgui_ctrl_send(SHM_INFO *region, unsigned char instid, unsigned char modid,
                     unsigned char type, const char *payload);

#endif /* EWGUI_CTRL_H */
