#ifndef EWGUI_CFGFILE_H
#define EWGUI_CFGFILE_H

#include <stddef.h>

/*
 * Editor de archivos .d (sin GTK): lee un archivo línea a línea separando
 * clave/valor/comentario y lo reescribe preservando el formato y los
 * comentarios. Extraído de ew_controller.
 */

typedef struct {
    int   kind;      /* 0 = clave/valor, 1 = comentario, 2 = vacía */
    char *prefix;    /* sangría inicial */
    char *name;      /* clave */
    char *gap1;      /* espacios entre clave y valor */
    char *value;     /* valor (conserva comillas) */
    char *gap2;      /* espacios entre valor y comentario */
    char *comment;   /* comentario desde '#' */
    char *raw;       /* línea cruda (kind 1/2) */
} EwCfgLine;

typedef struct {
    char       path[512];
    int        nlines;
    int        last_newline;  /* 1 si el archivo termina en '\n' */
    EwCfgLine *lines;
} EwCfgFile;

/* Lee un .d. Devuelve 0 si OK, -1 si no se puede abrir. */
int  ewgui_cfgfile_read(const char *path, EwCfgFile *cf);

/* Reescribe el .d con copia .bak y rename atómico. 0 si OK, -1 si falla. */
int  ewgui_cfgfile_write(EwCfgFile *cf);

/* Libera las líneas y reinicia el contador. */
void ewgui_cfgfile_free(EwCfgFile *cf);

/* Separa una línea en sus campos (expuesto para tests). */
void ewgui_cfgfile_parse_line(char *line, EwCfgLine *l);

/* Resuelve la ruta de un .d, anteponiendo $EW_PARAMS si es relativa. */
void ewgui_cfgfile_resolve(const char *cfgfile, char *out, size_t n);

#endif /* EWGUI_CFGFILE_H */
