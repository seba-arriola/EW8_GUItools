#include "csntvp.h"

/* --- CONFIGURACION DEL MODULO --- */
char MyModName[MAX_STR];
char InRingName[MAX_STR];
char OutRingName[MAX_STR];
char StaFile[MAX_STR];
int HeartBeatInt;
int LogFile;
pid_t MyPid;

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
double g_color_p[3]    = {1.0, 0.0, 0.0};    /* P picks: red */
double g_color_s[3]    = {0.0, 0.0, 0.0};    /* S picks: black */

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
int g_pick_seq = 0;             /* pick sequence number (0-999999) */
char g_pick_phase = 'P';        /* fase activa del pick manual: 'P' | 'S' */
int g_warned_datatype = 0;      /* warns once about an unknown datatype */

/* A1/A2/A3: state of the envelope cache. The envelope is recalculated at
   most every ENV_PROCESS_MS or when something forces it (filter, window, hold,
   width); the draw only reads it and scales it (zoom does not invalidate). */
int64_t  g_last_env_process_ms = 0;
double   g_last_env_t_right = 0.0;
int      g_last_env_width = 0;
gboolean g_last_env_ok = FALSE;
gboolean g_bForceEnv = FALSE;
gboolean g_envelope_updated = FALSE;

DEV_STATION *StaArray = NULL;
int iNumStas = 0;

GtkWidget *g_scrolled_window;
EwGuiCanvas *g_drawing_waves;
EwGuiCanvas *drawing_axis;
GtkWidget *btn_hold;

double dTrackHeight = 60.0;
int iVisStas = 12;
int iTimeWindowMinutes = 6;


GApplication *g_app = NULL;

/* Exporta la vista de trazas a SVG/PDF/PNG (diálogo Guardar). */
static void act_export(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p;
    ewgui_export_dialog_run(g_drawing_waves, GTK_WINDOW(ud));
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
    GtkWidget *vbox;
    (void)user_data;

    GtkWidget *window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "CSN tvp - Trace Viewer-Picker");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);

    GtkEventController *keyctl = gtk_event_controller_key_new();
    g_signal_connect(keyctl, "key-pressed", G_CALLBACK(on_key_press), NULL);
    gtk_widget_add_controller(window, keyctl);

    /* --- Acciones GIO (menú del headerbar; portable a GTK4) --- */
    GSimpleActionGroup *actions = g_simple_action_group_new();
    ewgui_action_add(G_ACTION_MAP(actions), "stas-per-screen", NULL, act_stas_per_screen, window);
    ewgui_action_add(G_ACTION_MAP(actions), "time-window", NULL, act_time_window, window);
    ewgui_action_add(G_ACTION_MAP(actions), "clean-view", NULL, act_clean_view, window);
    ewgui_action_add(G_ACTION_MAP(actions), "reload-default", NULL, act_reload_default, window);
    ewgui_action_add(G_ACTION_MAP(actions), "filter-options", NULL, act_filter_options, window);
    ewgui_action_add(G_ACTION_MAP(actions), "colour", "i", act_colour, NULL);
    GSimpleAction *dark = g_simple_action_new_stateful("dark-mode", NULL,
                                                       g_variant_new_boolean(FALSE));
    g_signal_connect(dark, "activate", G_CALLBACK(on_toggle_dark), NULL);
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(dark));
    ewgui_action_add(G_ACTION_MAP(actions), "export", NULL, act_export, window);
    gtk_widget_insert_action_group(window, "win", G_ACTION_GROUP(actions));
    g_object_unref(actions);

    EwMenuItem ctrl_items[] = {
        { "Stations per screen", "win.stas-per-screen", NULL, 0 },
        { "Time window", "win.time-window", NULL, 0 },
        { "Clean view", "win.clean-view", NULL, 0 },
        { "Reload default stations", "win.reload-default", NULL, 0 },
    };
    EwMenuItem colour_items[] = {
        { "Waveforms", "win.colour", "i", 1 },
        { "Background", "win.colour", "i", 2 },
        { "Font", "win.colour", "i", 3 },
        { "Separator", "win.colour", "i", 4 },
        { "P picks", "win.colour", "i", 5 },
        { "S picks", "win.colour", "i", 6 },
    };
    EwMenuItem filter_items[] = {
        { "Filter Options", "win.filter-options", NULL, 0 },
    };
    EwMenuItem view_items[] = {
        { "Export view...", "win.export", NULL, 0 },
        { "Dark mode", "win.dark-mode", NULL, 0 },
    };
    EwMenuGroup menu_groups[] = {
        { "Control Panel", ctrl_items, 4 },
        { "Colours", colour_items, 6 },
        { "Filter", filter_items, 1 },
        { "View", view_items, 2 },
    };
    GMenuModel *menu_model = ewgui_menu_build(menu_groups, 4);

    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), menu_model);
    g_object_unref(menu_model);

    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_btn);

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), vbox);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbar_view);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_start(toolbar, 10); gtk_widget_set_margin_end(toolbar, 10); gtk_widget_set_margin_top(toolbar, 5); gtk_widget_set_margin_bottom(toolbar, 5);
    btn_hold = gtk_toggle_button_new_with_label("HOLD (Congelar Pantalla)");
    g_signal_connect(btn_hold, "toggled", G_CALLBACK(on_btn_hold_toggled), NULL);
    gtk_box_append(GTK_BOX(toolbar), btn_hold);
    gtk_box_append(GTK_BOX(toolbar), gtk_label_new(" | (En HOLD) P/S: elegir fase | Clic Izq: picar | Flechas Arriba/Abajo: Zoom Vertical"));
    gtk_box_append(GTK_BOX(vbox), toolbar);

    g_scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(g_scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(g_scrolled_window, TRUE);
    gtk_box_append(GTK_BOX(vbox), g_scrolled_window);
    g_drawing_waves = ewgui_canvas_new();
    ewgui_canvas_set_draw(g_drawing_waves, on_draw_waves, NULL);
    GtkWidget *waves_widget = ewgui_canvas_widget(g_drawing_waves);
    gtk_widget_set_size_request(waves_widget, -1, iNumStas * dTrackHeight);
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_canvas_button_press), NULL);
    gtk_widget_add_controller(waves_widget, GTK_EVENT_CONTROLLER(click));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(g_scrolled_window), waves_widget);
    drawing_axis = ewgui_canvas_new();
    ewgui_canvas_set_draw(drawing_axis, on_draw_axis, NULL);
    gtk_widget_set_size_request(ewgui_canvas_widget(drawing_axis), -1, BOTTOM_AXIS_H);
    gtk_box_append(GTK_BOX(vbox), ewgui_canvas_widget(drawing_axis));

    g_timeout_add(REFRESH_MS, fetch_realtime_data, window);
    g_timeout_add(1000, ew_background_tasks, NULL);
    gtk_window_present(GTK_WINDOW(window));
    RecalcTrackHeight();
}

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr, "Uso: %s <configfile.d>\n", argv[0]); exit(1); }
    if (ReadConfig(argv[1]) != 0) { fprintf(stderr, "Error leyendo configuracion de %s\n", argv[1]); exit(1); }
    setenv("TZ", "GMT", 1); tzset(); logit_init(argv[1], 0, 1024, LogFile); MyPid = getpid();
    ConnectToEarthworm(); LoadStationsFromFile();

    AdwApplication *app = adw_application_new("cl.csn.csntvp", G_APPLICATION_NON_UNIQUE);
    g_app = G_APPLICATION(app);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(g_app, 0, NULL);

    tport_detach( &WaveRegion ); tport_detach( &PickRegion ); FreeAllStations();
    return status;
}
