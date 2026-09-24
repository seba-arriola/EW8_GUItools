/***********************************************************************
 *                             csntvp                                  *
 *                                                                     *
 *  Real-time seismogram display and manual picking tool. It reads     *
 *  the InRing (SLINK_RING) for trace data (TYPE_TRACEBUF2), renders   *
 *  the last minutes of every station in a scrollable canvas and lets  *
 *  the operator pick P arrivals that are reported to the OutRing      *
 *  (PICK_RING) as TYPE_PICK_SCNL. Includes HOLD/zoom, colour, filter  *
 *  and time-window controls, plus an envelope cache with adaptive     *
 *  refresh cadence for smooth, efficient rendering.                   *
 *                                                                     *
 *  Usage: csntvp <configfile.d>                                       *
 ***********************************************************************/

#include <gtk/gtk.h>
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

/* --- CONFIGURACION DEL MODULO --- */
char MyModName[MAX_STR];
char InRingName[MAX_STR];
char OutRingName[MAX_STR];
char StaFile[MAX_STR];
int HeartBeatInt;
int LogFile;
pid_t MyPid;
time_t timeLastBeat = 0;

unsigned char MyInstId;
unsigned char MyModId;
unsigned char TypeHeartBeat;
unsigned char TypeError;
unsigned char TypeTrace;
unsigned char TypePickSCNL;

SHM_INFO  WaveRegion;
SHM_INFO  PickRegion;
MSG_LOGO  WaveLogo[1];
MSG_LOGO  PickLogo[1];

/* --- GLOBALS FOR CUSTOM COLORS --- */
double g_color_bg[3]   = {1.0, 1.0, 1.0};    /* background color */
double g_color_wave[3] = {0.0, 0.0, 0.0};    /* waveform color */
double g_color_font[3] = {1.0, 0.0, 0.0};    /* font color */
double g_color_sep[3]  = {0.85, 0.85, 0.85}; /* separator color */

/* --- GLOBALS FOR DYNAMIC FILTERS --- */
int g_filter_type = 0;  /* 0=Raw, 1=HP, 2=LP, 3=BP */
double g_f1 = 0.7;      /* first corner frequency (Hz) */
double g_f2 = 2.0;      /* second corner frequency (Hz, band-pass) */
int g_order = 4;        /* filter order (2 or 4) */

double    g_latest_time = 0.0;
double    g_smooth_time = 0.0;
int64_t   g_last_frame_realtime = 0;

gboolean g_is_hold = FALSE;
double   g_t_hold_time = 0.0;
double   g_zoom_factor = 1.0;

double   g_last_data_realtime = 0.0;   /* time of the last data packet */
gboolean g_data_stale = FALSE;         /* true when the data feed is stale */
double   g_pick_replace_secs = 15.0;   /* seconds to replace an existing pick */
static int g_pick_seq = 0;             /* pick sequence number (0-999999) */
static int g_warned_datatype = 0;      /* warns once about an unknown datatype */

/* A1/A2/A3: state of the envelope cache. The envelope is recalculated at
   most every ENV_PROCESS_MS or when something forces it (filter, window, hold,
   width); the draw only reads it and scales it (zoom does not invalidate). */
static int64_t  g_last_env_process_ms = 0;
static double   g_last_env_t_right = 0.0;
static int      g_last_env_width = 0;
static gboolean g_last_env_ok = FALSE;
static gboolean g_bForceEnv = FALSE;
static gboolean g_envelope_updated = FALSE;

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

    /* Cache of the processed trace (extract+interp+DC+filter) to avoid
       recomputing everything every frame. Invalidated if the window, the
       filter change or data arrives within the cached window. */
    long *plProcBuf;
    long lProcCap;
    long lProcAbsStart;
    long lProcAbsEnd;
    int iProcValid;
    int iProcFoundFirst;
    int iProcFilterType;
    double dProcF1;
    double dProcF2;
    int iProcOrder;

    /* A3: envelope decimated per pixel column, in raw units (without
       auto_scale/zoom). Computed in the processing pass (A2); the draw only
       scales it, so the zoom does not invalidate it. */
    double  *dEnvMin;
    double  *dEnvMax;
    int     *iEnvHas;
    int      iEnvCap;
    int      iEnvWidth;
    gboolean bEnvValid;
    double   dEnvMaxAbs;
    int      iEnvFoundFirst;
} DEV_STATION;

DEV_STATION *StaArray = NULL;
int iNumStas = 0;

GtkWidget *g_scrolled_window;
GtkWidget *g_drawing_waves;
GtkWidget *drawing_axis;
GtkWidget *btn_hold;

double dTrackHeight = 60.0;
int iVisStas = 12;
int iTimeWindowMinutes = 6;

/* --------------------------------------------------------------------
 * FORWARD DECLARATIONS
 * -------------------------------------------------------------------- */
int ReadConfig(char *configfile);
void Status(unsigned char type, short ierr, char *note);
void ConnectToEarthworm(void);
static void compute_station_envelope(int i, int width);
static void update_wave_envelopes(void);

/* --------------------------------------------------------------------
 * COLOR HELPERS
 * -------------------------------------------------------------------- */
static int ParseHexColor(const char *s, double rgb[3]) {
    unsigned int r, g, b;
    if (!s || s[0] == '\0') return -1;
    while (*s == '#') s++;
    if (sscanf(s, "%2x%2x%2x", &r, &g, &b) != 3) return -1;
    rgb[0] = (double)r / 255.0;
    rgb[1] = (double)g / 255.0;
    rgb[2] = (double)b / 255.0;
    return 0;
}

static void ColorToHex(const double rgb[3], char out[8]) {
    snprintf(out, 8, "%02X%02X%02X",
             (int)(rgb[0] * 255.0 + 0.5), (int)(rgb[1] * 255.0 + 0.5), (int)(rgb[2] * 255.0 + 0.5));
}

static void LogColorConfig(void) {
    char c_wave[8], c_bg[8], c_font[8], c_sep[8];
    ColorToHex(g_color_wave, c_wave);
    ColorToHex(g_color_bg, c_bg);
    ColorToHex(g_color_font, c_font);
    ColorToHex(g_color_sep, c_sep);
    logit("et", "csntvp: Color config (copy to .d):\n");
    logit("et", "  waveformsColor   %s\n", c_wave);
    logit("et", "  backgroundColor  %s\n", c_bg);
    logit("et", "  fontColor        %s\n", c_font);
    logit("et", "  separatorColor   %s\n", c_sep);
}

/* --------------------------------------------------------------------
 * DSP: BUTTERWORTH IIR FILTER (ported from libsrc/dataprocessing.c)
 *   type: 1 = High-Pass, 2 = Low-Pass
 *   order: 2 or 4 (forced to even values; 4 uses 2 cascaded biquads)
 *   INT_MAX samples are preserved as gaps and reset the filter state.
 * -------------------------------------------------------------------- */
static void aplicar_filtro_iir(long *data, long size, double fs, int type, double fc, int order) {
    if (fs <= 0.0 || size == 0 || fc <= 0.0) return;

    double w0 = 2.0 * M_PI * fc / fs;
    double cosW = cos(w0);
    double sinW = sin(w0);

    int n_biquads = (order >= 4) ? 2 : 1;

    double Q[2] = {0.70710678, 0.0};
    if (n_biquads == 2) {
        Q[0] = 0.54119610;
        Q[1] = 1.30656296;
    }

    for (int b = 0; b < n_biquads; b++) {
        double alpha = sinW / (2.0 * Q[b]);
        double a0 = 1.0 + alpha;
        double b0_f, b1_f, b2_f, a1_f, a2_f;

        if (type == 1) { /* High-Pass */
            b0_f = ((1.0 + cosW) / 2.0) / a0;
            b1_f = -(1.0 + cosW) / a0;
            b2_f = ((1.0 + cosW) / 2.0) / a0;
        } else { /* Low-Pass */
            b0_f = ((1.0 - cosW) / 2.0) / a0;
            b1_f = (1.0 - cosW) / a0;
            b2_f = ((1.0 - cosW) / 2.0) / a0;
        }

        a1_f = (-2.0 * cosW) / a0;
        a2_f = (1.0 - alpha) / a0;

        double x1 = (double)data[0], x2 = (double)data[0];
        double y1 = 0.0, y2 = 0.0;
        if (type == 1) { y1 = 0.0; y2 = 0.0; }
        else { y1 = (double)data[0]; y2 = (double)data[0]; }

        for (long i = 0; i < size; i++) {
            if (data[i] == INT_MAX) {
                x1 = x2 = 0.0; y1 = y2 = 0.0;
                continue;
            }
            double x0 = (double)data[i];
            double y0 = b0_f*x0 + b1_f*x1 + b2_f*x2 - a1_f*y1 - a2_f*y2;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            data[i] = (long)y0;
        }
    }
}

/* --------------------------------------------------------------------
 * FUNCIONES EARTHWORM
 * -------------------------------------------------------------------- */
int ReadConfig(char *configfile) {
    int ncommand = 6, nmiss = 0, i;
    char init[10] = {0};
    char *com, *str;

    if (!k_open(configfile)) {
        fprintf(stderr, "csntvp: Error abriendo archivo config <%s>\n", configfile);
        return -1;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if (k_its("MyModuleId")) {
            str = k_str();
            if (str) strcpy(MyModName, str);
            init[0] = 1;
        } else if (k_its("InRing")) {
            str = k_str();
            if (str) strcpy(InRingName, str);
            init[1] = 1;
        } else if (k_its("OutRing")) {
            str = k_str();
            if (str) strcpy(OutRingName, str);
            init[2] = 1;
        } else if (k_its("HeartBeatInt")) {
            HeartBeatInt = k_int();
            init[3] = 1;
        } else if (k_its("LogFile")) {
            LogFile = k_int();
            init[4] = 1;
        } else if (k_its("StaFile")) {
            str = k_str();
            if (str) strcpy(StaFile, str);
            init[5] = 1;
        } else if (k_its("waveformsColor")) {
            str = k_str();
            if (str && ParseHexColor(str, g_color_wave)) logit("et", "csntvp: waveformsColor <%s> invalid, using default\n", str);
        } else if (k_its("backgroundColor")) {
            str = k_str();
            if (str && ParseHexColor(str, g_color_bg)) logit("et", "csntvp: backgroundColor <%s> invalid, using default\n", str);
        } else if (k_its("fontColor")) {
            str = k_str();
            if (str && ParseHexColor(str, g_color_font)) logit("et", "csntvp: fontColor <%s> invalid, using default\n", str);
        } else if (k_its("separatorColor")) {
            str = k_str();
            if (str && ParseHexColor(str, g_color_sep)) logit("et", "csntvp: separatorColor <%s> invalid, using default\n", str);
        } else if (k_its("StationsPerScreen")) {
            str = k_str();
            if (str) { int v = atoi(str); if (v >= 1 && v <= MAX_STATIONS) iVisStas = v; else logit("et", "csntvp: StationsPerScreen <%s> invalid, using default %d\n", str, iVisStas); }
        } else if (k_its("TimeWindow")) {
            str = k_str();
            if (str) { int v = atoi(str); if (v >= 1 && v <= MAX_MINUTES) iTimeWindowMinutes = v; else logit("et", "csntvp: TimeWindow <%s> invalid, using default %d\n", str, iTimeWindowMinutes); }
        } else if (k_its("PickReplaceWindow")) {
            str = k_str();
            if (str) { double v = atof(str); if (v >= 0.0) g_pick_replace_secs = v; else logit("et", "csntvp: PickReplaceWindow <%s> invalid, using default %.1f\n", str, g_pick_replace_secs); }
        } else {
            continue;
        }
        if (k_err()) {
            fprintf(stderr, "csntvp: Error parseando <%s> en <%s>\n", com, configfile);
            return -1;
        }
    }
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close();
    if (nmiss > 0) {
        fprintf(stderr, "csntvp: ERROR, faltan parametros en <%s>\n", configfile);
        return -1;
    }
    return 0;
}

void Status(unsigned char type, short ierr, char *note) {
    MSG_LOGO logo;
    char msg[256];
    long size;
    time_t t;

    logo.instid = MyInstId;
    logo.mod    = MyModId;
    logo.type   = type;

    time(&t);
    if (type == TypeHeartBeat) {
        sprintf(msg, "%ld %d\n", (long) t, (int) MyPid);
    } else if (type == TypeError) {
        sprintf(msg, "%ld %hd %s\n", (long) t, ierr, note);
        logit("et", "csntvp: Error: %s\n", note);
    }

    size = strlen(msg);
    tport_putmsg(&WaveRegion, &logo, size, msg);
}

void ConnectToEarthworm() {
    long WaveRingKey = GetKey(InRingName);
    long PickRingKey = GetKey(OutRingName);

    if (WaveRingKey == -1) { logit("e", "Error: Anillo entrada %s invalido.\n", InRingName); exit(-1); }
    if (PickRingKey == -1) { logit("e", "Error: Anillo salida %s invalido.\n", OutRingName); exit(-1); }

    if (GetLocalInst(&MyInstId) != 0) { logit("e", "Error en GetLocalInst.\n"); exit(-1); }
    if (GetModId(MyModName, &MyModId) != 0) { logit("e", "Error en GetModId (%s).\n", MyModName); exit(-1); }

    if (GetType("TYPE_TRACEBUF2", &TypeTrace) != 0) { logit("e", "Falta TYPE_TRACEBUF2.\n"); exit(-1); }
    if (GetType("TYPE_PICK_SCNL", &TypePickSCNL) != 0) { logit("e", "Falta TYPE_PICK_SCNL.\n"); exit(-1); }
    if (GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0) { logit("e", "Falta TYPE_HEARTBEAT.\n"); exit(-1); }
    if (GetType("TYPE_ERROR", &TypeError) != 0) { logit("e", "Falta TYPE_ERROR.\n"); exit(-1); }

    unsigned char InstWild, ModWild;
    GetInst("INST_WILDCARD", &InstWild);
    GetModId("MOD_WILDCARD", &ModWild);

    WaveLogo[0].instid = InstWild;
    WaveLogo[0].mod = ModWild;
    WaveLogo[0].type = TypeTrace;
    tport_attach( &WaveRegion, WaveRingKey );
    logit("t", "=== CSNtvp: CONECTADO A INRING: %s ===\n", InRingName);

    PickLogo[0].instid = InstWild;
    PickLogo[0].mod = ModWild;
    PickLogo[0].type = TypePickSCNL;
    tport_attach( &PickRegion, PickRingKey );
    logit("t", "=== CSNtvp: CONECTADO A OUTRING: %s ===\n", OutRingName);
}

gboolean ew_background_tasks(gpointer user_data) {
    time_t timeNow;
    time(&timeNow);

    if (timeNow - timeLastBeat >= HeartBeatInt) {
        timeLastBeat = timeNow;
        Status(TypeHeartBeat, 0, "");
    }

    int flag = tport_getflag(&WaveRegion);
    if (flag == TERMINATE || flag == MyPid) {
        logit("t", "csntvp: Señal de terminacion recibida. Cerrando...\n");
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* --------------------------------------------------------------------
 * FUNCIONES GRAFICAS Y DE DATOS
 * -------------------------------------------------------------------- */
void FreeAllStations() {
    if (StaArray) {
        for (int i = 0; i < iNumStas; i++) {
            if (StaArray[i].plRawCircBuff) { free(StaArray[i].plRawCircBuff); StaArray[i].plRawCircBuff = NULL; }
            if (StaArray[i].plProcBuf) { free(StaArray[i].plProcBuf); StaArray[i].plProcBuf = NULL; }
            if (StaArray[i].dEnvMin) { free(StaArray[i].dEnvMin); StaArray[i].dEnvMin = NULL; }
            if (StaArray[i].dEnvMax) { free(StaArray[i].dEnvMax); StaArray[i].dEnvMax = NULL; }
            if (StaArray[i].iEnvHas) { free(StaArray[i].iEnvHas); StaArray[i].iEnvHas = NULL; }
        }
        free(StaArray);
        StaArray = NULL;
    }
    iNumStas = 0;
}

void on_btn_hold_toggled(GtkToggleButton *togglebutton, gpointer user_data) {
    g_is_hold = gtk_toggle_button_get_active(togglebutton);
    g_bForceEnv = TRUE; /* The envelope is recalculated for the (in)active window */
    if (g_is_hold) {
        if (g_smooth_time > 0.0) g_t_hold_time = g_smooth_time;
        else { time_t t; time(&t); g_t_hold_time = (double)t; }
    } else {
        g_zoom_factor = 1.0;
    }
    if (g_drawing_waves) gtk_widget_queue_draw(g_drawing_waves);
    if (drawing_axis) gtk_widget_queue_draw(drawing_axis);
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    if (!g_is_hold) return FALSE;
    if (event->keyval == GDK_KEY_Up) {
        g_zoom_factor *= 1.5;
        if (g_zoom_factor > MAX_ZOOM) g_zoom_factor = MAX_ZOOM;
        if (g_drawing_waves) gtk_widget_queue_draw(g_drawing_waves);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Down) {
        g_zoom_factor /= 1.5;
        if (g_zoom_factor < MIN_ZOOM) g_zoom_factor = MIN_ZOOM;
        if (g_drawing_waves) gtk_widget_queue_draw(g_drawing_waves);
        return TRUE;
    }
    return FALSE;
}

void on_colour_select(GtkWidget *widget, gpointer data) {
    int type = GPOINTER_TO_INT(data);
    GtkWidget *dialog = gtk_color_chooser_dialog_new("Select Colour", GTK_WINDOW(gtk_widget_get_toplevel(widget)));

    GdkRGBA current_color;
    current_color.alpha = 1.0;
    if (type == 1) { current_color.red = g_color_wave[0]; current_color.green = g_color_wave[1]; current_color.blue = g_color_wave[2]; }
    else if (type == 2) { current_color.red = g_color_bg[0]; current_color.green = g_color_bg[1]; current_color.blue = g_color_bg[2]; }
    else if (type == 3) { current_color.red = g_color_font[0]; current_color.green = g_color_font[1]; current_color.blue = g_color_font[2]; }
    else if (type == 4) { current_color.red = g_color_sep[0]; current_color.green = g_color_sep[1]; current_color.blue = g_color_sep[2]; }

    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &current_color);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        GdkRGBA new_color;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &new_color);

        if (type == 1) { g_color_wave[0] = new_color.red; g_color_wave[1] = new_color.green; g_color_wave[2] = new_color.blue; }
        else if (type == 2) { g_color_bg[0] = new_color.red; g_color_bg[1] = new_color.green; g_color_bg[2] = new_color.blue; }
        else if (type == 3) { g_color_font[0] = new_color.red; g_color_font[1] = new_color.green; g_color_font[2] = new_color.blue; }
        else if (type == 4) { g_color_sep[0] = new_color.red; g_color_sep[1] = new_color.green; g_color_sep[2] = new_color.blue; }

        LogColorConfig();
        if (g_drawing_waves) gtk_widget_queue_draw(g_drawing_waves);
    }
    gtk_widget_destroy(dialog);
}

void RecalcTrackHeight() {
    GtkAllocation alloc;
    if (!g_scrolled_window) return;
    gtk_widget_get_allocation(g_scrolled_window, &alloc);
    if (alloc.height > 0 && iVisStas > 0) {
        dTrackHeight = (double)alloc.height / iVisStas;
        if (dTrackHeight < 2.0) dTrackHeight = 2.0;
        gtk_widget_set_size_request(g_drawing_waves, -1, iNumStas * dTrackHeight);
    }
    if (g_drawing_waves) gtk_widget_queue_draw(g_drawing_waves);
}

void on_stas_per_screen_activate(GtkWidget *widget, gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Stations per screen", GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_REJECT, NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(content_area), hbox, TRUE, TRUE, 15);
    GtkWidget *label = gtk_label_new("Visible stations:");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

    GtkWidget *spin = gtk_spin_button_new_with_range(1, MAX_STATIONS, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), iVisStas);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        iVisStas = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        RecalcTrackHeight();
    }
    gtk_widget_destroy(dialog);
}

void on_time_window_activate(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Time window", GTK_WINDOW(data),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_REJECT, NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(content_area), hbox, TRUE, TRUE, 15);
    GtkWidget *label = gtk_label_new("Window size (minutes):");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

    GtkWidget *spin = gtk_spin_button_new_with_range(1, MAX_MINUTES, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), iTimeWindowMinutes);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        iTimeWindowMinutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        g_bForceEnv = TRUE; /* New window: recompute envelope */
        if (g_drawing_waves) gtk_widget_queue_draw(g_drawing_waves);
        if (drawing_axis) gtk_widget_queue_draw(drawing_axis);
    }
    gtk_widget_destroy(dialog);
}

/* --------------------------------------------------------------------
 * INTERACTIVE DYNAMIC FILTER HANDLER
 * -------------------------------------------------------------------- */
static void on_filter_combo_changed(GtkComboBox *widget, gpointer data) {
    GtkWidget **entries = (GtkWidget **)data;
    int type = gtk_combo_box_get_active(widget);
    gtk_widget_set_sensitive(entries[0], (type != 0)); /* F1 (HP, LP, BP) */
    gtk_widget_set_sensitive(entries[1], (type == 3)); /* F2 (Only Band-Pass) */
    gtk_widget_set_sensitive(entries[2], (type != 0)); /* Order (HP, LP, BP) */
}

void on_filter_menu_activate(GtkWidget *widget, gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Filter Settings", GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Apply", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_REJECT, NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);

    GtkWidget *cb_type = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_type), "Raw (No Filter)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_type), "High-Pass");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_type), "Low-Pass");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_type), "Band-Pass");
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_type), g_filter_type);

    GtkWidget *e_f1 = gtk_entry_new();
    char buf[16]; snprintf(buf, sizeof(buf), "%.2f", g_f1);
    gtk_entry_set_text(GTK_ENTRY(e_f1), buf);

    GtkWidget *e_f2 = gtk_entry_new();
    snprintf(buf, sizeof(buf), "%.2f", g_f2);
    gtk_entry_set_text(GTK_ENTRY(e_f2), buf);

    GtkWidget *cb_order = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_order), "2");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_order), "4");
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_order), (g_order==2)?0:1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Type:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cb_type, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("F1 (Hz):"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e_f1, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("F2 (Hz):"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), e_f2, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Order:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cb_order, 1, 3, 1, 1);

    GtkWidget *entries[3] = {e_f1, e_f2, cb_order};
    g_signal_connect(cb_type, "changed", G_CALLBACK(on_filter_combo_changed), entries);
    on_filter_combo_changed(GTK_COMBO_BOX(cb_type), entries);

    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        g_filter_type = gtk_combo_box_get_active(GTK_COMBO_BOX(cb_type));
        g_f1 = atof(gtk_entry_get_text(GTK_ENTRY(e_f1)));
        g_f2 = atof(gtk_entry_get_text(GTK_ENTRY(e_f2)));
        g_order = (gtk_combo_box_get_active(GTK_COMBO_BOX(cb_order)) == 0) ? 2 : 4;

        g_bForceEnv = TRUE; /* New filter: recompute envelope */
        if (g_drawing_waves) gtk_widget_queue_draw(g_drawing_waves);
    }
    gtk_widget_destroy(dialog);
}

void LoadStationsFromFile() {
    FILE *fp = fopen(StaFile, "r");
    if (!fp) { logit("e", "csntvp: Error abriendo %s\n", StaFile); exit(-1); }
    StaArray = (DEV_STATION *) calloc(MAX_STATIONS, sizeof(DEV_STATION));
    iNumStas = 0; char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char sta[20] = "", chan[20] = "", net[20] = "", loc[20] = "--"; double scale = 0.0;
        int parsed = sscanf(line, "%19s %19s %19s %19s %lf", sta, chan, net, loc, &scale);
        if (parsed == 4) { scale = atof(loc); strcpy(loc, "--"); }
        else if (parsed < 4) continue;
        if (iNumStas < MAX_STATIONS) {
            DEV_STATION *s = &StaArray[iNumStas];
            snprintf(s->szStation, sizeof(s->szStation), "%s", sta);
            snprintf(s->szChannel, sizeof(s->szChannel), "%s", chan);
            snprintf(s->szNetID, sizeof(s->szNetID), "%s", net);
            snprintf(s->szLocation, sizeof(s->szLocation), "%s", loc);
            s->dScreenScale = scale;
            /* Raw buffer is allocated lazily on the first data packet, sized by
               the actual sample rate (memory refinement over a fixed max). */
            s->dSampRate = 0.0;
            s->plRawCircBuff = NULL;
            s->lRawCircSize = 0;
            s->iNumPicks = 0;
            s->lPickRingNext = 0;
            s->lLastAbsIdx = 0;
            s->dLastPacketSysTime = 0.0;
            s->plProcBuf = NULL;
            s->lProcCap = 0;
            s->lProcAbsStart = 0;
            s->lProcAbsEnd = 0;
            s->iProcValid = 0;
            s->iProcFoundFirst = 0;
            s->iProcFilterType = 0;
            s->dProcF1 = 0.0;
            s->dProcF2 = 0.0;
            s->iProcOrder = 0;
            s->dEnvMin = NULL;
            s->dEnvMax = NULL;
            s->iEnvHas = NULL;
            s->iEnvCap = 0;
            s->iEnvWidth = 0;
            s->bEnvValid = FALSE;
            s->dEnvMaxAbs = 1.0;
            s->iEnvFoundFirst = 0;
            iNumStas++;
        }
    }
    fclose(fp); logit("t", "csntvp: %d estaciones cargadas.\n", iNumStas);
}

void on_clean_view_activate(GtkWidget *widget, gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Clean View (Remove Inactive)", GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_REJECT, NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(content_area), hbox, TRUE, TRUE, 15);
    GtkWidget *spin = gtk_spin_button_new_with_range(1, 10080, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 60);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Max time without data (min):"), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        int min_val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        double max_age_secs = min_val * 60.0;
        double sys_time = (double)g_get_real_time() / 1000000.0;
        int active_count = 0;
        for (int i = 0; i < iNumStas; i++) {
            if (StaArray[i].dLastPacketSysTime > 0.0 && (sys_time - StaArray[i].dLastPacketSysTime) <= max_age_secs) {
                if (i != active_count) {
                    StaArray[active_count] = StaArray[i];
                    StaArray[i].plRawCircBuff = NULL;
                    StaArray[i].plProcBuf = NULL;
                    StaArray[i].dEnvMin = NULL;
                    StaArray[i].dEnvMax = NULL;
                    StaArray[i].iEnvHas = NULL;
                }
                active_count++;
            } else {
                if (StaArray[i].plRawCircBuff) { free(StaArray[i].plRawCircBuff); StaArray[i].plRawCircBuff = NULL; }
                if (StaArray[i].plProcBuf) { free(StaArray[i].plProcBuf); StaArray[i].plProcBuf = NULL; }
                if (StaArray[i].dEnvMin) { free(StaArray[i].dEnvMin); StaArray[i].dEnvMin = NULL; }
                if (StaArray[i].dEnvMax) { free(StaArray[i].dEnvMax); StaArray[i].dEnvMax = NULL; }
                if (StaArray[i].iEnvHas) { free(StaArray[i].iEnvHas); StaArray[i].iEnvHas = NULL; }
                StaArray[i].iEnvCap = 0;
                StaArray[i].bEnvValid = FALSE;
            }
        }
        iNumStas = active_count;
        RecalcTrackHeight();
    }
    gtk_widget_destroy(dialog);
}

void on_reload_default_activate(GtkWidget *widget, gpointer data) {
    FreeAllStations(); LoadStationsFromFile();
    RecalcTrackHeight();
}

/* Decodes one TRACE_BUF sample of the given datatype:
   i2/s2 (int16), i4/s4 (int32) or f4/t4 (float32). */
static int ReadSampleAt(const char *szType, const char *pData, int idx, int32_t *out) {
    if (strcmp(szType, "i2") == 0 || strcmp(szType, "s2") == 0) {
        int16_t v; memcpy(&v, pData + idx * 2, 2); *out = v; return 0;
    }
    if (strcmp(szType, "i4") == 0 || strcmp(szType, "s4") == 0) {
        int32_t v; memcpy(&v, pData + idx * 4, 4); *out = v; return 0;
    }
    if (strcmp(szType, "f4") == 0 || strcmp(szType, "t4") == 0) {
        float v; memcpy(&v, pData + idx * 4, 4);
        *out = (int32_t)(v + (v >= 0.0f ? 0.5f : -0.5f)); return 0;
    }
    *out = 0;
    return -1;
}

static gboolean on_canvas_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (!g_is_hold || event->button != 1 || event->x <= PANEL_WIDTH) return FALSE;
    guint width = gtk_widget_get_allocated_width(widget);
    double draw_area_width = width - PANEL_WIDTH;
    int sta_idx = (int)(event->y / dTrackHeight);
    if (sta_idx < 0 || sta_idx >= iNumStas) return FALSE;
    double window_secs = iTimeWindowMinutes * 60.0;
    double t_right = g_t_hold_time, t_left = t_right - window_secs;
    double fraction = (event->x - PANEL_WIDTH) / draw_area_width;
    double clicked_time = t_left + fraction * window_secs;

    DEV_STATION *dev = &StaArray[sta_idx];

    /* Window-based replacement (PickReplaceWindow): if a pick already exists
       for the same station within +/-g_pick_replace_secs, it is marked as
       replaced locally. NOTE: the TYPE_PICK_SCNL protocol has no retract
       message, so the old pick is only deactivated in the local overlay; the
       associator (ew2glass/GLASS3) will see the corrected pick as a new one. */
    if (g_pick_replace_secs > 0.0) {
        for (int pi = 0; pi < MAX_PICKS_PER_STA; pi++) {
            if (dev->picks[pi].iUseMe > 0 &&
                fabs(dev->picks[pi].dTime - clicked_time) <= g_pick_replace_secs) {
                logit("et", "csntvp: replacing pick %ld of %s (%.3f -> %.3f)\n",
                      dev->picks[pi].lPickIndex, dev->szStation, dev->picks[pi].dTime, clicked_time);
                dev->picks[pi].iUseMe = 0;
            }
        }
    }

    int seq = g_pick_seq;
    g_pick_seq = (g_pick_seq + 1) % 1000000; /* spec: 0-999999 (bugfix: was unsigned char) */

    /* Show the manual pick immediately at the station, without waiting for it
       to circulate back through PICK_RING. */
    {
        int slot = (int)(dev->lPickRingNext % MAX_PICKS_PER_STA);
        dev->picks[slot].dTime = clicked_time;
        strncpy(dev->picks[slot].szPhase, "P", 7);
        dev->picks[slot].szPhase[7] = '\0';
        dev->picks[slot].lPickIndex = seq;
        dev->picks[slot].iUseMe = 1;
        dev->lPickRingNext++;
        if (dev->iNumPicks < MAX_PICKS_PER_STA) dev->iNumPicks++;
    }

    char out_msg[256], time_str[64];
    time_t t_sec = (time_t)clicked_time;
    double t_msec = clicked_time - (double)t_sec;
    struct tm *ptm = gmtime(&t_sec);
    snprintf(time_str, sizeof(time_str), "%04d%02d%02d%02d%02d%06.3f", ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, (double)ptm->tm_sec + t_msec);
    for (int k = 0; time_str[k] != '\0'; k++) if (time_str[k] == ',') time_str[k] = '.';
    snprintf(out_msg, sizeof(out_msg), "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0\n", TypePickSCNL, MyModId, MyInstId, seq, StaArray[sta_idx].szStation, StaArray[sta_idx].szChannel, StaArray[sta_idx].szNetID, StaArray[sta_idx].szLocation, time_str);
    MSG_LOGO logo = {MyInstId, MyModId, TypePickSCNL};
    if (tport_putmsg(&PickRegion, &logo, strlen(out_msg), out_msg) != PUT_OK) logit("e", "csntvp: Error inyectando pick.\n");
    else logit("t", "csntvp: INYECTADO: %s", out_msg);
    gtk_widget_queue_draw(widget); return TRUE;
}

/* ====================================================================
 * DATA FETCHING: REALTIME ENGINE (raw only; DSP happens in the envelope pass)
 * ==================================================================== */
gboolean fetch_realtime_data(gpointer user_data) {
    char msg[MAX_TRACE_BYTES]; MSG_LOGO reclogo; long recsize; int res;
    TRACE2_HEADER *WaveHead;

    int64_t current_realtime = g_get_real_time();
    if (g_last_frame_realtime == 0) g_last_frame_realtime = current_realtime;
    double dt = (current_realtime - g_last_frame_realtime) / 1000000.0;
    g_last_frame_realtime = current_realtime;
    double sys_time = (double)current_realtime / 1000000.0;

    do {
        res = tport_getmsg( &WaveRegion, WaveLogo, 1, &reclogo, &recsize, msg, sizeof(msg) );
        if ( res == GET_OK || res == GET_MISS || res == GET_NOTRACK ) {
            WaveHead = (TRACE2_HEADER *) msg;
            double t_start = WaveHead->starttime;
            double t_end = WaveHead->endtime;
            double rate = WaveHead->samprate;
            if (rate <= 0) rate = 20.0;

            if (fabs(sys_time - t_end) > MAX_REALTIME_SKEW) {
                if (t_end > g_latest_time) g_latest_time = t_end;
            }

            for ( int i = 0; i < iNumStas; i++ ) {
                if ( !strcmp(StaArray[i].szStation, WaveHead->sta) && !strcmp(StaArray[i].szChannel, WaveHead->chan) ) {
                    StaArray[i].dLastPacketSysTime = sys_time;
                    g_last_data_realtime = sys_time;

                    /* Lazily (re)size the raw circular buffer by the actual
                       sample rate (memory refinement). */
                    int rate_int = (int)(rate + 0.5);
                    if (rate_int < 1) rate_int = 1;
                    if (rate_int > MAX_SAMP_RATE) rate_int = MAX_SAMP_RATE;
                    long want_size = (long)MAX_MINUTES * 60 * rate_int;
                    if (StaArray[i].lRawCircSize == 0 || want_size != StaArray[i].lRawCircSize) {
                        int32_t *nb = (int32_t *) realloc(StaArray[i].plRawCircBuff, want_size * sizeof(int32_t));
                        if (!nb) { logit("e", "csntvp: sin memoria para %s (%ld muestras)\n", StaArray[i].szStation, want_size); break; }
                        StaArray[i].plRawCircBuff = nb;
                        StaArray[i].lRawCircSize = want_size;
                        StaArray[i].dSampRate = rate;
                        for (long k = 0; k < want_size; k++) StaArray[i].plRawCircBuff[k] = INT_MAX;
                        StaArray[i].lLastAbsIdx = 0;
                        StaArray[i].iProcValid = 0;
                        StaArray[i].bEnvValid = FALSE;
                    }

                    int64_t abs_start = (int64_t)(t_start * rate);
                    int64_t abs_end = (int64_t)(t_end * rate);

                    /* Standard gap fill with the empty flag (INT_MAX) */
                    if (StaArray[i].lLastAbsIdx > 0 && abs_start > StaArray[i].lLastAbsIdx) {
                        int64_t gap_samps = abs_start - StaArray[i].lLastAbsIdx;
                        if (gap_samps > StaArray[i].lRawCircSize) gap_samps = StaArray[i].lRawCircSize;
                        for (int64_t g = 0; g < gap_samps; g++) {
                            int64_t clr_abs = StaArray[i].lLastAbsIdx + g;
                            int idx = CIRC_IDX(clr_abs, StaArray[i].lRawCircSize);
                            StaArray[i].plRawCircBuff[idx] = INT_MAX;
                        }
                        if (StaArray[i].iProcValid && (abs_start - 1) >= StaArray[i].lProcAbsStart)
                            StaArray[i].iProcValid = 0;
                    }

                    char szType[3]; strncpy(szType, WaveHead->datatype, 2); szType[2] = '\0';
                    char *pRaw = msg + sizeof(TRACE2_HEADER);

                    for (int s = 0; s < WaveHead->nsamp; s++) {
                        int32_t x;
                        if (ReadSampleAt(szType, pRaw, s, &x) != 0) {
                            if (!g_warned_datatype) {
                                g_warned_datatype = 1;
                                logit("et", "csntvp: datatype <%s> not supported, reading as int32\n", szType);
                            }
                            memcpy(&x, pRaw + s * sizeof(int32_t), sizeof(int32_t));
                        }

                        double t_samp = t_start + ((double)s / rate);
                        int64_t abs_idx = (int64_t)(t_samp * rate);
                        int idx = CIRC_IDX(abs_idx, StaArray[i].lRawCircSize);
                        StaArray[i].plRawCircBuff[idx] = x;
                    }

                    if (abs_end > StaArray[i].lLastAbsIdx) StaArray[i].lLastAbsIdx = abs_end;
                    if (StaArray[i].iProcValid && abs_start <= StaArray[i].lProcAbsEnd)
                        StaArray[i].iProcValid = 0;
                    break;
                }
            }
        }
    } while ( res != GET_NONE );

    if (!g_is_hold) {
        if (g_last_data_realtime > 0.0 && (sys_time - g_last_data_realtime) > DATA_STALE_SECS) {
            g_data_stale = TRUE;
        } else {
            g_data_stale = FALSE;
            gboolean is_realtime = TRUE;
            if (g_latest_time > 0.0 && fabs(sys_time - g_latest_time) > MAX_REALTIME_SKEW) is_realtime = FALSE;

            if (is_realtime) {
                g_latest_time = sys_time - 2.0;
                g_smooth_time = g_latest_time;
            } else {
                if (g_smooth_time == 0.0) g_smooth_time = g_latest_time;
                else {
                    g_smooth_time += dt;
                    if (fabs(g_latest_time - g_smooth_time) > 2.0) g_smooth_time = g_latest_time;
                    else if (g_latest_time > g_smooth_time) g_smooth_time += dt * 0.1;
                    else if (g_latest_time < g_smooth_time) g_smooth_time -= dt * 0.1;
                }
            }
        }
    }

    gboolean picks_changed = FALSE;

    do {
        res = tport_getmsg( &PickRegion, PickLogo, 1, &reclogo, &recsize, msg, sizeof(msg) - 1 );
        if ( res == GET_OK || res == GET_MISS || res == GET_NOTRACK ) {
            msg[recsize] = '\0'; if (reclogo.type == TypePickSCNL) {
                int t, m, inst, seq; char scnl[64], ph[10], ts[30];
                if (sscanf(msg, "%d %d %d %d %63s %9s %29s", &t, &m, &inst, &seq, scnl, ph, ts) >= 7) {
                    char sta[10]="", ch[10]="", net[10]="", loc[10]=""; sscanf(scnl, "%9[^.].%9[^.].%9[^.].%9s", sta, ch, net, loc);
                    for (int k=0; ts[k]; k++) if (ts[k]==',') ts[k]='.';
                    int py, pm, pd, phh, pmn; double psec;
                    if (sscanf(ts, "%4d%2d%2d%2d%2d%lf", &py, &pm, &pd, &phh, &pmn, &psec) == 6) {
                        struct tm pt = {0}; pt.tm_year=py-1900; pt.tm_mon=pm-1; pt.tm_mday=pd; pt.tm_hour=phh; pt.tm_min=pmn; pt.tm_sec=(int)psec;
                        char *otz=getenv("TZ"); setenv("TZ", "GMT", 1); tzset(); double pT=mktime(&pt)+(psec-(int)psec);
                        if(otz) setenv("TZ", otz, 1); else unsetenv("TZ"); tzset();
                        char dph[8]; if (ph[0]=='U'||ph[0]=='D'||ph[0]=='?') snprintf(dph, sizeof(dph), "P(%.4s)", ph); else snprintf(dph, sizeof(dph), "%.7s", ph);
                        for (int i=0; i<iNumStas; i++) if (!strcmp(StaArray[i].szStation, sta) && !strcmp(StaArray[i].szChannel, ch)) {
                            DEV_STATION *dev = &StaArray[i];
                            /* Upsert our own picks by seq (avoid double count of
                               the manual pick when it comes back through the ring). */
                            int found = -1;
                            if (m == MyModId && inst == MyInstId) {
                                for (int k = 0; k < MAX_PICKS_PER_STA; k++) {
                                    if (dev->picks[k].lPickIndex == seq && dev->picks[k].iUseMe > 0) { found = k; break; }
                                }
                            }
                            if (found >= 0) {
                                dev->picks[found].dTime = pT;
                                snprintf(dev->picks[found].szPhase, sizeof(dev->picks[found].szPhase), "%s", dph);
                                dev->picks[found].iUseMe = 1;
                            } else {
                                int slot = (int)(dev->lPickRingNext % MAX_PICKS_PER_STA);
                                dev->picks[slot].dTime = pT;
                                snprintf(dev->picks[slot].szPhase, sizeof(dev->picks[slot].szPhase), "%s", dph);
                                dev->picks[slot].lPickIndex = seq;
                                dev->picks[slot].iUseMe = 1;
                                dev->lPickRingNext++;
                                if (dev->iNumPicks < MAX_PICKS_PER_STA) dev->iNumPicks++;
                            }
                            picks_changed = TRUE;
                            break;
                        }
                    }
                }
            }
        }
    } while ( res != GET_NONE );

    /* A1/A2/A3: refresh the envelope cache at its rhythm (2Hz) and redraw
       only if something changed (new envelope, picks, or forced change). */
    g_envelope_updated = FALSE;
    update_wave_envelopes();

    if (g_envelope_updated || picks_changed) {
        if (g_drawing_waves) gtk_widget_queue_draw(g_drawing_waves);
        if (drawing_axis) gtk_widget_queue_draw(drawing_axis);
    }

    return G_SOURCE_CONTINUE;
}

/* ====================================================================
 * A2/A3: CACHED PROCESSING AND ENVELOPE DECIMATED BY COLUMN
 * ==================================================================== */
static void compute_station_envelope(int i, int width) {
    DEV_STATION *sta = &StaArray[i];
    double rate = sta->dSampRate > 0 ? sta->dSampRate : 20.0;
    double window_secs = iTimeWindowMinutes * 60.0;
    double t_right = g_is_hold ? g_t_hold_time : g_smooth_time;
    double t_left = t_right - window_secs;

    /* MAGIC PADDING: 30s backwards so that the filter ringing dies */
    double pad_secs = 30.0;
    double extract_start = t_left - pad_secs;

    int64_t abs_start = (int64_t)(extract_start * rate);
    int64_t abs_end = (int64_t)(t_right * rate);
    long num_samps = abs_end - abs_start;

    if (num_samps <= 0 || sta->lRawCircSize <= 0 || num_samps > sta->lRawCircSize) return;

    /* 1. CACHE OF THE PROCESSED TRACE */
    int cache_hit =
        sta->iProcValid &&
        sta->lProcAbsStart == abs_start &&
        sta->lProcAbsEnd == abs_end &&
        sta->iProcFilterType == g_filter_type &&
        sta->iProcOrder == g_order &&
        sta->dProcF1 == g_f1 &&
        sta->dProcF2 == g_f2;

    long *trace_buf;
    int found_first;

    if (!cache_hit) {
        if (sta->lProcCap < num_samps) {
            long *nb = (long *) realloc(sta->plProcBuf, num_samps * sizeof(long));
            if (!nb) return;
            sta->plProcBuf = nb;
            sta->lProcCap = num_samps;
        }
        trace_buf = sta->plProcBuf;

        /* 2. EXTRACTION TO LINEAR ARRAY */
        for (long k = 0; k < num_samps; k++) {
            int64_t abs_k = abs_start + k;
            if (abs_k > sta->lLastAbsIdx || abs_k < 0) {
                trace_buf[k] = INT_MAX;
            } else {
                trace_buf[k] = sta->plRawCircBuff[CIRC_IDX(abs_k, sta->lRawCircSize)];
            }
        }

        /* 3. GAP INTERPOLATION TO AVOID FILTERING SPIKES */
        long last_valid = 0;
        long gap_start = -1;
        found_first = 0;
        for (long k = 0; k < num_samps; k++) {
            if (trace_buf[k] != INT_MAX) { last_valid = trace_buf[k]; found_first = 1; break; }
        }

        if (found_first) {
            for (long k = 0; k < num_samps; k++) { if (trace_buf[k] != INT_MAX) break; trace_buf[k] = last_valid; }
            for (long k = 0; k < num_samps; k++) {
                if (trace_buf[k] == INT_MAX) {
                    if (gap_start == -1) gap_start = k;
                } else {
                    if (gap_start != -1) {
                        long gap_len = k - gap_start;
                        long next_valid = trace_buf[k];
                        for (long g = gap_start; g < k; g++) {
                            double frac = (double)(g - gap_start + 1) / (double)(gap_len + 1);
                            trace_buf[g] = last_valid + (long)(frac * (next_valid - last_valid));
                        }
                        gap_start = -1;
                    }
                    last_valid = trace_buf[k];
                }
            }
            if (gap_start != -1) { for (long g = gap_start; g < num_samps; g++) trace_buf[g] = last_valid; }
        } else {
            for (long k = 0; k < num_samps; k++) trace_buf[k] = 0;
        }

        /* 4. REMOVE DC (BASELINE) */
        double sum = 0;
        for (long k = 0; k < num_samps; k++) sum += (double)trace_buf[k];
        long mean = (long)(sum / num_samps);
        for (long k = 0; k < num_samps; k++) trace_buf[k] -= mean;

        /* 5. APPLY FILTERS (default Raw = no filter) */
        if (g_filter_type != 0 && found_first) {
            double nyquist = rate / 2.0;
            double safe_f1 = g_f1, safe_f2 = g_f2;

            if (safe_f1 >= nyquist) safe_f1 = nyquist * 0.95;
            if (g_filter_type == 3) {
                if (safe_f2 >= nyquist) safe_f2 = nyquist * 0.95;
                if (safe_f1 >= safe_f2) safe_f1 = safe_f2 * 0.5;
            }

            if (g_filter_type == 1) aplicar_filtro_iir(trace_buf, num_samps, rate, 1, safe_f1, g_order);
            else if (g_filter_type == 2) aplicar_filtro_iir(trace_buf, num_samps, rate, 2, safe_f1, g_order);
            else if (g_filter_type == 3) {
                aplicar_filtro_iir(trace_buf, num_samps, rate, 1, safe_f1, g_order);
                aplicar_filtro_iir(trace_buf, num_samps, rate, 2, safe_f2, g_order);
            }
        }

        sta->iProcValid = 1;
        sta->iProcFoundFirst = found_first;
        sta->lProcAbsStart = abs_start;
        sta->lProcAbsEnd = abs_end;
        sta->iProcFilterType = g_filter_type;
        sta->iProcOrder = g_order;
        sta->dProcF1 = g_f1;
        sta->dProcF2 = g_f2;
    } else {
        trace_buf = sta->plProcBuf;
        found_first = sta->iProcFoundFirst;
    }

    /* 6. max_abs over the drawn range (for auto_scale in the draw) */
    long draw_start_idx = (long)(pad_secs * rate);
    if (draw_start_idx > num_samps) draw_start_idx = 0;

    long real_samps_end = (long)(sta->lLastAbsIdx - abs_start + 1);
    if (real_samps_end > num_samps) real_samps_end = num_samps;
    if (real_samps_end < 0) real_samps_end = 0;

    double max_abs = 0.0;
    gboolean has_data = FALSE;
    for (long k = draw_start_idx; k < real_samps_end; k++) {
        double abs_val = fabs((double)trace_buf[k]);
        if (abs_val > max_abs) max_abs = abs_val;
        has_data = TRUE;
    }
    if (max_abs < 1.0 || !has_data || !found_first) max_abs = 1.0;
    sta->dEnvMaxAbs = max_abs;
    sta->iEnvFoundFirst = found_first;

    /* 7. Envelope (min/max) per pixel column, in raw units */
    if (sta->iEnvCap < width) {
        int new_cap = width + 256;
        double *nmin = (double *) realloc(sta->dEnvMin, new_cap * sizeof(double));
        double *nmax = (double *) realloc(sta->dEnvMax, new_cap * sizeof(double));
        int *nhas = (int *) realloc(sta->iEnvHas, new_cap * sizeof(int));
        if (!nmin || !nmax || !nhas) {
            if (nmin) free(nmin);
            if (nmax) free(nmax);
            if (nhas) free(nhas);
            sta->bEnvValid = FALSE;
            return;
        }
        sta->dEnvMin = nmin; sta->dEnvMax = nmax; sta->iEnvHas = nhas;
        sta->iEnvCap = new_cap;
    }
    sta->iEnvWidth = width;

    if (found_first) {
        for (int px = 0; px < width; px++) {
            double px_t_start = t_left + ((double)px / width) * window_secs;
            double px_t_end = t_left + ((double)(px + 1) / width) * window_secs;

            long p_local_start = (long)((px_t_start - extract_start) * rate);
            long p_local_end = (long)((px_t_end - extract_start) * rate);
            if (p_local_end == p_local_start) p_local_end++;

            if (p_local_start < 0) p_local_start = 0;
            if (p_local_end > num_samps) p_local_end = num_samps;

            double p_min = 1e12, p_max = -1e12;
            gboolean px_has_data = FALSE;

            for (long local_k = p_local_start; local_k < p_local_end; local_k++) {
                if (local_k >= real_samps_end) continue;
                double val = (double)trace_buf[local_k];
                if (val < p_min) p_min = val;
                if (val > p_max) p_max = val;
                px_has_data = TRUE;
            }

            if (px_has_data) {
                sta->dEnvMin[px] = p_min;
                sta->dEnvMax[px] = p_max;
                sta->iEnvHas[px] = 1;
            } else {
                sta->iEnvHas[px] = 0;
            }
        }
    }

    sta->bEnvValid = TRUE;
}

static void update_wave_envelopes(void) {
    if (iNumStas == 0 || !g_drawing_waves) return;

    int width = gtk_widget_get_allocated_width(g_drawing_waves) - PANEL_WIDTH;
    if (width <= 0) return;

    double t_right = g_is_hold ? g_t_hold_time : g_smooth_time;
    int64_t now_ms = g_get_monotonic_time() / 1000;
    gboolean force = g_bForceEnv;
    g_bForceEnv = FALSE;

    gboolean should = FALSE;
    if (force) {
        should = TRUE;
    } else if (g_is_hold) {
        if (g_last_env_width != width || !g_last_env_ok) should = TRUE;
    } else {
        if (!(fabs(t_right - g_last_env_t_right) < 0.5 && g_data_stale)) {
            double window_secs = iTimeWindowMinutes * 60.0;
            double interval_ms = (window_secs / width) * 1000.0;
            if (interval_ms < ENV_PROCESS_MS) interval_ms = ENV_PROCESS_MS;
            if (interval_ms > 1500.0) interval_ms = 1500.0;

            if ((now_ms - g_last_env_process_ms) >= (int64_t)interval_ms || g_last_env_width != width)
                should = TRUE;
        }
    }
    if (!should) return;

    g_envelope_updated = TRUE;
    g_last_env_process_ms = now_ms;
    g_last_env_t_right = t_right;
    g_last_env_width = width;
    g_last_env_ok = TRUE;

    for (int i = 0; i < iNumStas; i++) compute_station_envelope(i, width);
}

/* ====================================================================
 * RENDER
 * ==================================================================== */
gboolean on_draw_waves(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    if (iNumStas == 0 || g_latest_time <= 0.0) return FALSE;
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);

    cairo_set_source_rgb(cr, g_color_bg[0], g_color_bg[1], g_color_bg[2]); cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.96, 0.96, 0.96); cairo_rectangle(cr, 0, 0, PANEL_WIDTH, height); cairo_fill(cr);

    double draw_area_width = width - PANEL_WIDTH; if (draw_area_width <= 0) return FALSE;

    double font_size = dTrackHeight * 0.7;
    if (font_size > 12.0) font_size = 12.0;
    if (font_size < 4.0) font_size = 4.0;

    double window_secs = iTimeWindowMinutes * 60.0;
    double t_right = g_is_hold ? g_t_hold_time : g_smooth_time, t_left = t_right - window_secs;

    for (int i = 0; i < iNumStas; i++) {
        double y_top = i * dTrackHeight, y_center = y_top + (dTrackHeight / 2.0);

        /* --- ETIQUETA DE ESTACION Y CANAL --- */
        cairo_set_source_rgb(cr, g_color_font[0], g_color_font[1], g_color_font[2]);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font_size);
        cairo_move_to(cr, 6, y_center + (font_size * 0.35));
        cairo_show_text(cr, StaArray[i].szStation);

        cairo_text_extents_t extents;
        cairo_text_extents(cr, StaArray[i].szStation, &extents);
        double next_x = 6 + extents.x_advance + 6;

        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_move_to(cr, next_x, y_center + (font_size * 0.35));
        char comp_net[32];
        snprintf(comp_net, sizeof(comp_net), "%s %s", StaArray[i].szChannel, StaArray[i].szNetID);
        cairo_show_text(cr, comp_net);

        /* --- SEPARADOR --- */
        cairo_set_source_rgb(cr, g_color_sep[0], g_color_sep[1], g_color_sep[2]);
        cairo_move_to(cr, PANEL_WIDTH, y_top);
        cairo_line_to(cr, width, y_top);
        cairo_stroke(cr);

        /* --- ENVELOPE CACHEADO --- */
        DEV_STATION *sta = &StaArray[i];
        if (sta->bEnvValid && sta->iEnvCap >= (int)draw_area_width && sta->iEnvFoundFirst) {
            double max_abs = sta->dEnvMaxAbs;
            if (max_abs < 1.0) max_abs = 1.0;
            double auto_scale = (dTrackHeight * 0.425) / max_abs;
            auto_scale *= g_zoom_factor;

            cairo_save(cr);
            cairo_rectangle(cr, PANEL_WIDTH, y_top, draw_area_width, dTrackHeight);
            cairo_clip(cr);
            cairo_set_source_rgb(cr, g_color_wave[0], g_color_wave[1], g_color_wave[2]);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

            int ncol = (int)draw_area_width;
            for (int px = 0; px < ncol; px++) {
                if (!sta->iEnvHas[px]) continue;
                double s_max = sta->dEnvMax[px] * auto_scale;
                double s_min = sta->dEnvMin[px] * auto_scale;
                if (s_max - s_min < 2.0) {
                    double avg = (s_max + s_min) / 2.0;
                    s_max = avg + 1.0;
                    s_min = avg - 1.0;
                }
                double x = PANEL_WIDTH + px;
                cairo_rectangle(cr, x, y_center - s_max, 1.0, s_max - s_min);
            }
            cairo_fill(cr);
            cairo_restore(cr);
        }

        /* --- PICKS --- */
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0); cairo_set_line_width(cr, 2.0);
        cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

        int n_draw = (StaArray[i].iNumPicks < MAX_PICKS_PER_STA) ? StaArray[i].iNumPicks : MAX_PICKS_PER_STA;
        int start_slot = (int)((StaArray[i].lPickRingNext - n_draw + MAX_PICKS_PER_STA) % MAX_PICKS_PER_STA);
        for (int pi = 0; pi < n_draw; pi++) {
            int p = (start_slot + pi) % MAX_PICKS_PER_STA;
            if (StaArray[i].picks[p].iUseMe <= 0 || StaArray[i].picks[p].dTime <= 0.0) continue;
            double pTime = StaArray[i].picks[p].dTime;
            if (pTime >= t_left && pTime <= t_right + 15.0) {
                double x_pos = PANEL_WIDTH + ((pTime - t_left) / window_secs * draw_area_width);
                if (x_pos > PANEL_WIDTH && x_pos < width) {
                    cairo_move_to(cr, x_pos, y_top); cairo_line_to(cr, x_pos, y_top + dTrackHeight); cairo_stroke(cr);
                    cairo_move_to(cr, x_pos + 4, y_top + font_size + 2); cairo_show_text(cr, StaArray[i].picks[p].szPhase);
                }
            }
        }
    }
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7); cairo_move_to(cr, PANEL_WIDTH, 0); cairo_line_to(cr, PANEL_WIDTH, height); cairo_stroke(cr);
    return FALSE;
}

gboolean on_draw_axis(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    guint width = gtk_widget_get_allocated_width(widget), height = gtk_widget_get_allocated_height(widget);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); cairo_rectangle(cr, 0, 0, width, height); cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7); cairo_set_line_width(cr, 2.0); cairo_move_to(cr, 0, 0); cairo_line_to(cr, width, 0); cairo_stroke(cr);
    if (g_latest_time > 0) {
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); cairo_set_font_size(cr, 13); cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        double ws = iTimeWindowMinutes * 60.0; time_t tr = (time_t)(g_is_hold ? g_t_hold_time : g_smooth_time), tl = (time_t)(tr - ws), tc = (time_t)(tr - (ws/2.0));
        char sl[32], sc[32], sr[32]; strftime(sl, 32, "%H:%M:%S", gmtime(&tl)); strftime(sc, 32, "%H:%M:%S", gmtime(&tc)); strftime(sr, 32, "%H:%M:%S UTC", gmtime(&tr));
        cairo_move_to(cr, 10, height - 10); cairo_show_text(cr, "UTC TIME"); cairo_move_to(cr, PANEL_WIDTH + 10, height - 10); cairo_show_text(cr, sl);
        cairo_move_to(cr, PANEL_WIDTH + (width - PANEL_WIDTH)/2 - 30, height - 10); cairo_show_text(cr, sc); cairo_move_to(cr, width - 100, height - 10); cairo_show_text(cr, sr);

        if (g_data_stale) {
            cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
            cairo_move_to(cr, 10, 14);
            cairo_show_text(cr, "STALE DATA");
        }
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    GtkWidget *window, *vbox;
    if (argc != 2) { fprintf(stderr, "Uso: %s <configfile.d>\n", argv[0]); exit(1); }
    if (ReadConfig(argv[1]) != 0) { fprintf(stderr, "Error leyendo configuracion de %s\n", argv[1]); exit(1); }
    setenv("TZ", "GMT", 1); tzset(); logit_init(argv[1], 0, 1024, LogFile); MyPid = getpid();
    gtk_init(&argc, &argv);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, "#btn_filter { background-image: none; background-color: #28a745; color: #ffffff; font-weight: bold; padding: 2px 10px; border-radius: 4px; }\n#btn_filter:hover { background-color: #218838; }\n#btn_reload { background-image: none; background-color: #6c757d; color: #ffffff; font-weight: bold; padding: 2px 10px; border-radius: 4px; }\n#btn_reload:hover { background-color: #5a6268; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    ConnectToEarthworm(); LoadStationsFromFile();
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "CSN tvp - Trace Viewer-Picker");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *menu_bar = gtk_menu_bar_new();
    GtkWidget *ctrl_panel_item = gtk_menu_item_new_with_label("Control Panel"), *ctrl_panel_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(ctrl_panel_item), ctrl_panel_menu);
    GtkWidget *stas_per_screen_item = gtk_menu_item_new_with_label("Stations per screen");
    g_signal_connect(stas_per_screen_item, "activate", G_CALLBACK(on_stas_per_screen_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctrl_panel_menu), stas_per_screen_item);
    GtkWidget *time_window_item = gtk_menu_item_new_with_label("Time window");
    g_signal_connect(time_window_item, "activate", G_CALLBACK(on_time_window_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctrl_panel_menu), time_window_item);
    GtkWidget *clean_view_item = gtk_menu_item_new_with_label("Clean view");
    g_signal_connect(clean_view_item, "activate", G_CALLBACK(on_clean_view_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctrl_panel_menu), clean_view_item);
    GtkWidget *reload_default_item = gtk_menu_item_new_with_label("Reload default stations");
    g_signal_connect(reload_default_item, "activate", G_CALLBACK(on_reload_default_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctrl_panel_menu), reload_default_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), ctrl_panel_item);

    /* --- MENU COLOURS --- */
    GtkWidget *colours_item = gtk_menu_item_new_with_label("Colours"), *colours_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(colours_item), colours_menu);
    GtkWidget *waveforms_item = gtk_menu_item_new_with_label("Waveforms");
    g_signal_connect(waveforms_item, "activate", G_CALLBACK(on_colour_select), GINT_TO_POINTER(1));
    gtk_menu_shell_append(GTK_MENU_SHELL(colours_menu), waveforms_item);
    GtkWidget *background_item = gtk_menu_item_new_with_label("Background");
    g_signal_connect(background_item, "activate", G_CALLBACK(on_colour_select), GINT_TO_POINTER(2));
    gtk_menu_shell_append(GTK_MENU_SHELL(colours_menu), background_item);
    GtkWidget *font_item = gtk_menu_item_new_with_label("Font");
    g_signal_connect(font_item, "activate", G_CALLBACK(on_colour_select), GINT_TO_POINTER(3));
    gtk_menu_shell_append(GTK_MENU_SHELL(colours_menu), font_item);
    GtkWidget *separator_item = gtk_menu_item_new_with_label("Separator");
    g_signal_connect(separator_item, "activate", G_CALLBACK(on_colour_select), GINT_TO_POINTER(4));
    gtk_menu_shell_append(GTK_MENU_SHELL(colours_menu), separator_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), colours_item);

    /* --- MENU FILTER --- */
    GtkWidget *filter_item = gtk_menu_item_new_with_label("Filter"), *filter_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(filter_item), filter_menu);
    GtkWidget *set_filter_item = gtk_menu_item_new_with_label("Filter Options");
    g_signal_connect(set_filter_item, "activate", G_CALLBACK(on_filter_menu_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(filter_menu), set_filter_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), filter_item);

    gtk_box_pack_start(GTK_BOX(vbox), menu_bar, FALSE, FALSE, 0);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_start(toolbar, 10); gtk_widget_set_margin_end(toolbar, 10); gtk_widget_set_margin_top(toolbar, 5); gtk_widget_set_margin_bottom(toolbar, 5);
    btn_hold = gtk_toggle_button_new_with_label("HOLD (Congelar Pantalla)");
    g_signal_connect(btn_hold, "toggled", G_CALLBACK(on_btn_hold_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_hold, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_label_new(" | (En HOLD) Clic Izq: Picar onda | Flechas Arriba/Abajo: Zoom Vertical"), FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    g_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(g_scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(g_scrolled_window, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), g_scrolled_window, TRUE, TRUE, 0);
    g_drawing_waves = gtk_drawing_area_new();
    gtk_widget_set_size_request(g_drawing_waves, -1, iNumStas * dTrackHeight);
    gtk_widget_add_events(g_drawing_waves, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(g_drawing_waves, "button-press-event", G_CALLBACK(on_canvas_button_press), NULL);
    gtk_container_add(GTK_CONTAINER(g_scrolled_window), g_drawing_waves);
    g_signal_connect(g_drawing_waves, "draw", G_CALLBACK(on_draw_waves), NULL);
    drawing_axis = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_axis, -1, BOTTOM_AXIS_H);
    gtk_box_pack_start(GTK_BOX(vbox), drawing_axis, FALSE, FALSE, 0);
    g_signal_connect(drawing_axis, "draw", G_CALLBACK(on_draw_axis), NULL);
    g_timeout_add(REFRESH_MS, fetch_realtime_data, window);
    g_timeout_add(1000, ew_background_tasks, NULL);
    gtk_widget_show_all(window);
    RecalcTrackHeight();
    gtk_main();
    tport_detach( &WaveRegion ); tport_detach( &PickRegion ); FreeAllStations();
    return 0;
}
