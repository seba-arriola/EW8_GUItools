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
#define CSNHYPODBP_PREVIEW_NVALS 7

typedef struct {
    int  candidate;                             /* 1 = candidata del locate */
    char title[48];                             /* "New solution · csnloc" | "Selected solution" | "No solution selected" */
    char val[CSNHYPODBP_PREVIEW_NVALS][24];     /* Lat, Lon, Depth, Nph, RMS, ML, Mwp */
} CsnhypodbpPreviewVals;

/* Titulos de la fila 0 (indice = el de `val[]`). */
extern const char *const csnhypodbp_preview_headers[CSNHYPODBP_PREVIEW_NVALS];

/* Sin seleccion: titulo propio y los 7 valores en "-". */
void csnhypodbp_preview_clear(CsnhypodbpPreviewVals *v);

/* Solucion nueva del `locate`: titulo con el localizador activo y `nph`/`rms`
 * formateados desde numero. `ml`/`mwp` vacios -> "-". */
void csnhypodbp_preview_from_candidate(CsnhypodbpPreviewVals *v, const char *locator,
                                       double lat, double lon, double depth, int nph,
                                       double rms, const char *ml, const char *mwp);

/* Solucion elegida en el detalle: `rms`/`nph`/`ml`/`mwp` son los textos de la
 * fila (celdas c5/c7/c9/c10); vacios -> "-". */
void csnhypodbp_preview_from_selected(CsnhypodbpPreviewVals *v, double lat, double lon,
                                      double depth, const char *rms, const char *nph,
                                      const char *ml, const char *mwp);

#endif /* CSNHYPODBP_PREVIEW_H */
