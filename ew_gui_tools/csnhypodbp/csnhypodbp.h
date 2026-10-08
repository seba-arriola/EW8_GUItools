#ifndef CSNHYPODBP_H
#define CSNHYPODBP_H

#include <gtk/gtk.h>
#include "ewgui/ring.h"
#include "ewgui/wave.h"
#include "ewgui/view.h"
#include "csnhypodbp_row.h"
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
#include <ws_clientII.h>
#include <rw_mag.h> 

#define MAX_STR 256
#define MAX_ESTA 300
#define MAX_MINUTES 15   /* OPTIMIZACION EXTREMA: Bajado de 120 a 15 para evitar OOM Killer de Linux */
#define SAMPRATE_DEF 20  
#define CIRC_IDX(abs_val, size) (int)(((abs_val) % (size) + (size)) % (size))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- ESTRUCTURAS --- */
typedef struct {
    double dTime;       
    char szPhase[8];    
} PICK;

#define MAX_PICKS_PER_STA 50

typedef struct {
    char szStation[16];
    char szChannel[16];
    char szNetID[16];
    char szLocation[16]; 
    double dLat;
    double dLon;
    
    EwGuiTrace *trace;   /* buffer crudo + vista filtrada + rate + oldest */
    
    double dScreenScale;  
    
    int64_t lLastAbsIdx; 
    double dManualPickTime; 
    
    PICK picks[MAX_PICKS_PER_STA];
    int iNumPicks;
} DEV_STATION;

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
extern char MyModName[MAX_STR];
extern char InRingName[MAX_STR];
extern char OutRingName[MAX_STR];
extern char StaFile[MAX_STR];
extern char StateFile[MAX_STR];   /* archivo de estado de csnloc (recuperacion) */
extern char WsIP[MAX_STR];
extern char WsPort[MAX_STR];
extern int  WsTimeout;
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
extern SHM_INFO PRegion;        
extern MSG_LOGO GetLogo[2]; 

extern EVENT_PICK_CACHE PickCache[MAX_CACHED_EVENTS];
extern int pick_cache_idx;

extern DEV_STATION *StaArray; 
extern int     bHasData[MAX_ESTA]; 
extern double  g_StaDist[MAX_ESTA]; 
extern int     NumEstaciones;
extern int     selected_qid;
extern int     selected_mod;
extern double  selected_otime;
extern char    selected_id[32];
extern WS_MENU_QUEUE_REC ws_menu; 

extern EwGuiCanvas *canvas_global;
extern GtkWidget *tree_global;
extern GListStore *g_store_hypo;              /* modelo de la tabla (GTK4) */
extern GtkSingleSelection *g_selection_hypo;  /* selección de la tabla (GTK4) */
extern GtkWidget *g_notebook;                 /* pestañas Eventos/Ondas (GTK4) */
extern GtkWidget *btn_repick;
extern GtkWidget *window_global; 
extern GtkWidget *box_fetch;
extern GtkWidget *entry_dist;
extern GtkWidget *entry_time; 

extern double g_max_dist_km; 
extern gboolean edit_mode;
extern double   g_zoom_factor; 
extern double   g_dWindowStart;
extern double   g_dScreenTime; 
extern int      g_margin_left; 
extern int      g_spacing;

extern StaNode g_SortedNodes[MAX_ESTA];
extern int     g_NumSortedNodes;
extern gboolean g_history_needs_saving; 

/* --- FILTRO CONFIGURABLE (portado de EW7 new_hypo_display) --- */
extern int      g_filter_type;     /* 0=Raw, 1=High-Pass, 2=Low-Pass, 3=Band-Pass */
extern double   g_align_lead;      /* segundos de ventana antes de la onda P */
extern GtkWidget *combo_filter;
extern GtkWidget *entry_freq1;
extern GtkWidget *entry_freq2;
extern GtkWidget *combo_order;
extern GtkWidget *btn_apply_filter;

/* --- DEBOUNCE DE RECARGA DE WAVEFORMS --- */
extern gboolean pending_waveform_reload;
extern double   selected_lat;
extern double   selected_lon;

/* --- PROTOTIPOS DE FUNCIONES --- */
int ReadConfig(char *configfile);
void Status(unsigned char type, short ierr, char *note);
void ConnectToEarthworm(void);
void LoadStationsFromFile(void);
int ParseY2K_Hypo(char *msg, double *otime, double *lat, double *lon, double *depth, double *res, int *nps, int *azm, int *qid, int *qver, double *pref_mag, char *mag_type, int mod);

void cargar_sismos_iniciales(GtkWidget *tree);
int procesar_mensaje_sismo(GtkWidget *tree, const char *payload, int mod);
int procesar_mensaje_mag(GtkWidget *tree, const char *payload);
void FetchWaveformsForEvent(double otime, double eq_lat, double eq_lon, int qid, int mod);
const char *ModLabel(int mod);

gboolean escuchar_anillo_earthworm(gpointer user_data);
gboolean ew_background_tasks(gpointer user_data);

/* Callbacks de GUI */
void actualizar_altura_canvas(void);
void on_btn_fetch_clicked(GtkWidget *widget, gpointer data);
gboolean on_key_press(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer data);
void on_row_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data);
void on_list_double_click(GtkGestureClick *g, int n_press, double x, double y, gpointer data);
void on_btn_repick_clicked(GtkWidget *widget, gpointer data);
void on_canvas_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data);
void on_draw_signal(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data);

/* --- Filtro configurable (UI) --- */
void ApplySelectedFilter(void);
void on_filter_changed(GObject *obj, GParamSpec *pspec, gpointer data);
void on_btn_apply_filter_clicked(GtkWidget *widget, gpointer data);

/* --- Debounce de recarga --- */
gboolean waveform_reload_timer(gpointer data);

#endif
