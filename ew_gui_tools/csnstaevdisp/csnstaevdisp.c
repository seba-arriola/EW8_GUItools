/***********************************************************************
 *                           csnstaevdisp                             *
 *                                                                     *
 *  Network and seismicity viewer. Loads the station list and the      *
 *  quake history file and renders them on an equirectangular map      *
 *  image, with zoom and pan. Station triangles are colored by         *
 *  elevation and quakes are drawn as red circles scaled by            *
 *  magnitude.                                                         *
 *                                                                     *
 *  Usage: csnstaevdisp <configfile.d>                                *
 ***********************************************************************/

#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <locale.h>

/* Earthworm headers */
#pragma warning(disable: 4005)
#include <earthworm.h>
#include <transport.h>
#include <kom.h>

#define MAX_STR 256
#define MAX_QUAKES 1000
#define MAX_ESTA 512

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- MODULE CONFIGURATION --- */
char MyModName[MAX_STR] = "MOD_STAEVDISP";   /* Earthworm module name */
char RingName[MAX_STR] = "HYPO_RING";        /* hypo solutions ring */
char StaFile[MAX_STR] = "";                  /* station list file (con lat/lon/elev) */
char QuakeFile[MAX_STR] = "";                /* quake history file */
char MapImageFile[MAX_STR] = "";             /* base map image file */
int  HeartBeatInt = 30;                      /* heartbeat interval (seconds) */
int  LogFile = 1;                            /* 1 = write log to disk */

/* PARAMETER: Size of the station triangles (Mandatorily read from the .d) */
double g_station_size = 5.0;                 /* size of the station triangles (from the .d) */

pid_t MyPid;                                 /* this process id */
time_t timeLastBeat = 0;                     /* time of the last heartbeat sent */
unsigned char TypeHeartBeat = 0;             /* TYPE_HEARTBEAT */
unsigned char TypeError = 0;                 /* TYPE_ERROR */
unsigned char MyModId = 0;                   /* Earthworm module id */
unsigned char MyInstId = 0;                  /* Earthworm installation id */
SHM_INFO Region;                             /* transport region of the input ring */

/* --- DATA STRUCTURES --- */
typedef struct {
    double lat;
    double lon;
    double mag;
    int qid;
} QUAKE_MARKER;

QUAKE_MARKER QuakeList[MAX_QUAKES];          /* quakes read from the history file */
int NumQuakes = 0;                           /* number of loaded quakes */

/* Estacion del visor: solo lo necesario para dibujar los triangulos. */
typedef struct {
    char   sta[16];
    double lat;
    double lon;
    double elev_m;
} STATION_MARK;

STATION_MARK *StaArray = NULL;               /* station array (loader propio) */
int NumEstaciones = 0;                       /* number of loaded stations */
double g_min_elev = 99999.0;                 /* minimum elevation for the color gradient */
double g_max_elev = -99999.0;                /* maximum elevation for the color gradient */

time_t last_file_mod_time = 0;               /* mtime of the quake file on last check */
GdkPixbuf *g_map_pixbuf = NULL;              /* base map image */

/* --- Map Zoom and Pan Control Variables --- */
double g_map_zoom = 2.0;                     /* map zoom factor */
double g_map_pan_x = 0.0;                    /* horizontal pan offset (pixels) */
double g_map_pan_y = 0.0;                    /* vertical pan offset (pixels) */
gboolean g_is_dragging = FALSE;              /* left button dragging the map */
double g_last_mouse_x = 0.0;                 /* last mouse x during a drag */
double g_last_mouse_y = 0.0;                 /* last mouse y during a drag */

/* Control of the Events to Display */
int g_max_events_display = 10;               /* number of quakes to draw */

/* Interface Widgets */
GtkWidget *map_canvas;                       /* map drawing area */

/* --------------------------------------------------------------------
 * EARTHWORM CONFIGURATION READING
 * -------------------------------------------------------------------- */
 /***********************************************************************
  *                            ReadConfig()                             *
  *             Reads the module command file with kom.c, loading the   *
  *             module, ring, station files, quake history and the map, *
  *             and the mandatory StationSize parameter.                *
  *               0 on success, -1 if a parameter is missing or bad.    *
  ***********************************************************************/

int ReadConfig(char *configfile) {
    /* Parametros obligatorios: MyModuleId, RingName, HeartBeatInt, LogFile,
       QuakeFile, MapImageFile, StaFile, StationSize. */
    int ncommand = 8, nmiss = 0, i;
    char init[10] = {0};
    char *com, *str;

    if (!k_open(configfile)) {
        fprintf(stderr, "csnstaevdisp: Error opening config file <%s>\n", configfile);
        return -1;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if (k_its("MyModuleId")) { str = k_str(); if (str) strcpy(MyModName, str); init[0] = 1; } 
        else if (k_its("RingName")) { str = k_str(); if (str) strcpy(RingName, str); init[1] = 1; } 
        else if (k_its("HeartBeatInt")) { HeartBeatInt = k_int(); init[2] = 1; } 
        else if (k_its("LogFile")) { LogFile = k_int(); init[3] = 1; } 
        else if (k_its("QuakeFile")) { str = k_str(); if (str) strcpy(QuakeFile, str); init[4] = 1; } 
        else if (k_its("MapImageFile")) { str = k_str(); if (str) strcpy(MapImageFile, str); init[5] = 1; } 
        else if (k_its("StaFile")) { str = k_str(); if (str) strcpy(StaFile, str); init[6] = 1; }
        /* Now it is MANDATORY to ensure Earthworm does not ignore it */
        else if (k_its("StationSize")) { g_station_size = k_val(); init[7] = 1; }
        else { continue; }
        
        if (k_err()) {
            fprintf(stderr, "csnstaevdisp: Error parsing <%s> in <%s>\n", com, configfile);
            return -1;
        }
    }
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close();
    if (nmiss > 0) {
        fprintf(stderr, "csnstaevdisp: ERROR, missing parameters (e.g. StationSize) in <%s>\n", configfile);
        return -1;
    }
    return 0;
}

 /***********************************************************************
  *                              Status()                               *
  *             Sends a heartbeat or error message to the input ring so *
  *             startstop knows the module is alive.                    *
  *               Nothing.                                              *
  ***********************************************************************/

void Status(unsigned char type, short ierr, char *note) {
    MSG_LOGO logo; char msg[256]; time_t t;
    logo.instid = MyInstId; logo.mod = MyModId; logo.type = type;
    time(&t);
    if (type == TypeHeartBeat) sprintf(msg, "%ld %d\n", (long) t, MyPid);
    else if (type == TypeError) {
        sprintf(msg, "%ld %hd %s\n", (long) t, ierr, note);
        logit("et", "csnstaevdisp: Error: %s\n", note);
    }
    tport_putmsg(&Region, &logo, strlen(msg), msg);
}

 /***********************************************************************
  *                        ConnectToEarthworm()                         *
  *             Resolves the ring key, the installation and module      *
  *             ids and the message types, and attaches to the input    *
  *             ring.                                                   *
  *               Nothing; exits if the ring is not registered.         *
  ***********************************************************************/

void ConnectToEarthworm() {
    long RingKey = GetKey(RingName);
    
    if (RingKey == -1) { printf("Error: Invalid ring <%s>.\n", RingName); exit(-1); }
    
    if (GetLocalInst(&MyInstId) != 0) MyInstId = 0;
    if (GetModId(MyModName, &MyModId) != 0) {
        if (GetModId("MOD_WILDCARD", &MyModId) != 0) MyModId = 0;
    }
    
    if (GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0) TypeHeartBeat = 0; 
    if (GetType("TYPE_ERROR", &TypeError) != 0) TypeError = 0; 
    
    tport_attach(&Region, RingKey);
    printf("=== CONNECTED TO %s ===\n", RingName);
}

 /***********************************************************************
  *                        ew_background_tasks()                        *
  *             Periodic GTK timeout: sends heartbeats and exits the    *
  *             main loop when the ring signals termination.            *
  *               G_SOURCE_REMOVE on termination, else CONTINUE.        *
  ***********************************************************************/

gboolean ew_background_tasks(gpointer user_data) {
    time_t timeNow; time(&timeNow);
    if (timeNow - timeLastBeat >= HeartBeatInt) {
        timeLastBeat = timeNow;
        Status(TypeHeartBeat, 0, "");
    }

    int flag = tport_getflag(&Region);
    if (flag == TERMINATE || flag == MyPid) {
        printf("csnstaevdisp: Termination signal received. Closing...\n");
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* --------------------------------------------------------------------
 * LOADING OF STATIC AND DYNAMIC DATA
 * -------------------------------------------------------------------- */
 /***********************************************************************
  *                           LoadStations()                            *
  *             Loads the station list from the .sta files and scans the*
  *             minimum and maximum elevations for the color gradient.  *
  *               Nothing; prints an error if the station list fails.   *
  ***********************************************************************/

/* Parsea una linea del station file: Sta Net Chan Loc Lat Lon Elev Sens. */
static int parse_station_line(const char *line, STATION_MARK *s) {
    char sta[16], net[16], chan[16], loc[16];
    double lat, lon, elev, sens;
    int n = sscanf(line, "%15s %15s %15s %15s %lf %lf %lf %lf",
                   sta, net, chan, loc, &lat, &lon, &elev, &sens);
    if (n < 7) return -1;
    snprintf(s->sta, sizeof(s->sta), "%s", sta);
    s->lat    = lat;
    s->lon    = lon;
    s->elev_m = elev;
    return 0;
}

void LoadStations() {
    FILE *f;
    char line[512];

    StaArray = (STATION_MARK *) calloc(MAX_ESTA, sizeof(STATION_MARK));
    if (StaArray == NULL) {
        fprintf(stderr, ">> FATAL: Could not allocate memory for StaArray.\n");
        exit(-1);
    }

    f = fopen(StaFile, "r");
    if (f == NULL) {
        fprintf(stderr, ">> Error opening station file <%s>.\n", StaFile);
        return;
    }

    NumEstaciones = 0;
    while (fgets(line, sizeof(line), f) && NumEstaciones < MAX_ESTA) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#' || *p == '\n') continue;
        if (parse_station_line(p, &StaArray[NumEstaciones]) == 0)
            NumEstaciones++;
    }
    fclose(f);

    printf(">> Loaded %d stations to plot (Size: %.1f px).\n", NumEstaciones, g_station_size);

    /* Scan minimum and maximum elevations for the color gradient */
    for (int i = 0; i < NumEstaciones; i++) {
        if (StaArray[i].elev_m < g_min_elev) g_min_elev = StaArray[i].elev_m;
        if (StaArray[i].elev_m > g_max_elev) g_max_elev = StaArray[i].elev_m;
    }
    /* Fallback if all stations have the same elevation */
    if (g_min_elev >= g_max_elev) { g_min_elev = 0.0; g_max_elev = 1000.0; }
}

 /***********************************************************************
  *                         LoadQuakeHistory()                          *
  *             Reads the quake history file and fills the quake marker *
  *             array with the latitude, longitude, magnitude and id of *
  *             each event.                                             *
  *               Nothing.                                              *
  ***********************************************************************/

void LoadQuakeHistory() {
    FILE *f = fopen(QuakeFile, "r");
    if (!f) return;
    
    char line[512];
    NumQuakes = 0;
    
    while (fgets(line, sizeof(line), f) && NumQuakes < MAX_QUAKES) {
        if (strlen(line) < 20) continue;

        /* Formato de csnhypodbp_hist.txt (17 campos separados por coma):
           fecha,hora,lat,lon,depth,res,azm,stn,id,ml,mwp,otime,qver,qid,lat,lon,depth
           El archivo viene con el mas reciente primero, y asi se conserva
           QuakeList[0] = ultimo sismo. */
        char fecha[32], hora[32], lat_s[32], lon_s[32], dep_s[32], res_s[32],
             azm_s[32], stn_s[32], id_s[32], ml_s[32], mwp_s[32];
        double otime = 0.0, lat = 0.0, lon = 0.0, depth = 0.0;
        int qver = 0, qid = 0;

        int parsed = sscanf(line,
            "%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],"
            "%31[^,],%31[^,],%31[^,],%lf,%d,%d,%lf,%lf,%lf",
            fecha, hora, lat_s, lon_s, dep_s, res_s, azm_s, stn_s, id_s,
            ml_s, mwp_s, &otime, &qver, &qid, &lat, &lon, &depth);

        if (parsed != 17) continue;

        /* Longitude adjustment (0 to 360 or -180 to 180) */
        double dLon = lon;
        if (dLon > 180.0) dLon -= 360.0;

        /* Magnitud preferida: Mwp si es fuerte y con estaciones; si no Ml. */
        double ml_mag = 0.0, mwp_mag = 0.0;
        int ml_stn = 0, mwp_stn = 0;
        if (strcmp(ml_s, "-") != 0)  sscanf(ml_s, "%lf-%d", &ml_mag, &ml_stn);
        if (strcmp(mwp_s, "-") != 0) sscanf(mwp_s, "%lf-%d", &mwp_mag, &mwp_stn);

        double pref_mag = 0.0;
        if (mwp_mag >= 5.5 && mwp_stn >= 3) pref_mag = mwp_mag;
        else if (ml_mag > 0.0)              pref_mag = ml_mag;
        else if (mwp_mag > 0.0)             pref_mag = mwp_mag;

        QuakeList[NumQuakes].lat = lat;
        QuakeList[NumQuakes].lon = dLon;
        QuakeList[NumQuakes].mag = pref_mag;
        QuakeList[NumQuakes].qid = qid;
        NumQuakes++;
    }
    fclose(f);
}

/* --------------------------------------------------------------------
 * USER INTERFACE: MENUS AND EVENTS
 * -------------------------------------------------------------------- */
 /***********************************************************************
  *                     on_events_number_activate()                     *
  *             Shows a dialog to set how many quakes are drawn on      *
  *             the map, then redraws it.                               *
  *               Nothing.                                              *
  ***********************************************************************/

void on_events_number_activate(GtkWidget *widget, gpointer data) {
    GtkWidget *window = GTK_WIDGET(data);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Events number", GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_REJECT, NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(content_area), hbox, TRUE, TRUE, 15);
    GtkWidget *label = gtk_label_new("Number of events to display:");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

    GtkWidget *spin = gtk_spin_button_new_with_range(1, MAX_QUAKES, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), g_max_events_display);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 5);
    
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        g_max_events_display = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        if (map_canvas) gtk_widget_queue_draw(map_canvas);
    }
    gtk_widget_destroy(dialog);
}

 /***********************************************************************
  *                           on_map_scroll()                           *
  *             GTK handler for the mouse wheel: zooms the map in/out   *
  *             keeping the zoom within its limits, and redraws.        *
  *               TRUE (event consumed).                                *
  ***********************************************************************/

static gboolean on_map_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    if (event->direction == GDK_SCROLL_UP) {
        g_map_zoom *= 1.2; 
    } else if (event->direction == GDK_SCROLL_DOWN) {
        g_map_zoom /= 1.2; 
    }
    
    if (g_map_zoom < 0.2) g_map_zoom = 0.2;
    if (g_map_zoom > 100.0) g_map_zoom = 100.0;
    
    gtk_widget_queue_draw(widget);
    return TRUE;
}

 /***********************************************************************
  *                        on_map_button_press()                        *
  *             GTK handler for the left button press: starts a drag to *
  *             pan the map.                                            *
  *               TRUE (event consumed).                                *
  ***********************************************************************/

static gboolean on_map_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == 1) { 
        g_is_dragging = TRUE;
        g_last_mouse_x = event->x;
        g_last_mouse_y = event->y;
    }
    return TRUE;
}

 /***********************************************************************
  *                       on_map_button_release()                       *
  *             GTK handler for the left button release: ends the drag. *
  *               TRUE (event consumed).                                *
  ***********************************************************************/

static gboolean on_map_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == 1) { 
        g_is_dragging = FALSE;
    }
    return TRUE;
}

 /***********************************************************************
  *                           on_map_motion()                           *
  *             GTK handler for mouse motion: pans the map while        *
  *             dragging, and redraws.                                  *
  *               TRUE (event consumed).                                *
  ***********************************************************************/

static gboolean on_map_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    if (g_is_dragging) {
        double dx = event->x - g_last_mouse_x;
        double dy = event->y - g_last_mouse_y;
        g_map_pan_x += dx;
        g_map_pan_y += dy;
        g_last_mouse_x = event->x;
        g_last_mouse_y = event->y;
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

/* --------------------------------------------------------------------
 * WATCHDOG FUNCTION: Refreshes the catalog if the disk file changes
 * -------------------------------------------------------------------- */
 /***********************************************************************
  *                      check_history_file_loop()                      *
  *             Periodic watchdog: reloads the quake history and        *
  *             redraws the map when the history file changes.          *
  *               TRUE always (keeps the timer alive).                  *
  ***********************************************************************/

static gboolean check_history_file_loop(gpointer data) {
    struct stat file_stat;
    
    if (stat(QuakeFile, &file_stat) == 0) {
        if (file_stat.st_mtime != last_file_mod_time) {
            last_file_mod_time = file_stat.st_mtime;
            
            LoadQuakeHistory();
            
            if (map_canvas) {
                gtk_widget_queue_draw(map_canvas);
            }
        }
    }
    return TRUE; 
}

/* --------------------------------------------------------------------
 * MAP CANVAS (Equirectangular Projection + Dynamic Rendering)
 * -------------------------------------------------------------------- */
 /***********************************************************************
  *                            on_draw_map()                            *
  *             Cairo draw handler: renders the base map with the grid, *
  *             the station triangles colored by elevation and the quake*
  *             circles scaled by magnitude, plus the legends.          *
  *               FALSE always.                                         *
  ***********************************************************************/

static gboolean on_draw_map(GtkWidget *widget, cairo_t *cr, gpointer data) {
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);

    if (g_map_pixbuf != NULL) {
        double img_w = gdk_pixbuf_get_width(g_map_pixbuf);
        double img_h = gdk_pixbuf_get_height(g_map_pixbuf);
        
        /* Determine the virtual center of the screen */
        double screen_cx = width / 2.0 + g_map_pan_x;
        double screen_cy = height / 2.0 + g_map_pan_y;

        /* Default focus on Chile if the image is the whole world map (-70.0 Lon, -30.0 Lat) */
        double center_x = img_w * (-70.0 + 180.0) / 360.0;
        double center_y = img_h * (90.0 - (-30.0)) / 180.0;

        cairo_save(cr);
        
        /* 1. Move the origin to the window center and apply Pan and Global Zoom */
        cairo_translate(cr, screen_cx, screen_cy);
        cairo_scale(cr, g_map_zoom, g_map_zoom);
        cairo_translate(cr, -center_x, -center_y);

        /* 2. Draw Base Image */
        gdk_cairo_set_source_pixbuf(cr, g_map_pixbuf, 0, 0);
        cairo_paint(cr);
        
        /* 3. Draw Grid (Latitude / Longitude) */
        double step = 10.0; 
        if (g_map_zoom > 30.0) step = 1.0;
        else if (g_map_zoom > 15.0) step = 2.0;
        else if (g_map_zoom > 5.0) step = 5.0;

        cairo_new_path(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3); /* Semi-transparent white */
        cairo_set_line_width(cr, 1.0 / g_map_zoom);    /* Thickness invariant to zoom */

        for (double lon = -180.0; lon <= 180.0; lon += step) {
            double x = img_w * (lon + 180.0) / 360.0;
            cairo_move_to(cr, x, 0); cairo_line_to(cr, x, img_h);
        }
        for (double lat = -90.0; lat <= 90.0; lat += step) {
            double y = img_h * (90.0 - lat) / 180.0;
            cairo_move_to(cr, 0, y); cairo_line_to(cr, img_w, y);
        }
        cairo_stroke(cr);

        /* 4. Draw Stations with Dynamic Elevation Color */
        cairo_set_line_width(cr, 1.0 / g_map_zoom); 
        
        /* USE OF THE GLOBAL SIZE CONFIGURATION VARIABLE */
        double s_size = g_station_size / g_map_zoom; /* Triangle size adjusted to the zoom */

        for(int i = 0; i < NumEstaciones; i++) {
            if (StaArray[i].lat != 0.0 && StaArray[i].lon != 0.0) {
                double x = img_w * (StaArray[i].lon + 180.0) / 360.0;
                double y = img_h * (90.0 - StaArray[i].lat) / 180.0;

                /* Compute the elevation ratio for the color palette [0.0 = low, 1.0 = high] */
                double elev_ratio = (StaArray[i].elev_m - g_min_elev) / (g_max_elev - g_min_elev);
                if (elev_ratio < 0.0) elev_ratio = 0.0;
                if (elev_ratio > 1.0) elev_ratio = 1.0;

                /* Gradient: Light Green (Low elevation) to Dark Green (High elevation) */
                double r = 0.6 - (0.6 * elev_ratio); 
                double g = 1.0 - (0.7 * elev_ratio); 
                double b = 0.6 - (0.6 * elev_ratio); 

                cairo_new_path(cr); /* Lift the pen, avoids anomalous lines */
                cairo_set_source_rgb(cr, r, g, b);
                cairo_move_to(cr, x, y - s_size);
                cairo_line_to(cr, x - s_size, y + s_size);
                cairo_line_to(cr, x + s_size, y + s_size);
                cairo_close_path(cr);
                cairo_fill_preserve(cr);
                
                cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); /* Black border to distinguish */
                cairo_stroke(cr);
            }
        }

        /* 5. Draw Quakes (Red Circles with Controlled Linear Scaling) */
        int events_to_draw = (NumQuakes > g_max_events_display) ? g_max_events_display : NumQuakes;
        
        for(int i = events_to_draw - 1; i >= 0; i--) { /* We paint from back to front (the newest on top) */
            double x = img_w * (QuakeList[i].lon + 180.0) / 360.0;
            double y = img_h * (90.0 - QuakeList[i].lat) / 180.0;

            double mag = QuakeList[i].mag;
            if (mag < 0.0) mag = 1.0; 
            
            /* Adjusted Linear Scaling: Reduces overlap but keeps obvious differences */
            double radius = (mag * 2.0) / g_map_zoom;

            cairo_new_path(cr); /* Lift the pen between circles */

            if (i == 0) {
                /* Last Event: Bright red (100% Opacity), Thick black border */
                cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 1.0); 
                cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
                cairo_fill_preserve(cr);
                
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0); 
                cairo_set_line_width(cr, 2.0 / g_map_zoom);
                cairo_stroke(cr);
            } else {
                /* Historical Events: Subtle red (50% Opacity), Intermediate dark border */
                cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 0.50); 
                cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
                cairo_fill_preserve(cr);
                
                cairo_set_source_rgba(cr, 0.4, 0.0, 0.0, 0.6); 
                cairo_set_line_width(cr, 1.0 / g_map_zoom);
                cairo_stroke(cr);
            }
        }

        cairo_restore(cr); /* Restore the matrix to absolute pixels to draw UI (Legends) */
        
        /* --- LEGENDS AND VERTICAL SCALES (FIXED UI) --- */
        cairo_new_path(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85); /* Expanded semi-transparent box */
        cairo_rectangle(cr, 10, height - 370, 120, 360);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        
        cairo_new_path(cr);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, 20, height - 345);
        cairo_show_text(cr, "Magnitude");

        /* Circle Scale for Magnitudes (Vertical Layout) */
        double mag_base_x = 40;
        double mag_base_y = height - 310;
        for (int m = 4; m <= 8; m++) {
            double r = m * 2.0; /* Coherent with the adjusted map multiplier */
            
            cairo_new_path(cr);
            cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 0.6); 
            cairo_arc(cr, mag_base_x, mag_base_y, r, 0, 2 * M_PI);
            cairo_fill_preserve(cr);
            
            cairo_set_source_rgba(cr, 0.4, 0.0, 0.0, 0.8);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
            
            cairo_new_path(cr);
            char m_lbl[8];
            snprintf(m_lbl, sizeof(m_lbl), "M%d", m);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_font_size(cr, 12.0);
            cairo_move_to(cr, mag_base_x + 25, mag_base_y + 4);
            cairo_show_text(cr, m_lbl);
            
            mag_base_y += 35; /* Readjusted vertical spacing */
        }

        /* Color Scale for Elevation (Green Triangle + Text) */
        double t_x = 25.0;
        double t_y = height - 120.0;
        
        /* FIX: We link the legend triangle to the size of the global parameter */
        double leg_s_size = g_station_size; 

        cairo_new_path(cr);
        cairo_set_source_rgb(cr, 0.0, 0.8, 0.0); /* Representative green */
        cairo_move_to(cr, t_x, t_y - leg_s_size);
        cairo_line_to(cr, t_x - leg_s_size, t_y + leg_s_size);
        cairo_line_to(cr, t_x + leg_s_size, t_y + leg_s_size);
        cairo_close_path(cr);
        cairo_fill_preserve(cr);
        
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        cairo_new_path(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, t_x + 12.0, t_y + 4.0);
        cairo_show_text(cr, "Elevation (m)");

        cairo_pattern_t *pat = cairo_pattern_create_linear(0, height - 20, 0, height - 100);
        cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.6, 1.0, 0.6); /* Light Green (Low) */
        cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.0, 0.3, 0.0); /* Dark Green (High) */
        
        cairo_new_path(cr);
        cairo_rectangle(cr, 25, height - 100, 20, 80);
        cairo_set_source(cr, pat);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
        
        cairo_new_path(cr);
        cairo_rectangle(cr, 25, height - 100, 20, 80);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        /* Round the elevation scale values for visual cleanliness */
        int min_val = (int)(floor(g_min_elev / 100.0) * 100);
        if (min_val < 0 && g_min_elev >= 0.0) min_val = 0; 
        int max_val = (int)(ceil(g_max_elev / 100.0) * 100);

        char min_el[32], max_el[32];
        snprintf(min_el, sizeof(min_el), "%d", min_val);
        snprintf(max_el, sizeof(max_el), "%d", max_val);
        
        cairo_new_path(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_font_size(cr, 14.0); /* Larger elevation numbers */
        
        cairo_move_to(cr, 55, height - 20);
        cairo_show_text(cr, min_el);
        
        cairo_move_to(cr, 55, height - 90);
        cairo_show_text(cr, max_el);

    } else {
        /* Fallback: If the JPG file does not exist, draw a gray/bluish background */
        cairo_set_source_rgb(cr, 0.85, 0.92, 0.98); 
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, width / 2 - 120, height / 2);
        cairo_show_text(cr, "Missing map file (Equirectangular)");
    }

    /* Perimeter frame */
    cairo_new_path(cr);
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_set_line_width(cr, 3.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_stroke(cr);

    return FALSE;
}

 /***********************************************************************
  *                               main()                                *
  *             Entry point: reads the config, connects to Earthworm,   *
  *             loads the stations and quake history, builds the GTK    *
  *             interface and runs the main loop.                       *
  *               0 on clean exit.                                      *
  ***********************************************************************/

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <configfile.d>\n", argv[0]);
        exit(1);
    }
    if (ReadConfig(argv[1]) != 0) exit(1);

    setenv("TZ", "GMT", 1);
    tzset();
    logit_init(argv[1], 0, 1024, LogFile);
    MyPid = getpid();
    
    gtk_init(&argc, &argv);
    setlocale(LC_NUMERIC, "C");

    ConnectToEarthworm();
    
    /* 1. Load Stations (Extract Lat/Lon and Elevation limits) */
    LoadStations();
    
    /* 2. Initial Load of the Quake History */
    LoadQuakeHistory();
    
    /* 3. Load the base map image into memory */
    GError *err = NULL;
    g_map_pixbuf = gdk_pixbuf_new_from_file(MapImageFile, &err);
    if (!g_map_pixbuf) {
        printf(">> [WARNING] Could not load %s: %s\n", MapImageFile, err->message);
        g_error_free(err);
    }

    /* --- GTK WINDOW CREATION --- */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Network & Seismicity Viewer");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox_main);

    /* --- MENU BAR --- */
    GtkWidget *menu_bar = gtk_menu_bar_new();
    
    GtkWidget *ctrl_panel_item = gtk_menu_item_new_with_label("Control Panel"); 
    GtkWidget *ctrl_panel_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(ctrl_panel_item), ctrl_panel_menu);
    
    GtkWidget *events_number_item = gtk_menu_item_new_with_label("Events number");
    g_signal_connect(events_number_item, "activate", G_CALLBACK(on_events_number_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(ctrl_panel_menu), events_number_item);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), ctrl_panel_item); 
    gtk_box_pack_start(GTK_BOX(vbox_main), menu_bar, FALSE, FALSE, 0);

    /* --- MAP CANVAS --- */
    map_canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(map_canvas, -1, -1); 
    
    /* BIND MOUSE EVENTS */
    gtk_widget_add_events(map_canvas, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(G_OBJECT(map_canvas), "scroll-event", G_CALLBACK(on_map_scroll), NULL);
    g_signal_connect(G_OBJECT(map_canvas), "button-press-event", G_CALLBACK(on_map_button_press), NULL);
    g_signal_connect(G_OBJECT(map_canvas), "button-release-event", G_CALLBACK(on_map_button_release), NULL);
    g_signal_connect(G_OBJECT(map_canvas), "motion-notify-event", G_CALLBACK(on_map_motion), NULL);
    
    g_signal_connect(G_OBJECT(map_canvas), "draw", G_CALLBACK(on_draw_map), NULL);
    
    gtk_box_pack_start(GTK_BOX(vbox_main), map_canvas, TRUE, TRUE, 0);

    /* --- BACKGROUND TASK TIMERS --- */
    g_timeout_add(2000, check_history_file_loop, NULL);  /* Checks quakes every 2s */
    g_timeout_add(1000, ew_background_tasks, NULL);      /* EW heartbeats every 1s */

    gtk_widget_show_all(window);
    gtk_main();

    tport_detach(&Region);
    if (g_map_pixbuf) g_object_unref(g_map_pixbuf);
    if (StaArray) free(StaArray);
    
    return 0;
}
