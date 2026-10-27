/******************************************************************************
 * csnloc.h                                                                   *
 *                                                                            *
 * csnloc: asociador de fases + localizacion hipocentral rapida para          *
 * EarthWorm. Asociador + localizador local (sin asociador externo).  *
 *                                                                            *
 * Estrategia: back-projection 4D (lat, lon, depth, t0) sobre una grilla +    *
 * DBSCAN sobre las fases para separar eventos simultaneos + refinamiento     *
 * en grilla fina. Threading sobre los nodos de la grilla.                    *
 *                                                                            *
 * Salida: TYPE_HYP2000ARC en HYPO_RING (layout del ARC HYP2000ARC).          *
 ******************************************************************************/
#ifndef CSNLOC_H
#define CSNLOC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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

    /* Identidad de evento. El ancla es la PRIMERA solucion del evento
       (t0/lat/lon congelados): no se usa el t0 de la ultima deteccion.
       Es el mismo evento si |dt| <= EventMatchTimeSec (+ d/V si
       EventMatchVelocityKmS>0) y la distancia <= EventMatchDistKm.
       EventPickTolSec es la tolerancia de tiempo para considerar que una
       picada del candidato es la MISMA picada de un evento ya emitido. */
    double EventMatchTimeSec;        /* ventana temporal base (s)          */
    double EventMatchDistKm;         /* distancia maxima (km)              */
    double EventMatchVelocityKmS;    /* 0 = fija; >0 = crece con la dist.  */
    double EventPickTolSec;          /* tolerancia de picada compartida (s)*/
    /* Segundo criterio de identidad, en OR con el geometrico: dos soluciones
       que comparten >= EventMatchSharedPicks picadas (misma estacion+fase,
       |dt| <= EventPickTolSec) son el MISMO evento aunque sus hipocentros
       disten mas de EventMatchDistKm. Es el criterio de scevent (>= 3) y
       evita que un ancla preliminar mala cree un id nuevo para el mismo
       sismo. 0 = off (solo geometria, comportamiento anterior). */
    int    EventMatchSharedPicks;

    /* Gatillo (0 = off). Solo afecta a la asociacion de csnloc; los
       localizadores reciben despues todas las picadas asociadas. */
    int    MinPPhases;               /* P minimas de un candidato          */
    double MaxAssocDistKm;           /* dist. max. estacion-hipocentro (km) */
    double MaxGapDeg;                /* gap azimutal maximo (grados)       */

    /* Emision. Un evento se crea siempre (para el dedup), pero solo viaja al
       anillo cuando la solucion es publicable: asi la primera y unica
       solucion de csnloc es la buena y no hace falta corregirla. */
    int    MinEmitPhases;            /* fases minimas para publicar (0=off) */
    int    EmitImproveMinPicks;      /* +N fases para corregir una emitida  */

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
    double RefineDepthKm;      /* semiancho inicial de la caja en z (km);
                                  0 = auto (2*RefineNodeKm)          */
    int    FixedDepthOn;       /* 1 = profundidad fija (memset => apagado) */
    double FixedDepthKm;       /* valor de la profundidad fija (km)        */
    double DepthPriorKm;       /* profundidad a priori (km); 0 = sin prior */
    double DepthPriorSigmaKm;  /* sigma del prior gaussiano (km)          */

    /* Salida */
    char   AgencyID[16];
    char   Author[32];
    double EventTTLSec;

    /* Asociador: emite SOLO la primera solucion de cada evento. El rol de
     * locator en vivo (si live_usage=1) es el que versiona; el asociador
     * nunca itera. */
    int    AssociateOnly;

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
    int    manual;         /* 1 si el 12.º token fue M/m        */
    char   pref_mag;       /* 13.er token: L/W/B/S (0 si ausente) */
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
    double t0, lat, lon, depth_km;  /* ancla congelada de la 1.ª solucion  */
    double t0_detect;              /* legado: t0 del candidato que lo creo;
                                      solo se conserva en el state, NO se usa
                                      para el match                          */
    int    nphases;
    double rms_sec, gap_deg, dmin_km, score;
    int    grid_level;
    double last_update_wall;       /* epoch wall-clock de la ultima
                                      deteccion; base del TTL               */
    int    emitted;                /* 1 si ya se emitio alguna version     */
    int    has_manual;             /* 1 si alguna fase es manual           */

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

/* Fija el eje z a UN nodo en `depth_km` (profundidad fija): recalcula nz,
 * n_nodes y descarta el precomputo previo estacion<->nodo. Debe llamarse antes
 * de Grid_PrecomputeStations. Devuelve 0, -1 si los argumentos son invalidos. */
int  Grid_ForceDepth(Grid *g, double depth_km);
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
/* Diagnostico interno de AssembleCandidates (segundos acumulados y conteo de
   llamadas a la tabla de tiempos). Solo lectura; no altera el resultado. */
void AssembleCandidates_Stats(double *dbscan, double *avg, double *claim,
                              long *tt_calls);

/* dbscan.c: numero de hilos para la matriz de adyacencia (el resultado no
   depende de el; solo reparte filas). */
void DBSCAN_SetThreads(int nthreads);

/* dbscan.c: diagnostico (llamadas, suma de n, de n^2, de pares dentro de eps,
   maximo n y histograma de n en potencias de 2). `hn` = tamano de `hist`. */
void DBSCAN_Stats(long *calls, long *sum_n, long long *sum_n2,
                  long long *sum_near, long *max_n, long *hist, int hn);
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
/* Callback de trazas (Debug>=1). csnloc.c lo conecta a logit(); en tests NULL. */
void EventRegistry_SetLogCallback(void (*cb)(const char *msg));
/* Criterio unico de "mismo evento" (lo usan DedupEvents y FindMatch).
   Devuelve 1 si (t0a,lata,lona) y (t0b,latb,lonb) caen dentro de la ventana
   EventMatchTimeSec (+ d/EventMatchVelocityKmS) y de EventMatchDistKm.
   dt_out/d_km_out pueden ser NULL. */
int  CSLoc_EventMatch(const CSLocParams *cfg,
                      double t0a, double lata, double lona,
                      double t0b, double latb, double lonb,
                      double *dt_out, double *d_km_out);
/* Devuelve 1 y el indice del mejor match (menor dt/tmax y distancia).
   shared_out recibe cuantas picadas del candidato coinciden con el evento
   (misma estacion+fase+tiempo dentro de EventPickTolSec). */
int  EventRegistry_FindMatch(const EventRegistry *r, const HypoCandidate *c,
                             const Pick *window, int nwin,
                             const CSLocParams *cfg, int *idx_out,
                             int *shared_out);
/* Cuenta picadas del candidato que ya estan en el evento por estacion+fase
   y |dt| <= EventPickTolSec. */
int  EventRegistry_SharedPicks(const EventRecord *e, const Pick *window,
                               int nwin, const int *phase_idx, int nph,
                               double tol_sec);
/* Picadas comunes entre DOS candidatos (misma estacion+fase, |dt| <= tol_sec).
   Lo usa el dedup de la misma pasada: dos candidatos que comparten picadas
   son el mismo sismo aunque sus hipocentros esten lejos. */
int  CSLoc_SharedPicks(const HypoCandidate *a, const HypoCandidate *b,
                       const Pick *window, int nwin, double tol_sec);
/* 1 si la solucion merece viajar al anillo: nph >= MinEmitPhases (0=off),
   rms <= MaxRMS y gap <= MaxGapDeg (0=off cada uno). El registro la conserva
   igualmente para el dedup y el TTL. */
int  CSLoc_Emittable(const HypoCandidate *c, const CSLocParams *cfg);
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
                          unsigned long id_base, double now_epoch,
                          unsigned long *id_out,
                          unsigned int *ver_out, int *manual_out);

/* state.c */
int  State_Save(const char *path, const EventRegistry *r);
int  State_Load(const char *path, EventRegistry *r);

/* hypo_out.c */
int  FormatHYP2000ARC(const HypoCandidate *h, const StationList *st,
                      const Pick *picks, const CSLocParams *cfg,
                      unsigned long event_id, unsigned int version,
                      int manual, char *buf, int buflen);

/* Fases auxiliares compartidas */
const char *Phase_Name(int phase);
int  Phase_FromName(const char *name);   /* P->0, S->1, resto -1 */

#endif /* CSNLOC_H */
