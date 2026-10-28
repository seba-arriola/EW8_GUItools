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
    int      confirmed; /* 1 = snapshot confirmado a mano (no viaja en el ARC) */
    char     pref_mag;  /* tipo preferido del ARC (BMTYPI): L/W/B/S, 0 = ninguna */
    int      npicks;
    CsnhypodbpPick picks[CSNHYPODBP_MAX_PICKS];
    /* id de origen de la BD (`E<qid>-<otime>:<mod>:<version>`), si se cargo de
     * ahi; vacio en las soluciones que llegan por anillo. */
    char     origin_id[64];
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

/* Escribe un ARC Y2K con la cabecera de `sol` y TODAS sus fases. */
int csnhypodbp_arc_build(const CsnhypodbpSolution *sol, int manual,
                         char *out, size_t outn);

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
    int      confirmed;  /* 1 = snapshot confirmado a mano (id con version) */
    unsigned version;
    int      nph;
    double   rms;
    double   otime;
    double   created;    /* epoch: creacion en la BD / insercion en la GUI */
    char     origin_id[64]; /* id de la BD; vacio en soluciones de anillo */
} CsnhypodbpSolMeta;

/* Confirmada mas reciente si hay confirmadas; si no, manual mas reciente; si
 * no, mas fases y menor RMS. -1 si n=0. */
int csnhypodbp_preferred_index(const CsnhypodbpSolMeta *sols, int n);

/* ¿La fila (qid, mod, confirmed, version) es la misma solucion que `sol`?
 * `history` = el mod es de un localizador con live_usage=1: entonces la version
 * forma parte de la identidad y cada iteracion publicada es una fila propia.
 * Los snapshots confirmados tambien se separan por version (cada Confirm es
 * inmutable); la fila viva del asociador (history=0, no confirmada) colapsa a
 * una sola fila por (qid, mod), la de version vigente. */
int csnhypodbp_sol_same_row(int qid, int mod, int confirmed, unsigned version,
                            const CsnhypodbpSolution *sol, int history);

/* --- Almacen de magnitudes por SOLUCION -----------------------------------
 * Clave (qid, modulo origen): cada mod es el que resolvio la tabla de locators
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
