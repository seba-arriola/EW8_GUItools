#include "ew_controller.h"

/* Configuration parameters read from the .d file
 *************************************************/
char MyModName[MAX_STR] = "MOD_CONTROL";   /* Earthworm module name */
char RingName[MAX_STR] = "CONTROL_RING";   /* name of the dedicated control ring */
char LogDir[MAX_STR] = "";                 /* directory of the module logs (EW_LOG wins) */
int  HeartBeatInt = 30;                    /* heartbeat interval (seconds) */
int  LogFile = 1;                          /* 1 = write log to disk, 0 = console only */
int  PollInt = 5;                          /* status request interval to startstop (seconds) */

pid_t MyPid;                               /* this process id */

/* Message types looked up in the earthworm.h tables
 ****************************************************/
unsigned char TypeHeartBeat = 0;   /* TYPE_HEARTBEAT */
unsigned char TypeReqStatus = 0;   /* TYPE_REQSTATUS (status request) */
unsigned char TypeStatus = 0;      /* TYPE_STATUS (startstop reply) */
unsigned char TypeStop = 0;        /* TYPE_STOP (stop a module) */
unsigned char TypeRestart = 0;     /* TYPE_RESTART (restart a module) */
unsigned char TypeReconfig = 0;    /* TYPE_RECONFIG (reconfigure) */

/* Ring connection globals
 *************************/
unsigned char MyInstId = 0;        /* Earthworm installation id */
unsigned char MyModId = 0;         /* Earthworm module id */
SHM_INFO Region;                   /* transport region of the control ring */
long g_ring_key = -1;              /* shared memory key of the control ring */
int  g_attached = 0;               /* 1 once attached to the ring */



EwCtrlStatus g_status = { .hostname = "-", .starttime = "-", .curtime = "-", .disk = "-", .version = "-" };

time_t g_last_status = 0;    /* time of the last status message received */

/* --- GTK --- */
GtkWidget *g_tree, *g_rings_tree;            /* tree views for modules and rings */
GListStore *g_store, *g_rings_store;
GtkSingleSelection *g_mod_sel = NULL;
GMainLoop *g_loop = NULL;       /* tree models */
GtkWidget *g_btn_start, *g_btn_restart, *g_btn_stop, *g_btn_reconfig, *g_btn_refresh;  /* action buttons */
GtkWidget *g_lbl_header, *g_lbl_statusbar;   /* header and status bar labels */
GtkWidget *g_combo, *g_logview;              /* module combo and log text view */
GtkTextBuffer *g_logbuf;                     /* log text buffer */

/* Configuration tab */
GtkWidget *g_cfg_combo, *g_cfg_grid, *g_lbl_cfgpath, *g_lbl_cfg_status;   /* config tab widgets */
GtkWidget *g_btn_cfg_save, *g_btn_cfg_reconfig, *g_btn_cfg_reload;        /* config buttons */
EwCfgFile g_cfg;                 /* configuration file being edited */
GtkWidget **g_cfg_entries;     /* entry widgets, one per editable line */
int g_cfg_loaded = 0;          /* 1 once a config file has been loaded */
int g_cfg_modidx = -1;         /* index of the module whose config is loaded */
int g_cfg_dirty = 0;           /* 1 if there are unsaved changes */
int g_cfg_suppress = 0;        /* 1 to suppress the combo "changed" handler */

int g_sel_idx = -1;            /* index of the selected module row */
int g_sel_pid = -1;            /* pid of the selected module (kept across refreshes) */
time_t g_last_user_click = 0;  /* time of the last user click on the tree */
GtkWidget *g_window;           /* main window */



static void enviar(unsigned char type, const char *payload)
{
   if (ewgui_ctrl_send(&Region, MyInstId, MyModId, type, payload) != 0)
      logit("t", "ew_controller: Error sending message type %d to the ring.\n", (int) type);
}

void pedir_estado(void)
{
   enviar(TypeReqStatus, "?");
}

void detener_mod(int pid)
{
   char buf[32];
   snprintf(buf, sizeof(buf), "%d", pid);
   enviar(TypeStop, buf);
}

void reiniciar_mod(int pid)
{
   char buf[32];
   snprintf(buf, sizeof(buf), "%d", pid);
   enviar(TypeRestart, buf);
}

void reconfigurar(void)
{
   enviar(TypeReconfig, "");
}

int ReadConfig(char *configfile)
{
   EwKeySpec spec[] = {
      { "MyModuleId",   EW_KEY_STR, MyModName,     sizeof(MyModName), 1 },
      { "Ring",         EW_KEY_STR, RingName,      sizeof(RingName),  1 },
      { "HeartBeatInt", EW_KEY_INT, &HeartBeatInt, 0,                 1 },
      { "LogFile",      EW_KEY_INT, &LogFile,      0,                 1 },
      { "PollInt",      EW_KEY_INT, &PollInt,      0,                 1 },
      { "LogDir",       EW_KEY_STR, LogDir,        sizeof(LogDir),    0 },
   };

   if (ewgui_config_load(configfile, spec, 6) != 0) {
      fprintf(stderr, "ew_controller: error leyendo la configuracion <%s>\n", configfile);
      return -1;
   }
   return 0;
}

/* LogDir: gana EW_LOG; si no, el valor de config; si sigue vacio, "logs".
   Sin esto el visor mostraba "(LogDir not accessible: )". */
static void resolve_logdir(void)
{
   const char *ewlog = getenv("EW_LOG");
   if (ewlog && *ewlog)
      snprintf(LogDir, sizeof(LogDir), "%s", ewlog);
   else if (LogDir[0] == '\0')
      snprintf(LogDir, sizeof(LogDir), "logs");
}

void ConnectToEarthworm(void)
{
   long RingKey = GetKey(RingName);
   if (RingKey == -1) {
      fprintf(stderr, "ew_controller: Ring <%s> not registered in earthworm.d\n", RingName);
      exit(-1);
   }
   if (GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0) TypeHeartBeat = 0;
   if (GetType("TYPE_REQSTATUS", &TypeReqStatus) != 0) TypeReqStatus = 0;
   if (GetType("TYPE_STATUS", &TypeStatus) != 0) TypeStatus = 0;
   if (GetType("TYPE_STOP", &TypeStop) != 0) TypeStop = 0;
   if (GetType("TYPE_RESTART", &TypeRestart) != 0) TypeRestart = 0;
   if (GetType("TYPE_RECONFIG", &TypeReconfig) != 0) TypeReconfig = 0;
   if (GetLocalInst(&MyInstId) != 0) MyInstId = 0;
   if (GetModId(MyModName, &MyModId) != 0) {
      if (GetModId("MOD_WILDCARD", &MyModId) != 0) MyModId = 0;
   }
   g_ring_key = RingKey;
}

static gboolean on_try_attach(gpointer data)
{
   if (g_attached) return G_SOURCE_REMOVE;
   if (g_ring_key < 0 || shmget(g_ring_key, 0, 0) < 0) {
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar),
         "Waiting for the CONTROL_RING ring (start startstop)...");
      return G_SOURCE_CONTINUE;
   }
   tport_attach(&Region, g_ring_key);
   g_attached = 1;
   pedir_estado();
   gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Connected to the control ring.");
   return G_SOURCE_REMOVE;
}

static gboolean on_poll(gpointer data)
{
   static EwGuiHeartbeat hb = {0};
   if (!g_attached) return G_SOURCE_CONTINUE;
   time_t now;
   time(&now);
   if (ewgui_heartbeat_due(&hb, (double)now, HeartBeatInt)) {
      enviar(TypeHeartBeat, "");
   }
   pedir_estado();
   /* if too long without a response */
   if (g_last_status && (now - g_last_status) > (time_t)(PollInt * 4)) {
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "NO RESPONSE from startstop");
      gtk_widget_set_sensitive(g_btn_stop, FALSE);
      gtk_widget_set_sensitive(g_btn_restart, FALSE);
      gtk_widget_set_sensitive(g_btn_start, FALSE);
      gtk_widget_set_sensitive(g_btn_reconfig, FALSE);
   }
   return G_SOURCE_CONTINUE;
}

static gboolean on_read(gpointer data)
{
   MSG_LOGO reclogo;
   char msg[STATUS_MAX + 1];
   long recsize;
   int res;
   MSG_LOGO filtro[1];
   unsigned char wc_inst, wc_mod;
   static int got_inst = 0, got_mod = 0;

   if (!g_attached) return G_SOURCE_CONTINUE;

   if (!got_inst) { if (GetInst("INST_WILDCARD", &wc_inst) == 0) got_inst = 1; else wc_inst = 0; }
   if (!got_mod) { if (GetModId("MOD_WILDCARD", &wc_mod) == 0) got_mod = 1; else wc_mod = 0; }
   filtro[0].instid = wc_inst;
   filtro[0].mod = wc_mod;
   filtro[0].type = TypeStatus;

   do {
      res = tport_getmsg(&Region, filtro, 1, &reclogo, &recsize, msg, sizeof(msg) - 1);
      if (res == GET_OK || res == GET_MISS || res == GET_NOTRACK || res == GET_MISS_SEQGAP) {
         msg[recsize] = '\0';
         ewgui_ctrl_parse_status(msg, &g_status);
         poblar_listas();
         populate_combo();
         time(&g_last_status);
         char sb[256];
         if (g_sel_idx >= 0 && g_sel_idx < g_status.nmods)
            snprintf(sb, sizeof(sb), "Updated %s UTC - %d modules - target: %s (%s)",
                     g_status.curtime, g_status.nmods, g_status.mods[g_sel_idx].name, g_status.mods[g_sel_idx].status);
         else
            snprintf(sb, sizeof(sb), "Updated %s UTC - %d modules", g_status.curtime, g_status.nmods);
         gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), sb);
         gtk_widget_set_sensitive(g_btn_reconfig, TRUE);
         actualizar_log();
      }
   } while (res == GET_OK);

   return G_SOURCE_CONTINUE;
}

static gboolean on_window_close_cb(GtkWindow *w, gpointer data) {
    (void)w; (void)data;
    if (g_loop) g_main_loop_quit(g_loop);
    return FALSE;
}
static void ec_col_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer d) {
    (void)f; (void)d;
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_list_item_set_child(item, lbl);
}
static void ec_col_bind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer d) {
    (void)f;
    const char *prop = d;
    GtkWidget *lbl = gtk_list_item_get_child(item);
    GObject *row = gtk_list_item_get_item(item);
    if (!row) { gtk_label_set_text(GTK_LABEL(lbl), ""); return; }
    GParamSpec *ps = g_object_class_find_property(G_OBJECT_GET_CLASS(row), prop);
    if (!ps) { gtk_label_set_text(GTK_LABEL(lbl), ""); return; }
    GValue v = G_VALUE_INIT;
    g_value_init(&v, G_PARAM_SPEC_VALUE_TYPE(ps));
    g_object_get_property(row, prop, &v);
    if (G_VALUE_HOLDS_STRING(&v)) gtk_label_set_text(GTK_LABEL(lbl), g_value_get_string(&v) ? g_value_get_string(&v) : "");
    else if (G_VALUE_HOLDS_INT(&v)) { char b[32]; snprintf(b, sizeof(b), "%d", g_value_get_int(&v)); gtk_label_set_text(GTK_LABEL(lbl), b); }
    g_value_unset(&v);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { fprintf(stderr, "Uso: %s <configfile.d>\n", argv[0]); exit(1); }
    if (ReadConfig(argv[1]) != 0) exit(1);
    resolve_logdir();
    logit_init(argv[1], 0, 1024, LogFile);
    MyPid = getpid();

    gtk_init();
    setlocale(LC_NUMERIC, "C");

    ConnectToEarthworm();

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
       "#btn_stop { background-image: none; box-shadow: none; border: none; background-color: #dc3545; color: #ffffff; font-weight: bold; padding: 5px 18px; border-radius: 4px; }\n"
       "#btn_start { background-image: none; box-shadow: none; border: none; background-color: #28a745; color: #ffffff; font-weight: bold; padding: 5px 18px; border-radius: 4px; }\n"
       "#btn_restart { background-image: none; box-shadow: none; border: none; background-color: #ffc107; color: #212529; font-weight: bold; padding: 5px 18px; border-radius: 4px; }\n"
       "#btn_reconfig { background-image: none; box-shadow: none; border: none; background-color: #17a2b8; color: #ffffff; font-weight: bold; padding: 5px 18px; border-radius: 4px; }\n"
       "#btn_refresh { background-image: none; box-shadow: none; border: none; background-color: #6c757d; color: #ffffff; font-weight: bold; padding: 5px 18px; border-radius: 4px; }\n"
       "textview { font-family: monospace; font-size: 11px; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *window = gtk_window_new();
    g_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Earthworm Controller");
    gtk_window_set_default_size(GTK_WINDOW(window), 980, 720);
    g_signal_connect(window, "close-request", G_CALLBACK(on_window_close_cb), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    g_lbl_header = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(g_lbl_header), 0.0);
    gtk_widget_set_margin_top(g_lbl_header, 6);
    gtk_widget_set_margin_bottom(g_lbl_header, 2);
    gtk_box_append(GTK_BOX(vbox), g_lbl_header);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_box_append(GTK_BOX(vbox), notebook);

    /* ================= Tab 1: MODULES ================= */
    GtkWidget *page_mod = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_mod, gtk_label_new("Modules"));

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(page_mod), paned);

    GtkWidget *box_mod = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_paned_set_start_child(GTK_PANED(paned), box_mod);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

    g_store = g_list_store_new(EC_TYPE_MOD_ROW);
    g_mod_sel = gtk_single_selection_new(G_LIST_MODEL(g_store));
    gtk_single_selection_set_autoselect(g_mod_sel, FALSE);
    gtk_single_selection_set_can_unselect(g_mod_sel, TRUE);
    g_tree = gtk_column_view_new(GTK_SELECTION_MODEL(g_mod_sel));
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(g_tree), TRUE);
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(g_tree), TRUE);
    {
        const char *m_hdr[] = {"Module","PID","Status","Details"};
        const char *m_prop[] = {"name","pid","status","details"};
        for (int i = 0; i < 4; i++) {
            GtkListItemFactory *f = gtk_signal_list_item_factory_new();
            g_signal_connect(f, "setup", G_CALLBACK(ec_col_setup), NULL);
            g_signal_connect(f, "bind", G_CALLBACK(ec_col_bind), (gpointer)m_prop[i]);
            gtk_column_view_append_column(GTK_COLUMN_VIEW(g_tree), gtk_column_view_column_new(m_hdr[i], f));
        }
    }
    g_signal_connect(g_mod_sel, "notify::selected", G_CALLBACK(on_row_selected), NULL);
    {
        GtkGesture *click = gtk_gesture_click_new();
        g_signal_connect(click, "pressed", G_CALLBACK(on_tree_pressed), NULL);
        gtk_widget_add_controller(g_tree, GTK_EVENT_CONTROLLER(click));
    }

    GtkWidget *sw_mod = gtk_scrolled_window_new();
    gtk_widget_set_size_request(sw_mod, 620, 300);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_mod), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_mod), g_tree);
    gtk_widget_set_vexpand(sw_mod, TRUE);
    gtk_box_append(GTK_BOX(box_mod), sw_mod);

    GtkWidget *hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(box_mod), hbtn);

    g_btn_stop = gtk_button_new_with_label("Stop"); gtk_widget_set_name(g_btn_stop, "btn_stop");
    g_signal_connect(g_btn_stop, "clicked", G_CALLBACK(on_btn_stop), NULL); gtk_box_append(GTK_BOX(hbtn), g_btn_stop);
    g_btn_restart = gtk_button_new_with_label("Restart"); gtk_widget_set_name(g_btn_restart, "btn_restart");
    g_signal_connect(g_btn_restart, "clicked", G_CALLBACK(on_btn_restart), NULL); gtk_box_append(GTK_BOX(hbtn), g_btn_restart);
    g_btn_start = gtk_button_new_with_label("Start"); gtk_widget_set_name(g_btn_start, "btn_start");
    g_signal_connect(g_btn_start, "clicked", G_CALLBACK(on_btn_start), NULL); gtk_box_append(GTK_BOX(hbtn), g_btn_start);
    g_btn_reconfig = gtk_button_new_with_label("Reconfig"); gtk_widget_set_name(g_btn_reconfig, "btn_reconfig");
    g_signal_connect(g_btn_reconfig, "clicked", G_CALLBACK(on_btn_reconfig), NULL); gtk_box_append(GTK_BOX(hbtn), g_btn_reconfig);
    g_btn_refresh = gtk_button_new_with_label("Refresh"); gtk_widget_set_name(g_btn_refresh, "btn_refresh");
    g_signal_connect(g_btn_refresh, "clicked", G_CALLBACK(on_btn_refresh), NULL); gtk_box_append(GTK_BOX(hbtn), g_btn_refresh);

    gtk_widget_set_sensitive(g_btn_stop, FALSE);
    gtk_widget_set_sensitive(g_btn_restart, FALSE);
    gtk_widget_set_sensitive(g_btn_start, FALSE);
    gtk_widget_set_sensitive(g_btn_reconfig, FALSE);

    /* ---- Right panel: rings + logs ---- */
    GtkWidget *box_der = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_paned_set_end_child(GTK_PANED(paned), box_der);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

    GtkWidget *lbl_rings = gtk_label_new("RINGS");
    gtk_label_set_xalign(GTK_LABEL(lbl_rings), 0.0);
    gtk_box_append(GTK_BOX(box_der), lbl_rings);

    g_rings_store = g_list_store_new(EC_TYPE_RING_ROW);
    g_rings_tree = gtk_column_view_new(GTK_SELECTION_MODEL(gtk_no_selection_new(G_LIST_MODEL(g_rings_store))));
    {
        const char *r_hdr[] = {"Ring","Key","Size KB"};
        const char *r_prop[] = {"name","key","size"};
        for (int i = 0; i < 3; i++) {
            GtkListItemFactory *f = gtk_signal_list_item_factory_new();
            g_signal_connect(f, "setup", G_CALLBACK(ec_col_setup), NULL);
            g_signal_connect(f, "bind", G_CALLBACK(ec_col_bind), (gpointer)r_prop[i]);
            gtk_column_view_append_column(GTK_COLUMN_VIEW(g_rings_tree), gtk_column_view_column_new(r_hdr[i], f));
        }
    }
    GtkWidget *sw_rings = gtk_scrolled_window_new();
    gtk_widget_set_size_request(sw_rings, 340, 120);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_rings), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_rings), g_rings_tree);
    gtk_box_append(GTK_BOX(box_der), sw_rings);

    GtkWidget *lbl_log = gtk_label_new("LOGS (tail)");
    gtk_label_set_xalign(GTK_LABEL(lbl_log), 0.0);
    gtk_box_append(GTK_BOX(box_der), lbl_log);

    g_combo = gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(NULL)), NULL);
    g_signal_connect(g_combo, "notify::selected", G_CALLBACK(on_combo_changed), NULL);
    gtk_box_append(GTK_BOX(box_der), g_combo);

    g_logview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_logview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_logview), GTK_WRAP_NONE);
    g_logbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_logview));
    GtkWidget *sw_log = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_log), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_log), g_logview);
    gtk_widget_set_vexpand(sw_log, TRUE);
    gtk_box_append(GTK_BOX(box_der), sw_log);

    /* ================= Tab 2: CONFIGURATION ================= */
    GtkWidget *page_cfg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_cfg, gtk_label_new("Configuration"));

    GtkWidget *cfg_h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(page_cfg), cfg_h);
    gtk_box_append(GTK_BOX(cfg_h), gtk_label_new("Module:"));

    g_cfg_combo = gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(NULL)), NULL);
    g_signal_connect(g_cfg_combo, "notify::selected", G_CALLBACK(on_cfg_combo_changed), NULL);
    gtk_box_append(GTK_BOX(cfg_h), g_cfg_combo);

    g_lbl_cfgpath = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(g_lbl_cfgpath), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(g_lbl_cfgpath), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(g_lbl_cfgpath, TRUE);
    gtk_box_append(GTK_BOX(cfg_h), g_lbl_cfgpath);

    g_cfg_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g_cfg_grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(g_cfg_grid), 4);
    GtkWidget *sw_cfg = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_cfg), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_cfg), g_cfg_grid);
    gtk_widget_set_vexpand(sw_cfg, TRUE);
    gtk_box_append(GTK_BOX(page_cfg), sw_cfg);

    GtkWidget *cfg_btn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(page_cfg), cfg_btn);

    g_btn_cfg_save = gtk_button_new_with_label("Save");
    g_signal_connect(g_btn_cfg_save, "clicked", G_CALLBACK(on_btn_cfg_save), NULL);
    gtk_box_append(GTK_BOX(cfg_btn), g_btn_cfg_save);
    g_btn_cfg_reconfig = gtk_button_new_with_label("Reconfigure");
    g_signal_connect(g_btn_cfg_reconfig, "clicked", G_CALLBACK(on_btn_cfg_reconfig), NULL);
    gtk_box_append(GTK_BOX(cfg_btn), g_btn_cfg_reconfig);
    g_btn_cfg_reload = gtk_button_new_with_label("Reload");
    g_signal_connect(g_btn_cfg_reload, "clicked", G_CALLBACK(on_btn_cfg_reload), NULL);
    gtk_box_append(GTK_BOX(cfg_btn), g_btn_cfg_reload);

    g_lbl_cfg_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(g_lbl_cfg_status), 0.0);
    gtk_widget_set_hexpand(g_lbl_cfg_status, TRUE);
    gtk_box_append(GTK_BOX(cfg_btn), g_lbl_cfg_status);

    gtk_widget_set_sensitive(g_btn_cfg_save, FALSE);
    gtk_widget_set_sensitive(g_btn_cfg_reconfig, FALSE);
    gtk_widget_set_sensitive(g_btn_cfg_reload, FALSE);

    g_lbl_statusbar = gtk_label_new("Waiting for startstop response...");
    gtk_label_set_xalign(GTK_LABEL(g_lbl_statusbar), 0.0);
    gtk_box_append(GTK_BOX(vbox), g_lbl_statusbar);

    gtk_window_present(GTK_WINDOW(window));

    g_timeout_add_seconds((guint) PollInt, on_poll, NULL);
    g_timeout_add(500, on_read, NULL);
    g_timeout_add(2000, on_try_attach, NULL);

    g_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(g_loop);
    return 0;
}
