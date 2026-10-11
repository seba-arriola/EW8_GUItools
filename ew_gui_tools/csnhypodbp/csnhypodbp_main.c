#include "csnhypodbp.h"
#include "csnhypodbp_locator.h"

/* Definicion de Variables Globales */
char MyModName[MAX_STR];
char InRingName[MAX_STR];
char InRingRawName[MAX_STR];   /* 2.º anillo de entrada (csnloc crudo, opcional) */
char OutRingName[MAX_STR];
char InRingPickName[MAX_STR];  /* anillo de picks a leer (TYPE_PICK_SCNL) */
char DbEnvFile[MAX_STR];       /* credenciales PostgreSQL (historial) */
int  g_db_hours = 24;          /* ventana de historial de la BD (horas) */
char TauModel[MAX_STR];        /* modelo de tiempos de viaje (default iasp91) */
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
SHM_INFO InRegionRaw;    
SHM_INFO InRegionPick;   
SHM_INFO PRegion;        
MSG_LOGO GetLogo[2]; 
MSG_LOGO PickLogo[1]; 

EVENT_PICK_CACHE PickCache[MAX_CACHED_EVENTS];
int pick_cache_idx = 0;

DEV_STATION *StaArray = NULL; 
int     bHasData[MAX_ESTA]; 
double  g_StaDist[MAX_ESTA]; 
double  g_StaTheoP[MAX_ESTA]; 
int     NumEstaciones = 0;
int     selected_qid = 0;
int     selected_mod = 0;
int     selected_version = 0;
double  selected_otime = 0;
double  selected_depth = 0;
char    selected_id[32] = "None";
double  g_pick_window_sec = 600.0;   /* PickCaptureWindowSec */
char    g_pick_phase = 'P';          /* fase a picar en modo Repick */
WS_MENU_QUEUE_REC ws_menu; 

EwGuiCanvas *canvas_global = NULL;
GListStore *g_store_hypo = NULL;
GListStore *g_store_events = NULL;
GListStore *g_store_detail = NULL;
GtkSingleSelection *g_selection_hypo = NULL;
GtkSingleSelection *g_selection_events = NULL;
GtkSingleSelection *g_selection_detail = NULL;
GtkWidget *g_notebook = NULL;
GApplication *g_app = NULL;
GtkWidget *tree_global = NULL;
GtkWidget *tree_detail = NULL;

/* Agrupamiento (config .d; defaults del plan). */
double g_group_pick_tol_sec = 0.1;
int    g_group_min_match_picks = 3;
int    g_group_max_solutions = 100;

/* Config de la pestaña Ondas (channelsOrder/componentsOrder/locators). */
char g_channels_order[128] = "HH BH HN LH";
char g_components_order[64] = "Z N E";
CsnhypodbpLocator g_locators[MAX_LOCATORS];
int  g_nlocators = 0;
char g_active_locator[32] = "";
char g_manual_state_file[MAX_STR] = "csnhypodbp_manual.events";
int  g_locator_timeout_sec = 180;
int  g_prefetch_neighbors = 2;
int  g_hide_empty = 0;   /* HideEmptyStations: ocultar estaciones sin datos */
/* Backend de formas de onda: 0=wave_serverV, 1=store, 2=auto (store->ws). */
int  g_wave_backend = 2;
char g_store_root[512] = "";
int  g_fetch_threads = 4;
/* Colores de las fases (pColor/sColor del .d): marca + circulo del indicador. */
double g_color_p[3] = { 1.0, 0.0, 0.0 };   /* P: rojo */
double g_color_s[3] = { 0.0, 0.6, 0.0 };   /* S: verde (marca S actual) */
GtkWidget *window_global = NULL; 

double g_max_dist_km = 500.0; 
double   g_zoom_factor = 1.0; 
double   g_dWindowStart = 0;
double   g_dScreenTime = 120.0; 
int      g_margin_left = 160; 
int      g_spacing = 100;

StaNode g_SortedNodes[MAX_ESTA];
int     g_NumSortedNodes = 0;
gboolean g_history_needs_saving = FALSE; 

/* Lista de estaciones (pestaña Ondas). */
char g_selected_station[16] = "";
GtkWidget *g_list_stations = NULL;
int  g_rebuilding_stations = 0;   /* 1 mientras rebuild_station_list repuebla */

/* Vista de ondas: zoom por dos clics, modo de clic y eje de tiempo UTC. */
CsnhypodbpView      g_view;
CsnhypodbpClickMode g_click_mode = CSNHYPODBP_CLICK_ZOOM;
GtkWidget          *g_help_label = NULL;
EwGuiCanvas        *g_axis_canvas = NULL;
double              g_win_start_cur = 0.0;
double              g_cursor_x = 0.0, g_cursor_y = 0.0;  /* puntero en el canvas */
int                 g_cursor_valid = 0;                  /* 1 = sobre el canvas */

/* Preview de locate (solucion candidata). */
GtkWidget *g_preview_box = NULL;
GtkWidget *g_preview_label = NULL;
CsnhypodbpSolution g_preview_sol;
CsnhypodbpMagOut g_preview_mag;
gboolean g_preview_valid = FALSE;

int      g_filter_type = 0;      /* 0=Raw, 1=HP, 2=LP, 3=BP */
double   g_filter_f1 = 0.7;      /* F1 (Hz) */
double   g_filter_f2 = 2.0;      /* F2 (Hz) */
int      g_filter_order = 4;     /* orden IIR (2 o 4) */
double   g_align_lead = 20.0;    /* segundos antes de la onda P */

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

/* kom no tiene k_rest(): k_str() devuelve UN token. channelsOrder/componentsOrder
 * son listas separadas por espacios, asi que se concatenan los tokens restantes
 * de la linea. El ultimo k_str() deja ierr=-17 (fin de linea): se limpia para
 * que el chequeo `if (k_err())` del bucle de parseo no aborte. */
static void k_rest_join(char *out, size_t n)
{
    char *t;
    size_t off = 0;
    if (!out || n == 0) return;
    out[0] = '\0';
    while ((t = k_str()) != NULL) {
        int w = snprintf(out + off, n - off, "%s%s", off ? " " : "", t);
        if (w < 0 || (size_t)w >= n - off) break;
        off += (size_t)w;
    }
    (void)k_err();
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
        else if (k_its("InRingRaw")) { str = k_str(); if (str) strcpy(InRingRawName, str); }
        else if (k_its("InRingPick")) { str = k_str(); if (str) strcpy(InRingPickName, str); }
        else if (k_its("TauModel")) { str = k_str(); if (str) strcpy(TauModel, str); }
        else if (k_its("PickCaptureWindowSec")) { g_pick_window_sec = k_val(); }
        else if (k_its("GroupPickTolSec")) { g_group_pick_tol_sec = k_val(); }
        else if (k_its("GroupMinMatchPicks")) { g_group_min_match_picks = k_int(); }
        else if (k_its("GroupMaxSolutions")) { g_group_max_solutions = k_int(); }
        else if (k_its("DbEnvFile")) { str = k_str(); if (str) strcpy(DbEnvFile, str); }
        else if (k_its("DbHistoryHours")) { g_db_hours = k_int(); }
        else if (k_its("WsTimeout")) { WsTimeout = k_int(); } 
        else if (k_its("WaveBackend")) {
            str = k_str();
            if (str && (str[0] == 's' || str[0] == 'S'))      g_wave_backend = 1;
            else if (str && (str[0] == 'a' || str[0] == 'A')) g_wave_backend = 2;
            else                                              g_wave_backend = 0;
        }
        else if (k_its("StoreRoot")) { str = k_str(); if (str) snprintf(g_store_root, sizeof(g_store_root), "%s", str); }
        else if (k_its("FetchThreads")) { g_fetch_threads = k_int(); if (g_fetch_threads < 1) g_fetch_threads = 1; }
        else if (k_its("ModuleLabel")) {
            int mid = k_int(); str = k_str();
            if (str && g_nmodlabels < MAX_MODLABELS) {
                g_modlabels[g_nmodlabels].id = mid;
                strncpy(g_modlabels[g_nmodlabels].label, str, sizeof(g_modlabels[0].label) - 1);
                g_modlabels[g_nmodlabels].label[sizeof(g_modlabels[0].label) - 1] = '\0';
                g_nmodlabels++;
            }
        }
        else if (k_its("channelsOrder")) {
            char buf[128]; k_rest_join(buf, sizeof(buf));
            if (buf[0]) snprintf(g_channels_order, sizeof(g_channels_order), "%s", buf);
        }
        else if (k_its("componentsOrder")) {
            char buf[64]; k_rest_join(buf, sizeof(buf));
            if (buf[0]) snprintf(g_components_order, sizeof(g_components_order), "%s", buf);
        }
        else if (k_its("locator")) {
            char *nm = k_str(), *bn = k_str(), *cf = k_str();
            if (nm && bn && cf)
                csnhypodbp_locator_add(g_locators, &g_nlocators, MAX_LOCATORS, nm, bn, cf);
        }
        else if (k_its("ManualStateFile")) { str = k_str(); if (str) snprintf(g_manual_state_file, sizeof(g_manual_state_file), "%s", str); }
        else if (k_its("LocatorTimeoutSec")) { g_locator_timeout_sec = k_int(); }
        else if (k_its("PrefetchNeighbors")) { g_prefetch_neighbors = k_int(); }
        else if (k_its("HideEmptyStations")) { g_hide_empty = k_int() ? 1 : 0; }
        else if (k_its("pColor")) {
            str = k_str();
            if (str && csnhypodbp_view_parse_color(str, g_color_p) != 0)
                logit("et", "csnhypodbp: pColor <%s> invalido, uso default\n", str);
        }
        else if (k_its("sColor")) {
            str = k_str();
            if (str && csnhypodbp_view_parse_color(str, g_color_s) != 0)
                logit("et", "csnhypodbp: sColor <%s> invalido, uso default\n", str);
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
    PickLogo[0].instid = InstWild; PickLogo[0].mod = ModWild; PickLogo[0].type = TypePickSCNL;

    tport_attach(&InRegion, InRingKey); tport_attach(&PRegion, OutRingKey);
    if (InRingRawName[0]) {
        long RawKey = GetKey(InRingRawName);
        if (RawKey != -1) { tport_attach(&InRegionRaw, RawKey); }
        else { logit("e", "csnhypodbp: InRingRaw %s no existe\n", InRingRawName); InRingRawName[0] = '\0'; }
    }
    /* Lectura del anillo de picks (token 11 fase / token 12 origen A/M). Por
     * defecto el mismo que OutRing (PICK_RING). */
    if (!InRingPickName[0]) strcpy(InRingPickName, OutRingName);
    {
        long PickKey = InRingPickName[0] ? GetKey(InRingPickName) : -1;
        if (PickKey != -1) { tport_attach(&InRegionPick, PickKey); }
        else { logit("e", "csnhypodbp: InRingPick %s no existe\n", InRingPickName); InRingPickName[0] = '\0'; }
    }
    logit("t", "=== csnhypodbp: Conectado a %s%s%s, %s (picks) y %s ===\n",
          InRingName, InRingRawName[0] ? " + " : "", InRingRawName, InRingPickName, OutRingName);
    ws_menu.head = NULL; ws_menu.tail = NULL;
}

static void drain_ring(SHM_INFO *region, GtkWidget *tree) {
    static char msg[65000]; MSG_LOGO reclogo; long recsize; int res;
    while (TRUE) {
        res = tport_getmsg(region, GetLogo, 2, &reclogo, &recsize, msg, sizeof(msg)-1);
        if (res == GET_NONE) break;
        if (res == GET_OK || res == GET_NOTRACK || res == GET_MISS) {
            msg[recsize] = '\0';
            if (reclogo.type == TypeHyp2000Arc) procesar_mensaje_sismo(tree, msg, reclogo.mod);
            else if (reclogo.type == TypeMagnitude) procesar_mensaje_mag(tree, msg);
        }
    }
}

static int station_index(const char *sta) {
    if (!StaArray || !sta) return -1;
    for (int i = 0; i < NumEstaciones; i++)
        if (!strcmp(StaArray[i].szStation, sta)) return i;
    return -1;
}

/* Captura las picadas del PICK_RING (fase + origen A/M) en la estacion
 * correspondiente, para la pestana de ondas. */
static void drain_pick_ring(SHM_INFO *region) {
    static char msg[65000]; MSG_LOGO reclogo; long recsize; int res;
    while (TRUE) {
        res = tport_getmsg(region, PickLogo, 1, &reclogo, &recsize, msg, sizeof(msg)-1);
        if (res == GET_NONE) break;
        if (res == GET_OK || res == GET_NOTRACK || res == GET_MISS) {
            msg[recsize] = '\0';
            if (reclogo.type == TypePickSCNL) {
                CsnhypodbpTracePick pk;
                if (csnhypodbp_pick_parse(msg, &pk)) {
                    int idx = station_index(pk.sta);
                    if (idx >= 0) csnhypodbp_cap_add(&StaArray[idx].pickcap, &pk);
                }
            }
        }
    }
}

gboolean escuchar_anillo_earthworm(gpointer user_data) {
    GtkWidget *tree = GTK_WIDGET(user_data);
    drain_ring(&InRegion, tree);
    if (InRingRawName[0]) drain_ring(&InRegionRaw, tree);
    if (InRingPickName[0]) drain_pick_ring(&InRegionPick);
    return TRUE;
}

gboolean ew_background_tasks(gpointer user_data) {
    static EwGuiHeartbeat hb = {0};
    time_t timeNow; time(&timeNow);
    if (ewgui_heartbeat_due(&hb, (double)timeNow, HeartBeatInt)) { Status(TypeHeartBeat, 0, ""); }
    /* El estado de eventos lo persiste csnloc (unico escritor). csnhypodbp
       solo lo lee al arrancar para recuperarse. */
    if (ewgui_ring_should_quit(&InRegion, MyPid)) { if (g_app) g_application_quit(g_app); return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}

static void col_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data) {
    (void)f; (void)data;
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
    gtk_list_item_set_child(item, lbl);
}
static void col_unbind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data) {
    (void)f; (void)data;
    g_object_set_data(G_OBJECT(item), "cell-binding", NULL);
}

/* Bind por NOMBRE de propiedad (para estado/nsol); data = nombre persistente. */
static void col_bind_name(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data) {
    (void)f;
    const char *prop = (const char *)data;
    GtkWidget *lbl = gtk_list_item_get_child(item);
    CsnhypodbpRow *row = CSNHYPODBP_ROW(gtk_list_item_get_item(item));
    GBinding *b;
    if (!row || !lbl || !prop) return;
    b = g_object_bind_property(row, prop, lbl, "label", G_BINDING_SYNC_CREATE);
    g_object_set_data_full(G_OBJECT(item), "cell-binding", b, g_object_unref);
}

static void add_text_column(GtkWidget *view, const char *title, const char *prop) {
    GtkListItemFactory *f = gtk_signal_list_item_factory_new();
    g_signal_connect(f, "setup", G_CALLBACK(col_setup), NULL);
    g_signal_connect(f, "bind", G_CALLBACK(col_bind_name), (gpointer)prop);
    g_signal_connect(f, "unbind", G_CALLBACK(col_unbind), NULL);
    GtkColumnViewColumn *col = gtk_column_view_column_new(title, f);
    gtk_column_view_column_set_expand(col, TRUE);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), col);
}

/* Exporta la vista de trazas a SVG/PDF/PNG (diálogo Guardar). */
static void act_export(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; (void)ud;
    ewgui_export_dialog_run(canvas_global, GTK_WINDOW(window_global));
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

/* Conmutador "Hide stations without data" (menu del headerbar). Modo dinamico:
   filtra la lista, no borra filas (una estacion con datos reaparece). */
static void on_toggle_hide_empty(GSimpleAction *action, GVariant *param, gpointer user_data) {
    GVariant *st;
    gboolean active;
    (void)param; (void)user_data;
    st = g_action_get_state(G_ACTION(action));
    active = !g_variant_get_boolean(st);
    g_variant_unref(st);
    g_simple_action_set_state(action, g_variant_new_boolean(active));
    g_hide_empty = active ? 1 : 0;
    rebuild_station_list();
    actualizar_altura_canvas();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

/* Divide el panel maestro/detalle por la mitad en el primer mapeo (una sola
   vez, para que luego el usuario pueda arrastrar el separador). */
static gboolean paned_set_half_idle(gpointer data)
{
    GtkWidget *paned = GTK_WIDGET(data);
    int h = gtk_widget_get_height(paned);
    if (h > 0) gtk_paned_set_position(GTK_PANED(paned), h / 2);
    return G_SOURCE_REMOVE;
}
static void paned_map_half(GtkWidget *paned, gpointer data)
{
    (void)data;
    g_idle_add(paned_set_half_idle, paned);
}

/* --- Menú del headerbar dependiente de la vista (Eventos vs Ondas) --- */
static const EwMenuItem view_items[] = {
    { "Export view...", "win.export", NULL, 0 },
    { "Dark mode", "win.dark-mode", NULL, 0 },
};
static const EwMenuItem wave_items[] = {
    { "Filters...", "win.filters", NULL, 0 },
    { "Fetch waveforms", "win.fetch", NULL, 0 },
    { "Time window...", "win.timewin", NULL, 0 },
    { "Hide stations without data", "win.hide-empty", NULL, 0 },
};
static const EwMenuItem colour_items[] = {
    { "P phase", "win.colour", "i", 1 },
    { "S phase", "win.colour", "i", 2 },
};
static const EwMenuGroup wave_groups[] = {
    { "Waveforms", wave_items, 4 },
    { "Colours", colour_items, 2 },
    { "View", view_items, 2 },
};
static const EwMenuGroup base_groups[] = { { "View", view_items, 2 } };

static GtkWidget *g_menu_btn = NULL;

/* Adaptadores barra inferior -> menú ☰ (widget=NULL; los handlers solo usan
   `data`, mismo patrón que csntvp). */
static void act_filters(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; (void)ud; on_btn_filters_clicked(NULL, NULL); }
static void act_fetch(GSimpleAction *a, GVariant *p, gpointer ud)   { (void)a; (void)p; (void)ud; on_btn_fetch_clicked(NULL, NULL); }
static void act_timewin(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; (void)ud; on_btn_timewin_clicked(NULL, NULL); }

/* En la pestaña Ondas (página 1) el ☰ muestra el grupo Waveforms. */
static void on_notebook_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer ud) {
    (void)nb; (void)page; (void)ud;
    if (!g_menu_btn) return;
    if (num == 1) ewgui_menu_button_set_groups(g_menu_btn, wave_groups, 3);
    else          ewgui_menu_button_set_groups(g_menu_btn, base_groups, 1);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    static gboolean css_done = FALSE;
    (void)user_data;

    /* GTK re-aplica setlocale(LC_ALL,"") al inicializar, dejando LC_NUMERIC
     * en el locale del entorno (p. ej. es_ES -> coma decimal). Los mensajes
     * TYPE_MAGNITUDE de EarthWorm se leen con sscanf (rd_mag) y usan punto,
     * asi que restauramos C DESPUES del arranque de GTK. */
    setlocale(LC_NUMERIC, "C");

    csnhypodbp_view_set_default(&g_view, g_dScreenTime);

    if (!css_done) {
        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_string(provider,
            "#btn_repick { background-image: none; box-shadow: none; border: none; background-color: #cce5ff; color: #0a58ca; font-weight: bold; border-radius: 4px; }\n"
            "#btn_relocate { background-image: none; box-shadow: none; border: none; background-color: #90ee90; color: #000000; font-weight: bold; border-radius: 4px; }\n"
            "#btn_fetch { background-image: none; box-shadow: none; border: none; background-color: #e2e3e5; font-weight: bold; border-radius: 4px; }\n"
            "#btn_filter { background-image: none; box-shadow: none; border: none; background-color: #28a745; color: #ffffff; font-weight: bold; padding: 2px 10px; border-radius: 4px; }\n"
            ".section-title { font-weight: bold; }\n"
            ".master-events row:selected { background-image: none; background-color: #ffd54f; color: #000000; }\n"
            ".detail-solutions { font-size: 0.82em; }\n"
            ".detail-solutions row { background-image: none; background-color: #fff8e1; }\n"
            ".detail-solutions row:selected { background-image: none; background-color: #ffd54f; color: #000000; }");
        gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        css_done = TRUE;
    }

    window_global = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Hypocenter database picker (EW8)");
    gtk_window_set_default_size(GTK_WINDOW(window_global), 1100, 750);
    /* Fase CAPTURE: la ventana ve las teclas antes que el widget con foco.
       Solo se consumen Shift+flechas / p / S / Esc en la pestaña Ondas; el
       resto (p. ej. ↑/↓ para navegar la lista de estaciones) pasa intacto. */
    GtkEventController *keyctl = gtk_event_controller_key_new();
    g_signal_connect(keyctl, "key-pressed", G_CALLBACK(on_key_press), NULL);
    gtk_event_controller_set_propagation_phase(keyctl, GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(window_global, keyctl);

    /* --- Acciones GIO + menú del headerbar --- */
    GSimpleActionGroup *actions = g_simple_action_group_new();
    GSimpleAction *dark = g_simple_action_new_stateful("dark-mode", NULL,
                                                       g_variant_new_boolean(FALSE));
    g_signal_connect(dark, "activate", G_CALLBACK(on_toggle_dark), NULL);
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(dark));
    {
        GSimpleAction *hide = g_simple_action_new_stateful("hide-empty", NULL,
                                                           g_variant_new_boolean(g_hide_empty ? TRUE : FALSE));
        g_signal_connect(hide, "activate", G_CALLBACK(on_toggle_hide_empty), NULL);
        g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(hide));
    }
    ewgui_action_add(G_ACTION_MAP(actions), "export", NULL, act_export, window_global);
    ewgui_action_add(G_ACTION_MAP(actions), "filters", NULL, act_filters, window_global);
    ewgui_action_add(G_ACTION_MAP(actions), "fetch", NULL, act_fetch, window_global);
    ewgui_action_add(G_ACTION_MAP(actions), "timewin", NULL, act_timewin, window_global);
    ewgui_action_add(G_ACTION_MAP(actions), "colour", "i", act_colour, window_global);
    gtk_widget_insert_action_group(window_global, "win", G_ACTION_GROUP(actions));
    g_object_unref(actions);

    g_menu_btn = ewgui_menu_button_new(base_groups, 1);

    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), g_menu_btn);

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), vbox);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_global), toolbar_view);

    g_notebook = gtk_notebook_new();
    gtk_widget_set_vexpand(g_notebook, TRUE);
    g_signal_connect(g_notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), NULL);
    gtk_box_append(GTK_BOX(vbox), g_notebook);

    /* --- Pestaña "Eventos": maestro (eventos) + detalle (soluciones) --- */
    GtkWidget *page_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    g_store_hypo   = g_list_store_new(CSNHYPODBP_TYPE_ROW);
    g_store_events = g_list_store_new(CSNHYPODBP_TYPE_ROW);
    g_store_detail = g_list_store_new(CSNHYPODBP_TYPE_ROW);

    /* Nombres de propiedad persistentes (los data de los bindings). */
    static const char *cprop[15] = {"c0","c1","c2","c3","c4","c5","c6","c7",
                                    "c8","c9","c10","c11","c12","c13","c14"};

    /* ---- Maestro: una fila por evento, ordenada por hora origen DESC ---- */
    {
        GtkNumericSorter *sorter = gtk_numeric_sorter_new(
            gtk_property_expression_new(CSNHYPODBP_TYPE_ROW, NULL, "otime"));
        GtkSortListModel *sorted;
        gtk_numeric_sorter_set_sort_order(sorter, GTK_SORT_DESCENDING);
        sorted = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(g_store_events)), GTK_SORTER(sorter));
        g_selection_events = gtk_single_selection_new(G_LIST_MODEL(sorted));
    }
    gtk_single_selection_set_autoselect(g_selection_events, FALSE);
    gtk_single_selection_set_can_unselect(g_selection_events, TRUE);

    tree_global = gtk_column_view_new(GTK_SELECTION_MODEL(g_selection_events));
    gtk_widget_add_css_class(tree_global, "master-events");
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(tree_global), TRUE);
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(tree_global), TRUE);
    g_signal_connect(g_selection_events, "notify::selected", G_CALLBACK(on_event_selected), NULL);
    {
        const char *mheaders[15] = {"Date", "O-time", "Lat.", "Lon.", "Dep", "Res", "Azm", "#Stn", "ID", "Ml", "Mwp", "Mb", "Ms", "Mod", "Ver"};
        for (int i = 0; i < 15; i++) add_text_column(tree_global, mheaders[i], cprop[i]);
        add_text_column(tree_global, "N", "nsol");
        add_text_column(tree_global, "Est.", "estado");
    }

    /* ---- Detalle: soluciones del evento seleccionado ---- */
    g_selection_detail = gtk_single_selection_new(G_LIST_MODEL(g_store_detail));
    gtk_single_selection_set_autoselect(g_selection_detail, FALSE);
    gtk_single_selection_set_can_unselect(g_selection_detail, TRUE);
    g_selection_hypo = g_selection_detail;   /* compat */

    tree_detail = gtk_column_view_new(GTK_SELECTION_MODEL(g_selection_detail));
    gtk_widget_add_css_class(tree_detail, "detail-solutions");
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(tree_detail), TRUE);
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(tree_detail), TRUE);
    g_signal_connect(g_selection_detail, "notify::selected", G_CALLBACK(on_row_selected), NULL);
    {
        GtkGesture *dclick = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dclick), 1);
        g_signal_connect(dclick, "pressed", G_CALLBACK(on_list_double_click), NULL);
        gtk_widget_add_controller(tree_detail, GTK_EVENT_CONTROLLER(dclick));
    }
    {
        const char *dheaders[] = {"Mod", "Ver", "Est.", "Date", "O-time", "Lat.", "Lon.", "Dep", "Res", "#Stn", "ID", "Ml", "Mwp", "Mb", "Ms"};
        const char *dprops[]   = {"c13", "c14", "estado", "c0", "c1", "c2", "c3", "c4", "c5", "c7", "c8", "c9", "c10", "c11", "c12"};
        for (int i = 0; i < 15; i++) add_text_column(tree_detail, dheaders[i], dprops[i]);
    }

    GtkWidget *sw_master = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_master), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw_master, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_master), tree_global);

    GtkWidget *sw_detail = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_detail), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw_detail, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_detail), tree_detail);

    /* Titulos de seccion + separador evidente entre eventos y soluciones. */
    GtkWidget *lbl_master = gtk_label_new("Preferred solutions");
    gtk_widget_add_css_class(lbl_master, "section-title");
    gtk_widget_set_halign(lbl_master, GTK_ALIGN_START);
    gtk_widget_set_margin_start(lbl_master, 6);
    GtkWidget *master_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_vexpand(master_box, TRUE);
    gtk_box_append(GTK_BOX(master_box), lbl_master);
    gtk_box_append(GTK_BOX(master_box), sw_master);

    GtkWidget *lbl_detail = gtk_label_new("Historical solutions");
    gtk_widget_add_css_class(lbl_detail, "section-title");
    gtk_widget_set_halign(lbl_detail, GTK_ALIGN_START);
    gtk_widget_set_margin_start(lbl_detail, 6);
    GtkWidget *detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_vexpand(detail_box, TRUE);
    gtk_box_append(GTK_BOX(detail_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(detail_box), lbl_detail);
    gtk_box_append(GTK_BOX(detail_box), sw_detail);

    /* Cuadro que subraya que las soluciones historicas dependen del evento
       seleccionado arriba (fondo tenue + borde). */
    GtkWidget *detail_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(detail_frame, "detail-frame");
    gtk_widget_set_margin_start(detail_frame, 4);
    gtk_widget_set_margin_end(detail_frame, 4);
    gtk_widget_set_margin_bottom(detail_frame, 4);
    gtk_widget_set_vexpand(detail_frame, TRUE);
    gtk_frame_set_child(GTK_FRAME(detail_frame), detail_box);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_start_child(GTK_PANED(paned), master_box);
    gtk_paned_set_end_child(GTK_PANED(paned), detail_frame);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_position(GTK_PANED(paned), 340);   /* respaldo; el map lo pone a la mitad */
    /* Reparto inicial ~50/50 (y el detalle llega hasta el fondo). */
    g_signal_connect(paned, "map", G_CALLBACK(paned_map_half), NULL);
    gtk_box_append(GTK_BOX(page_list), paned);
    gtk_notebook_append_page(GTK_NOTEBOOK(g_notebook), page_list, gtk_label_new("Eventos"));

    /* --- Pestaña "Ondas": las trazas --- */
    GtkWidget *page_waves = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *sw_ondas = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_ondas), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_widget_set_vexpand(sw_ondas, TRUE);
    gtk_widget_set_hexpand(sw_ondas, TRUE);

    /* Columna derecha: canvas scrolleable + eje de tiempo UTC (fijo abajo). */
    GtkWidget *waves_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(waves_right, TRUE);
    gtk_widget_set_hexpand(waves_right, TRUE);
    gtk_box_append(GTK_BOX(waves_right), sw_ondas);
    g_axis_canvas = ewgui_canvas_new();
    ewgui_canvas_set_draw(g_axis_canvas, on_draw_axis, NULL);
    {
        GtkWidget *axis_widget = ewgui_canvas_widget(g_axis_canvas);
        gtk_widget_set_size_request(axis_widget, -1, 26);
        /* Arrastre sobre el eje: izq->der zoom al rango, der->izq reset. */
        GtkGesture *adrag = gtk_gesture_drag_new();
        g_signal_connect(adrag, "drag-begin",  G_CALLBACK(on_axis_drag_begin),  NULL);
        g_signal_connect(adrag, "drag-update", G_CALLBACK(on_axis_drag_update), NULL);
        g_signal_connect(adrag, "drag-end",    G_CALLBACK(on_axis_drag_end),    NULL);
        gtk_widget_add_controller(axis_widget, GTK_EVENT_CONTROLLER(adrag));
        gtk_box_append(GTK_BOX(waves_right), axis_widget);
    }

    /* Panel izquierdo: lista de estaciones encajonada con scroll propio
       (mismo estilo que la tabla historica de Eventos). */
    GtkWidget *list_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(list_frame, "detail-frame");
    gtk_widget_set_size_request(list_frame, 190, -1);
    gtk_widget_set_vexpand(list_frame, TRUE);
    GtkWidget *sw_list = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_list), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw_list, TRUE);
    g_list_stations = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_list_stations), GTK_SELECTION_SINGLE);
    g_signal_connect(g_list_stations, "row-selected", G_CALLBACK(on_station_selected), NULL);
    {
        GtkGesture *lclick = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(lclick), 3);
        g_signal_connect(lclick, "pressed", G_CALLBACK(on_station_right_click), NULL);
        gtk_widget_add_controller(g_list_stations, GTK_EVENT_CONTROLLER(lclick));
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_list), g_list_stations);
    gtk_frame_set_child(GTK_FRAME(list_frame), sw_list);

    GtkWidget *waves_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(waves_hbox), list_frame);
    gtk_box_append(GTK_BOX(waves_hbox), waves_right);
    gtk_widget_set_vexpand(waves_hbox, TRUE);

    /* Preview de la solucion candidata (locate): oculto hasta que hay resultado. */
    g_preview_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(g_preview_box, 8);
    gtk_widget_set_margin_end(g_preview_box, 8);
    gtk_widget_set_margin_top(g_preview_box, 4);
    g_preview_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(g_preview_label), 0.0);
    gtk_widget_set_hexpand(g_preview_label, TRUE);
    {
        GtkWidget *pc = gtk_button_new_with_label("Confirm");
        GtkWidget *pd = gtk_button_new_with_label("Discard");
        gtk_widget_add_css_class(pc, "suggested-action");
        g_signal_connect(pc, "clicked", G_CALLBACK(on_preview_confirm), NULL);
        g_signal_connect(pd, "clicked", G_CALLBACK(on_preview_discard), NULL);
        gtk_box_append(GTK_BOX(g_preview_box), g_preview_label);
        gtk_box_append(GTK_BOX(g_preview_box), pc);
        gtk_box_append(GTK_BOX(g_preview_box), pd);
    }
    gtk_widget_set_visible(g_preview_box, FALSE);
    gtk_box_append(GTK_BOX(page_waves), g_preview_box);

    gtk_box_append(GTK_BOX(page_waves), waves_hbox);

    canvas_global = ewgui_canvas_new();
    ewgui_canvas_set_draw(canvas_global, on_draw_signal, NULL);
    GtkWidget *canvas_widget = ewgui_canvas_widget(canvas_global);
    gtk_widget_set_size_request(canvas_widget, -1, 600);
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_canvas_clicked), NULL);
    gtk_widget_add_controller(canvas_widget, GTK_EVENT_CONTROLLER(click));
    {
        GtkGesture *rclick = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), 3);
        g_signal_connect(rclick, "pressed", G_CALLBACK(on_canvas_right_click), NULL);
        gtk_widget_add_controller(canvas_widget, GTK_EVENT_CONTROLLER(rclick));
    }
    {
        /* Puntero: posicion para el indicador de picado (P/S junto al mouse). */
        GtkEventController *motion = gtk_event_controller_motion_new();
        g_signal_connect(motion, "motion", G_CALLBACK(on_canvas_motion), NULL);
        g_signal_connect(motion, "leave", G_CALLBACK(on_canvas_leave), NULL);
        gtk_widget_add_controller(canvas_widget, motion);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_ondas), canvas_widget);
    gtk_notebook_append_page(GTK_NOTEBOOK(g_notebook), page_waves, gtk_label_new("Ondas"));

    GtkWidget *hbox_bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hbox_bottom, 10); gtk_widget_set_margin_end(hbox_bottom, 10); gtk_widget_set_margin_bottom(hbox_bottom, 5);
    GtkWidget *hbox_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);

    /* Barra inferior (Ondas): Cancel | locators + locate. Filters / Fetch
       waveforms / Time window / Hide-empty viven en el menú ☰ de la vista. */
    {
        GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
        g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_btn_cancel_clicked), NULL);

        GtkWidget *btn_locators = gtk_button_new_with_label("locators");
        g_signal_connect(btn_locators, "clicked", G_CALLBACK(on_btn_locators_clicked), NULL);

        GtkWidget *btn_locate = gtk_button_new_with_label("locate");
        gtk_widget_add_css_class(btn_locate, "suggested-action");
        g_signal_connect(btn_locate, "clicked", G_CALLBACK(on_btn_locate_clicked), NULL);

        gtk_box_append(GTK_BOX(hbox_center), btn_cancel);
        gtk_box_append(GTK_BOX(hbox_center), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
        gtk_box_append(GTK_BOX(hbox_center), btn_locators);
        gtk_box_append(GTK_BOX(hbox_center), btn_locate);
        g_help_label = gtk_label_new("");
        gtk_box_append(GTK_BOX(hbox_center), g_help_label);
        csnhypodbp_update_help();
    }
    gtk_box_append(GTK_BOX(hbox_bottom), hbox_center);
    gtk_box_append(GTK_BOX(page_waves), hbox_bottom);

    cargar_sismos_iniciales(tree_global);
    cargar_sismos_db();
    csnhypodbp_load_manual_state();
    rebuild_station_list();
    gtk_window_present(GTK_WINDOW(window_global));

    g_timeout_add(1000, ew_background_tasks, NULL);
    g_timeout_add(500, escuchar_anillo_earthworm, tree_global);
    g_timeout_add(1000, waveform_reload_timer, NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr, "Uso: %s <configfile.d>\n", argv[0]); exit(1); }
    if (ReadConfig(argv[1]) != 0) exit(1);
    csnhypodbp_db_init(DbEnvFile, g_db_hours);
    setenv("TZ", "GMT", 1); tzset(); logit_init(argv[1], 0, 1024, LogFile); MyPid = getpid();
    logit("t", "csnhypodbp: config channelsOrder=[%s] componentsOrder=[%s] HideEmptyStations=%d\n",
          g_channels_order, g_components_order, g_hide_empty);
    csnhypodbp_log_colors();
    setlocale(LC_NUMERIC, "C");

    ConnectToEarthworm(); LoadStationsFromFile();

    /* Motor de tiempos de viaje (IASP91) para la P teorica. Se lee desde el
       cwd (EW_PARAMS), igual que csnloc; si falta, la teorica cae a otime. */
    if (!TauModel[0]) strcpy(TauModel, "iasp91");
    if (csnhypodbp_tt_init(TauModel) != 0)
        logit("e", "csnhypodbp: sin modelo de tiempos de viaje <%s> en el cwd; teorica = hora origen\n", TauModel);
    else
        logit("t", "csnhypodbp: motor de tiempos de viaje <%s> cargado\n", TauModel);

    signal(SIGPIPE, SIG_IGN);

    AdwApplication *app = adw_application_new("cl.csn.csnhypodbp", G_APPLICATION_NON_UNIQUE);
    g_app = G_APPLICATION(app);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(g_app, 0, NULL);

    tport_detach(&InRegion); tport_detach(&PRegion);
    if (StaArray) {
        for (int i = 0; i < NumEstaciones; i++) ewgui_trace_free(StaArray[i].trace);
        free(StaArray);
    }
    return status;
}
