#ifndef CSNHYPODBP_PREVIEW_H
#define CSNHYPODBP_PREVIEW_H

/* Valores YA FORMATEADOS de la tablita del preview de la pestana Ondas: fila 0
 * = cabeceras de parametro, fila 1 = valores. Es logica pura (sin GTK) para
 * poder testearla headless: la capa de vista solo vuelca `val[]` en etiquetas.
 *
 * Tres estados, con esta precedencia en el render:
 *   candidate=1            -> solucion nueva del `locate` (fondo rojo leve)
 *   candidate=0 + valores  -> solucion elegida en el detalle
 *   guiones (`_clear`)     -> nada seleccionado
 *
 * `rms`/`nph` de la solucion ELEGIDA llegan ya formateados por la tabla de
 * detalle (una decimal / entero) y se copian tal cual; los de la candidata se
 * formatean aqui (%.2f / %d) porque llegan como numero del localizador. */
#define CSNHYPODBP_PREVIEW_NVALS 10

typedef struct {
    int  candidate;                             /* 1 = candidata del locate */
    char title[48];                             /* "New solution · csnloc" | "Selected solution" | "No solution selected" */
    char val[CSNHYPODBP_PREVIEW_NVALS][24];     /* Lat, Lon, Depth, Nph, RMS, ML, Mwp, Mb, Ms, Mww */
} CsnhypodbpPreviewVals;

/* Titulos de la fila 0 (indice = el de `val[]`). */
extern const char *const csnhypodbp_preview_headers[CSNHYPODBP_PREVIEW_NVALS];

/* --- Magnitud preferida (radios del cuadro de la candidata) ---------------- */

/* ML, Mwp, Mb, Ms y Mww. El Mww lo calcula la inversión de fase W (panel `Mww`)
 * y NO viaja en el ARC: su radio solo marca la magnitud preferida en la base. */
#define CSNHYPODBP_PREVIEW_MAGS 5

/* Índice del radio `Mww` (el último de la fila de radios). */
#define CSNHYPODBP_PREVIEW_MWW_KIND (CSNHYPODBP_PREVIEW_MAGS - 1)

/* Parte "valor-nst" de una celda de magnitud ("4.5-12"): 1 si habia valor > 0
 * (deja `val` y `nst`), 0 si la celda esta vacia o es "-". */
int csnhypodbp_mag_split(const char *txt, double *val, int *nst);

/* Magnitud preferida por defecto, con la MISMA regla que csnmags (asi el radio
 * arranca donde el sistema ya la considera preferida):
 *   Mwp con nst>=3 y valor>=5.5 > Ms con nst>=3 > Mb con nst>=3 > ML>0.
 * Devuelve 0=ML 1=Mwp 2=Mb 3=Ms, o -1 si ninguna tiene valor. El Mww (4) NO
 * entra: no viene del ARC, asi que solo se marca si el usuario lo elige. */
int csnhypodbp_mag_pref_default(const char *ml, const char *mwp,
                                const char *mb, const char *ms);

/* 1 si la magnitud `kind` (0=ML..4=Mww) se puede marcar como preferida: tiene que
 * haber venido con valor. `has[i]` = 1 si esa magnitud se calculo. */
int csnhypodbp_mag_pref_ok(const int has[CSNHYPODBP_PREVIEW_MAGS], int kind);

/* Sin seleccion: titulo propio y los 10 valores en "-". */
void csnhypodbp_preview_clear(CsnhypodbpPreviewVals *v);

/* Solucion nueva del `locate`: titulo con el localizador activo y `nph`/`rms`
 * formateados desde numero. Cada magnitud vacia -> "-". `mww` es el valor de la
 * inversion de fase W (vacio si no hay para esa solucion). */
void csnhypodbp_preview_from_candidate(CsnhypodbpPreviewVals *v, const char *locator,
                                       double lat, double lon, double depth, int nph,
                                       double rms, const char *ml, const char *mwp,
                                       const char *mb, const char *ms, const char *mww);

/* Solucion elegida en el detalle: `rms`/`nph` son los textos de la fila y
 * `ml`/`mwp`/`mb`/`ms` los valores formateados; vacios -> "-". */
void csnhypodbp_preview_from_selected(CsnhypodbpPreviewVals *v, double lat, double lon,
                                      double depth, const char *rms, const char *nph,
                                      const char *ml, const char *mwp, const char *mb,
                                      const char *ms, const char *mww);

#endif /* CSNHYPODBP_PREVIEW_H */
