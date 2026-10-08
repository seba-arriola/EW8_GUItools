#ifndef EWGUI_CONFIG_H
#define EWGUI_CONFIG_H

#include <stddef.h>

/*
 * Lectura de archivos .d de EarthWorm (vía kom), sin GTK.
 * Sustituye los ReadConfig repetidos en los 5 módulos GUI.
 */

typedef enum {
    EW_KEY_STR,    /* char  *  (out = buffer, size = capacidad)   */
    EW_KEY_INT,    /* int   *                                     */
    EW_KEY_DBL,    /* double *                                    */
    EW_KEY_LONG,   /* long  *                                     */
    EW_KEY_COLOR   /* double[3] (lee "#RRGGBB")                   */
} EwKeyType;

typedef struct {
    const char *name;   /* token tal cual aparece en el .d         */
    EwKeyType   type;
    void       *out;    /* destino (según type)                    */
    size_t      size;   /* EW_KEY_STR: tamaño del buffer destino   */
    int         required;
} EwKeySpec;

/*
 * Carga `path` y vuelca los valores en `spec`.
 * Devuelve 0 si todo OK; -1 si falta una clave obligatoria o hay error de parseo.
 */
int ewgui_config_load(const char *path, const EwKeySpec *spec, size_t n);

/* "#RRGGBB" -> rgb[3] en [0,1]. Devuelve 0 si OK, -1 si el formato es inválido. */
int ewgui_parse_hex_color(const char *s, double rgb[3]);

#endif /* EWGUI_CONFIG_H */
