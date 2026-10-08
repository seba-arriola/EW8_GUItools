#ifndef CSNTVP_H
#define CSNTVP_H

#include <gtk/gtk.h>
#include "ewgui/ring.h"
#include "ewgui/dsp.h"
#include "ewgui/wave.h"
#include "ewgui/actions.h"
#include "ewgui/view.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#undef TRUE
#undef FALSE

#include <earthworm.h>
#include <transport.h>
#include <trace_buf.h>
#include <kom.h>

#define MAX_STR 256
#define REFRESH_MS 150           /* A1: 150ms ~ 6.7fps (before 50ms = 20fps) */
#define ENV_PROCESS_MS 500       /* A2: processing/envelope cadence (2Hz) */
#define PANEL_WIDTH 90
#define BOTTOM_AXIS_H 30
#define MAX_TRACE_BYTES 4096
#define MAX_STATIONS 512
#define MAX_MINUTES 120
#define MAX_SAMP_RATE 250        /* safety cap for per-station buffer sizing (samples/sec) */
#define MAX_PICKS_PER_STA 50

#define MIN_ZOOM 0.05
#define MAX_ZOOM 100.0
#define MAX_REALTIME_SKEW 86400.0
#define DATA_STALE_SECS 10.0

#define CIRC_IDX(abs_val, size) (int)(((abs_val) % (size) + (size)) % (size))

/* --- CONFIGURACION DEL MODULO (definidas en csntvp_main.c) --- */
extern char MyModName[MAX_STR];
extern char InRingName[MAX_STR];
extern char OutRingName[MAX_STR];
extern char StaFile[MAX_STR];
extern int HeartBeatInt;
extern int LogFile;
extern pid_t MyPid;

extern unsigned char MyInstId;
extern unsigned char MyModId;
extern unsigned char TypeHeartBeat;
extern unsigned char TypeError;
extern unsigned char TypeTrace;
extern unsigned char TypePickSCNL;

extern SHM_INFO  WaveRegion;
extern SHM_INFO  PickRegion;
extern MSG_LOGO  WaveLogo[1];
extern MSG_LOGO  PickLogo[1];

/* --- COLORES --- */
extern double g_color_bg[3];
extern double g_color_wave[3];
extern double g_color_font[3];
extern double g_color_sep[3];

/* --- FILTROS --- */
extern int g_filter_type;
extern double g_f1;
extern double g_f2;
extern int g_order;

extern double    g_latest_time;
extern double    g_smooth_time;
extern int64_t   g_last_frame_realtime;

extern gboolean g_is_hold;
extern double   g_t_hold_time;
extern double   g_zoom_factor;

extern double   g_last_data_realtime;
extern gboolean g_data_stale;
extern double   g_pick_replace_secs;
extern int      g_pick_seq;
extern int      g_warned_datatype;

/* estado de la caché de envolvente */
extern int64_t  g_last_env_process_ms;
extern double   g_last_env_t_right;
extern int      g_last_env_width;
extern gboolean g_last_env_ok;
extern gboolean g_bForceEnv;
extern gboolean g_envelope_updated;

typedef struct {
    double dTime;
    char szPhase[8];
    long lPickIndex;
    int iUseMe;
} PICK;

typedef struct {
    char szStation[7];
    char szChannel[5];
    char szNetID[4];
    char szLocation[4];

    double dSampRate;
    int32_t *plRawCircBuff;
    long lRawCircSize;
    double dScreenScale;

    int64_t lLastAbsIdx;
    double dLastPacketSysTime;

    PICK picks[MAX_PICKS_PER_STA];
    int iNumPicks;
    long lPickRingNext;

    /* Caché de procesado + envolvente (ewgui_wave). */
    EwTraceCache cache;
} DEV_STATION;

extern DEV_STATION *StaArray;
extern int iNumStas;

extern GtkWidget *g_scrolled_window;
extern EwGuiCanvas *g_drawing_waves;
extern EwGuiCanvas *drawing_axis;
extern GtkWidget *btn_hold;

extern double dTrackHeight;
extern int iVisStas;
extern int iTimeWindowMinutes;

extern GMainLoop *g_loop;

/* --- PROTOTIPOS --- */
int  ReadConfig(char *configfile);
void Status(unsigned char type, short ierr, char *note);
void ConnectToEarthworm(void);
void FreeAllStations(void);
void LoadStationsFromFile(void);
gboolean fetch_realtime_data(gpointer user_data);
gboolean ew_background_tasks(gpointer user_data);
void RecalcTrackHeight(void);
void LogColorConfig(void);

/* Callbacks de UI */
void on_btn_hold_toggled(GtkToggleButton *togglebutton, gpointer user_data);
gboolean on_key_press(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer data);
void on_colour_select(GtkWidget *widget, gpointer data);
void on_stas_per_screen_activate(GtkWidget *widget, gpointer data);
void on_time_window_activate(GtkWidget *widget, gpointer data);
void on_filter_menu_activate(GtkWidget *widget, gpointer data);
void on_clean_view_activate(GtkWidget *widget, gpointer data);
void on_reload_default_activate(GtkWidget *widget, gpointer data);
void on_canvas_button_press(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data);

/* Dibujo */
void on_draw_waves(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *user_data);
void on_draw_axis(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *user_data);

/* Adaptadores GAction -> callbacks */
void act_stas_per_screen(GSimpleAction *a, GVariant *p, gpointer ud);
void act_time_window(GSimpleAction *a, GVariant *p, gpointer ud);
void act_clean_view(GSimpleAction *a, GVariant *p, gpointer ud);
void act_reload_default(GSimpleAction *a, GVariant *p, gpointer ud);
void act_filter_options(GSimpleAction *a, GVariant *p, gpointer ud);
void act_colour(GSimpleAction *a, GVariant *p, gpointer ud);

#endif /* CSNTVP_H */
