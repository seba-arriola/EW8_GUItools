#ifndef CSNHYPODBP_PREVIEW_H
#define CSNHYPODBP_PREVIEW_H

/* Valores YA FORMATEADOS de la tablita del preview de la pestana Ondas: fila 0
 * = cabeceras de parametro, fila 1 = valores. Es logica pura (sin GTK) para
 * poder testearla headless: la capa de vista solo vuelca `val[]` en etiquetas.
 *
 * Estados, con esta precedencia en el render (la decide `preview_update`):
 *   RUNNING                -> `calculando <locator>… N s`; valores en guion
 *   ERROR                  -> guiones + `err` visible (p. ej. sin solucion nueva)
 *   CANDIDATE + candidate=1 -> solucion nueva del `locate` (fondo rojo leve)
 *   CANDIDATE + candidate=0 -> solucion elegida en el detalle
 *   IDLE (`_clear`)         -> nada seleccionado (guiones)
 *
 * `rms`/`nph` de la solucion ELEGIDA llegan ya formateados por la tabla de
 * detalle (una decimal / entero) y se copian tal cual; los de la candidata se
 * formatean aqui (%.2f / %d) porque llegan como numero del localizador. */
#define CSNHYPODBP_PREVIEW_NVALS 10

/* Frontera de la vista: 5 parametros (Lat..RMS) a la izquierda y 5 magnitudes
 * (ML..Mww) a la derecha. El orden de `val[]` NO cambia. */
#define CSNHYPODBP_PREVIEW_NPARAMS \
    (CSNHYPODBP_PREVIEW_NVALS - CSNHYPODBP_PREVIEW_MAGS)

typedef enum {
    CSNHYPODBP_PREVIEW_IDLE = 0,
    CSNHYPODBP_PREVIEW_RUNNING,
    CSNHYPODBP_PREVIEW_CANDIDATE,
    CSNHYPODBP_PREVIEW_ERROR
} CsnhypodbpPreviewState;

typedef struct {
    int  candidate;                             /* 1 = candidata del locate */
    int  state;                                 /* IDLE/RUNNING/CANDIDATE/ERROR */
    char title[48];                             /* "New solution · csnloc" | "Selected solution" | "No solution selected" */
    char otime[48];                             /* "Origin time: YYYY-MM-DD HH:MM:SS UTC" | "Origin time: --" */
    char busy[64];                              /* "calculando nlloc_ring… 12 s" */
    char err[320];                              /* motivo visible en ERROR */
    char val[CSNHYPODBP_PREVIEW_NVALS][24];     /* Lat, Lon, Depth, Nph, RMS, ML, Mwp, Mb, Ms, Mww */
} CsnhypodbpPreviewVals;

/* Titulos de la fila 0 (indice = el de `val[]`). */
extern const char *const csnhypodbp_preview_headers[CSNHYPODBP_PREVIEW_NVALS];

/* 1 si `i` es una magnitud (`NPARAMS <= i < NVALS`); 0 si es parametro o
 * esta fuera de rango. La vista usa esto para separar los dos bloques. */
int csnhypodbp_preview_is_mag(int i);

/* Columna 0..MAGS-1 dentro del bloque de magnitudes; -1 si no es magnitud. */
int csnhypodbp_preview_mag_col(int i);

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

/* Estado de calculo: state=RUNNING y texto `calculando <locator>… <secs> s`.
 * `locator` vacio/NULL -> "csnloc". NO hay resultado todavia: el titulo es
 * "New solution · <locator>", el tiempo de origen queda en "--" y los 10 valores
 * en "-" (asi el cuadro no muestra cifras viejas de la solucion elegida).
 * `candidate=0` y `err` vacio. */
void csnhypodbp_preview_set_busy(CsnhypodbpPreviewVals *v, const char *locator, long secs);

/* Estado de error SIN solucion: state=ERROR, titulo "New solution · <locator>",
 * tiempo de origen "--", los 10 valores en "-", `candidate=0` y `busy` vacio.
 * `err` es el mensaje ya compuesto (ver `_nosol_msg`). */
void csnhypodbp_preview_set_error(CsnhypodbpPreviewVals *v, const char *locator,
                                  const char *err);

/* Mensaje de fallo del `locate`, acotado y NUL-terminado:
 *   "sin solución nueva con <locator>: <detail>" (sin ": <detail>" si vacio). */
void csnhypodbp_preview_nosol_msg(char *out, size_t n, const char *locator,
                                  const char *detail);

/* Sin seleccion: titulo propio y los 10 valores en "-". */
void csnhypodbp_preview_clear(CsnhypodbpPreviewVals *v);

/* Solucion nueva del `locate`: titulo con el localizador activo y `nph`/`rms`
 * formateados desde numero. Cada magnitud vacia -> "-". `mww` es el valor de la
 * inversion de fase W (vacio si no hay para esa solucion). */
void csnhypodbp_preview_from_candidate(CsnhypodbpPreviewVals *v, double otime,
                                       const char *locator,
                                       double lat, double lon, double depth, int nph,
                                       double rms, const char *ml, const char *mwp,
                                       const char *mb, const char *ms, const char *mww);

/* Solucion elegida en el detalle: `rms`/`nph` son los textos de la fila y
 * `ml`/`mwp`/`mb`/`ms` los valores formateados; vacios -> "-". */
void csnhypodbp_preview_from_selected(CsnhypodbpPreviewVals *v, double otime,
                                      double lat, double lon,
                                      double depth, const char *rms, const char *nph,
                                      const char *ml, const char *mwp, const char *mb,
                                      const char *ms, const char *mww);

#endif /* CSNHYPODBP_PREVIEW_H */
