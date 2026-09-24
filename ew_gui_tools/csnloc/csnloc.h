/******************************************************************************
 * csnloc.h                                                                   *
 *                                                                            *
 * csnloc: asociador de fases + localizacion hipocentral rapida para          *
 * EarthWorm. Reemplaza localmente el camino ew2glass -> GLASS3 -> glass2ew.  *
 *                                                                            *
 * Estrategia: back-projection 4D (lat, lon, depth, t0) sobre una grilla +    *
 * DBSCAN sobre las fases para separar eventos simultaneos + refinamiento     *
 * en grilla fina. Threading sobre los nodos de la grilla.                    *
 *                                                                            *
 * Salida: TYPE_HYP2000ARC en HYPO_RING (mismo layout que glass2ew).          *
 ******************************************************************************/
#ifndef CSNLOC_H
#define CSNLOC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <earthworm.h>
#include <transport.h>

/* ------------------------------------------------------------------------- */
/* Limites generales                                                          */
/* ------------------------------------------------------------------------- */
#define CSLOC_MAX_PICKS       512
#define CSLOC_MAX_STATIONS    1024
#define CSLOC_MAX_PHASES      64
#define CSLOC_TT_MAX          60   /* trtm: MAX=60 en ttlim.h */
#define CSLOC_MAX_EVENTS      16
#define CSLOC_MAX_CAND       256
#define CSLOC_MAX_NUC       4096   /* maximos locales de back-projection */
#define CSLOC_STR             256

#define CSLOC_MAX_GRIDS        32   /* grillas anidadas simultaneas    */
#define CSLOC_MAX_GRID_DEPTHS  64   /* capas de profundidad por grilla */

/* Niveles de resolucion (de mas gruesa a mas fina). */
#define GRID_LEVEL_GLOBAL      0
#define GRID_LEVEL_REGIONAL    1
#define GRID_LEVEL_LOCAL       2

#define CSLOC_PHASE_P        0
#define CSLOC_PHASE_S        1
#define CSLOC_NPHASE         2

/* ------------------------------------------------------------------------- */
/* Parametros configurables (csnloc.d)                                        */
/* ------------------------------------------------------------------------- */
typedef struct {
    char   MyModName[CSLOC_STR];   /* MyModuleId            */
    char   InRingName[CSLOC_STR];  /* InRing    (PICK_RING) */
    char   OutRingName[CSLOC_STR]; /* OutRing   (HYPO_RING) */
    long   InKey;
    long   OutKey;
    int    HeartbeatInt;
    int    LogFile;
    int    Debug;
    int    DumpHypo;               /* 1 = volcar el HYP2000ARC al log */

    char   StaFile[CSLOC_STR];     /* metadata de estaciones        */
    char   TauModel[CSLOC_STR];    /* nombre del modelo tau (iasp91) */

    /* Grillas anidadas (back-projection). Cada entrada apunta a un archivo
       .grid y su nivel (GRID_LEVEL_*); el orden es el de aparicion. */
    char   GridFiles[CSLOC_MAX_GRIDS][CSLOC_STR];
    int    GridFileLevel[CSLOC_MAX_GRIDS];
    int    n_gridfiles;
    int    GridActivationMinPicks;   /* picks para activar una grilla fina */
    double GridActivationMarginDeg;  /* margen de activacion por cercania  */
    double EventDedupSec;            /* dedup de eventos entre grillas (s) */
    double EventDedupKm;             /* dedup de eventos entre grillas (km)*/

    /* Asociacion */
    double AssocWindowSec;     /* ventana temporal de picks        */
    double RePickWindowSec;    /* reemplazo mismo SCNL             */
    double PickTTLSec;         /* expiracion de picks              */
    double T0ToleranceSec;     /* tolerancia del stacking de t0    */
    double DBSCAN_Eps;         /* epsilon de clustering            */
    int    DBSCAN_MinPts;      /* min. fases por cluster           */
    double BackProjThreshold;  /* umbral relativo de nucleacion    */
    int    MaxEventsPerWindow;
    int    MinPhasesPerEvent;
    double MaxRMS;
    double PhaseWeightP;
    double PhaseWeightS;

    /* Computo */
    int    NumThreads;         /* hilos de back-projection         */
    int    RefineIterations;   /* iteraciones de refinamiento      */
    double RefineNodeKm;       /* resolucion de grilla fina        */

    /* Salida */
    char   AgencyID[16];
    char   Author[32];
    double EventTTLSec;

    /* Versionado dinamico de eventos */
    double MaxRMSDegrade;          /* empeoramiento RMS tolerado (fraccion) */
    double MaxGapDegradeDeg;       /* empeoramiento de gap tolerado (grados) */
    double PhaseAssocTolSec;       /* residual max. asociacion P (s)         */
    double PhaseAssocTolSecS;      /* residual max. asociacion S (s)         */
    double PhaseResidualMaxSec;    /* umbral de poda P (s)                   */
    double PhaseResidualMaxSecS;   /* umbral de poda S (s)                   */
    int    RenucleateMinNewPhases; /* fases nuevas para re-nuclear           */
} CSLocParams;

/* ------------------------------------------------------------------------- */
/* Tipos de datos del pipeline                                                */
/* ------------------------------------------------------------------------- */
typedef struct {
    char   sta[8];
    char   net[4];
    char   chan[8];
    char   loc[4];
    int    phase;          /* CSLOC_PHASE_P | CSLOC_PHASE_S     */
    char   phase_name[8];  /* "P" | "S" (tal como llego)        */
    double t_epoch;        /* tiempo de arribo (epoch, s)       */
    int    weight;         /* 0..4                              */
    int    used;           /* asignado a un evento (0/1)        */
} Pick;

typedef struct {
    Pick  *items;
    int    n;
    int    cap;
    double repick_window_sec;
    unsigned long n_inserted;
    unsigned long n_replaced;
} PickBuffer;

typedef struct {
    char   sta[8];
    double lat;            /* grados, geocentrica para taulib   */
    double lon;
    double elev_km;
} Station;

typedef struct {
    int    n;              /* numero de estaciones cargadas     */
    Station st[CSLOC_MAX_STATIONS];
    double lat_geoc_sta[CSLOC_MAX_STATIONS];
} StationList;

typedef struct {
    double lat;            /* geocentrica */
    double lon;
    double depth_km;
    double t0;
    int    nphases;
    double rms_sec;
    double gap_deg;
    double dmin_km;
    double score;          /* stack de back-projection (peso apilado)   */
    int    grid_level;     /* GRID_LEVEL_* de la grilla de origen       */
    int    phase_idx[CSLOC_MAX_PHASES];
    double residual[CSLOC_MAX_PHASES];
    unsigned long id;
} HypoCandidate;

/* ------------------------------------------------------------------------- */
/* Registro de eventos activos (versionado dinamico).                         */
/*                                                                            */
/* Un evento acumula sus fases: la ventana deslizante solo sirve para         */
/* DESCUBRIR eventos nuevos; una vez creado, el evento conserva sus fases y   */
/* las optimiza (incorpora re-picks / fases nuevas, poda las que no ajustan). */
/* ------------------------------------------------------------------------- */
typedef struct {
    unsigned long id;              /* ID base persistente                  */
    unsigned int  version;         /* ultima version emitida (0 = ninguna) */
    double t0, lat, lon, depth_km;
    double t0_detect;              /* t0 del candidato que lo detecto      */
    int    nphases;
    double rms_sec, gap_deg, dmin_km, score;
    int    grid_level;
    double last_update_epoch;      /* para TTL                             */
    int    emitted;                /* 1 si ya se emitio alguna version     */

    Pick   phases[CSLOC_MAX_PHASES];    /* copia autocontenida de las fases */
    double residual[CSLOC_MAX_PHASES];  /* residual vs solucion vigente     */
    int    nphases_stored;

    Pick   pruned[CSLOC_MAX_PHASES];    /* fases podadas (no re-incorporar) */
    int    npruned;
} EventRecord;

typedef struct {
    EventRecord ev[CSLOC_MAX_EVENTS];
    int         n;
} EventRegistry;

typedef struct {
    /* --- especificacion (archivo .grid) --- */
    char   name[CSLOC_STR];
    char   path[CSLOC_STR];
    int    level;              /* GRID_LEVEL_*                        */
    double lat_min, lon_min, lat_max, lon_max;
    double node_km;
    double depth_min, depth_max, depth_step;    /* homogenea          */
    double depth_layers[CSLOC_MAX_GRID_DEPTHS]; /* no homogenea       */
    int    n_depth_layers;     /* 0 -> usar min/max/step              */
    int    max_nodes;          /* 0 -> sin tope                       */
    double sta_max_dist_km;    /* radio estacion<->nodo (0 -> todas)  */
    int    num_stations_per_node; /* 0 -> sin tope                    */
    int    min_phases;         /* 0 -> hereda cfg->MinPhasesPerEvent  */
    double backproj_threshold; /* <0 -> hereda cfg->BackProjThreshold */

    /* --- ejes construidos --- */
    int    ny, nx, nz;
    double *lat;           /* ny   (geocentrica) */
    double *lon;           /* nx                 */
    double *depth;         /* nz                 */
    int     n_nodes;

    /* --- precomputo estacion<->nodo (CSR por nodo) --- */
    int    *node_sta_off;      /* n_nodes+1: offsets en node_sta_idx */
    int    *node_sta_idx;      /* nnz: indices de estacion           */
    int     nnz;
    unsigned char *sta_mask;   /* n_sta: estacion asociada a la grilla */
    int     n_sta;
} Grid;

/* Conjunto de grillas anidadas (de gruesa a fina). */
typedef struct {
    int  n;
    Grid g[CSLOC_MAX_GRIDS];
} GridSet;

/* ------------------------------------------------------------------------- */
/* Modulo de tiempos de viaje.                                                *
 *                                                                            *
 * taulib usa estado global (depset/trtm), por lo que NO es thread-safe. La   *
 * solucion: precomputar una tabla 2D [profundidad][distancia] para P y S en  *
 * el hilo principal y que los workers solo interpolen (lectura pura).        *
 * ------------------------------------------------------------------------- */
typedef struct {
    char   model[CSLOC_STR];
    int    nd;                 /* capas de profundidad            */
    int    nk;                 /* muestras de distancia           */
    double dk_step;            /* paso de distancia (grados)      */
    double k_max;              /* distancia maxima (grados)       */
    double *depth;             /* nd                              */
    double *tt[CSLOC_NPHASE];  /* nd x nk (segundos)              */
} TTModel;

/* ------------------------------------------------------------------------- */
/* Contexto global del proceso (hilo principal)                               */
/* ------------------------------------------------------------------------- */
typedef struct {
    CSLocParams  cfg;
    StationList  stations;
    PickBuffer   picks;
    GridSet      grids;
    TTModel      tt;
    unsigned long next_event_id;
    unsigned long id_base;         /* base de IDs de la sesion (epoch)      */
    unsigned long seq;             /* contador de eventos de la sesion     */
    EventRegistry events;          /* eventos activos (versionado)         */
    char          state_path[CSLOC_STR]; /* archivo de estado (recuperacion) */

    unsigned char MyInstId;
    unsigned char MyModId;
    unsigned char TypeHeartBeat;
    unsigned char TypeError;
    unsigned char TypePickSCNL;
    unsigned char TypeHyp2000Arc;

    SHM_INFO      OutRegion;
    pid_t         MyPid;
    int           Terminate;

    /* Modo offline (validacion): lee picks de un fichero con reloj virtual
       y emite un JSON por evento en vez de escribir al HYPO_RING. */
    int           Offline;
    FILE         *OfflineOut;
} CSLocCtx;

/* ------------------------------------------------------------------------- */
/* API entre modulos                                                          */
/* ------------------------------------------------------------------------- */

/* pick_scln.c */
int  PickSCNL_Parse(const char *msg, int len, Pick *out);

/* pick_buffer.c */
void PickBuffer_Init(PickBuffer *b, int cap, double repick_window_sec);
void PickBuffer_Free(PickBuffer *b);
int  PickBuffer_AddOrReplace(PickBuffer *b, const Pick *p);
int  PickBuffer_Prune(PickBuffer *b, double now_epoch, double ttl_sec);
int  PickBuffer_Snapshot(PickBuffer *b, double now_epoch, double window_sec,
                         Pick *out, int max_out);
int  PickBuffer_MarkUsed(PickBuffer *b, const int *idx, int n, int used);

/* stations.c */
int  Stations_Load(const char *sta_file, StationList *out);
int  Stations_Find(const StationList *list, const char *sta);

/* ttmodel.c */
int  TTModel_Init(TTModel *m, const char *tau_dir, const char *model,
                  double dmin, double dmax, double dstep);
void TTModel_Free(TTModel *m);
int  TTModel_Predict(TTModel *m, double delta_deg, double depth_km,
                     int phase, double *t_sec, double *dtdd);
double TT_GreatCircleDeg(double lat1, double lon1, double lat2, double lon2);
double TT_AzimuthDeg(double lat1, double lon1, double lat2, double lon2);

/* grid.c */
int  Grid_LoadFile(const char *path, int level, Grid *g);
int  Grid_BuildAxes(Grid *g);
int  Grid_PrecomputeStations(Grid *g, const StationList *st);
int  Grid_ContainsLL(const Grid *g, double lat, double lon, double margin_deg);
void Grid_Free(Grid *g);
void Grid_IndexToLLD(const Grid *g, int iy, int ix, int iz,
                     double *lat, double *lon, double *depth);

/* gridset.c */
int  GridSet_Load(GridSet *gs, const CSLocParams *cfg, const StationList *st);
void GridSet_Free(GridSet *gs);
int  Grid_IsActive(const Grid *g, const Pick *picks, const int *pick_sidx,
                   int npick, const StationList *st, const CSLocParams *cfg,
                   const HypoCandidate *nuc, int nnuc);

/* backprojection.c */
int  BackProject_Nucleations(const Grid *g, const StationList *st,
                             const Pick *picks, int npick, TTModel *tt,
                             const CSLocParams *cfg, HypoCandidate *nuc,
                             int max_nuc, int nthreads);
int  AssembleCandidates(const HypoCandidate *nuc, int nnuc,
                        const StationList *st, const Pick *picks, int npick,
                        TTModel *tt, const CSLocParams *cfg,
                        HypoCandidate *out, int max_out);
int  BackProject(const Grid *g, const StationList *st, const Pick *picks,
                 int npick, TTModel *tt, const CSLocParams *cfg,
                 HypoCandidate *cand, int max_cand, int nthreads);

/* dbscan.c (features: lat, lon, t0 ajustado por distancia) */
int  DBSCAN_Cluster(const double *X, int n, int dim, double eps, int min_pts,
                    int *labels);

/* refine.c */
int  RefineHypo(HypoCandidate *h, const StationList *st, const Pick *picks,
                TTModel *tt, const CSLocParams *cfg);

/* event_registry.c */
void EventRegistry_Init(EventRegistry *r);
int  EventRegistry_FindMatch(const EventRegistry *r, const HypoCandidate *c,
                             const CSLocParams *cfg, int *idx_out);
int  EventRegistry_Accept(const EventRecord *prev, const HypoCandidate *c,
                          const CSLocParams *cfg);   /* 1 = aceptar version */
int  EventRegistry_AddPhase(EventRecord *e, const Pick *p,
                            const CSLocParams *cfg);  /* 1 = cambio */
int  EventRegistry_PrunePhases(EventRecord *e, const CSLocParams *cfg);
void EventRegistry_Expire(EventRegistry *r, double now, double ttl_sec);
/* Fusiona un candidato con el registro. Devuelve 1 si hay que emitir y llena
   id_out/ver_out; 0 si no hay nada nuevo que emitir. */
int  EventRegistry_Upsert(EventRegistry *r, const HypoCandidate *c,
                          const Pick *window, int nwin, const CSLocParams *cfg,
                          const StationList *st, TTModel *tt,
                          const GridSet *grids,
                          unsigned long id_base, unsigned long *id_out,
                          unsigned int *ver_out);

/* state.c */
int  State_Save(const char *path, const EventRegistry *r);
int  State_Load(const char *path, EventRegistry *r);

/* hypo_out.c */
int  FormatHYP2000ARC(const HypoCandidate *h, const StationList *st,
                      const Pick *picks, const CSLocParams *cfg,
                      unsigned long event_id, unsigned int version,
                      char *buf, int buflen);

/* Fases auxiliares compartidas */
const char *Phase_Name(int phase);
int  Phase_FromName(const char *name);   /* P->0, S->1, resto -1 */

#endif /* CSNLOC_H */
