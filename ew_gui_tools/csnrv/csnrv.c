#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <adwaita.h>
#include "ewgui/ring.h"
#include "ewgui/geo.h"
#include "ewgui/view.h"
#include "ewgui/actions.h"
#include "ewgui/export.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <locale.h>
#include <math.h>

#include <earthworm.h>
#include <transport.h>
#include <kom.h>
#include <rw_mag.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_STR 256

/* Limites y default del zoom del mapa (compartidos por scroll y configuracion) */
#define ZOOM_MIN     0.2
#define ZOOM_MAX     50.0
#define ZOOM_DEFAULT 8.0

/* --- Variables de Configuracion (Leidas desde csnrv.d) --- */
char MyModName[MAX_STR];
char RingName[MAX_STR];
char QuakeFile[MAX_STR];
char MapImageFile[MAX_STR];
int HeartBeatInt;
int LogFile;
double InitialZoom = ZOOM_DEFAULT;   /* Clave opcional InitialZoom de csnrv.d */

/* --- Variables Globales Earthworm --- */
SHM_INFO Region;
pid_t MyPid;
unsigned char MyInstId, MyModId, TypeHeartBeat, TypeError, TypeHyp2000Arc, TypeMagnitude;
MSG_LOGO HypoLogo;   /* filtro de lectura de HYPO_RING (TYPE_HYP2000ARC) */
MSG_LOGO MagLogo;    /* filtro de lectura de HYPO_RING (TYPE_MAGNITUDE) */

/* --- Magnitudes del evento vigente (por qid) --- */
typedef struct {
    int    qid;
    double ml, mwp, mb, ms;
    int    ml_stn, mwp_stn, mb_stn, ms_stn;
} MagState;
static MagState g_mag = { 0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0, 0 };
static gboolean g_ui_ready = FALSE;   /* FALSE en modo headless: no tocar widgets */

/* --- Variables del Visor --- */
time_t last_file_mod_time = 0;
gboolean has_valid_data = FALSE;
double g_origin_time = 0.0;
double g_epicenter_lat = 0.0;
double g_epicenter_lon = 0.0;
GdkPixbuf *g_map_pixbuf = NULL;

/* Variables de Control de Zoom y Paneo del Mapa */
double g_map_zoom = ZOOM_DEFAULT;
double g_map_pan_x = 0.0;
double g_map_pan_y = 0.0;
gboolean g_is_dragging = FALSE;
double g_last_mouse_x = 0.0;
double g_last_mouse_y = 0.0;

/* Widgets de la Interfaz */
GtkWidget *lbl_origin_time;
GtkWidget *lbl_coordinates;
GtkWidget *lbl_depth;
GtkWidget *lbl_time_elapsed;
EwGuiCanvas *map_canvas;

/* App de la ventana, para pedir la salida desde el timer de Earthworm. */
static GApplication *g_app = NULL;

/* Widgets para la Tabla de Magnitudes */
GtkWidget *lbl_pref_type, *lbl_pref_val, *lbl_pref_stn;
GtkWidget *lbl_mwp_val, *lbl_mwp_stn;
GtkWidget *lbl_ml_val, *lbl_ml_stn;
GtkWidget *lbl_mb_val, *lbl_mb_stn;
GtkWidget *lbl_ms_val, *lbl_ms_stn;

/* --------------------------------------------------------------------
 * FUNCIONES EARTHWORM
 * -------------------------------------------------------------------- */
int ReadConfig(char *configfile) {
    int ncommand = 5, nmiss = 0, i;
    char init[10] = {0};
    char *com, *str;

    if (!k_open(configfile)) {
        fprintf(stderr, "csnrv: Error abriendo archivo config <%s>\n", configfile);
        return -1;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if (k_its("MyModuleId")) {
            str = k_str();
            if (str) strcpy(MyModName, str);
            init[0] = 1;
        } else if (k_its("RingName")) {
            str = k_str();
            if (str) strcpy(RingName, str);
            init[1] = 1;
        } else if (k_its("HeartBeatInt")) {
            HeartBeatInt = k_int();
            init[2] = 1;
        } else if (k_its("LogFile")) {
            LogFile = k_int();
            init[3] = 1;
        } else if (k_its("QuakeFile")) {
            /* Clave OPCIONAL (obsoleta): el display viene del anillo. */
            str = k_str();
            if (str) strcpy(QuakeFile, str);
        } else if (k_its("MapImageFile")) {
            str = k_str();
            if (str) strcpy(MapImageFile, str);
            init[4] = 1;
        } else if (k_its("InitialZoom")) {
            /* Clave OPCIONAL: si falta se conserva ZOOM_DEFAULT (comportamiento previo).
             * NO cuenta en ncommand/nmiss. */
            InitialZoom = k_val();
            if (InitialZoom < ZOOM_MIN) {
                fprintf(stderr, "csnrv: InitialZoom %.3f fuera de rango, se satura a %.1f\n",
                        InitialZoom, ZOOM_MIN);
                InitialZoom = ZOOM_MIN;
            } else if (InitialZoom > ZOOM_MAX) {
                fprintf(stderr, "csnrv: InitialZoom %.3f fuera de rango, se satura a %.1f\n",
                        InitialZoom, ZOOM_MAX);
                InitialZoom = ZOOM_MAX;
            }
        } else {
            continue;
        }
        if (k_err()) {
            fprintf(stderr, "csnrv: Error parseando <%s> en <%s>\n", com, configfile);
            return -1;
        }
    }
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close();
    if (nmiss > 0) {
        fprintf(stderr, "csnrv: ERROR, faltan parametros en <%s>\n", configfile);
        return -1;
    }
    return 0;
}

void Lookup(void) {
    if (GetLocalInst(&MyInstId) != 0) { fprintf(stderr, "Falla en GetLocalInst.\n"); exit(-1); }
    if (GetModId(MyModName, &MyModId) != 0) { fprintf(stderr, "Falla en GetModId (%s).\n", MyModName); exit(-1); }
    if (GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0) { fprintf(stderr, "Falta TYPE_HEARTBEAT.\n"); exit(-1); }
    if (GetType("TYPE_ERROR", &TypeError) != 0) { fprintf(stderr, "Falta TYPE_ERROR.\n"); exit(-1); }
    if (GetType("TYPE_HYP2000ARC", &TypeHyp2000Arc) != 0) { fprintf(stderr, "Falta TYPE_HYP2000ARC.\n"); exit(-1); }
    if (GetType("TYPE_MAGNITUDE", &TypeMagnitude) != 0) { fprintf(stderr, "Falta TYPE_MAGNITUDE.\n"); exit(-1); }
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
        sprintf(msg, "%ld %d\n", (long) t, MyPid);
    } else if (type == TypeError) {
        sprintf(msg, "%ld %hd %s\n", (long) t, ierr, note);
        logit("et", "csnrv: Error: %s\n", note);
    }

    size = strlen(msg);
    tport_putmsg(&Region, &logo, size, msg);
}

gboolean ew_background_tasks(gpointer user_data) {
    static EwGuiHeartbeat hb = {0};
    time_t timeNow;
    time(&timeNow);
    
    if (ewgui_heartbeat_due(&hb, (double)timeNow, HeartBeatInt)) {
        Status(TypeHeartBeat, 0, "");
    }

    if (ewgui_ring_should_quit(&Region, MyPid)) {
        logit("t", "csnrv: Senal de terminacion recibida. Cerrando...\n");
        if (g_app) g_application_quit(g_app);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* --------------------------------------------------------------------
 * FUNCION AUXILIAR PARA ACTUALIZAR LA TABLA DE MAGNITUDES
 * -------------------------------------------------------------------- */
void update_mag_row(GtkWidget *lbl_val, GtkWidget *lbl_stn, double mag, int stn, gboolean is_pref, const char* pref_type) {
    char buf[128];
    if (mag > 0.0) {
        if (is_pref) {
            snprintf(buf, sizeof(buf), "<span size='x-large' weight='bold' foreground='red'>%.1f</span>", mag);
            gtk_label_set_markup(GTK_LABEL(lbl_val), buf);
            snprintf(buf, sizeof(buf), "<span size='large' weight='bold' foreground='red'>%d</span>", stn);
            gtk_label_set_markup(GTK_LABEL(lbl_stn), buf);
            snprintf(buf, sizeof(buf), "<span size='large' weight='bold'>Preferred: %s</span>", pref_type);
            gtk_label_set_markup(GTK_LABEL(lbl_pref_type), buf);
        } else {
            snprintf(buf, sizeof(buf), "<span size='large'>%.1f</span>", mag);
            gtk_label_set_markup(GTK_LABEL(lbl_val), buf);
            snprintf(buf, sizeof(buf), "<span size='large'>%d</span>", stn);
            gtk_label_set_markup(GTK_LABEL(lbl_stn), buf);
        }
    } else {
        gtk_label_set_markup(GTK_LABEL(lbl_val), "<span foreground='gray'>--</span>");
        gtk_label_set_markup(GTK_LABEL(lbl_stn), "<span foreground='gray'>--</span>");
    }
}

/* --------------------------------------------------------------------
 * EVENTOS DEL RATON (ZOOM IN/OUT Y PANEO EN EL MAPA)
 * -------------------------------------------------------------------- */
static void on_map_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data) {
    (void)ctrl; (void)dx; (void)data;
    g_map_zoom = ewgui_geo_zoom_step(g_map_zoom, dy < 0.0, ZOOM_MIN, ZOOM_MAX);
    ewgui_canvas_queue_draw(map_canvas);
}

static void on_map_button_press(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)n_press; (void)data;
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == 1) {
        g_is_dragging = TRUE;
        g_last_mouse_x = x;
        g_last_mouse_y = y;
    }
}

static void on_map_button_release(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)n_press; (void)x; (void)y; (void)data;
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == 1)
        g_is_dragging = FALSE;
}

static void on_map_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer data) {
    (void)ctrl; (void)data;
    if (g_is_dragging) {
        double dx = x - g_last_mouse_x;
        double dy = y - g_last_mouse_y;
        g_map_pan_x += dx;
        g_map_pan_y += dy;
        g_last_mouse_x = x;
        g_last_mouse_y = y;
        ewgui_canvas_queue_draw(map_canvas);
    }
}

/* --------------------------------------------------------------------
 * MAGNITUDES: parseo de TYPE_MAGNITUDE y seleccion de la preferida.
 *
 * csnmags_toy publica TYPE_MAGNITUDE al mismo HYPO_RING. Se correlaciona
 * por qid con el evento vigente y se muestran ML y Mwp; la preferida se
 * destaca en rojo/negrita (regla: Mwp si >=5.5 con >=3 estaciones; si no
 * Ml si >0; si no Mwp si >0).
 * -------------------------------------------------------------------- */
static void mag_preferida(double *mag, int *stn, const char **type) {
    if (g_mag.mwp >= 5.5 && g_mag.mwp_stn >= 3) {
        *mag = g_mag.mwp; *stn = g_mag.mwp_stn; *type = "Mwp";
    } else if (g_mag.ms > 0.0 && g_mag.ms_stn >= 3) {
        *mag = g_mag.ms; *stn = g_mag.ms_stn; *type = "Ms";
    } else if (g_mag.mb > 0.0 && g_mag.mb_stn >= 3) {
        *mag = g_mag.mb; *stn = g_mag.mb_stn; *type = "Mb";
    } else if (g_mag.ml > 0.0) {
        *mag = g_mag.ml; *stn = g_mag.ml_stn; *type = "Ml";
    } else if (g_mag.mwp > 0.0) {
        *mag = g_mag.mwp; *stn = g_mag.mwp_stn; *type = "Mwp";
    } else {
        *mag = 0.0; *stn = 0; *type = "--";
    }
}

static void aplicar_magnitudes(void) {
    double pref_mag = 0.0;
    int    pref_stn = 0;
    const char *pref_type = "--";

    mag_preferida(&pref_mag, &pref_stn, &pref_type);

    if (!g_ui_ready) return;   /* modo headless: solo actualizar el estado */

    update_mag_row(lbl_pref_val, lbl_pref_stn, pref_mag, pref_stn, TRUE, pref_type);
    update_mag_row(lbl_ml_val, lbl_ml_stn, g_mag.ml, g_mag.ml_stn, FALSE, "");
    update_mag_row(lbl_mb_val, lbl_mb_stn, g_mag.mb, g_mag.mb_stn, FALSE, "");
    update_mag_row(lbl_ms_val, lbl_ms_stn, g_mag.ms, g_mag.ms_stn, FALSE, "");
    update_mag_row(lbl_mwp_val, lbl_mwp_stn, g_mag.mwp, g_mag.mwp_stn, FALSE, "");
}

static void procesar_mensaje_mag(const char *msg, long recsize, int current_qid) {
    MAG_INFO mag;
    int qid;

    if (recsize <= 0) return;
    /* rd_mag lee pMagAux/size_aux ANTES de su memset interno: hay que
       inicializar la estructura para no usar punteros basura. */
    setlocale(LC_NUMERIC, "C");
    memset(&mag, 0, sizeof(mag));
    if (rd_mag((char *)msg, (int)recsize, &mag) != 0) return;

    qid = atoi(mag.qid);
    /* Magnitud de un evento que ya no es el vigente: ignorar. */
    if (qid == 0 || qid != current_qid) return;

    if (strcmp(mag.szmagtype, "ML") == 0) {
        g_mag.ml = mag.mag;
        g_mag.ml_stn = mag.nstations;
    } else if (strcmp(mag.szmagtype, "Mb") == 0 || strcmp(mag.szmagtype, "MB") == 0) {
        g_mag.mb = mag.mag;
        g_mag.mb_stn = mag.nstations;
    } else if (strcmp(mag.szmagtype, "Ms") == 0 || strcmp(mag.szmagtype, "MS") == 0) {
        g_mag.ms = mag.mag;
        g_mag.ms_stn = mag.nstations;
    } else if (strcmp(mag.szmagtype, "Mwp") == 0) {
        g_mag.mwp = mag.mag;
        g_mag.mwp_stn = mag.nstations;
    } else {
        return;
    }
    aplicar_magnitudes();
}

/* --------------------------------------------------------------------
 * FUNCION VIGIA: consumo de HYPO_RING (fuente primaria en tiempo real).
 *
 * Drena el anillo y se queda con la solucion de mayor version por qid.
 * El archivo de estado de csnloc solo se usa al arrancar (recuperacion).
 * -------------------------------------------------------------------- */
static gboolean update_summary_loop(gpointer data) {
    static int last_processed_qid = 0;
    static int last_qver = -1;
    MSG_LOGO reclogo;
    MSG_LOGO logos[2];
    long     recsize;
    char     msg[65536];
    int      res;

    logos[0] = HypoLogo;
    logos[1] = MagLogo;

    /* Drenar todos los mensajes disponibles del anillo (ARC o magnitud). */
    while ((res = tport_getmsg(&Region, logos, 2, &reclogo, &recsize,
                               msg, sizeof(msg) - 1)) == GET_OK) {
        char str[32];
        double otime, lat, lon, depth;
        int    qid, qver;

        if (recsize <= 0 || recsize >= (long)sizeof(msg) - 1) continue;
        msg[recsize] = '\0';

        /* Magnitudes: se procesan contra el evento vigente y no tocan el
           hipocentro. */
        if (reclogo.type == TypeMagnitude) {
            procesar_mensaje_mag(msg, recsize, last_processed_qid);
            continue;
        }
        if (reclogo.type != TypeHyp2000Arc) continue;

        /* Parseo minimo del ARC: t0, lat, lon, depth, qid, eventVersion. */
        strncpy(str, msg, 14); str[14] = '\0';
        {
            struct tm t; memset(&t, 0, sizeof(t));
            char tmp[8];
            strncpy(tmp, str, 4); tmp[4] = '\0'; t.tm_year = atoi(tmp) - 1900;
            strncpy(tmp, str + 4, 2); tmp[2] = '\0'; t.tm_mon = atoi(tmp) - 1;
            strncpy(tmp, str + 6, 2); tmp[2] = '\0'; t.tm_mday = atoi(tmp);
            strncpy(tmp, str + 8, 2); tmp[2] = '\0'; t.tm_hour = atoi(tmp);
            strncpy(tmp, str + 10, 2); tmp[2] = '\0'; t.tm_min = atoi(tmp);
            strncpy(tmp, str + 12, 2); tmp[2] = '\0'; t.tm_sec = atoi(tmp);
            setenv("TZ", "GMT", 1); tzset();
            otime = (double)mktime(&t);
        }
        strncpy(str, msg + 14, 2); str[2] = '\0'; otime += atof(str) / 100.0;

        strncpy(str, msg + 16, 2); str[2] = '\0'; lat = atof(str);
        { char dir = msg[18]; strncpy(str, msg + 19, 4); str[4] = '\0';
          lat += (atof(str) / 100.0) / 60.0; if (dir == 'S') lat = -lat; }
        strncpy(str, msg + 23, 3); str[3] = '\0'; lon = atof(str);
        { char dir = msg[26]; strncpy(str, msg + 27, 4); str[4] = '\0';
          lon += (atof(str) / 100.0) / 60.0; if (dir == 'W') lon = -lon; }
        strncpy(str, msg + 31, 5); str[5] = '\0'; depth = atof(str) / 100.0;

        strncpy(str, msg + 136, 10); str[10] = '\0'; qid = atoi(str);
        strncpy(str, msg + 178, 4); str[4] = '\0'; qver = atoi(str);
        if (qver == 0) { strncpy(str, msg + 161, 1); str[1] = '\0'; qver = atoi(str); }

        /* Solo actualizar si es un evento nuevo o una version mas reciente. */
        if (qid == last_processed_qid && qver <= last_qver) continue;

        has_valid_data = TRUE;
        g_origin_time = otime;
        g_epicenter_lat = lat;
        g_epicenter_lon = lon;
        if (g_epicenter_lon > 180.0) g_epicenter_lon -= 360.0;

        if (qid != last_processed_qid) {
            g_map_zoom = InitialZoom;
            g_map_pan_x = 0.0;
            g_map_pan_y = 0.0;
            last_processed_qid = qid;
            /* Evento nuevo: limpiar magnitudes hasta que lleguen las suyas. */
            g_mag.qid = qid;
            g_mag.ml = g_mag.mwp = g_mag.mb = g_mag.ms = 0.0;
            g_mag.ml_stn = g_mag.mwp_stn = g_mag.mb_stn = g_mag.ms_stn = 0;
            aplicar_magnitudes();
        }
        last_qver = qver;

        {
            char buffer[512];
            time_t rawtime = (time_t)(otime + 0.5);
            struct tm *ptm = gmtime(&rawtime);
            if (ptm) {
                snprintf(buffer, sizeof(buffer),
                         "<span size='xx-large' weight='bold' foreground='#0055a4'>%02d:%02d:%02d UTC\n%02d/%02d/%04d</span>",
                         ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
                         ptm->tm_mday, ptm->tm_mon + 1, ptm->tm_year + 1900);
                gtk_label_set_markup(GTK_LABEL(lbl_origin_time), buffer);
            }
            snprintf(buffer, sizeof(buffer),
                     "<span size='large'>Lat: %.3f  Lon: %.3f</span>", lat, lon);
            gtk_label_set_markup(GTK_LABEL(lbl_coordinates), buffer);
            snprintf(buffer, sizeof(buffer),
                     "<span size='large'>Depth: %.0f km</span>", depth);
            gtk_label_set_markup(GTK_LABEL(lbl_depth), buffer);
        }

        /* Las magnitudes se actualizan al recibir TYPE_MAGNITUDE; aqui solo
           se refresca la vista con el estado vigente del evento. */
        aplicar_magnitudes();

        if (map_canvas) ewgui_canvas_queue_draw(map_canvas);
    }

    if (has_valid_data && g_origin_time > 0.0) {
        time_t current_time;
        time(&current_time);
        
        int diff_seconds = (int)difftime(current_time, (time_t)g_origin_time);
        if (diff_seconds < 0) diff_seconds = 0;
        
        int hours = diff_seconds / 3600;
        int minutes = (diff_seconds % 3600) / 60;
        int seconds = diff_seconds % 60;
        
        char clock_buf[256];
        snprintf(clock_buf, sizeof(clock_buf), "<span size='40000' weight='bold' foreground='blue'>%02d:%02d:%02d</span>", 
                hours, minutes, seconds);
        gtk_label_set_markup(GTK_LABEL(lbl_time_elapsed), clock_buf);
    } else {
        gtk_label_set_markup(GTK_LABEL(lbl_time_elapsed), "<span size='40000' weight='bold' foreground='gray'>--:--:--</span>");
    }

    return TRUE; 
}

/* --------------------------------------------------------------------
 * CANVAS DEL MAPA (Proyección Equirectangular)
 * -------------------------------------------------------------------- */
static void on_draw_map(EwGuiCanvas *canvas, cairo_t *cr, int width, int height, void *data) {
    (void)canvas; (void)data;

    if (g_map_pixbuf != NULL) {
        double img_w = gdk_pixbuf_get_width(g_map_pixbuf);
        double img_h = gdk_pixbuf_get_height(g_map_pixbuf);
        
        double epi_x = img_w / 2.0;
        double epi_y = img_h / 2.0;

        if (has_valid_data) {
            ewgui_geo_project(g_epicenter_lat, g_epicenter_lon, img_w, img_h, &epi_x, &epi_y);
        }

        double effective_zoom = has_valid_data ? g_map_zoom : fmin((double)width/img_w, (double)height/img_h);

        cairo_save(cr);
        
        cairo_translate(cr, width / 2.0 + g_map_pan_x, height / 2.0 + g_map_pan_y);
        cairo_scale(cr, effective_zoom, effective_zoom);
        cairo_translate(cr, -epi_x, -epi_y);

        gdk_cairo_set_source_pixbuf(cr, g_map_pixbuf, 0, 0);
        cairo_paint(cr);
        
        double step = ewgui_geo_grid_step(effective_zoom);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4);
        cairo_set_line_width(cr, 1.0 / effective_zoom); 

        for (double lon = -180.0; lon <= 180.0; lon += step) {
            double x; ewgui_geo_project(0.0, lon, img_w, img_h, &x, NULL);
            cairo_move_to(cr, x, 0); cairo_line_to(cr, x, img_h);
        }
        for (double lat = -90.0; lat <= 90.0; lat += step) {
            double y; ewgui_geo_project(lat, 0.0, img_w, img_h, NULL, &y);
            cairo_move_to(cr, 0, y); cairo_line_to(cr, img_w, y);
        }
        cairo_stroke(cr);
        
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85); 
        cairo_set_font_size(cr, 12.0 / effective_zoom); 
        
        double world_top_y = epi_y - (height / 2.0 + g_map_pan_y) / effective_zoom;
        double world_left_x = epi_x - (width / 2.0 + g_map_pan_x) / effective_zoom;
        
        if (world_top_y < 0) world_top_y = 0;
        if (world_left_x < 0) world_left_x = 0;

        char lbl[32];
        for (double lon = -180.0; lon <= 180.0; lon += step) {
            double x; ewgui_geo_project(0.0, lon, img_w, img_h, &x, NULL);
            cairo_move_to(cr, x + 4.0 / effective_zoom, world_top_y + 14.0 / effective_zoom);
            snprintf(lbl, sizeof(lbl), "%.0f\xC2\xB0", lon); cairo_show_text(cr, lbl);
        }
        for (double lat = -90.0; lat <= 90.0; lat += step) {
            double y; ewgui_geo_project(lat, 0.0, img_w, img_h, NULL, &y);
            cairo_move_to(cr, world_left_x + 4.0 / effective_zoom, y - 4.0 / effective_zoom);
            snprintf(lbl, sizeof(lbl), "%.0f\xC2\xB0", lat); cairo_show_text(cr, lbl);
        }
        cairo_restore(cr);
    } else {
        cairo_set_source_rgb(cr, 0.85, 0.92, 0.98); 
        cairo_rectangle(cr, 0, 0, width, height); cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 14); cairo_move_to(cr, width / 2 - 120, height / 2);
        cairo_show_text(cr, "Falta imagen de mapa");
    }

    if (has_valid_data) {
        double screen_epi_x = width / 2.0 + g_map_pan_x;
        double screen_epi_y = height / 2.0 + g_map_pan_y;

        cairo_new_path(cr); 
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0); 
        cairo_arc(cr, screen_epi_x, screen_epi_y, 6, 0, 2 * M_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
    }

    cairo_new_path(cr);
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3); cairo_set_line_width(cr, 3.0);
    cairo_rectangle(cr, 0, 0, width, height); cairo_stroke(cr);
}

/* Exporta el mapa a SVG/PDF/PNG (diálogo Guardar). */
static void act_export(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p;
    ewgui_export_dialog_run(map_canvas, GTK_WINDOW(ud));
}

/* Conmutador claro/oscuro (menú del headerbar) vía AdwStyleManager. */
static void on_toggle_dark(GSimpleAction *action, GVariant *param, gpointer user_data) {
    GVariant *st;
    gboolean active;
    (void)param; (void)user_data;
    st = g_action_get_state(G_ACTION(action));
    active = !g_variant_get_boolean(st);
    g_variant_unref(st);
    g_simple_action_set_state(action, g_variant_new_boolean(active));
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(),
        active ? ADW_COLOR_SCHEME_FORCE_DARK : ADW_COLOR_SCHEME_DEFAULT);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    /* GTK re-aplica setlocale(LC_ALL,"") al inicializar; rd_mag usa sscanf
     * (punto decimal), asi que restauramos C tras el arranque de GTK. */
    setlocale(LC_NUMERIC, "C");

    GtkWidget *window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "CSNrv - Report Viewer");
    gtk_window_set_default_size(GTK_WINDOW(window), 450, 780);

    /* Acciones + menú del headerbar (View -> Dark mode). */
    GSimpleActionGroup *actions = g_simple_action_group_new();
    GSimpleAction *dark = g_simple_action_new_stateful("dark-mode", NULL,
                                                       g_variant_new_boolean(FALSE));
    g_signal_connect(dark, "activate", G_CALLBACK(on_toggle_dark), NULL);
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(dark));
    ewgui_action_add(G_ACTION_MAP(actions), "export", NULL, act_export, window);
    gtk_widget_insert_action_group(window, "win", G_ACTION_GROUP(actions));

    EwMenuItem view_items[] = {
        { "Export view...", "win.export", NULL, 0 },
        { "Dark mode", "win.dark-mode", NULL, 0 },
    };
    EwMenuGroup view_groups[] = { { "View", view_items, 2 } };
    GMenuModel *menu_model = ewgui_menu_build(view_groups, 1);

    GtkWidget *header = adw_header_bar_new();
    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), menu_model);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_btn);
    g_object_unref(menu_model);

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);

    GtkWidget *vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox_main, 15);
    gtk_widget_set_margin_end(vbox_main, 15);
    gtk_widget_set_margin_top(vbox_main, 15);
    gtk_widget_set_margin_bottom(vbox_main, 15);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), vbox_main);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbar_view);

    lbl_origin_time = gtk_label_new("<span size='xx-large' weight='bold' foreground='gray'>Esperando Datos...</span>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_origin_time), TRUE);
    gtk_label_set_justify(GTK_LABEL(lbl_origin_time), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(vbox_main), lbl_origin_time);

    lbl_coordinates = gtk_label_new("");
    gtk_box_append(GTK_BOX(vbox_main), lbl_coordinates);

    lbl_depth = gtk_label_new("");
    gtk_box_append(GTK_BOX(vbox_main), lbl_depth);

    GtkWidget *separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(vbox_main), separator1);

    GtkWidget *lbl_timer_title = gtk_label_new("<span size='large'>Time Since Quake:</span>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_timer_title), TRUE);
    gtk_box_append(GTK_BOX(vbox_main), lbl_timer_title);

    lbl_time_elapsed = gtk_label_new("");
    gtk_box_append(GTK_BOX(vbox_main), lbl_time_elapsed);

    GtkWidget *separator2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(vbox_main), separator2);

    GtkWidget *mag_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(mag_grid), 40);
    gtk_grid_set_row_spacing(GTK_GRID(mag_grid), 5);
    gtk_widget_set_halign(mag_grid, GTK_ALIGN_CENTER);

    GtkWidget *h1 = gtk_label_new(""); gtk_label_set_markup(GTK_LABEL(h1), "<b>Type</b>");
    GtkWidget *h2 = gtk_label_new(""); gtk_label_set_markup(GTK_LABEL(h2), "<b>Magnitude</b>");
    GtkWidget *h3 = gtk_label_new(""); gtk_label_set_markup(GTK_LABEL(h3), "<b>Stations</b>");
    gtk_grid_attach(GTK_GRID(mag_grid), h1, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), h2, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), h3, 2, 0, 1, 1);

    lbl_pref_type = gtk_label_new("Preferred: --");
    lbl_pref_val = gtk_label_new("--");
    lbl_pref_stn = gtk_label_new("--");
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_pref_type, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_pref_val, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_pref_stn, 2, 1, 1, 1);

    GtkWidget *l_ml = gtk_label_new(""); gtk_label_set_markup(GTK_LABEL(l_ml), "<span size='large'>Ml</span>");
    lbl_ml_val = gtk_label_new("--"); lbl_ml_stn = gtk_label_new("--");
    gtk_grid_attach(GTK_GRID(mag_grid), l_ml, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_ml_val, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_ml_stn, 2, 2, 1, 1);

    GtkWidget *l_mwp = gtk_label_new(""); gtk_label_set_markup(GTK_LABEL(l_mwp), "<span size='large'>Mwp</span>");
    lbl_mwp_val = gtk_label_new("--"); lbl_mwp_stn = gtk_label_new("--");
    gtk_grid_attach(GTK_GRID(mag_grid), l_mwp, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_mwp_val, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_mwp_stn, 2, 3, 1, 1);

    GtkWidget *l_mb = gtk_label_new(""); gtk_label_set_markup(GTK_LABEL(l_mb), "<span size='large'>Mb</span>");
    lbl_mb_val = gtk_label_new("--"); lbl_mb_stn = gtk_label_new("--");
    gtk_grid_attach(GTK_GRID(mag_grid), l_mb, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_mb_val, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_mb_stn, 2, 4, 1, 1);

    GtkWidget *l_ms = gtk_label_new(""); gtk_label_set_markup(GTK_LABEL(l_ms), "<span size='large'>Ms</span>");
    lbl_ms_val = gtk_label_new("--"); lbl_ms_stn = gtk_label_new("--");
    gtk_grid_attach(GTK_GRID(mag_grid), l_ms, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_ms_val, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(mag_grid), lbl_ms_stn, 2, 5, 1, 1);

    gtk_box_append(GTK_BOX(vbox_main), mag_grid);

    map_canvas = ewgui_canvas_new();
    ewgui_canvas_set_draw(map_canvas, on_draw_map, NULL);
    GtkWidget *map_widget = ewgui_canvas_widget(map_canvas);
    gtk_widget_set_size_request(map_widget, 400, 400);
    gtk_widget_set_vexpand(map_widget, TRUE);

    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_map_scroll), NULL);
    gtk_widget_add_controller(map_widget, scroll);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_map_button_press), NULL);
    g_signal_connect(click, "released", G_CALLBACK(on_map_button_release), NULL);
    gtk_widget_add_controller(map_widget, GTK_EVENT_CONTROLLER(click));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_map_motion), NULL);
    gtk_widget_add_controller(map_widget, motion);

    gtk_box_append(GTK_BOX(vbox_main), map_widget);

    /* Timers */
    g_timeout_add(1000, update_summary_loop, NULL);
    g_timeout_add(1000, ew_background_tasks, NULL);

    gtk_window_present(GTK_WINDOW(window));
    g_ui_ready = TRUE;
}

int main(int argc, char *argv[]) {
    /* Modo headless para tests: ejercita el parseo de TYPE_MAGNITUDE y la
     * regla de magnitud preferida, sin GTK ni Earthworm. */
    if (argc == 2 && strcmp(argv[1], "--test-mag") == 0) {
        MAG_INFO m;
        char buf[1024];
        double pm; int ps; const char *pt;
        int fails = 0;

        /* C3: parseo de una linea wr_mag real (ML). */
        memset(&m, 0, sizeof(m));
        strcpy(m.qid, "42"); m.imagtype = 1; strcpy(m.szmagtype, "ML");
        m.mag = 4.7; strcpy(m.algorithm, "CSNNet"); m.nstations = 5;
        m.nchannels = 5; m.error = 0.1; m.quality = 1.0; m.mindist = 0.0;
        m.azimuth = 0; strcpy(m.qauthor, "CL");
        if (wr_mag(&m, buf, sizeof(buf)) != 0) { printf("FAIL: wr_mag ML\n"); return 1; }
        g_mag.qid = 42; g_mag.ml = g_mag.mwp = 0.0; g_mag.ml_stn = g_mag.mwp_stn = 0;
        procesar_mensaje_mag(buf, (long)strlen(buf), 42);
        if (g_mag.ml != 4.7 || g_mag.ml_stn != 5) { printf("FAIL: C3 ML parse\n"); fails++; }
        else printf("ok  : C3 ML parse (%.1f, %d stn)\n", g_mag.ml, g_mag.ml_stn);

        /* C4: magnitud de qid ajeno se ignora. */
        memset(&m, 0, sizeof(m));
        strcpy(m.qid, "7"); m.imagtype = 1; strcpy(m.szmagtype, "Mwp");
        m.mag = 6.0; strcpy(m.algorithm, "CSNNet"); m.nstations = 4;
        if (wr_mag(&m, buf, sizeof(buf)) != 0) { printf("FAIL: wr_mag Mwp\n"); return 1; }
        procesar_mensaje_mag(buf, (long)strlen(buf), 42);
        if (g_mag.mwp != 0.0) { printf("FAIL: C4 qid ajeno\n"); fails++; }
        else printf("ok  : C4 qid ajeno ignorado\n");

        /* C5: regla de preferencia. */
        g_mag.ml = 4.7; g_mag.ml_stn = 5; g_mag.mwp = 6.0; g_mag.mwp_stn = 4;
        mag_preferida(&pm, &ps, &pt);
        if (strcmp(pt, "Mwp") != 0 || pm != 6.0) { printf("FAIL: C5 Mwp>=5.5\n"); fails++; }
        else printf("ok  : C5 Mwp preferida (%.1f)\n", pm);

        g_mag.mwp = 5.0; g_mag.mwp_stn = 4;   /* Mwp < 5.5 -> Ml */
        mag_preferida(&pm, &ps, &pt);
        if (strcmp(pt, "Ml") != 0 || pm != 4.7) { printf("FAIL: C5 Ml\n"); fails++; }
        else printf("ok  : C5 Ml preferida (%.1f)\n", pm);

        g_mag.ml = 0.0; g_mag.ml_stn = 0;     /* sin Ml -> Mwp */
        mag_preferida(&pm, &ps, &pt);
        if (strcmp(pt, "Mwp") != 0 || pm != 5.0) { printf("FAIL: C5 Mwp fallback\n"); fails++; }
        else printf("ok  : C5 Mwp fallback (%.1f)\n", pm);

        g_mag.mwp = 0.0; g_mag.mwp_stn = 0;   /* sin nada -> -- */
        mag_preferida(&pm, &ps, &pt);
        if (strcmp(pt, "--") != 0) { printf("FAIL: C5 sin magnitud\n"); fails++; }
        else printf("ok  : C5 sin magnitud -> --\n");

        /* C6: cambio de qid resetea magnitudes. */
        g_mag.ml = 4.7; g_mag.mwp = 6.0;
        g_mag.qid = 99; g_mag.ml = g_mag.mwp = 0.0; g_mag.ml_stn = g_mag.mwp_stn = 0;
        mag_preferida(&pm, &ps, &pt);
        if (strcmp(pt, "--") != 0) { printf("FAIL: C6 reset\n"); fails++; }
        else printf("ok  : C6 reset a --\n");

        /* C7: parseo de Mb y Ms. */
        g_mag.qid = 42; g_mag.ml = g_mag.mwp = g_mag.mb = g_mag.ms = 0.0;
        g_mag.ml_stn = g_mag.mwp_stn = g_mag.mb_stn = g_mag.ms_stn = 0;
        memset(&m, 0, sizeof(m));
        strcpy(m.qid, "42"); m.imagtype = 3; strcpy(m.szmagtype, "Mb");
        m.mag = 5.2; m.nstations = 6;
        wr_mag(&m, buf, sizeof(buf));
        procesar_mensaje_mag(buf, (long)strlen(buf), 42);
        memset(&m, 0, sizeof(m));
        strcpy(m.qid, "42"); m.imagtype = 4; strcpy(m.szmagtype, "Ms");
        m.mag = 5.6; m.nstations = 5;
        wr_mag(&m, buf, sizeof(buf));
        procesar_mensaje_mag(buf, (long)strlen(buf), 42);
        if (g_mag.mb != 5.2 || g_mag.mb_stn != 6 || g_mag.ms != 5.6 || g_mag.ms_stn != 5) {
            printf("FAIL: C7 Mb/Ms parse\n"); fails++;
        } else printf("ok  : C7 Mb/Ms parse (%.1f/%.1f)\n", g_mag.mb, g_mag.ms);

        /* C8: Ms preferida sobre Ml cuando n>=3. */
        g_mag.ml = 4.5; g_mag.ml_stn = 6; g_mag.mwp = 0.0; g_mag.mwp_stn = 0;
        mag_preferida(&pm, &ps, &pt);
        if (strcmp(pt, "Ms") != 0 || pm != 5.6) { printf("FAIL: C8 Ms prefiere\n"); fails++; }
        else printf("ok  : C8 Ms preferida (%.1f)\n", pm);

        /* C9: Mb preferida sobre Ml cuando no hay Ms. */
        g_mag.ms = 0.0; g_mag.ms_stn = 0;
        mag_preferida(&pm, &ps, &pt);
        if (strcmp(pt, "Mb") != 0 || pm != 5.2) { printf("FAIL: C9 Mb prefiere\n"); fails++; }
        else printf("ok  : C9 Mb preferida (%.1f)\n", pm);

        if (fails) { printf("\n%d FALLOS\n", fails); return 1; }
        printf("\nOK test_mag\n");
        return 0;
    }

    /* Modo headless para tests: valida la config y sale sin inicializar GTK
     * ni Earthworm. No requiere DISPLAY. */
    if (argc == 3 && strcmp(argv[1], "--print-config") == 0) {
        if (ReadConfig(argv[2]) != 0) {
            fprintf(stderr, "Error leyendo configuracion de %s\n", argv[2]);
            return 1;
        }
        printf("InitialZoom=%.6f\n", InitialZoom);
        printf("MapImageFile=%s\n", MapImageFile);
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <configfile.d>\n", argv[0]);
        exit(1);
    }

    if (ReadConfig(argv[1]) != 0) {
        fprintf(stderr, "Error leyendo configuracion de %s\n", argv[1]);
        exit(1);
    }

    g_map_zoom = InitialZoom;   /* Zoom inicial configurable (default ZOOM_DEFAULT) */

    setenv("TZ", "GMT", 1);
    tzset();
    
    logit_init(argv[1], 0, 1024, LogFile);
    MyPid = getpid();
    
    Lookup();

    long RingKey = GetKey(RingName);
    if (RingKey == -1) {
        logit("e", "csnrv: Anillo invalido <%s>\n", RingName);
        exit(-1);
    }
    tport_attach(&Region, RingKey);
    logit("t", "csnrv: Conectado a anillo %s\n", RingName);

    /* Filtro de lectura: TYPE_HYP2000ARC + TYPE_MAGNITUDE (mismo anillo). */
    HypoLogo.instid = 0;
    HypoLogo.mod    = 0;
    HypoLogo.type   = TypeHyp2000Arc;
    MagLogo.instid  = 0;
    MagLogo.mod     = 0;
    MagLogo.type    = TypeMagnitude;

    setlocale(LC_NUMERIC, "C");
    
    GError *err = NULL;
    g_map_pixbuf = gdk_pixbuf_new_from_file(MapImageFile, &err);
    if (!g_map_pixbuf) {
        logit("e", ">> [AVISO] No se pudo cargar %s: %s\n", MapImageFile, err->message);
        g_error_free(err);
    }

    AdwApplication *app = adw_application_new("cl.csn.csnrv", G_APPLICATION_NON_UNIQUE);
    g_app = G_APPLICATION(app);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    /* argv ya se interpreto arriba: no pasamos el <configfile.d> a GApplication. */
    int status = g_application_run(g_app, 0, NULL);

    tport_detach(&Region);
    if (g_map_pixbuf) g_object_unref(g_map_pixbuf);
    return status;
}
