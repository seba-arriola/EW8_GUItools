#ifndef EWGUI_STA_H
#define EWGUI_STA_H

#include <stddef.h>

/*
 * Carga de archivos de estaciones (sin GTK). Formato por línea:
 *   Sta Net Chan Loc Lat Lon Elev Sens
 * (los campos de nombre son opcionales a partir del 7.º). Ignora líneas
 * vacías y comentarios que empiezan por '#'.
 */

typedef struct {
    char   sta[16];
    char   net[16];
    char   chan[16];
    char   loc[16];
    double lat;
    double lon;
    double elev_m;
    double sens;
} EwStation;

/* Parsea una línea. Devuelve 0 si hay al menos 7 campos (sta..elev). */
int ewgui_sta_parse_line(const char *line, EwStation *out);

/* Carga hasta `max` estaciones de `path`. Devuelve el número cargado. */
size_t ewgui_sta_load(const char *path, EwStation *arr, size_t max);

#endif /* EWGUI_STA_H */
