/******************************************************************************
 * csnhypodbp_group.h                                                         *
 *                                                                            *
 * Nucleo PURO (sin GTK) del agrupamiento de soluciones hipocentrales y del    *
 * parseo del mensaje TYPE_HYP2000ARC. Se testea headless.                    *
 *                                                                            *
 * Criterio de agrupacion (ver plan):                                          *
 *   - mismo qid  => mismo evento;                                            *
 *   - o firma de picadas compatible (subconjunto dentro de GroupPickTolSec    *
 *     con al menos GroupMinMatchPicks picadas); ignorando red/canal/loc.      *
 ******************************************************************************/
#ifndef CSNHYPODBP_GROUP_H
#define CSNHYPODBP_GROUP_H

#include <stddef.h>
#include <stdint.h>

#define CSNHYPODBP_MAX_PICKS 256

typedef struct {
    char   sta[8];
    char   chan[8];     /* para el overlay de trazas (no entra en la firma) */
    char   phase;       /* 'P' | 'S' */
    double t_pick;      /* epoch (s) */
    int    manual;      /* reservado (origen por fase) */
    /* Extensiones para hypodb (AL FINAL: no alteran los offsets previos). */
    char   net[4];      /* red (ARC [5:7])                              */
    char   loc[4];      /* localizacion (ARC [111:113]); "--" => ""     */
    double weight;      /* 0.0-1.0 (LPWT P / LSWT S del ARC)            */
    double residual;    /* residual del localizador (s); NAN si no aplica */
} CsnhypodbpPick;

typedef struct {
    int      qid;
    unsigned version;
    int      mod;
    double   otime, lat, lon, depth, rms;
    int      nph;
    int      gap;
    int      manual;    /* marca de solucion manual (byte 174 del ARC) */
    int      npicks;
    CsnhypodbpPick picks[CSNHYPODBP_MAX_PICKS];
} CsnhypodbpSolution;

/* Clave de grupo: en la practica la UNION de las picadas de sus miembros. */
typedef struct {
    int    qid;
    int    npicks;
    CsnhypodbpPick picks[CSNHYPODBP_MAX_PICKS];
} CsnhypodbpGroupKey;

/* --- Parseo del ARC (offset 0-based, ver csnhypodbp_ws.c) --- */
int csnhypodbp_parse_solution(const char *msg, size_t len, int mod,
                              CsnhypodbpSolution *out);

/* --- Firma / comparacion de picadas --- */
int      csnhypodbp_pick_match(const CsnhypodbpPick *a, const CsnhypodbpPick *b,
                               double tol_sec);
int      csnhypodbp_picks_subset(const CsnhypodbpPick *a, int na,
                                 const CsnhypodbpPick *b, int nb,
                                 double tol_sec, int min_match);
/* Nº de picadas en comun (emparejamiento 1-1 por estacion+fase dentro de tol). */
int      csnhypodbp_picks_overlap(const CsnhypodbpPick *a, int na,
                                  const CsnhypodbpPick *b, int nb,
                                  double tol_sec);
/* Compatibilidad de firma: >= `min_match` picadas en comun. NO exige que una
 * sea subconjunto de la otra: la ventana deslizante de csnloc anade y quita
 * picadas, asi que dos emisiones del MISMO sismo (p. ej. una con CO06 y otra
 * con LMEL) no son subconjunto una de otra y quedaban en grupos separados
 * (la maestra mostraba el mismo evento varias veces). */
int      csnhypodbp_picks_compatible(const CsnhypodbpPick *a, int na,
                                     const CsnhypodbpPick *b, int nb,
                                     double tol_sec, int min_match);
uint64_t csnhypodbp_picks_signature(const CsnhypodbpPick *p, int n);

/* --- Grupos --- */
/* Indices de grupos compatibles con la solucion candidata (<= out_cap). */
int  csnhypodbp_group_matches(const CsnhypodbpGroupKey *groups, int ngroups,
                              int qid, const CsnhypodbpPick *picks, int npicks,
                              double tol_sec, int min_match,
                              int *out_idx, int out_cap);
/* Incorpora a la union del grupo las picadas que aun no esten (tol de dedup). */
void csnhypodbp_group_union(CsnhypodbpGroupKey *g, const CsnhypodbpPick *picks,
                            int npicks, double tol_sec);

/* --- Seleccion de la solucion preferida --- */
typedef struct {
    int      manual;
    unsigned version;
    int      nph;
    double   rms;
    double   otime;
} CsnhypodbpSolMeta;

/* Manual mas reciente si hay manuales; si no, mas fases y menor RMS. -1 si n=0. */
int csnhypodbp_preferred_index(const CsnhypodbpSolMeta *sols, int n);

/* --- Almacen de magnitudes por SOLUCION -----------------------------------
 * Clave (qid, modulo origen): csnloc crudo (162), hyp2000 (163) y nlloc (164)
 * publican el MISMO qid, asi que el qid por si solo no distingue una solucion
 * de otra. Permite mostrar la magnitud de cada solucion y no perderla si el
 * TYPE_MAGNITUDE llega antes del ARC.
 * Slots en el orden de columnas c9..c12: 0=ML, 1=Mwp, 2=Mb, 3=Ms. */
#define CSNHYPODBP_MAG_SLOTS 4
#define CSNHYPODBP_MAG_STORE_CAP 128

typedef struct {
    int  qid;
    int  mod;
    int  used;
    char mag[CSNHYPODBP_MAG_SLOTS][32];
} CsnhypodbpMagSlot;

typedef struct {
    CsnhypodbpMagSlot slots[CSNHYPODBP_MAG_STORE_CAP];
} CsnhypodbpMagStore;

void csnhypodbp_mag_store_init(CsnhypodbpMagStore *s);
void csnhypodbp_mag_store_put(CsnhypodbpMagStore *s, int qid, int mod, int slot,
                              const char *mag);
/* Devuelve 1 y copia los slots de (qid,mod); libera la entrada (se "toma una vez"). */
int  csnhypodbp_mag_store_take(CsnhypodbpMagStore *s, int qid, int mod,
                               char out[CSNHYPODBP_MAG_SLOTS][32]);

/* ¿La magnitud (qid_msg,mod_msg) corresponde a la solucion (qid_sol,mod_sol)?
 * mod_msg==0 (magnitud sin modulo, p.ej. csnmags viejo) coincide con cualquier
 * modulo del mismo qid. */
int  csnhypodbp_mag_match(int qid_msg, int mod_msg, int qid_sol, int mod_sol);

#endif /* CSNHYPODBP_GROUP_H */
