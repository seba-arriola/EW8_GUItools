/******************************************************************************
 * csnhypodbp_statable.h                                                       *
 *                                                                             *
 * Nucleo PURO (sin GTK) de la tabla de estaciones de la pestana de ondas:     *
 * por cada estacion elige UNA picada P y UNA S (manual > automatica > teorica)*
 * y les asigna el residual de la solucion. Se testea headless.                *
 ******************************************************************************/
#ifndef CSNHYPODBP_STATABLE_H
#define CSNHYPODBP_STATABLE_H

#include "csnhypodbp_group.h"
#include "csnhypodbp_manual.h"
#include "csnhypodbp_trace.h"

/* Una picada elegida para la sub-fila P o S de una estacion. */
typedef struct {
    int    present;   /* 1 si hay picada de esa fase                     */
    char   origin;    /* 'M' | 'A' | 'T' (teorica)                       */
    char   chan[8];   /* canal de la picada elegida ("" si no hay)       */
    char   net[4];    /* red ("" si no hay)                              */
    char   loc[4];    /* localizacion ("" si no hay)                     */
    double t;         /* epoch (s)                                       */
    double resid;     /* residual de la solucion (s); NAN si no aplica   */
} CsnhypodbpStaPick;

/* Una fila por estacion: UNA P y UNA S (ranking M > A > T). */
typedef struct {
    char  sta[16];
    int    idx;       /* indice en StaArray (para los checkbox de seleccion) */
    double dist_km;
    int    has_data;
    CsnhypodbpStaPick p;
    CsnhypodbpStaPick s;
} CsnhypodbpStaRow;

/* Entrada por estacion para construir la tabla. */
typedef struct {
    char  sta[16];
    int    idx;                       /* indice en StaArray                */
    double dist_deg;
    int    has_data;
    /* SCNL de la estacion segun la metadata (StaFile/estaciones_107.txt):
       respaldo cuando la picada no trae canal/red/loc. Sin canal, HYPOINVERSE
       rechaza la fase (empareja STA+NET+COMP3+LOC) y el OBS de NLLoc queda con
       13 campos. */
    char  chan[16];
    char  net[16];
    char  loc[16];
    const CsnhypodbpTracePick *caps;  /* picadas del PICK_RING (A/M)       */
    int    ncap;
    const CsnhypodbpPick *sol;        /* picadas de la solucion base (A/M) */
    int    nsol;
    double theo_p;                    /* P teorica IASP91 (epoch) o 0      */
} CsnhypodbpStaInput;

/* Filtro global de origenes (checkboxes A/M/Teo de la barra inferior). */
typedef struct { int auto_on; int manual_on; int theo_on; } CsnhypodbpPickFilter;

/* Elige UNA picada entre las candidatas: ranking M(2) > A(1) > T(0); empate ->
 * la mas cercana a `t_ref` (si t_ref > 0), si no la de mayor t. Devuelve 1 y
 * copia origen/tiempo/SCNL a `dst` (sin tocar `dst->resid`); 0 si no hay. */
int csnhypodbp_stapick_one(const CsnhypodbpStaPick *cand, int n, double t_ref,
                           CsnhypodbpStaPick *dst);

/* Fuente de residuales para `csnhypodbp_statable_build`: la candidata de
 * `locate` si esta vigente, si no la solucion elegida; NULL si ninguna tiene
 * picadas. */
const CsnhypodbpSolution *csnhypodbp_resid_source(const CsnhypodbpSolution *preview,
                                                  int preview_valid,
                                                  const CsnhypodbpSolution *base);

/* Construye una fila por estacion (hasta `max`). `sol` aporta los residuales
 * (NAN si no hay match o sol==NULL). Devuelve el numero de filas. */
int csnhypodbp_statable_build(CsnhypodbpStaRow *out, int max,
                              const CsnhypodbpStaInput *in, int n,
                              const CsnhypodbpManualPick *man, int nman,
                              const CsnhypodbpSolution *sol);

/* 1 si la picada pasa los filtros: (pick de esa fase marcado) AND (origen
 * activo). `idx < 0` (estacion fuera de la tabla) no la filtra la seleccion.
 * `ph`: 0 = P, 1 = S. */
int csnhypodbp_pick_allowed(char origin, int idx, int ph,
                            const int (*pick_on)[2],
                            const CsnhypodbpPickFilter *f);

/* Set de picadas para el localizador desde las filas (una P y una S por
 * estacion), aplicando la seleccion de pick y el filtro de origen.
 * Devuelve cuantas. */
int csnhypodbp_locate_picks(const CsnhypodbpStaRow *rows, int nrows,
                            const int (*pick_on)[2],
                            const CsnhypodbpPickFilter *f,
                            CsnhypodbpPick *out, int max_out);

#endif /* CSNHYPODBP_STATABLE_H */
