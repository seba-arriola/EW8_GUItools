/*
 * csnhypodbp_magmarks.h - Ventanas de la sub-pestaña `magnitudes` (headless, sin
 * GTK ni EarthWorm).
 *
 * Por (estacion, magnitud) se guarda la ventana VIGENTE como desplazamientos en
 * segundos respecto del ANCLA (la P efectiva de la estacion: M > A > Teo) y si la
 * estacion entra al calculo de la magnitud de red. Sin marca, la ventana es la
 * default del .d de csnmags (csnhypodbp_magcfg_window) y `used` es 1 si la
 * estacion tiene traza.
 *
 * Las marcas viven por SOLUCION: al cambiar de solucion se limpian
 * (csnhypodbp_magmarks_clear), porque las anclas y las trazas son otras.
 */
#ifndef CSNHYPODBP_MAGMARKS_H
#define CSNHYPODBP_MAGMARKS_H

#include <stddef.h>

#define CSNHYPODBP_MAGMARKS_STA_LEN 16
#define CSNHYPODBP_MAGMARKS_KIND     4   /* ML, Mwp, Mb, Ms */
#define CSNHYPODBP_MAGMARKS_MAX    600   /* = MAX_ESTA */

/* Que agarra un clic sobre la franja dibujada. */
typedef enum {
    CSNHYPODBP_MAGG_NONE = 0,   /* fuera de la franja */
    CSNHYPODBP_MAGG_T0,         /* borde izquierdo (mueve t0) */
    CSNHYPODBP_MAGG_T1,         /* borde derecho (mueve t1) */
    CSNHYPODBP_MAGG_BOTH        /* dentro: se mueve la franja completa */
} CsnhypodbpMagGrab;

/* Alto del HANDLE de arrastre: la barra rellena que se dibuja en la parte
 * SUPERIOR de la franja. Solo desde ahi se mueve o redimensiona la ventana, de
 * modo que el resto del canvas queda libre para el zoom por dos clics. */
#define CSNHYPODBP_MAG_HANDLE_H 16.0

/* 1 si `y` cae dentro del handle que empieza en `band_top`. */
int csnhypodbp_magmarks_handle_hit(double y, double band_top);

/* Una fila del archivo de ventanas por (estacion, magnitud) que lee csnmags
 * (clave `WindowOverridesFile`). t0/t1 son tiempo ABSOLUTO (epoch). */
typedef struct {
    char   sta[CSNHYPODBP_MAGMARKS_STA_LEN];
    int    kind;      /* 0=ML 1=Mwp 2=Mb 3=Ms */
    double t0, t1;
} CsnhypodbpMagOvrRow;

/* Escribe el archivo (una linea `STA KIND T0 T1` por fila valida; se saltan las
 * filas sin estacion, con kind fuera de rango o con t1 <= t0). Devuelve el numero
 * de lineas de datos escritas, o -1 si no se pudo abrir el archivo. */
int csnhypodbp_mag_ovr_write(const char *path, const CsnhypodbpMagOvrRow *rows, int n);

/* Marca de UNA estacion: las cuatro magnitudes a la vez. */
typedef struct {
    char          sta[CSNHYPODBP_MAGMARKS_STA_LEN];
    double        t0[CSNHYPODBP_MAGMARKS_KIND];
    double        t1[CSNHYPODBP_MAGMARKS_KIND];
    unsigned char set[CSNHYPODBP_MAGMARKS_KIND];      /* 1 = ajustada a mano */
    unsigned char used[CSNHYPODBP_MAGMARKS_KIND];     /* 1 = entra al calculo */
    unsigned char used_set[CSNHYPODBP_MAGMARKS_KIND]; /* 1 = used fijado a mano */
} CsnhypodbpMagMark;

typedef struct {
    CsnhypodbpMagMark *v;
    int                n, cap;
} CsnhypodbpMagMarks;

void csnhypodbp_magmarks_init(CsnhypodbpMagMarks *m);
void csnhypodbp_magmarks_free(CsnhypodbpMagMarks *m);
void csnhypodbp_magmarks_clear(CsnhypodbpMagMarks *m);

/* Ventana vigente de (sta, kind): la ajustada si existe, si no (def_t0, def_t1).
 * Escribe en `t0`/`t1` (cualquiera de los dos puede ser NULL). */
void csnhypodbp_magmarks_window(const CsnhypodbpMagMarks *m, const char *sta, int kind,
                                double def_t0, double def_t1, double *t0, double *t1);

/* Fija la ventana de (sta, kind) y la marca como ajustada. 0 si OK, -1 si no
 * cabe (tope) o los argumentos no valen. */
int csnhypodbp_magmarks_set(CsnhypodbpMagMarks *m, const char *sta, int kind,
                            double t0, double t1);

/* 1 si la ventana de (sta, kind) fue ajustada a mano. */
int csnhypodbp_magmarks_touched(const CsnhypodbpMagMarks *m, const char *sta, int kind);

/* Uso de (sta, kind): el fijado si existe, si no `def_used`. */
int csnhypodbp_magmarks_used(const CsnhypodbpMagMarks *m, const char *sta, int kind,
                             int def_used);
int csnhypodbp_magmarks_set_used(CsnhypodbpMagMarks *m, const char *sta, int kind,
                                 int used);

/* Cuantas (estacion, magnitud) tienen ventana ajustada a mano. */
int csnhypodbp_magmarks_touched_count(const CsnhypodbpMagMarks *m);

/* Origen de la VENTANA de (sta, kind), que es lo que filtran los checkboxes
 * A/M/Teo de la barra inferior:
 *   'M' = la ajusto el usuario a mano (arrastrando la franja)
 *   'T' = ancla teorica (la estacion no tiene P observada)
 *   'A' = ventana default del .d con ancla P observada
 * `anchor` es el origen de la picada P de la fila ('M'|'A'|'T'|0). */
char csnhypodbp_magmarks_winsrc(const CsnhypodbpMagMarks *m, const char *sta,
                                int kind, char anchor);

/* 1 si (sta, kind) entra al forzado con el filtro A/M/Teo dado. */
int csnhypodbp_magmarks_allowed(const CsnhypodbpMagMarks *m, const char *sta, int kind,
                                char anchor, int auto_on, int manual_on, int theo_on);

/* Que agarra un clic en `x` (px) sobre la franja [x0, x1] (px), con tolerancia
 * `tol_px` en cada borde. Con la franja mas angosta que 2*tol agarra la franja
 * completa. Devuelve NONE si x0/x1 no son finitos o el clic esta lejos. */
CsnhypodbpMagGrab csnhypodbp_magmarks_grab(double x, double x0, double x1, double tol_px);

#endif /* CSNHYPODBP_MAGMARKS_H */
