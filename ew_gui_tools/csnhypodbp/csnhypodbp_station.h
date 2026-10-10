#ifndef CSNHYPODBP_STATION_H
#define CSNHYPODBP_STATION_H

#include <stddef.h>
#include <stdint.h>

#include "ewgui/wave.h"        /* EwGuiTrace (headless, sin GTK) */
#include "csnhypodbp_trace.h"  /* CsnhypodbpPickCap (headless) */

/*
 * csnhypodbp_station.c - Logica pura de la lista de estaciones y del orden de
 * canales (headless, sin GTK/EW).
 *
 * El orden de canales sale de dos claves del .d:
 *   channelsOrder   HH BH HN LH     (orden externo: banda+instrumento)
 *   componentsOrder Z N E           (orden interno: orientacion)
 * => HHZ, HHN, HHE, BHZ, BHN, BHE, ... omitiendo los no disponibles.
 *
 * DEV_STATION vive aqui (y no en csnhypodbp.h) para poder testear headless las
 * decisiones de la vista: que canales hay que descargar y si una estacion
 * tiene datos en la ventana del evento.
 */

/* --- Fila de estacion/canal (una por canal) --- */
typedef struct {
    double dTime;
    char szPhase[8];
} PICK;

#define MAX_PICKS_PER_STA 50

typedef struct {
    char szStation[16];
    char szChannel[16];
    char szNetID[16];
    char szLocation[16];
    double dLat;
    double dLon;

    EwGuiTrace *trace;   /* buffer crudo + vista filtrada + rate + oldest */
    int fetched;         /* 1 si ya se intento descargar (evita re-fetch) */

    double dScreenScale;

    int64_t lLastAbsIdx;
    double dManualPickTime;

    PICK picks[MAX_PICKS_PER_STA];
    int iNumPicks;

    CsnhypodbpPickCap pickcap;   /* picadas capturadas del PICK_RING (P/S + A/M) */
} DEV_STATION;

/* Tokeniza por espacios/tabs/comas. Devuelve el nº de tokens escritos. */
int  csnhypodbp_split_ws(const char *s, char out[][16], int max);

/* Codigo de canal "<prefix><comp>" (p.ej. "HH"+"Z" = "HHZ"). */
void csnhypodbp_chan_code(char *out, size_t n, const char *prefix, const char *comp);

/* Todos los "canal+componente" en orden (canal externo, componente interno),
 * sin filtrar por disponibilidad. */
int  csnhypodbp_channel_candidates(const char *channels_order,
                                   const char *components_order,
                                   char out[][8], int max_out);

/* Ordena "canal+componente" segun channels_order (externo) y components_order
 * (interno), conservando solo los que aparecen en `avail` (codigos "HHZ"...).
 * Escribe hasta max_out en out y devuelve cuantos hay. */
int  csnhypodbp_channel_order(const char *channels_order,
                              const char *components_order,
                              const char *const *avail, int navail,
                              char out[][8], int max_out);

/* --- Decisiones de la vista (headless) --- */

/* 1 si hay que (re)descargar `sta`: alguna de sus filas tiene fetched==0
 * (incluye el caso de que no exista ninguna fila). 0 si todas ya se intentaron. */
int  csnhypodbp_station_needs_fetch(const DEV_STATION *arr, int n, const char *sta);

/* 1 si alguna fila de `sta` tiene has_data[i] != 0 (datos en la ventana). */
int  csnhypodbp_station_has_data(const DEV_STATION *arr, int n,
                                 const int *has_data, const char *sta);

/* 1 si alguna fila de `sta` tiene un canal de la misma banda (2 primeros
 * caracteres) que `code` (p. ej. estacion con HHZ -> HHZ/HHN/HHE). */
int  csnhypodbp_station_has_band(const DEV_STATION *arr, int n,
                                 const char *sta, const char *code);

/* Rotulo de una traza: canal (+ distancia). Ej: "HHZ (37 km)" o "HHZ". */
void csnhypodbp_trace_label(char *out, size_t outsz,
                            const char *station, const char *channel, double dist_km);

#endif /* CSNHYPODBP_STATION_H */
