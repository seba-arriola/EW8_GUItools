#include "ew_controller.h"

static char *tail_file(const char *path, long maxbytes);
static const char *dd_text(GtkWidget *dd) {
   GtkStringObject *o = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(dd)));
   return o ? gtk_string_object_get_string(o) : "";
}

void poblar_listas(void)
{
   int i;
   if (g_sel_idx >= 0 && g_sel_idx < g_status.nmods) g_sel_pid = g_status.mods[g_sel_idx].pid;
   int intent_pid = g_sel_pid;

   g_list_store_remove_all(g_store);
   for (i = 0; i < g_status.nmods; i++) {
      EcModRow *row = ec_mod_row_new(g_status.mods[i].name, g_status.mods[i].pid,
                                     g_status.mods[i].status, g_status.mods[i].detalle);
      g_list_store_append(g_store, row);
      g_object_unref(row);
   }
   g_list_store_remove_all(g_rings_store);
   for (i = 0; i < g_status.nrings; i++) {
      EcRingRow *row = ec_ring_row_new(g_status.rings[i].name, g_status.rings[i].key, g_status.rings[i].size);
      g_list_store_append(g_rings_store, row);
      g_object_unref(row);
   }

   g_sel_idx = -1;
   for (i = 0; i < g_status.nmods; i++)
      if (g_status.mods[i].pid == intent_pid) { g_sel_idx = i; break; }

   if (g_sel_idx >= 0 && time(NULL) - g_last_user_click > 3 && g_mod_sel) {
      gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_mod_sel), (guint)g_sel_idx, TRUE);
   }
   aplicar_botones();

   char hdr[512];
   snprintf(hdr, sizeof(hdr),
            "EARTHWORM SYSTEM STATUS\n%s   Start: %s   Current: %s   Disk: %s   Version: %s",
            g_status.hostname, g_status.starttime, g_status.curtime, g_status.disk, g_status.version);
   gtk_label_set_text(GTK_LABEL(g_lbl_header), hdr);
}

void aplicar_botones(void)
{
   gboolean sel_ok = (g_sel_idx >= 0 && g_sel_idx < g_status.nmods);
   gboolean es_startstop = sel_ok && !strcmp(g_status.mods[g_sel_idx].name, "startstop");
   const char *st = sel_ok ? g_status.mods[g_sel_idx].status : "";

   gtk_widget_set_sensitive(g_btn_stop, sel_ok && !es_startstop && !strcmp(st, "Alive"));
   gtk_widget_set_sensitive(g_btn_restart, sel_ok && !es_startstop);
   gtk_widget_set_sensitive(g_btn_start, sel_ok && !es_startstop &&
                            (!strcmp(st, "Stop") || !strcmp(st, "Dead")));
}

void on_row_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data)
{
   (void)sel; (void)pspec; (void)data;
   g_sel_idx = -1;
   if (g_mod_sel) {
      guint idx = gtk_single_selection_get_selected(g_mod_sel);
      if (idx != GTK_INVALID_LIST_POSITION) {
         EcModRow *row = g_list_model_get_item(G_LIST_MODEL(g_store), idx);
         if (row) {
            int pid = 0; g_object_get(row, "pid", &pid, NULL);
            for (int i = 0; i < g_status.nmods; i++)
               if (g_status.mods[i].pid == pid) { g_sel_idx = i; break; }
            if (g_sel_idx >= 0) g_sel_pid = g_status.mods[g_sel_idx].pid;
            g_object_unref(row);
         }
      }
   }
   aplicar_botones();

   if (g_cfg_combo && g_sel_idx >= 0 && g_sel_idx < g_status.nmods) {
      int cur = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_cfg_combo));
      if (cur != g_sel_idx)
         gtk_drop_down_set_selected(GTK_DROP_DOWN(g_cfg_combo), (guint)g_sel_idx);
   }
}

void on_tree_pressed(GtkGestureClick *g, int n_press, double x, double y, gpointer data)
{
   (void)g; (void)n_press; (void)x; (void)y; (void)data;
   time(&g_last_user_click);
}

typedef struct { GMainLoop *loop; int response; } EcDialogRun;
static void ec_dlg_yes(GtkButton *b, gpointer d) { (void)b; EcDialogRun *r = d; r->response = 1; g_main_loop_quit(r->loop); }
static void ec_dlg_no(GtkButton *b, gpointer d)  { (void)b; EcDialogRun *r = d; r->response = 0; g_main_loop_quit(r->loop); }

gboolean confirmar(const char *msg)
{
   GtkWidget *win = gtk_window_new();
   gtk_window_set_title(GTK_WINDOW(win), "Confirm");
   gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(g_window));
   gtk_window_set_modal(GTK_WINDOW(win), TRUE);
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
   gtk_widget_set_margin_start(box, 15); gtk_widget_set_margin_end(box, 15);
   gtk_widget_set_margin_top(box, 15); gtk_widget_set_margin_bottom(box, 15);
   gtk_box_append(GTK_BOX(box), gtk_label_new(msg));
   GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_widget_set_halign(hbox, GTK_ALIGN_END);
   GtkWidget *no = gtk_button_new_with_label("No");
   GtkWidget *yes = gtk_button_new_with_label("Yes");
   gtk_box_append(GTK_BOX(hbox), no);
   gtk_box_append(GTK_BOX(hbox), yes);
   gtk_box_append(GTK_BOX(box), hbox);
   gtk_window_set_child(GTK_WINDOW(win), box);
   EcDialogRun r; r.loop = g_main_loop_new(NULL, FALSE); r.response = 0;
   g_signal_connect(no, "clicked", G_CALLBACK(ec_dlg_no), &r);
   g_signal_connect(yes, "clicked", G_CALLBACK(ec_dlg_yes), &r);
   gtk_window_present(GTK_WINDOW(win));
   g_main_loop_run(r.loop);
   g_main_loop_unref(r.loop);
   gtk_window_destroy(GTK_WINDOW(win));
   return r.response == 1;
}

void on_btn_stop(GtkWidget *w, gpointer data)
{
   if (g_sel_idx >= 0) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Stop the module '%s' (pid %d)?",
               g_status.mods[g_sel_idx].name, g_status.mods[g_sel_idx].pid);
      if (!confirmar(msg)) return;
      detener_mod(g_status.mods[g_sel_idx].pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Stop sent...");
      pedir_estado();
   }
}

void on_btn_restart(GtkWidget *w, gpointer data)
{
   if (g_sel_idx >= 0) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Restart the module '%s' (pid %d)?",
               g_status.mods[g_sel_idx].name, g_status.mods[g_sel_idx].pid);
      if (!confirmar(msg)) return;
      reiniciar_mod(g_status.mods[g_sel_idx].pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Restart sent...");
      pedir_estado();
   }
}

void on_btn_start(GtkWidget *w, gpointer data)
{
   if (g_sel_idx >= 0) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Start the module '%s'?",
               g_status.mods[g_sel_idx].name);
      if (!confirmar(msg)) return;
      reiniciar_mod(g_status.mods[g_sel_idx].pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Start sent...");
      pedir_estado();
   }
}

void on_btn_reconfig(GtkWidget *w, gpointer data)
{
   reconfigurar();
   gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Reconfig sent...");
   pedir_estado();
}

void on_btn_refresh(GtkWidget *w, gpointer data)
{
   pedir_estado();
   gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Updating...");
}

void populate_combo(void)
{
   if (g_combo) {
      const char *sel = dd_text(g_combo);
      char saved[128]; snprintf(saved, sizeof(saved), "%s", sel ? sel : "");
      GtkStringList *sl = gtk_string_list_new(NULL);
      for (int i = 0; i < g_status.nmods; i++) gtk_string_list_append(sl, g_status.mods[i].name);
      gtk_drop_down_set_model(GTK_DROP_DOWN(g_combo), G_LIST_MODEL(sl));
      g_object_unref(sl);
      int idx = -1;
      for (int i = 0; i < g_status.nmods; i++) if (!strcmp(g_status.mods[i].name, saved)) { idx = i; break; }
      gtk_drop_down_set_selected(GTK_DROP_DOWN(g_combo), idx < 0 ? (g_status.nmods > 0 ? 0 : GTK_INVALID_LIST_POSITION) : (guint)idx);
   }
   if (!g_cfg_combo) return;
   const char *csel = dd_text(g_cfg_combo);
   char saved[128]; snprintf(saved, sizeof(saved), "%s", csel ? csel : "");
   GtkStringList *sl = gtk_string_list_new(NULL);
   for (int i = 0; i < g_status.nmods; i++) gtk_string_list_append(sl, g_status.mods[i].name);
   gtk_drop_down_set_model(GTK_DROP_DOWN(g_cfg_combo), G_LIST_MODEL(sl));
   g_object_unref(sl);
   int idx = -1;
   for (int i = 0; i < g_status.nmods; i++) if (!strcmp(g_status.mods[i].name, saved)) { idx = i; break; }
   gtk_drop_down_set_selected(GTK_DROP_DOWN(g_cfg_combo), idx < 0 ? (g_status.nmods > 0 ? 0 : GTK_INVALID_LIST_POSITION) : (guint)idx);
}

void actualizar_log(void)
{
   const char *sel = dd_text(g_combo);
   if (!sel || !*sel) return;
   char base[64];
   strncpy(base, sel, sizeof(base) - 1);
   base[sizeof(base) - 1] = '\0';

   /* startstop names its log with the executable name (argv[0]),
      not with the config file (it makes a second logit_init with argv[0]) */
   if (!strcmp(base, "startstop")) strcpy(base, "startstop");
   else {
      int k = -1;
      for (int i = 0; i < g_status.nmods; i++)
         if (!strcmp(g_status.mods[i].name, base)) { k = i; break; }
      if (k >= 0 && g_status.mods[k].config[0])
         strncpy(base, g_status.mods[k].config, sizeof(base) - 1);
      base[sizeof(base) - 1] = '\0';
   }

   DIR *d = opendir(LogDir);
   if (!d) {
      char msg[512];
      snprintf(msg, sizeof(msg), "(LogDir not accessible: %s)", LogDir);
      gtk_text_buffer_set_text(g_logbuf, msg, -1);
      return;
   }
   char best[512] = "";
   long best_mtime = -1;
   struct dirent *e;
   char prefix[128];
   snprintf(prefix, sizeof(prefix), "%s_", base);
   while ((e = readdir(d)) != NULL) {
      if (strncmp(e->d_name, prefix, strlen(prefix)) != 0) continue;
      if (!strstr(e->d_name, ".log")) continue;
      char path[512];
      snprintf(path, sizeof(path), "%s/%s", LogDir, e->d_name);
      struct stat st;
      if (stat(path, &st) != 0) continue;
      if (st.st_mtime > best_mtime) { best_mtime = st.st_mtime; strcpy(best, path); }
   }
   closedir(d);
   if (best[0] == '\0') {
      gtk_text_buffer_set_text(g_logbuf, "(no log file)", -1);
      return;
   }
   char *txt = tail_file(best, LOG_TAIL);
   gtk_text_buffer_set_text(g_logbuf, txt ? txt : "(could not be read)", -1);
   if (txt) free(txt);
}

void on_combo_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
   (void)obj; (void)pspec; (void)data;
   actualizar_log();
}

void actualizar_botones_cfg(void)
{
   gboolean ok = (g_cfg_loaded == 1);
   gtk_widget_set_sensitive(g_btn_cfg_save, ok);
   gtk_widget_set_sensitive(g_btn_cfg_reconfig, ok);
   gtk_widget_set_sensitive(g_btn_cfg_reload, ok);
}

void on_cfg_entry_changed(GtkWidget *entry, gpointer data)
{
   g_cfg_dirty = 1;
   gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), "There are unsaved changes...");
}

void cfg_vaciar_grid(void)
{
   GtkWidget *child = gtk_widget_get_first_child(g_cfg_grid);
   while (child) {
      GtkWidget *next = gtk_widget_get_next_sibling(child);
      gtk_grid_remove(GTK_GRID(g_cfg_grid), child);
      child = next;
   }
}

void cfg_rebuild_grid(void)
{
   int i, r = 0;
   cfg_vaciar_grid();
   if (g_cfg_entries) { free(g_cfg_entries); g_cfg_entries = NULL; }
   g_cfg_entries = calloc(g_cfg.nlines + 1, sizeof(GtkWidget *));

   for (i = 0; i < g_cfg.nlines; i++) {
      EwCfgLine *l = &g_cfg.lines[i];
      g_cfg_entries[i] = NULL;
      if (l->kind != 0) continue;

      gchar *mark = g_markup_printf_escaped("<b>%s</b>", l->name);
      GtkWidget *lbl = gtk_label_new(NULL);
      gtk_label_set_markup(GTK_LABEL(lbl), mark);
      g_free(mark);
      gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
      gtk_widget_set_hexpand(lbl, FALSE);

      GtkWidget *entry = gtk_entry_new();
      gtk_editable_set_text(GTK_EDITABLE(entry), l->value);
      gtk_widget_set_hexpand(entry, TRUE);

      gtk_grid_attach(GTK_GRID(g_cfg_grid), lbl, 0, r, 1, 1);
      gtk_grid_attach(GTK_GRID(g_cfg_grid), entry, 1, r, 1, 1);
      g_cfg_entries[i] = entry;
      g_signal_connect(entry, "changed", G_CALLBACK(on_cfg_entry_changed), NULL);
      r++;
   }
}

void cfg_cargar_modulo(int idx)
{
   char path[512];
   char msg[512];
   int nedit = 0, i;

   if (idx < 0 || idx >= g_status.nmods) return;
   ewgui_cfgfile_resolve(g_status.mods[idx].cfgfile, path, sizeof(path));

   ewgui_cfgfile_free(&g_cfg);
   if (ewgui_cfgfile_read(path, &g_cfg) != 0) {
      g_cfg_loaded = 0;
      gtk_label_set_text(GTK_LABEL(g_lbl_cfgpath), path);
      snprintf(msg, sizeof(msg), "Could not read the config file: %s", path);
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), msg);
      cfg_rebuild_grid();
      actualizar_botones_cfg();
      return;
   }

   g_cfg_loaded = 1;
   g_cfg_modidx = idx;
   g_cfg_dirty = 0;
   for (i = 0; i < g_cfg.nlines; i++)
      if (g_cfg.lines[i].kind == 0) nedit++;
   gtk_label_set_text(GTK_LABEL(g_lbl_cfgpath), path);
   snprintf(msg, sizeof(msg), "Loaded: %s  (%d editable variables)", path, nedit);
   gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), msg);
   cfg_rebuild_grid();
   actualizar_botones_cfg();
}

void on_cfg_combo_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
   (void)pspec; (void)data;
   gint idx;
   if (g_cfg_suppress) return;
   idx = (gint)gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
   if (idx < 0) return;
   if (g_cfg_loaded && g_cfg_modidx >= 0 && g_cfg_modidx < g_status.nmods &&
       idx < g_status.nmods && !strcmp(g_status.mods[g_cfg_modidx].cfgfile, g_status.mods[idx].cfgfile)) {
      g_cfg_modidx = idx;
      return;
   }
   if (g_cfg_loaded && g_cfg_dirty) {
      char msg[300];
      snprintf(msg, sizeof(msg), "There are unsaved changes in '%s'. Discard them?",
               (g_cfg_modidx >= 0 && g_cfg_modidx < g_status.nmods) ? g_status.mods[g_cfg_modidx].name : "?");
      if (!confirmar(msg)) {
         g_cfg_suppress = 1;
         gtk_drop_down_set_selected(GTK_DROP_DOWN(obj), (guint)g_cfg_modidx);
         g_cfg_suppress = 0;
         return;
      }
   }
   cfg_cargar_modulo(idx);
}

void on_btn_cfg_save(GtkWidget *w, gpointer data)
{
   char msg[512];
   int i;

   if (!g_cfg_loaded) return;
   /* dump the entries into the model */
   for (i = 0; i < g_cfg.nlines; i++) {
      if (!g_cfg_entries[i]) continue;
      const gchar *txt = gtk_editable_get_text(GTK_EDITABLE(g_cfg_entries[i]));
      free(g_cfg.lines[i].value);
      g_cfg.lines[i].value = g_strdup(txt);
   }
   if (ewgui_cfgfile_write(&g_cfg) == 0) {
      g_cfg_dirty = 0;
      snprintf(msg, sizeof(msg), "Saved to %s (backup .bak)", g_cfg.path);
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), msg);
   } else {
      snprintf(msg, sizeof(msg), "ERROR saving %s", g_cfg.path);
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), msg);
   }
}

void on_btn_cfg_reconfig(GtkWidget *w, gpointer data)
{
   char msg[300];
   if (!g_cfg_loaded || g_cfg_modidx < 0) return;
   on_btn_cfg_save(w, data);
   if (g_cfg_dirty) return;   /* could not be saved */
   snprintf(msg, sizeof(msg), "Restart '%s' to apply the saved config?",
            g_status.mods[g_cfg_modidx].name);
   if (!confirmar(msg)) return;
   if (!strcmp(g_status.mods[g_cfg_modidx].name, "startstop")) {
      reconfigurar();
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Reconfig (startstop) sent...");
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), "startstop re-reads its config and applies the changes.");
   } else {
      reiniciar_mod(g_status.mods[g_cfg_modidx].pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Restart sent...");
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status),
         "Config saved. The module re-reads its config on restart.");
   }
   pedir_estado();
}

void on_btn_cfg_reload(GtkWidget *w, gpointer data)
{
   if (!g_cfg_loaded) return;
   if (g_cfg_dirty && !confirmar("There are unsaved changes. Reload the file and discard them?"))
      return;
   cfg_cargar_modulo(g_cfg_modidx);
}

static char *tail_file(const char *path, long maxbytes)
{
   FILE *fp = fopen(path, "rb");
   if (!fp) return NULL;
   fseek(fp, 0, SEEK_END);
   long sz = ftell(fp);
   long start = (sz > maxbytes) ? sz - maxbytes : 0;
   fseek(fp, start, SEEK_SET);
   char *buf = malloc(maxbytes + 2);
   if (!buf) { fclose(fp); return NULL; }
   long n = fread(buf, 1, maxbytes, fp);
   buf[n] = '\0';
   fclose(fp);
   if (n == 0) return buf;
   /* start at a line boundary */
   char *nl = strchr(buf, '\n');
   if (nl && nl != buf) memmove(buf, nl + 1, strlen(nl + 1) + 1);
   return buf;
}
