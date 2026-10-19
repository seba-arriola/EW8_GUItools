#ifndef CSNHYPODBP_H
#define CSNHYPODBP_H

#include <gtk/gtk.h>
#include <adwaita.h>
#include "ewgui/ring.h"
#include "ewgui/actions.h"
#include "ewgui/config.h"
#include "ewgui/db.h"
#include "ewgui/geo.h"
#include "ewgui/menu_ui.h"
#include "ewgui/wave.h"
#include "ewgui/view.h"
#include "ewgui/export.h"
#include "csnhypodbp_row.h"
#include "csnhypodbp_group.h"
#include "csnhypodbp_search.h"
#include "csnhypodbp_master.h"
#include "csnhypodbp_trace.h"
#include "csnhypodbp_statable.h"
#include "csnhypodbp_staitem.h"
#include "csnhypodbp_preview.h"
#include "csnhypodbp_profile.h"
#include "csnhypodbp_mapview.h"
#include "csnhypodbp_magcfg.h"
#include "csnhypodbp_magsta.h"
#include "csnhypodbp_magitem.h"
#include "csnhypodbp_magmarks.h"
#include "csnhypodbp_magview.h"
#include "ewgui/overlay.h"
#include "csnhypodbp_tt.h"
#include "csnhypodbp_view.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <locale.h>
#include <signal.h> 
#include <limits.h>
#include <stdint.h>

#include <earthworm.h>
#include <transport.h>
#include <trace_buf.h>
#include <kom.h>
#include <rw_mag.h> 

#define MAX_STR 256
#define MAX_ESTA 600
#define MAX_MINUTES 15   /* OPTIMIZACION EXTREMA: Bajado de 120 a 15 para evitar OOM Killer de Linux */
#define SAMPRATE_DEF 20  
#define CIRC_IDX(abs_val, size) (int)(((abs_val) % (size) + (size)) % (size))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- ESTRUCTURAS --- */
/* PICK, MAX_PICKS_PER_STA y DEV_STATION viven en csnhypodbp_station.h
 * (headless) para poder testear ahi las decisiones de la vista. */

#define MAX_CACHED_EVENTS 50
#define MAX_PICKS_PER_EVENT 200

typedef struct {
    char sta[8];
    char chan[8];
    char phase[8];
    double pTime;
} CACHED_PICK;

typedef struct {
    int qid;
    int mod;        /* modulo origen del ARC (MOD_CSNLOC / MOD_HYP2000_RING / MOD_NLLOC_RING) */
    int version;    /* version (qver) de la solucion */
    int num_picks;
    CACHED_PICK picks[MAX_PICKS_PER_EVENT];
} EVENT_PICK_CACHE;

typedef struct { 
    int idx; 
    double dist; 
    int y_top;    
    int y_bottom; 
} StaNode;

/* --- VARIABLES GLOBALES (EXTERNAS) --- */
#include "csnhypodbp_locator.h"
#include "csnhypodbp_station.h"
#include "csnhypodbp_manual.h"
#include "csnhypodbp_locate.h"
#define MAX_LOCATORS 8
extern char g_channels_order[128];     /* channelsOrder (orden externo de canales) */
extern char g_components_order[64];    /* componentsOrder (orden interno) */
extern CsnhypodbpLocator g_locators[MAX_LOCATORS];
extern int  g_nlocators;
extern char g_active_locator[32];      /* localizador elegido para `locate` */
extern char g_manual_state_file[MAX_STR];  /* ManualStateFile (solucion manual propia) */
extern int  g_locator_timeout_sec;     /* LocatorTimeoutSec */
extern int  g_prefetch_neighbors;      /* PrefetchNeighbors */
extern int  g_hide_empty;              /* HideEmptyStations (ocultar sin datos) */
extern double g_color_p[3];            /* pColor: P (marca + circulo del indicador) */
extern double g_color_s[3];            /* sColor: S (marcas + circulo del indicador) */
void csnhypodbp_log_colors(void);      /* loguea los colores en hex (copiar al .d) */
extern char MyModName[MAX_STR];
extern char InRingName[MAX_STR];
extern char InRingRawName[MAX_STR];   /* 2.º anillo de entrada (csnloc crudo) */
extern char OutRingName[MAX_STR];
extern char InRingPickName[MAX_STR];  /* anillo de picks a leer (default OutRing) */
extern char TauModel[MAX_STR];        /* modelo de tiempos de viaje (default iasp91) */
extern char StaFile[MAX_STR];
extern char DbEnvFile[MAX_STR];   /* credenciales PostgreSQL (historial/busqueda) */
extern char StateFile[MAX_STR];   /* archivo de estado de csnloc (recuperacion) */
extern char g_store_root[512]; /* StoreRoot del wstore */
extern int  g_fetch_threads;   /* hilos de fetch (reservado) */
extern int  HeartBeatInt;
extern int  LogFile;
extern pid_t MyPid;

extern unsigned char MyInstId;
extern unsigned char MyModId;
extern unsigned char TypeHeartBeat;
extern unsigned char TypeError;
extern unsigned char TypeHyp2000Arc;
extern unsigned char TypePickSCNL;
extern unsigned char TypeMagnitude; 

extern SHM_INFO InRegion;       
extern SHM_INFO InRegionRaw;    /* anillo crudo de csnloc (si InRingRaw) */
extern SHM_INFO InRegionPick;   /* lectura del anillo de picks (TYPE_PICK_SCNL) */
extern SHM_INFO PRegion;        
extern MSG_LOGO GetLogo[2]; 
extern MSG_LOGO PickLogo[1];    /* filtro de lectura de TYPE_PICK_SCNL */

extern EVENT_PICK_CACHE PickCache[MAX_CACHED_EVENTS];
extern int pick_cache_idx;

extern DEV_STATION *StaArray; 
extern int     bHasData[MAX_ESTA]; 
extern double  g_StaDist[MAX_ESTA]; 
extern double  g_StaTheoP[MAX_ESTA];   /* tiempo teorico de P por estacion (epoch) */
extern int     NumEstaciones;
extern int     selected_qid;
extern int     selected_mod;
extern int     selected_version;
extern int     g_ring_sync;      /* 1 = re-seleccion interna de una llegada */
extern double  selected_otime;
extern double  selected_depth;
extern char    selected_id[32];
/* origin_id de la BD del origen seleccionado ("" si todavia no esta en la base). */
extern char    selected_origin_id[64];
extern double  g_pick_window_sec;      /* ventana de asociacion de picks capturados */
extern char    g_pick_phase;           /* fase a picar en modo Repick ('P'|'S') */

extern EwGuiCanvas *canvas_global;
extern GtkWidget *tree_global;                /* GtkColumnView maestro (eventos) */
extern GtkWidget *tree_detail;                /* GtkColumnView detalle (soluciones) */
extern GListStore *g_store_hypo;              /* todas las soluciones (GTK4) */
extern GListStore *g_store_events;            /* maestro: 1 fila por evento */
extern GListStore *g_store_detail;            /* detalle: soluciones del evento */
extern GtkSingleSelection *g_selection_events;
extern GtkSingleSelection *g_selection_detail;
extern GtkSingleSelection *g_selection_hypo;  /* (compat) selección de soluciones */
extern GtkWidget *g_notebook;                 /* pestañas raiz: Eventos/Procesar (GTK4) */
extern GtkWidget *g_page_procesar;            /* pagina raiz "Procesar" (sub-notebook) */
extern GtkWidget *g_subnotebook;              /* sub-pestañas: picking / magnitudes */
extern GtkWidget *window_global; 

extern double g_max_dist_km; 
extern double   g_zoom_factor; 
extern double   g_dWindowStart;
extern double   g_dScreenTime; 
extern int      g_margin_left; 
extern int      g_spacing;

extern StaNode g_SortedNodes[MAX_ESTA];
extern int     g_NumSortedNodes;
extern gboolean g_history_needs_saving; 

/* --- LISTA DE ESTACIONES (pestaña Ondas) --- */
#define CSNHYPODBP_BAND_MIN 110       /* alto MINIMO por banda: por debajo, scroll */
#define CSNHYPODBP_CANVAS_MIN_W 260   /* ancho minimo del canvas (la tabla no lo come) */
/* La columna de mapas NO lleva `size_request`: su minimo ya lo fijan los dos
   canvas (300 px) y un minimo propio se suma al de la ventana, que es lo que
   dejaba la columna sin ancho. La protege `shrink_end_child(FALSE)`. */
extern char g_selected_station[16];     /* estacion seleccionada ("" = ninguna) */
extern GtkWidget *g_sta_view;           /* GtkColumnView de la tabla de estaciones */
extern GListStore *g_sta_store;         /* CSNHYPODBP_TYPE_STA_ITEM, 2 filas/estacion */
extern GtkSingleSelection *g_sta_sel;   /* seleccion de la tabla de estaciones */
extern int g_rebuilding_stations;       /* 1 mientras se repuebla la tabla */
extern int g_pick_on[MAX_ESTA][2];      /* pick P(0)/S(1) incluido en el locate */
void csnhypodbp_sta_sel_reset(void);    /* marca todas las estaciones/picks (default) */
void on_pick_check_toggled(GtkCheckButton *b, gpointer data);
/* Tabla vigente (una fila por estacion) y picadas de la solucion seleccionada. */
extern CsnhypodbpStaRow g_sta_rows[MAX_ESTA];
extern int g_sta_rows_n;
extern CsnhypodbpPick g_base_picks[CSNHYPODBP_MAX_PICKS];
extern int g_base_npicks;
/* Solucion elegida en el detalle (pestaña Eventos): aporta los residuales a la
   tabla de Ondas mientras no haya candidata de locate. */
extern CsnhypodbpSolution g_base_sol;
/* Modelos ordenados de las tres tablas (los usa el helper de ordenado). */
extern GtkSortListModel *g_sort_events;
extern GtkSortListModel *g_sort_detail;
extern GtkSortListModel *g_sort_sta;

/* --- VISTA DE ONDAS: zoom (2 clics), modo de clic y eje de tiempo --- */
typedef enum { CSNHYPODBP_CLICK_ZOOM = 0, CSNHYPODBP_CLICK_PICK } CsnhypodbpClickMode;
extern CsnhypodbpView      g_view;        /* ventana visible relativa + ancla */
extern CsnhypodbpView      g_mag_zoomview; /* idem para el canvas de magnitudes */
extern CsnhypodbpClickMode g_click_mode;  /* ZOOM (default) | PICK (tras p/S) */
extern EwGuiCanvas        *g_axis_canvas; /* eje de tiempo UTC (no scrollea) */
extern double              g_win_start_cur; /* winStart de la estacion dibujada */
/* Ancla de la ventana del picking (P de referencia congelada) y su refresco. */
extern double              g_win_ref_pick;
extern int                 g_win_ref_has_p;
void   csnhypodbp_pick_anchor_refresh(void);
extern double              g_cursor_x, g_cursor_y; /* puntero dentro del canvas */
extern int                 g_cursor_valid;  /* 1 = puntero sobre el canvas */
void csnhypodbp_help_dialog_run(GtkWindow *parent);
void on_draw_axis(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data);
void on_axis_drag_begin(GtkGestureDrag *g, double sx, double sy, gpointer data);
void on_axis_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer data);
void on_axis_drag_end(GtkGestureDrag *g, double ox, double oy, gpointer data);

/* --- Preview de la solucion: tablita de 2 filas (cabecera + valores) --- */
extern GtkWidget *g_preview_box;
extern GtkWidget *g_preview_title;                       /* titulo segun estado */
extern GtkWidget *g_preview_val[CSNHYPODBP_PREVIEW_NVALS]; /* fila de valores */
extern GtkWidget *g_preview_btn_confirm, *g_preview_btn_discard;
/* Radios de magnitud preferida del cuadro de la candidata. */
extern GtkWidget *g_preview_radio[CSNHYPODBP_PREVIEW_MAGS];
extern int        g_preview_pref_kind;    /* 0=ML..3=Ms, -1 = ninguna */
extern int        g_preview_pref_sync;    /* 1 = la GUI esta sincronizando */
void on_preview_pref_toggled(GtkCheckButton *b, gpointer data);
extern CsnhypodbpSolution g_preview_sol;
extern CsnhypodbpMagOut g_preview_mag;
extern gboolean g_preview_valid;

/* --- Mapas: panel de planta (contrato de csnrv) y panel de perfil --- */
extern char  MapImageFile[MAX_STR];
extern char  ProfileDataDir[MAX_STR];
extern char  ProfileFile[MAX_STR];
extern CsnhypodbpProfiles g_profiles;
extern int    g_profile_lon_col, g_profile_val_col;
extern double g_profile_depth_min_km;
extern double g_map_min_span_deg;        /* MapMinSpanDeg: ancho minimo del fit */
extern double g_map_zoom_factor;         /* MapZoom: factor sobre el auto-encuadre */
extern double g_map_split_frac;          /* MapSplit: fraccion de altura de la planta */

/* --- Columna derecha: panel de planta (arriba) y de perfil (abajo) --- */
#define CSNHYPODBP_MAX_MAP_PTS 256
extern GtkWidget   *g_maps_col;          /* contenedor de los dos cuadros */
extern EwGuiCanvas *g_map_plan;          /* mapa equirectangular (contrato csnrv) */
extern EwGuiCanvas *g_map_prof;          /* corte longitud/profundidad */
extern GtkWidget   *g_map_plan_cap;      /* caption (texto visible por AT-SPI) */
extern GtkWidget   *g_map_prof_cap;
extern GdkPixbuf   *g_map_pixbuf;        /* MapImageFile ya cargado */
extern EwOverlay   *g_map_overlay;       /* capas .lines cargadas */
extern CsnhypodbpMapPt g_map_pts[CSNHYPODBP_MAX_MAP_PTS];
extern int          g_map_npts;          /* puntos vigentes (preferida/otras/candidata) */
extern int          g_active_profile;    /* indice del perfil dibujado, -1 = ninguno */
extern CsnhypodbpProfileLine g_prof_slab, g_prof_topo;   /* datos del perfil activo */

void csnhypodbp_maps_load(void);         /* carga pixbuf + overlays (una vez) */
void csnhypodbp_maps_refresh(void);      /* UNICO punto que recalcula estado y repinta */
void on_draw_map_plan(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data);
void on_draw_map_profile(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data);
void on_map_plan_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data);
void on_map_plan_press(GtkGestureClick *g, int n_press, double x, double y, gpointer data);
void on_map_plan_release(GtkGestureClick *g, int n_press, double x, double y, gpointer data);
void on_map_plan_motion(GtkEventControllerMotion *c, double x, double y, gpointer data);
void on_map_prof_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data);
void on_map_prof_press(GtkGestureClick *g, int n_press, double x, double y, gpointer data);
void on_map_prof_release(GtkGestureClick *g, int n_press, double x, double y, gpointer data);
void on_map_prof_motion(GtkEventControllerMotion *c, double x, double y, gpointer data);

/* --- Barra inferior (pestaña Ondas): filtro de picadas + localizador activo --- */
extern int g_filter_auto;      /* incluir picadas automaticas en el locate */
extern int g_filter_manual;    /* incluir manuales (default 1) */
extern int g_filter_theo;      /* incluir teoricas */
extern GtkWidget *g_chk_a, *g_chk_m, *g_chk_teo;
extern GtkWidget *g_locator_label;
void csnhypodbp_update_locator_label(void);
void on_filter_toggled(GtkCheckButton *b, gpointer data);

/* --- FILTRO CONFIGURABLE --- */
extern int      g_filter_type;     /* 0=Raw, 1=High-Pass, 2=Low-Pass, 3=Band-Pass */
extern double   g_filter_f1;       /* F1 (Hz) */
extern double   g_filter_f2;       /* F2 (Hz) */
extern int      g_filter_order;    /* orden IIR (2 o 4) */
extern double   g_align_lead;      /* segundos de ventana antes de la onda P */
extern double   g_mag_fetch_sec;   /* ventana pedida por solucion (s) */

/* --- DEBOUNCE DE RECARGA DE WAVEFORMS --- */
extern gboolean pending_waveform_reload;
extern double   selected_lat;
extern double   selected_lon;

/* --- AGRUPAMIENTO DE SOLUCIONES --- */
extern double g_group_pick_tol_sec;   /* GroupPickTolSec (default 0.1) */
extern int    g_group_min_match_picks;/* GroupMinMatchPicks (default 3) */
extern int    g_group_max_solutions;  /* GroupMaxSolutions (default 100) */

/* --- BUSQUEDA HIPOCENTRAL (pestaña Eventos) --- */
extern int g_search_active;      /* 1 = la maestra muestra un resultado de busqueda */
extern int g_search_max_events;  /* SearchMaxEvents (tope de filas) */
int  search_apply(const CsnhypodbpSearch *s);  /* consulta y repuebla; nº eventos o -1 */
void search_clear(void);                       /* vuelve a la vista vivo+historial */
void csnhypodbp_update_search_ui(void);        /* sensibilidad del boton "Clear search" */

/* --- BACKEND SDS (archivo SeisComP) --- */
extern int  g_sds_enabled;          /* SdsEnabled */
extern char g_sds_root[512];        /* SdsRoot (p.ej. /mnt/sds) */
extern int  g_sds_fallback_hours;   /* SdsFallbackHours: edad a partir de la cual se usa SDS */

typedef struct EventGroup EventGroup;
EventGroup   *csnhypodbp_event_group_of(const CsnhypodbpRow *row);
size_t        csnhypodbp_event_group_nsol(const EventGroup *g);
CsnhypodbpRow *csnhypodbp_event_group_solution(const EventGroup *g, size_t i);
CsnhypodbpRow *csnhypodbp_event_group_preferred(const EventGroup *g);

/* --- PROTOTIPOS DE FUNCIONES --- */
int ReadConfig(char *configfile);
void Status(unsigned char type, short ierr, char *note);
void ConnectToEarthworm(void);
void LoadStationsFromFile(void);
int ParseY2K_Hypo(char *msg, double *otime, double *lat, double *lon, double *depth, double *res, int *nps, int *azm, int *qid, int *qver, double *pref_mag, char *mag_type, int mod);

void cargar_sismos_iniciales(GtkWidget *tree);
int procesar_mensaje_sismo(GtkWidget *tree, const char *payload, int mod);
int procesar_mensaje_mag(GtkWidget *tree, const char *payload);
void FetchWaveformsForEvent(double otime, double eq_lat, double eq_lon, int qid, int mod, int version);
void FetchStationWaveforms(const char *sta);
void EnsureAndFetchStation(const char *sta);
const char *ModLabel(int mod);
void cargar_sismos_db(void);
int  csnhypodbp_db_init(const char *envfile, int hours);
int  csnhypodbp_db_ready(void);   /* 1 si la BD esta conectada */
EwGuiDb *csnhypodbp_db(void);     /* handle de la conexion (NULL si no hay BD) */
void csnhypodbp_db_reconnect(void);  /* reabre la conexion a la BD */

gboolean escuchar_anillo_earthworm(gpointer user_data);
gboolean ew_background_tasks(gpointer user_data);

/* Callbacks de GUI */
void actualizar_altura_canvas(void);
/* --- Sub-pestaña `magnitudes` (tabla de una fila por estacion) --- */
#define CSNHYPODBP_MAG_NKIND 4
extern CsnhypodbpMagCfg g_magcfg;             /* parametros del .d de csnmags */
extern int g_mag_kind;                        /* magnitud activa (0=ML..3=Ms) */
extern CsnhypodbpMagRow g_mag_rows[MAX_ESTA];
extern int g_mag_rows_n;
extern int g_mag_sta_idx[MAX_ESTA];       /* fila de magnitudes -> fila de Ondas */
extern GtkWidget *g_mag_view;
extern GListStore *g_mag_store;
extern GtkSingleSelection *g_mag_sel;
extern GtkSortListModel *g_mag_sort;
extern GtkWidget *g_mag_radio[CSNHYPODBP_MAG_NKIND];
extern GtkWidget *g_mag_status;               /* linea de estado de la sub-pestaña */
extern EwGuiCanvas *g_mag_canvas;             /* traza procesada + franja de ventana */
extern EwGuiCanvas *g_mag_axis_canvas;        /* eje UTC del canvas de magnitudes */
extern double       g_mag_cursor_x;           /* puntero en el canvas de magnitudes */
extern int          g_mag_cursor_valid;       /* 1 = sobre el canvas */
extern char g_mag_station[16];                /* estacion elegida en la tabla de magnitudes */
extern CsnhypodbpMagMarks g_mag_marks;        /* ventanas ajustadas por (estacion, magnitud) */
extern int g_magf_auto, g_magf_manual, g_magf_theo;  /* filtro A/M/Teo de `magnitudes` */
extern CsnhypodbpMagGateOv g_mag_gate_ov[CSNHYPODBP_MAG_NKIND];  /* override de distancia */
extern GtkWidget *g_magf_chk[3];              /* checkboxes A/M/Teo de la barra */
extern GtkWidget *g_mag_ov_min, *g_mag_ov_max, *g_mag_ov_km;  /* puerta activa */
extern GtkWidget *g_mag_ov_dmin, *g_mag_ov_depth;             /* prof. min / max */
extern GtkWidget *g_mag_ov_reset;                             /* reset default */
extern GtkWidget *g_mag_btn_calc, *g_mag_btn_calc_all;        /* calculate / all */
/* `calculate` (magnitud activa) y `calculate all` (las cuatro): recalculan con
   las ventanas ajustadas a mano y sin relocalizar. */
void on_mag_calc_clicked(GtkWidget *w, gpointer d);
void on_mag_calc_all_clicked(GtkWidget *w, gpointer d);
void rebuild_mag_list(void);
void csnhypodbp_mag_status_update(void);
void csnhypodbp_mag_canvas_fit(void);
void csnhypodbp_mag_ov_sync(void);
void on_mag_filter_toggled(GtkCheckButton *b, gpointer data);
/* Cajas de la barra de overrides (`GtkEntry`, sin los +/- del spin): `changed`
   solo valida y pinta en rojo; el valor se aplica al CONFIRMAR (`activate`, o al
   salir del campo) y solo si los cinco campos valen. Ver csnhypodbp_magcfg.h. */
void on_mag_ov_changed(GtkEditable *e, gpointer data);
void on_mag_ov_commit(GtkWidget *w, gpointer data);
void on_mag_ov_focus_leave(GtkEventControllerFocus *fc, gpointer data);
void on_mag_ov_reset_clicked(GtkWidget *w, gpointer data);
/* Devuelve las cuatro magnitudes a la puerta del .d (lo usa `reset default` y
   el cambio de solucion elegida en Origins). */
void csnhypodbp_mag_ov_reset_all(void);
void csnhypodbp_mag_views_invalidate(void);
void on_draw_mag(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data);
void on_mag_row_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data);
void on_mag_drag_begin(GtkGestureDrag *g, double x, double y, gpointer data);
void on_mag_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer data);
void on_mag_drag_end(GtkGestureDrag *g, double ox, double oy, gpointer data);
/* Eje temporal del canvas de magnitudes: arrastre = zoom, doble clic = recentrar
   (vuelve al encuadre automatico del ancla, o sea a la franja entera). */
void on_draw_mag_axis(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data);
void on_mag_axis_drag_begin(GtkGestureDrag *g, double sx, double sy, gpointer data);
void on_mag_axis_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer data);
void on_mag_axis_drag_end(GtkGestureDrag *g, double ox, double oy, gpointer data);
void on_mag_axis_clicked(GtkGestureClick *g, int n_press, double x, double y, gpointer data);
void on_mag_canvas_clicked(GtkGestureClick *g, int n_press, double x, double y,
                           gpointer data);
/* Puntero del canvas de magnitudes (grab / ew-resize / crosshair). */
void on_mag_canvas_motion(GtkEventControllerMotion *c, double x, double y, gpointer data);
void on_mag_canvas_leave(GtkEventControllerMotion *c, gpointer data);
void on_mag_kind_toggled(GtkCheckButton *b, gpointer data);

/* Magnitudes POR ESTACION de una solucion (fila de la tabla) y una magnitud
 * (slot 0..3): primero las vivas del anillo y, si no, las del historial (BD).
 * Devuelve cuantas filas dejo en `out` (0 si no hay). */
int csnhypodbp_magsta_for(CsnhypodbpRow *row, int slot, CsnhypodbpMagIn *out, int max);

void rebuild_station_list(void);
/* Recompone Origins del evento visto conservando la solucion elegida (una
   solucion que llega del anillo no debe mover la vista del usuario). */
void csnhypodbp_detail_refresh(void);
void csnhypodbp_show_procesar(void);          /* lleva la ventana a la pestaña Procesar */
void on_station_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data);
void on_btn_fetch_clicked(GtkWidget *widget, gpointer data);
gboolean on_key_press(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer data);
void on_row_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data);
void on_event_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data);
void rebuild_event_store(void);
void reselect_event_by_qid(int qid);
void reselect_event_by_row(CsnhypodbpRow *row);
void on_list_double_click(GtkGestureClick *g, int n_press, double x, double y, gpointer data);
void on_canvas_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data);
void on_canvas_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data);
void on_canvas_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer data);
void on_canvas_leave(GtkEventControllerMotion *ctrl, gpointer data);
void on_draw_signal(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data);

/* --- Busqueda hipocentral (pestaña Eventos) --- */
void on_btn_search_clicked(GtkWidget *widget, gpointer data);
void on_btn_clear_search_clicked(GtkWidget *widget, gpointer data);

/* --- Barra inferior (pestaña Ondas) --- */
void on_btn_filters_clicked(GtkWidget *widget, gpointer data);void on_btn_timewin_clicked(GtkWidget *widget, gpointer data);
void on_btn_cancel_clicked(GtkWidget *widget, gpointer data);
void on_btn_locators_clicked(GtkWidget *widget, gpointer data);
void on_btn_locate_clicked(GtkWidget *widget, gpointer data);
void on_preview_confirm(GtkWidget *widget, gpointer data);
void on_preview_discard(GtkWidget *widget, gpointer data);
void csnhypodbp_preview_update(void);

/* --- Colores de fase (grupo "Colours" del menú ☰) --- */
void on_colour_select(GtkWidget *widget, gpointer data);
void act_colour(GSimpleAction *action, GVariant *param, gpointer user_data);
int  csnhypodbp_event_group_picks(const EventGroup *g, CsnhypodbpPick *out, int max);
int  csnhypodbp_event_sol_picks(const CsnhypodbpRow *row, CsnhypodbpPick *out, int max);
int  csnhypodbp_confirm_manual(const CsnhypodbpSolution *sol, const CsnhypodbpMagOut *mag,
                               int pref_kind);
void csnhypodbp_load_manual_state(void);

/* --- Filtro configurable (UI) --- */
void ApplySelectedFilter(void);

/* --- Debounce de recarga --- */
gboolean waveform_reload_timer(gpointer data);

#endif
