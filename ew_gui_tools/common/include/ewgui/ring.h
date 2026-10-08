#ifndef EWGUI_RING_H
#define EWGUI_RING_H

#include "transport.h"

/*
 * Envoltura de los anillos de EarthWorm (tport) y del latido periódico.
 * Sustituye la ew_background_tasks duplicada en los 4 módulos.
 *
 * No incluye GTK: se puede usar y testear sin toolkit.
 */

typedef struct {
    long          key;
    unsigned char instid;
    unsigned char modid;
    SHM_INFO      region;
    int           attached;
} EwGuiRing;

/* Estado del latido: el llamador fija `last` a la hora actual al arrancar. */
typedef struct {
    double last;
} EwGuiHeartbeat;

/* Resuelve clave de anillo + instid + modid desde la config (GetKey/GetLocalInst/
 * GetModId). Devuelve 0 si OK, -1 si algo no se encuentra. */
int ewgui_ring_resolve(const char *ring_name, const char *module_id,
                       long *key, unsigned char *instid, unsigned char *modid);

int  ewgui_ring_attach(EwGuiRing *r, long key, unsigned char instid,
                       unsigned char modid);
void ewgui_ring_detach(EwGuiRing *r);

/* Envolturas directas de tport_putmsg / tport_getmsg. */
int ewgui_ring_put(EwGuiRing *r, MSG_LOGO *logo, long len, char *msg);
int ewgui_ring_drain(EwGuiRing *r, MSG_LOGO *logos, short nlogo,
                     MSG_LOGO *outlogo, long *outlen, char *buf, long buflen);

/* 1 si la region recibio TERMINATE o una peticion de terminar este modulo
 * (flag == mypid). Es el patron tport_getflag de los modulos GUI. */
int ewgui_ring_should_quit(SHM_INFO *region, int mypid);

/* 1 si toca emitir latido (y actualiza `last`); 0 si no. Lógica pura, testeable. */
int ewgui_heartbeat_due(EwGuiHeartbeat *hb, double now, int interval_s);

#endif /* EWGUI_RING_H */
