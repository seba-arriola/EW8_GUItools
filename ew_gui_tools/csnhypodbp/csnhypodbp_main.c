#include "csnhypodbp.h"

/* Definicion de Variables Globales */
char MyModName[MAX_STR];
char InRingName[MAX_STR];
char OutRingName[MAX_STR];
char StaFile[MAX_STR];
char StateFile[MAX_STR];   /* archivo de estado de csnloc (recuperacion) */
char WsIP[MAX_STR];
char WsPort[MAX_STR];
int  WsTimeout;
int  HeartBeatInt;
int  LogFile;
pid_t MyPid;

unsigned char MyInstId;
unsigned char MyModId;
unsigned char TypeHeartBeat;
unsigned char TypeError;
unsigned char TypeHyp2000Arc;
unsigned char TypePickSCNL;
unsigned char TypeMagnitude; 

SHM_INFO InRegion;       
SHM_INFO PRegion;        
MSG_LOGO GetLogo[2]; 

EVENT_PICK_CACHE PickCache[MAX_CACHED_EVENTS];
int pick_cache_idx = 0;

DEV_STATION *StaArray = NULL; 
int     bHasData[MAX_ESTA]; 
double  g_StaDist[MAX_ESTA]; 
int     NumEstaciones = 0;
int     selected_qid = 0;
int     selected_mod = 0;
double  selected_otime = 0;
char    selected_id[32] = "None";
WS_MENU_QUEUE_REC ws_menu; 

EwGuiCanvas *canvas_global = NULL;
GListStore *g_store_hypo = NULL;
GtkSingleSelection *g_selection_hypo = NULL;
GtkWidget *g_notebook = NULL;
GMainLoop *g_loop = NULL;
GtkWidget *tree_global = NULL;
GtkWidget *btn_repick = NULL;
GtkWidget *window_global = NULL; 
GtkWidget *box_fetch = NULL;
GtkWidget *entry_dist = NULL;
GtkWidget *entry_time = NULL; 

double g_max_dist_km = 500.0; 
gboolean edit_mode = FALSE;
double   g_zoom_factor = 1.0; 
double   g_dWindowStart = 0;
double   g_dScreenTime = 120.0; 
int      g_margin_left = 160; 
int      g_spacing = 100;

StaNode g_SortedNodes[MAX_ESTA];
int     g_NumSortedNodes = 0;
gboolean g_history_needs_saving = FALSE; 

int      g_filter_type = 0;      /* 0=Raw, 1=HP, 2=LP, 3=BP */
double   g_align_lead = 20.0;    /* segundos antes de la onda P */
GtkWidget *combo_filter = NULL;
GtkWidget *entry_freq1 = NULL;
GtkWidget *entry_freq2 = NULL;
GtkWidget *combo_order = NULL;
GtkWidget *btn_apply_filter = NULL;

gboolean pending_waveform_reload = FALSE;
double   selected_lat = 0.0;
double   selected_lon = 0.0;

/* Etiquetas de modulo origen: config "ModuleLabel <id> <nombre>". */
#define MAX_MODLABELS 16
typedef struct { int id; char label[24]; } MOD_LABEL;
static MOD_LABEL g_modlabels[MAX_MODLABELS];
static int       g_nmodlabels = 0;

const char *ModLabel(int mod)
{
    static char buf[16];
    int i;
    for (i = 0; i < g_nmodlabels; i++)
        if (g_modlabels[i].id == mod) return g_modlabels[i].label;
    snprintf(buf, sizeof(buf), "%d", mod);
    return buf;
}

/* --------------------------------------------------------------------
 * LECTURA DE CONFIG Y CONEXION EW
 * -------------------------------------------------------------------- */
int ReadConfig(char *configfile) {
    int ncommand = 9, nmiss = 0, i; char init[10] = {0}; char *com, *str;
    if (!k_open(configfile)) { fprintf(stderr, "Error abriendo archivo config <%s>\n", configfile); return -1; }
    while (k_rd()) {
        com = k_str(); if (!com || com[0] == '#') continue;
        if (k_its("MyModuleId")) { str = k_str(); if (str) strcpy(MyModName, str); init[0] = 1; } 
        else if (k_its("InRing")) { str = k_str(); if (str) strcpy(InRingName, str); init[1] = 1; } 
        else if (k_its("OutRing")) { str = k_str(); if (str) strcpy(OutRingName, str); init[2] = 1; } 
        else if (k_its("HeartBeatInt")) { HeartBeatInt = k_int(); init[3] = 1; } 
        else if (k_its("LogFile")) { LogFile = k_int(); init[4] = 1; } 
        else if (k_its("StaFile")) { str = k_str(); if (str) strcpy(StaFile, str); init[5] = 1; } 
        else if (k_its("WsIP")) { str = k_str(); if (str) strcpy(WsIP, str); init[6] = 1; } 
        else if (k_its("WsPort")) { str = k_str(); if (str) strcpy(WsPort, str); init[7] = 1; } 
        else if (k_its("StateFile")) { str = k_str(); if (str) strcpy(StateFile, str); init[8] = 1; }
        else if (k_its("WsTimeout")) { WsTimeout = k_int(); } 
        else if (k_its("ModuleLabel")) {
            int mid = k_int(); str = k_str();
            if (str && g_nmodlabels < MAX_MODLABELS) {
                g_modlabels[g_nmodlabels].id = mid;
                strncpy(g_modlabels[g_nmodlabels].label, str, sizeof(g_modlabels[0].label) - 1);
                g_modlabels[g_nmodlabels].label[sizeof(g_modlabels[0].label) - 1] = '\0';
                g_nmodlabels++;
            }
        }
        else { continue; }
        if (k_err()) { fprintf(stderr, "Error parseando <%s>\n", com); return -1; }
    }
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close(); return (nmiss > 0) ? -1 : 0;
}

void Status(unsigned char type, short ierr, char *note) {
    MSG_LOGO logo; char msg[256]; time_t t;
    logo.instid = MyInstId; logo.mod = MyModId; logo.type = type;
    time(&t);
    if (type == TypeHeartBeat) sprintf(msg, "%ld %d\n", (long) t, MyPid);
    else if (type == TypeError) { sprintf(msg, "%ld %hd %s\n", (long) t, ierr, note); logit("et", "csnhypodbp: Error: %s\n", note); }
    tport_putmsg(&InRegion, &logo, strlen(msg), msg);
}

void ConnectToEarthworm() {
    long InRingKey = GetKey(InRingName), OutRingKey = GetKey(OutRingName);
    if (InRingKey == -1 || OutRingKey == -1) exit(-1);
    if (GetLocalInst(&MyInstId) != 0 || GetModId(MyModName, &MyModId) != 0) exit(-1);
    if (GetType("TYPE_HYP2000ARC", &TypeHyp2000Arc) != 0 || GetType("TYPE_PICK_SCNL", &TypePickSCNL) != 0) exit(-1);
    if (GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0 || GetType("TYPE_ERROR", &TypeError) != 0) exit(-1);
    if (GetType("TYPE_MAGNITUDE", &TypeMagnitude) != 0) exit(-1); 

    unsigned char InstWild, ModWild; GetInst("INST_WILDCARD", &InstWild); GetModId("MOD_WILDCARD", &ModWild);
    GetLogo[0].instid = InstWild; GetLogo[0].mod = ModWild; GetLogo[0].type = TypeHyp2000Arc;
    GetLogo[1].instid = InstWild; GetLogo[1].mod = ModWild; GetLogo[1].type = TypeMagnitude; 

    tport_attach(&InRegion, InRingKey); tport_attach(&PRegion, OutRingKey);
    logit("t", "=== csnhypodbp: Conectado a %s y %s ===\n", InRingName, OutRingName);
    ws_menu.head = NULL; ws_menu.tail = NULL;
}

gboolean escuchar_anillo_earthworm(gpointer user_data) {
    GtkWidget *tree = GTK_WIDGET(user_data);
    static char msg[65000]; MSG_LOGO reclogo; long recsize; int res;
    gboolean active_needs_refresh = FALSE, new_event_added = FALSE;
    
    while (TRUE) {
        res = tport_getmsg(&InRegion, GetLogo, 2, &reclogo, &recsize, msg, sizeof(msg)-1);
        if (res == GET_NONE) break;
        
        if (res == GET_OK || res == GET_NOTRACK || res == GET_MISS) {
            msg[recsize] = '\0'; 
            if (reclogo.type == TypeHyp2000Arc) {
                int status = procesar_mensaje_sismo(tree, msg, reclogo.mod);
                if (status == 1) active_needs_refresh = TRUE;
                if (status == 2) new_event_added = TRUE;
            } else if (reclogo.type == TypeMagnitude) {
                if (procesar_mensaje_mag(tree, msg) == 1) active_needs_refresh = TRUE;
            }
        }
    } 
    if (new_event_added && !edit_mode) {
        gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_selection_hypo), 0, TRUE);
    } else if (active_needs_refresh && !edit_mode) {
        on_row_selected(g_selection_hypo, NULL, NULL);
    }
    return TRUE; 
}

gboolean ew_background_tasks(gpointer user_data) {
    static EwGuiHeartbeat hb = {0};
    time_t timeNow; time(&timeNow);
    if (ewgui_heartbeat_due(&hb, (double)timeNow, HeartBeatInt)) { Status(TypeHeartBeat, 0, ""); }
    /* El estado de eventos lo persiste csnloc (unico escritor). csnhypodbp
       solo lo lee al arrancar para recuperarse. */
    if (ewgui_ring_should_quit(&InRegion, MyPid)) { if (g_loop) g_main_loop_quit(g_loop); return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}

static void on_window_close(GtkWindow *w, gpointer data) {
    (void)w; (void)data;
    if (g_loop) g_main_loop_quit(g_loop);
}
static gboolean on_window_close_cb(GtkWindow *w, gpointer data) { on_window_close(w, data); return FALSE; }
static void col_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data) {
    (void)f; (void)data;
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
    gtk_list_item_set_child(item, lbl);
}
static void col_bind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data) {
    (void)f;
    int col = GPOINTER_TO_INT(data);
    GtkWidget *lbl = gtk_list_item_get_child(item);
    CsnhypodbpRow *row = CSNHYPODBP_ROW(gtk_list_item_get_item(item));
    gtk_label_set_text(GTK_LABEL(lbl), row ? csnhypodbp_row_col(row, col) : "");
}

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr, "Uso: %s <configfile.d>\n", argv[0]); exit(1); }
    if (ReadConfig(argv[1]) != 0) exit(1);
    setenv("TZ", "GMT", 1); tzset(); logit_init(argv[1], 0, 1024, LogFile); MyPid = getpid();
    gtk_init(); setlocale(LC_NUMERIC, "C");

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "#btn_repick { background-image: none; box-shadow: none; border: none; background-color: #cce5ff; color: #0a58ca; font-weight: bold; border-radius: 4px; }\n"
        "#btn_relocate { background-image: none; box-shadow: none; border: none; background-color: #90ee90; color: #000000; font-weight: bold; border-radius: 4px; }\n"
        "#btn_fetch { background-image: none; box-shadow: none; border: none; background-color: #e2e3e5; font-weight: bold; border-radius: 4px; }\n"
        "#btn_filter { background-image: none; box-shadow: none; border: none; background-color: #28a745; color: #ffffff; font-weight: bold; padding: 2px 10px; border-radius: 4px; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    ConnectToEarthworm(); LoadStationsFromFile();

    window_global = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Hypocenter database picker (EW8)");
    gtk_window_set_default_size(GTK_WINDOW(window_global), 1100, 750);
    g_signal_connect(window_global, "close-request", G_CALLBACK(on_window_close_cb), NULL);
    GtkEventController *keyctl = gtk_event_controller_key_new();
    g_signal_connect(keyctl, "key-pressed", G_CALLBACK(on_key_press), NULL);
    gtk_widget_add_controller(window_global, keyctl);
    signal(SIGPIPE, SIG_IGN);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_window_set_child(GTK_WINDOW(window_global), vbox);

    g_notebook = gtk_notebook_new();
    gtk_widget_set_vexpand(g_notebook, TRUE);
    gtk_box_append(GTK_BOX(vbox), g_notebook);

    /* --- Pestaña "Eventos": la lista --- */
    GtkWidget *page_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *sw_lista = gtk_scrolled_window_new();
    gtk_widget_set_size_request(sw_lista, -1, 250);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_lista), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    g_store_hypo = g_list_store_new(CSNHYPODBP_TYPE_ROW);
    g_selection_hypo = gtk_single_selection_new(G_LIST_MODEL(g_store_hypo));
    gtk_single_selection_set_autoselect(g_selection_hypo, FALSE);
    gtk_single_selection_set_can_unselect(g_selection_hypo, TRUE);

    tree_global = gtk_column_view_new(GTK_SELECTION_MODEL(g_selection_hypo));
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(tree_global), TRUE);
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(tree_global), TRUE);
    g_signal_connect(g_selection_hypo, "notify::selected", G_CALLBACK(on_row_selected), NULL);
    {
        GtkGesture *dclick = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dclick), 1);
        g_signal_connect(dclick, "pressed", G_CALLBACK(on_list_double_click), NULL);
        gtk_widget_add_controller(tree_global, GTK_EVENT_CONTROLLER(dclick));
    }

    const char *headers[] = {"Date", "O-time", "Lat.", "Lon.", "Dep", "Res", "Azm", "#Stn", "ID", "Ml", "Mwp", "Mod", "Ver"};
    for (int i = 0; i < 13; i++) {
        GtkListItemFactory *f = gtk_signal_list_item_factory_new();
        g_signal_connect(f, "setup", G_CALLBACK(col_setup), NULL);
        g_signal_connect(f, "bind", G_CALLBACK(col_bind), GINT_TO_POINTER(i));
        GtkColumnViewColumn *col = gtk_column_view_column_new(headers[i], f);
        gtk_column_view_column_set_expand(col, TRUE);
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree_global), col);
        /* NO liberar f: GtkColumnViewColumn no toma su propia referencia de la
           factory en este GTK; unref la deja colgante y crashea al crear celdas. */
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_lista), tree_global);
    gtk_widget_set_vexpand(sw_lista, TRUE);
    gtk_box_append(GTK_BOX(page_list), sw_lista);
    gtk_notebook_append_page(GTK_NOTEBOOK(g_notebook), page_list, gtk_label_new("Eventos"));

    /* --- Pestaña "Ondas": las trazas --- */
    GtkWidget *page_waves = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *sw_ondas = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_ondas), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_widget_set_vexpand(sw_ondas, TRUE);
    gtk_box_append(GTK_BOX(page_waves), sw_ondas);

    canvas_global = ewgui_canvas_new();
    ewgui_canvas_set_draw(canvas_global, on_draw_signal, NULL);
    GtkWidget *canvas_widget = ewgui_canvas_widget(canvas_global);
    gtk_widget_set_size_request(canvas_widget, -1, 600);
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_canvas_clicked), NULL);
    gtk_widget_add_controller(canvas_widget, GTK_EVENT_CONTROLLER(click));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_ondas), canvas_widget);
    gtk_notebook_append_page(GTK_NOTEBOOK(g_notebook), page_waves, gtk_label_new("Ondas"));

    GtkWidget *hbox_bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hbox_bottom, 10); gtk_widget_set_margin_end(hbox_bottom, 10); gtk_widget_set_margin_bottom(hbox_bottom, 5);
    GtkWidget *hbox_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);

    btn_repick = gtk_button_new_with_label("Repick mode"); gtk_widget_set_name(btn_repick, "btn_repick"); gtk_widget_set_sensitive(btn_repick, FALSE);
    g_signal_connect(btn_repick, "clicked", G_CALLBACK(on_btn_repick_clicked), NULL);

    box_fetch = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    entry_dist = gtk_entry_new(); gtk_editable_set_width_chars(GTK_EDITABLE(entry_dist), 5); gtk_editable_set_text(GTK_EDITABLE(entry_dist), "500");
    entry_time = gtk_entry_new(); gtk_editable_set_width_chars(GTK_EDITABLE(entry_time), 5); gtk_editable_set_text(GTK_EDITABLE(entry_time), "2");
    GtkWidget *btn_fetch = gtk_button_new_with_label("Fetch from WS"); gtk_widget_set_name(btn_fetch, "btn_fetch");
    g_signal_connect(btn_fetch, "clicked", G_CALLBACK(on_btn_fetch_clicked), NULL);
    gtk_box_append(GTK_BOX(box_fetch), gtk_label_new("Distance (km):")); gtk_box_append(GTK_BOX(box_fetch), entry_dist);
    gtk_box_append(GTK_BOX(box_fetch), gtk_label_new("  Window (min):")); gtk_box_append(GTK_BOX(box_fetch), entry_time);
    gtk_box_append(GTK_BOX(box_fetch), btn_fetch);

    GtkWidget *box_filter = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    const char *filt_items[] = {"Raw (No Filter)","High-Pass","Low-Pass","Band-Pass",NULL};
    combo_filter = gtk_drop_down_new_from_strings(filt_items);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(combo_filter), 0);
    g_signal_connect(combo_filter, "notify::selected", G_CALLBACK(on_filter_changed), NULL);

    entry_freq1 = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(entry_freq1), 4);
    gtk_editable_set_text(GTK_EDITABLE(entry_freq1), "0.7");
    gtk_widget_set_sensitive(entry_freq1, FALSE);

    entry_freq2 = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(entry_freq2), 4);
    gtk_editable_set_text(GTK_EDITABLE(entry_freq2), "2.0");
    gtk_widget_set_sensitive(entry_freq2, FALSE);

    const char *ord_items[] = {"2","4",NULL};
    combo_order = gtk_drop_down_new_from_strings(ord_items);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(combo_order), 1);
    gtk_widget_set_sensitive(combo_order, FALSE);

    btn_apply_filter = gtk_button_new_with_label("Apply");
    gtk_widget_set_name(btn_apply_filter, "btn_filter");
    gtk_widget_set_sensitive(btn_apply_filter, FALSE);
    g_signal_connect(btn_apply_filter, "clicked", G_CALLBACK(on_btn_apply_filter_clicked), NULL);

    gtk_box_append(GTK_BOX(box_filter), gtk_label_new("  Filter:"));
    gtk_box_append(GTK_BOX(box_filter), combo_filter);
    gtk_box_append(GTK_BOX(box_filter), gtk_label_new(" F1(Hz):"));
    gtk_box_append(GTK_BOX(box_filter), entry_freq1);
    gtk_box_append(GTK_BOX(box_filter), gtk_label_new(" F2(Hz):"));
    gtk_box_append(GTK_BOX(box_filter), entry_freq2);
    gtk_box_append(GTK_BOX(box_filter), gtk_label_new(" Ord:"));
    gtk_box_append(GTK_BOX(box_filter), combo_order);
    gtk_box_append(GTK_BOX(box_filter), btn_apply_filter);

    gtk_box_append(GTK_BOX(hbox_center), btn_repick);
    gtk_box_append(GTK_BOX(hbox_center), box_filter);
    gtk_box_append(GTK_BOX(hbox_center), box_fetch);
    gtk_box_append(GTK_BOX(hbox_bottom), hbox_center);
    gtk_box_append(GTK_BOX(vbox), hbox_bottom);

    cargar_sismos_iniciales(tree_global);
    gtk_window_present(GTK_WINDOW(window_global));
    gtk_widget_set_visible(box_fetch, FALSE);

    g_timeout_add(1000, ew_background_tasks, NULL);
    g_timeout_add(500, escuchar_anillo_earthworm, tree_global);
    g_timeout_add(1000, waveform_reload_timer, NULL);

    g_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(g_loop);

    tport_detach(&InRegion); tport_detach(&PRegion);
    if (StaArray) {
        for (int i = 0; i < NumEstaciones; i++) ewgui_trace_free(StaArray[i].trace);
        free(StaArray);
    }
    return 0;
}
