#include "csnhypodbp.h"

/* Definicion de Variables Globales */
char MyModName[MAX_STR];
char InRingName[MAX_STR];
char OutRingName[MAX_STR];
char StaFile[MAX_STR];
char HistoryFile[MAX_STR]; 
char WsIP[MAX_STR];
char WsPort[MAX_STR];
int  WsTimeout;
int  HeartBeatInt;
int  LogFile;
pid_t MyPid;
time_t timeLastBeat = 0;

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
double  selected_otime = 0;
char    selected_id[32] = "None";
WS_MENU_QUEUE_REC ws_menu; 

GtkWidget *canvas_global = NULL;
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
        else if (k_its("HistoryFile")) { str = k_str(); if (str) strcpy(HistoryFile, str); init[8] = 1; }
        else if (k_its("WsTimeout")) { WsTimeout = k_int(); } 
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
                int status = procesar_mensaje_sismo(tree, msg);
                if (status == 1) active_needs_refresh = TRUE;
                if (status == 2) new_event_added = TRUE;
            } else if (reclogo.type == TypeMagnitude) {
                if (procesar_mensaje_mag(tree, msg) == 1) active_needs_refresh = TRUE;
            }
        }
    } 
    if (new_event_added && !edit_mode) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        GtkTreePath *path = gtk_tree_path_new_first();
        if (path) {
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree), path, NULL, FALSE, 0.0, 0.0);
            gtk_tree_selection_select_path(selection, path); 
            gtk_tree_path_free(path);
        }
    } else if (active_needs_refresh && !edit_mode) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        g_signal_emit_by_name(selection, "changed");
    }
    return TRUE; 
}

gboolean ew_background_tasks(gpointer user_data) {
    time_t timeNow; time(&timeNow);
    if (timeNow - timeLastBeat >= HeartBeatInt) { timeLastBeat = timeNow; Status(TypeHeartBeat, 0, ""); }
    if (g_history_needs_saving) {
        SaveHistoryFile(tree_global); g_history_needs_saving = FALSE;
    }
    int flag = tport_getflag(&InRegion);
    if (flag == TERMINATE || flag == MyPid) { gtk_main_quit(); return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr, "Uso: %s <configfile.d>\n", argv[0]); exit(1); }
    if (ReadConfig(argv[1]) != 0) exit(1);
    setenv("TZ", "GMT", 1); tzset(); logit_init(argv[1], 0, 1024, LogFile); MyPid = getpid();
    gtk_init(&argc, &argv); setlocale(LC_NUMERIC, "C");

    GtkCssProvider *provider = gtk_css_provider_new();
    
    /* FIX 2: Mejor contraste para el botón de Repick (Verde claro pastel #90ee90 con texto negro) */
    gtk_css_provider_load_from_data(provider,
        "treeview grid-line { border-color: #555555; }\n"
        "#btn_repick { background-color: #cce5ff; color: #0a58ca; font-weight: bold; border-radius: 4px; }\n"
        "#btn_relocate { background-color: #90ee90; color: #000000; font-weight: bold; border-radius: 4px; }\n"
        "#btn_fetch { background-color: #e2e3e5; font-weight: bold; border-radius: 4px; }\n"
        "#btn_filter { background-color: #28a745; color: #ffffff; font-weight: bold; padding: 2px 10px; border-radius: 4px; }", -1, NULL);
        
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    ConnectToEarthworm(); LoadStationsFromFile();

    window_global = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Hypocenter database picker (EW8)");
    gtk_window_set_default_size(GTK_WINDOW(window_global), 1100, 750); 
    g_signal_connect(window_global, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_add_events(window_global, GDK_KEY_PRESS_MASK);
    g_signal_connect(window_global, "key-press-event", G_CALLBACK(on_key_press), NULL);
    signal(SIGPIPE, SIG_IGN);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2); gtk_container_add(GTK_CONTAINER(window_global), vbox);
    GtkWidget *sw_lista = gtk_scrolled_window_new(NULL, NULL); gtk_widget_set_size_request(sw_lista, -1, 250);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_lista), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    GtkListStore *store = gtk_list_store_new(20, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_INT, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 14, GTK_SORT_DESCENDING);

    tree_global = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tree_global), GTK_TREE_VIEW_GRID_LINES_BOTH);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_global));
    g_signal_connect(selection, "changed", G_CALLBACK(on_row_selected), NULL);

    const char *headers[] = {"Date", "O-time", "Lat.", "Lon.", "Dep", "Res", "Azm", "#Stn", "ID", "Ml", "Mwp"};
    for (int i = 0; i < 11; i++) {
        GtkCellRenderer *rend = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(headers[i], rend, "text", i, NULL);
        g_object_set(rend, "xalign", 0.5, NULL); gtk_tree_view_column_set_alignment(col, 0.5); 
        gtk_tree_view_column_set_expand(col, TRUE); gtk_tree_view_column_set_cell_data_func(col, rend, color_rows_func, NULL, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_global), col);
    }
    gtk_container_add(GTK_CONTAINER(sw_lista), tree_global); gtk_box_pack_start(GTK_BOX(vbox), sw_lista, FALSE, FALSE, 0);

    GtkWidget *sw_ondas = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_ondas), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_box_pack_start(GTK_BOX(vbox), sw_ondas, TRUE, TRUE, 0);

    canvas_global = gtk_drawing_area_new(); gtk_widget_set_size_request(canvas_global, -1, 600); 
    gtk_widget_add_events(canvas_global, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(canvas_global), "button-press-event", G_CALLBACK(on_canvas_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(sw_ondas), canvas_global);
    g_signal_connect(G_OBJECT(canvas_global), "draw", G_CALLBACK(on_draw_signal), NULL);

    GtkWidget *hbox_bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hbox_bottom, 10); gtk_widget_set_margin_end(hbox_bottom, 10); gtk_widget_set_margin_bottom(hbox_bottom, 5);
    GtkWidget *hbox_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    
    btn_repick = gtk_button_new_with_label("Repick mode"); gtk_widget_set_name(btn_repick, "btn_repick"); gtk_widget_set_sensitive(btn_repick, FALSE); 
    g_signal_connect(btn_repick, "clicked", G_CALLBACK(on_btn_repick_clicked), NULL);

    box_fetch = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    entry_dist = gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(entry_dist), 5); gtk_entry_set_text(GTK_ENTRY(entry_dist), "500");
    entry_time = gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(entry_time), 5); gtk_entry_set_text(GTK_ENTRY(entry_time), "2");
    GtkWidget *btn_fetch = gtk_button_new_with_label("Fetch from WS"); gtk_widget_set_name(btn_fetch, "btn_fetch");
    g_signal_connect(btn_fetch, "clicked", G_CALLBACK(on_btn_fetch_clicked), NULL);
    
    gtk_box_pack_start(GTK_BOX(box_fetch), gtk_label_new("Distance (km):"), FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(box_fetch), entry_dist, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_fetch), gtk_label_new("  Window (min):"), FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(box_fetch), entry_time, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_fetch), btn_fetch, FALSE, FALSE, 5);

    /* --- GESTOR DE FILTRO DINAMICO (portado de EW7 new_hypo_display) --- */
    GtkWidget *box_filter = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    combo_filter = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter), "Raw (No Filter)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter), "High-Pass");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter), "Low-Pass");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter), "Band-Pass");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_filter), 0);
    g_signal_connect(combo_filter, "changed", G_CALLBACK(on_filter_changed), NULL);

    entry_freq1 = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry_freq1), 4);
    gtk_entry_set_text(GTK_ENTRY(entry_freq1), "0.7");
    gtk_widget_set_sensitive(entry_freq1, FALSE);

    entry_freq2 = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry_freq2), 4);
    gtk_entry_set_text(GTK_ENTRY(entry_freq2), "2.0");
    gtk_widget_set_sensitive(entry_freq2, FALSE);

    combo_order = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_order), "2");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_order), "4");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_order), 1);
    gtk_widget_set_sensitive(combo_order, FALSE);

    btn_apply_filter = gtk_button_new_with_label("Apply");
    gtk_widget_set_name(btn_apply_filter, "btn_filter");
    gtk_widget_set_sensitive(btn_apply_filter, FALSE);
    g_signal_connect(btn_apply_filter, "clicked", G_CALLBACK(on_btn_apply_filter_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(box_filter), gtk_label_new("  Filter:"), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box_filter), combo_filter, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box_filter), gtk_label_new(" F1(Hz):"), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box_filter), entry_freq1, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box_filter), gtk_label_new(" F2(Hz):"), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box_filter), entry_freq2, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box_filter), gtk_label_new(" Ord:"), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box_filter), combo_order, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(box_filter), btn_apply_filter, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(hbox_center), btn_repick, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(hbox_center), box_filter, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox_center), box_fetch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_bottom), hbox_center, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(vbox), hbox_bottom, FALSE, FALSE, 0);

    cargar_sismos_iniciales(tree_global);
    gtk_widget_show_all(window_global); gtk_widget_hide(box_fetch);
    
    g_timeout_add(1000, ew_background_tasks, NULL);
    g_timeout_add(500, escuchar_anillo_earthworm, tree_global);
    g_timeout_add(3000, waveform_reload_timer, NULL);

    gtk_main(); 
    
    tport_detach(&InRegion); tport_detach(&PRegion);
    if (StaArray) {
        for (int i = 0; i < NumEstaciones; i++) {
            if (StaArray[i].plRawCircBuff) free(StaArray[i].plRawCircBuff);
            if (StaArray[i].plFiltCircBuff) free(StaArray[i].plFiltCircBuff);
        }
        free(StaArray);
    }
    return 0;
}
