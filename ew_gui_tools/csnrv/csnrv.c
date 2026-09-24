#define _GNU_SOURCE
#include <gtk/gtk.h>
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
unsigned char MyInstId, MyModId, TypeHeartBeat, TypeError;
time_t timeLastBeat = 0;

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
GtkWidget *map_canvas;

/* Widgets para la Tabla de Magnitudes */
GtkWidget *lbl_pref_type, *lbl_pref_val, *lbl_pref_stn;
GtkWidget *lbl_mwp_val, *lbl_mwp_stn;
GtkWidget *lbl_ml_val, *lbl_ml_stn;

/* --------------------------------------------------------------------
 * FUNCIONES EARTHWORM
 * -------------------------------------------------------------------- */
int ReadConfig(char *configfile) {
    int ncommand = 6, nmiss = 0, i;
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
            str = k_str();
            if (str) strcpy(QuakeFile, str);
            init[4] = 1;
        } else if (k_its("MapImageFile")) {
            str = k_str();
            if (str) strcpy(MapImageFile, str);
            init[5] = 1;
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
    time_t timeNow;
    time(&timeNow);
    
    if (timeNow - timeLastBeat >= HeartBeatInt) {
        timeLastBeat = timeNow;
        Status(TypeHeartBeat, 0, "");
    }

    int flag = tport_getflag(&Region);
    if (flag == TERMINATE || flag == MyPid) {
        logit("t", "csnrv: Senal de terminacion recibida. Cerrando...\n");
        gtk_main_quit();
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
static gboolean on_map_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    if (event->direction == GDK_SCROLL_UP) {
        g_map_zoom *= 1.2; 
    } else if (event->direction == GDK_SCROLL_DOWN) {
        g_map_zoom /= 1.2; 
    }
    
    if (g_map_zoom < ZOOM_MIN) g_map_zoom = ZOOM_MIN;
    if (g_map_zoom > ZOOM_MAX) g_map_zoom = ZOOM_MAX;
    
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean on_map_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == 1) { 
        g_is_dragging = TRUE;
        g_last_mouse_x = event->x;
        g_last_mouse_y = event->y;
    }
    return TRUE;
}

static gboolean on_map_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == 1) { 
        g_is_dragging = FALSE;
    }
    return TRUE;
}

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
 * FUNCION VIGIA: Lectura de csnhypodbp CSV History (CON TRACERS)
 * -------------------------------------------------------------------- */
static gboolean update_summary_loop(gpointer data) {
    struct stat file_stat;
    static int last_processed_qid = 0;
    static int warn_file_missing = 0;
    
    if (stat(QuakeFile, &file_stat) == 0) {
        warn_file_missing = 0; /* Reset warning si encuentra el archivo */
        if (file_stat.st_mtime > last_file_mod_time) {
            logit("t", ">> TRACER: Archivo %s detecto modificaciones. Abriendo...\n", QuakeFile);
            last_file_mod_time = file_stat.st_mtime;
            
            FILE *f = fopen(QuakeFile, "r");
            if (f) {
                char line[512];
                char target_line[512] = "";
                int line_count = 0;
                
                while (fgets(line, sizeof(line), f)) {
                    line_count++;
                    if (strlen(line) > 20) {
                        strcpy(target_line, line);
                        break; 
                    }
                }
                fclose(f);

                logit("t", ">> TRACER: Lineas procesadas: %d. Linea objetivo: '%s'\n", line_count, target_line);

                if (strlen(target_line) > 0) {
                    char fecha[32], hora[32], lat_s[32], lon_s[32], dep_s[32], res_s[32], azm_s[32], stn_s[32], id_s[32], ml_s[32], mwp_s[32];
                    double otime=0, lat=0, lon=0, depth=0;
                    int qver=0, qid=0;

                    int parsed = sscanf(target_line, "%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%lf,%d,%d,%lf,%lf,%lf", 
                           fecha, hora, lat_s, lon_s, dep_s, res_s, azm_s, stn_s, id_s, ml_s, mwp_s, &otime, &qver, &qid, &lat, &lon, &depth);

                    logit("t", ">> TRACER: sscanf extrajo %d variables de las 17 esperadas.\n", parsed);

                    if (parsed == 17) {
                        logit("t", ">> TRACER: Sismo valido detectado (QID %d). Actualizando UI...\n", qid);
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
                        }
                        
                        char buffer[512]; 
                        time_t rawtime = (time_t)(otime + 0.5);
                        struct tm *ptm = gmtime(&rawtime); 
                        if (ptm) {
                            snprintf(buffer, sizeof(buffer), "<span size='xx-large' weight='bold' foreground='#0055a4'>%02d:%02d:%02d UTC\n%02d/%02d/%04d</span>", 
                                     ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
                                     ptm->tm_mday, ptm->tm_mon + 1, ptm->tm_year + 1900);
                            gtk_label_set_markup(GTK_LABEL(lbl_origin_time), buffer);
                        }
                        
                        snprintf(buffer, sizeof(buffer), "<span size='large'>Lat: %.3f  Lon: %.3f</span>", lat, lon);
                        gtk_label_set_markup(GTK_LABEL(lbl_coordinates), buffer);
                        
                        snprintf(buffer, sizeof(buffer), "<span size='large'>Depth: %.0f km</span>", depth);
                        gtk_label_set_markup(GTK_LABEL(lbl_depth), buffer);
                        
                        double ml_mag = 0.0, mwp_mag = 0.0;
                        int ml_stn = 0, mwp_stn = 0;

                        if (strcmp(ml_s, "-") != 0) sscanf(ml_s, "%lf-%d", &ml_mag, &ml_stn);
                        if (strcmp(mwp_s, "-") != 0) sscanf(mwp_s, "%lf-%d", &mwp_mag, &mwp_stn);

                        double pref_mag = 0.0;
                        int pref_stn = 0;
                        char pref_type[16] = "--";

                        if (mwp_mag >= 5.5 && mwp_stn >= 3) {
                            pref_mag = mwp_mag; pref_stn = mwp_stn; strcpy(pref_type, "Mwp");
                        } else if (ml_mag > 0.0) {
                            pref_mag = ml_mag; pref_stn = ml_stn; strcpy(pref_type, "Ml");
                        } else if (mwp_mag > 0.0) {
                            pref_mag = mwp_mag; pref_stn = mwp_stn; strcpy(pref_type, "Mwp");
                        }

                        update_mag_row(lbl_pref_val, lbl_pref_stn, pref_mag, pref_stn, TRUE, pref_type);
                        update_mag_row(lbl_ml_val, lbl_ml_stn, ml_mag, ml_stn, FALSE, "");
                        update_mag_row(lbl_mwp_val, lbl_mwp_stn, mwp_mag, mwp_stn, FALSE, "");

                        if (map_canvas) gtk_widget_queue_draw(map_canvas);
                    } else {
                        logit("e", ">> TRACER ERROR: El formato del string no coincide. Revisa si tiene menos variables.\n");
                    }
                } else {
                    logit("e", ">> TRACER ERROR: El archivo parece estar vacio o corrupto.\n");
                }
            } else {
                logit("e", ">> TRACER ERROR: fopen fallo. No se pudo leer %s\n", QuakeFile);
            }
        }
    } else {
        if (!warn_file_missing) {
            logit("e", ">> TRACER ERROR: stat() fallo. El archivo %s no existe aun.\n", QuakeFile);
            warn_file_missing = 1;
        }
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
static gboolean on_draw_map(GtkWidget *widget, cairo_t *cr, gpointer data) {
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);

    if (g_map_pixbuf != NULL) {
        double img_w = gdk_pixbuf_get_width(g_map_pixbuf);
        double img_h = gdk_pixbuf_get_height(g_map_pixbuf);
        
        double epi_x = img_w / 2.0;
        double epi_y = img_h / 2.0;

        if (has_valid_data) {
            epi_x = img_w * (g_epicenter_lon + 180.0) / 360.0;
            epi_y = img_h * (90.0 - g_epicenter_lat) / 180.0;
        }

        double effective_zoom = has_valid_data ? g_map_zoom : fmin((double)width/img_w, (double)height/img_h);

        cairo_save(cr);
        
        cairo_translate(cr, width / 2.0 + g_map_pan_x, height / 2.0 + g_map_pan_y);
        cairo_scale(cr, effective_zoom, effective_zoom);
        cairo_translate(cr, -epi_x, -epi_y);

        gdk_cairo_set_source_pixbuf(cr, g_map_pixbuf, 0, 0);
        cairo_paint(cr);
        
        double step = 10.0; 
        if (effective_zoom > 30.0) step = 1.0;
        else if (effective_zoom > 15.0) step = 2.0;
        else if (effective_zoom > 5.0) step = 5.0;

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4);
        cairo_set_line_width(cr, 1.0 / effective_zoom); 

        for (double lon = -180.0; lon <= 180.0; lon += step) {
            double x = img_w * (lon + 180.0) / 360.0;
            cairo_move_to(cr, x, 0); cairo_line_to(cr, x, img_h);
        }
        for (double lat = -90.0; lat <= 90.0; lat += step) {
            double y = img_h * (90.0 - lat) / 180.0;
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
            double x = img_w * (lon + 180.0) / 360.0;
            cairo_move_to(cr, x + 4.0 / effective_zoom, world_top_y + 14.0 / effective_zoom);
            snprintf(lbl, sizeof(lbl), "%.0f\xC2\xB0", lon); cairo_show_text(cr, lbl);
        }
        for (double lat = -90.0; lat <= 90.0; lat += step) {
            double y = img_h * (90.0 - lat) / 180.0;
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
    return FALSE;
}

int main(int argc, char *argv[]) {
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

    gtk_init(&argc, &argv);
    setlocale(LC_NUMERIC, "C");
    
    GError *err = NULL;
    g_map_pixbuf = gdk_pixbuf_new_from_file(MapImageFile, &err);
    if (!g_map_pixbuf) {
        logit("e", ">> [AVISO] No se pudo cargar %s: %s\n", MapImageFile, err->message);
        g_error_free(err);
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "CSNrv - Report Viewer");
    gtk_window_set_default_size(GTK_WINDOW(window), 450, 750);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_main), 15);
    gtk_container_add(GTK_CONTAINER(window), vbox_main);

    lbl_origin_time = gtk_label_new("<span size='xx-large' weight='bold' foreground='gray'>Esperando Datos...</span>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_origin_time), TRUE);
    gtk_label_set_justify(GTK_LABEL(lbl_origin_time), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox_main), lbl_origin_time, FALSE, FALSE, 5);

    lbl_coordinates = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox_main), lbl_coordinates, FALSE, FALSE, 0);

    lbl_depth = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox_main), lbl_depth, FALSE, FALSE, 0);

    GtkWidget *separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox_main), separator1, FALSE, FALSE, 5);

    GtkWidget *lbl_timer_title = gtk_label_new("<span size='large'>Time Since Quake:</span>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_timer_title), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox_main), lbl_timer_title, FALSE, FALSE, 0);

    lbl_time_elapsed = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox_main), lbl_time_elapsed, FALSE, FALSE, 0);

    GtkWidget *separator2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox_main), separator2, FALSE, FALSE, 5);

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

    gtk_box_pack_start(GTK_BOX(vbox_main), mag_grid, FALSE, FALSE, 10);

    map_canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(map_canvas, 400, 400); 
    
    gtk_widget_add_events(map_canvas, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(G_OBJECT(map_canvas), "scroll-event", G_CALLBACK(on_map_scroll), NULL);
    g_signal_connect(G_OBJECT(map_canvas), "button-press-event", G_CALLBACK(on_map_button_press), NULL);
    g_signal_connect(G_OBJECT(map_canvas), "button-release-event", G_CALLBACK(on_map_button_release), NULL);
    g_signal_connect(G_OBJECT(map_canvas), "motion-notify-event", G_CALLBACK(on_map_motion), NULL);
    g_signal_connect(G_OBJECT(map_canvas), "draw", G_CALLBACK(on_draw_map), NULL);
    
    gtk_box_pack_start(GTK_BOX(vbox_main), map_canvas, TRUE, TRUE, 10);

    /* Timer para UI */
    g_timeout_add(1000, update_summary_loop, NULL);
    
    /* Timer para Earthworm (Heartbeats) */
    g_timeout_add(1000, ew_background_tasks, NULL);

    gtk_widget_show_all(window);
    gtk_main();

    tport_detach(&Region);
    if (g_map_pixbuf) g_object_unref(g_map_pixbuf);
    return 0;
}
