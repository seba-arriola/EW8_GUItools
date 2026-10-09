#include "ew_controller.h"

/* Refresco sin parpadeo: se compara el estado con el anterior para decidir
   entre no tocar nada, actualizar las filas in situ o reconstruirlas. */
static EwCtrlStatus g_status_prev;
static int  g_status_prev_valid = 0;
/* Identidad de la selección: el NOMBRE del módulo (estable ante reinicios),
   no el pid (que cambia al reiniciar). */
static char g_sel_name[64] = "";
static int  g_row_sel_suppress = 0;
/* Tamano del array g_cfg_entries (config ANTERIOR). Necesario porque al recargar
   otro modulo g_cfg.nlines ya es el NUEVO y g_cfg_entries aun el viejo. */
static int  g_cfg_entries_n = 0;
/* Texto del log ya pintado, para no reescribir el buffer si no cambió. */
static char *g_log_last = NULL;

/* Nombre del modulo seleccionado para los logs (selector sin popup). */
static char g_log_name[64] = "";

static char *tail_file(const char *path, long maxbytes);

void poblar_listas(void)
{
   int i;
   EwCtrlSync plan = g_status_prev_valid
      ? ewgui_ctrl_sync_plan(&g_status_prev, &g_status)
      : EWCTRL_SYNC_STRUCTURE;

   if (plan == EWCTRL_SYNC_VALUES) {
      /* Misma estructura: actualizar las filas existentes sin destruirlas,
         así el ColumnView conserva la fila seleccionada. */
      for (i = 0; i < g_status.nmods; i++) {
         GObject *row = g_list_model_get_item(G_LIST_MODEL(g_store), (guint)i);
         if (row) {
            g_object_set(row, "status", g_status.mods[i].status,
                              "details", g_status.mods[i].detalle, NULL);
            g_object_unref(row);
         }
      }
      for (i = 0; i < g_status.nrings; i++) {
         GObject *row = g_list_model_get_item(G_LIST_MODEL(g_rings_store), (guint)i);
         if (row) {
            g_object_set(row, "size", g_status.rings[i].size, NULL);
            g_object_unref(row);
         }
      }
   } else if (plan == EWCTRL_SYNC_STRUCTURE) {
      /* Cambió el conjunto/orden: reconstruir y re-seleccionar por nombre. */
      char want[64];
      snprintf(want, sizeof(want), "%s", g_sel_name);

      g_row_sel_suppress = 1;
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
      g_row_sel_suppress = 0;

      g_sel_idx = -1;
      for (i = 0; i < g_status.nmods; i++)
         if (want[0] && !strcmp(g_status.mods[i].name, want)) { g_sel_idx = i; break; }
      if (g_sel_idx >= 0 && g_mod_sel) {
         g_row_sel_suppress = 1;
         gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_mod_sel), (guint)g_sel_idx, TRUE);
         g_row_sel_suppress = 0;
      } else {
         g_sel_name[0] = '\0';
      }
   }

   g_status_prev = g_status;
   g_status_prev_valid = 1;

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
   if (g_row_sel_suppress) return;   /* transitorio de reconstrucción */
   g_sel_idx = -1;
   g_sel_name[0] = '\0';
   if (g_mod_sel) {
      guint idx = gtk_single_selection_get_selected(g_mod_sel);
      if (idx != GTK_INVALID_LIST_POSITION) {
         EcModRow *row = g_list_model_get_item(G_LIST_MODEL(g_store), idx);
         if (row) {
            char *nm = NULL;
            g_object_get(row, "name", &nm, NULL);
            for (int i = 0; i < g_status.nmods; i++)
               if (nm && !strcmp(g_status.mods[i].name, nm)) { g_sel_idx = i; break; }
            if (g_sel_idx >= 0)
               snprintf(g_sel_name, sizeof(g_sel_name), "%s", g_status.mods[g_sel_idx].name);
            g_free(nm);
            g_object_unref(row);
         }
      }
   }
   aplicar_botones();
}

/* Confirmación ASÍNCRONA: sin GMainLoop anidado. Abrir un loop anidado (o hacer
   trabajo pesado) durante el grab de un GtkPopover/GtkDropDown es lo que dejaba
   el desplegable colgado. */
typedef struct { EcConfirmCb cb; gpointer user_data; } EcConfirmCtx;

static void on_alert_response(GObject *source, GAsyncResult *res, gpointer data)
{
   EcConfirmCtx *ctx = data;
   GError *err = NULL;
   int idx = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, &err);
   if (err) g_error_free(err);
   if (ctx->cb) ctx->cb(idx == 1, ctx->user_data);
   g_free(ctx);
}

void ec_confirmar(const char *msg, EcConfirmCb cb, gpointer user_data)
{
   GtkAlertDialog *dlg = gtk_alert_dialog_new("%s", msg);
   const char *buttons[] = { "No", "Yes", NULL };
   gtk_alert_dialog_set_buttons(dlg, buttons);
   gtk_alert_dialog_set_cancel_button(dlg, 0);
   gtk_alert_dialog_set_default_button(dlg, 1);
   EcConfirmCtx *ctx = g_new0(EcConfirmCtx, 1);
   ctx->cb = cb;
   ctx->user_data = user_data;
   gtk_alert_dialog_choose(dlg, GTK_WINDOW(g_window), NULL, on_alert_response, ctx);
   g_object_unref(dlg);
}

/* Referencia a un módulo, capturada al abrir la confirmación (el índice puede
   cambiar entre el diálogo y la respuesta). */
typedef struct { int pid; char name[64]; } EcModRef;

static EcModRef *ec_modref_from_sel(void)
{
   if (g_sel_idx < 0 || g_sel_idx >= g_status.nmods) return NULL;
   EcModRef *r = g_new0(EcModRef, 1);
   r->pid = g_status.mods[g_sel_idx].pid;
   snprintf(r->name, sizeof(r->name), "%s", g_status.mods[g_sel_idx].name);
   return r;
}

static void do_stop(gboolean ok, gpointer data)
{
   EcModRef *r = data;
   if (ok) {
      detener_mod(r->pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Stop sent...");
      pedir_estado();
   }
   g_free(r);
}

static void do_restart(gboolean ok, gpointer data)
{
   EcModRef *r = data;
   if (ok) {
      reiniciar_mod(r->pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Restart sent...");
      pedir_estado();
   }
   g_free(r);
}

static void do_start(gboolean ok, gpointer data)
{
   EcModRef *r = data;
   if (ok) {
      reiniciar_mod(r->pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Start sent...");
      pedir_estado();
   }
   g_free(r);
}

void on_btn_stop(GtkWidget *w, gpointer data)
{
   (void)w; (void)data;
   EcModRef *r = ec_modref_from_sel();
   if (!r) return;
   char msg[256];
   snprintf(msg, sizeof(msg), "Stop the module '%s' (pid %d)?", r->name, r->pid);
   ec_confirmar(msg, do_stop, r);
}

void on_btn_restart(GtkWidget *w, gpointer data)
{
   (void)w; (void)data;
   EcModRef *r = ec_modref_from_sel();
   if (!r) return;
   char msg[256];
   snprintf(msg, sizeof(msg), "Restart the module '%s' (pid %d)?", r->name, r->pid);
   ec_confirmar(msg, do_restart, r);
}

void on_btn_start(GtkWidget *w, gpointer data)
{
   (void)w; (void)data;
   EcModRef *r = ec_modref_from_sel();
   if (!r) return;
   char msg[256];
   snprintf(msg, sizeof(msg), "Start the module '%s'?", r->name);
   ec_confirmar(msg, do_start, r);
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

/* Etiqueta del selector de modulo de la pestaña Configuration. */
static void ec_cfg_set_label(void)
{
   if (!g_cfg_combo) return;
   const char *nm = (g_cfg_modidx >= 0 && g_cfg_modidx < g_status.nmods)
                    ? g_status.mods[g_cfg_modidx].name : "(none)";
   gtk_button_set_label(GTK_BUTTON(g_cfg_combo), nm);
}

/* Los selectores ya no son GtkDropDown (su popup queda detras de la ventana
   bajo WSLg/Wayland). Son botones que abren una ventana modal con la lista, asi
   que aqui solo se refresca su etiqueta. */
void populate_combo(void)
{
   int i;

   if (g_combo) {
      if (g_log_name[0] == '\0' && g_status.nmods > 0)
         snprintf(g_log_name, sizeof(g_log_name), "%s", g_status.mods[0].name);
      int found = 0;
      for (i = 0; i < g_status.nmods; i++)
         if (!strcmp(g_status.mods[i].name, g_log_name)) { found = 1; break; }
      if (!found && g_status.nmods > 0)
         snprintf(g_log_name, sizeof(g_log_name), "%s", g_status.mods[0].name);
      gtk_button_set_label(GTK_BUTTON(g_combo), g_log_name[0] ? g_log_name : "(none)");
   }

   ec_cfg_set_label();
}

/* Pinta el log sólo si el texto cambió (evita parpadeo y pérdida de scroll). */
static void log_set(const char *txt)
{
   if (!txt) txt = "";
   if (g_log_last && !strcmp(g_log_last, txt)) return;
   g_free(g_log_last);
   g_log_last = g_strdup(txt);
   gtk_text_buffer_set_text(g_logbuf, txt, -1);
}

void actualizar_log(void)
{
   const char *sel = g_log_name;
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
      log_set(msg);
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
      log_set("(no log file)");
      return;
   }
   char *txt = tail_file(best, LOG_TAIL);
   log_set(txt ? txt : "(could not be read)");
   if (txt) free(txt);
}

/* ---------------------------------------------------------------------------
 * Selector de modulo SIN popup: una ventana modal con la lista (toplevel
 * normal). Inmune al bug de xdg_popup de WSLg/Wayland (#1299), donde el popup
 * de GtkDropDown aparece detrás de la ventana y no cierra.
 * ------------------------------------------------------------------------- */
typedef void (*EcPickCb)(int idx, gpointer user_data);
typedef struct { EcPickCb cb; gpointer user_data; } EcPick;

/* Cierra el picker y entrega idx (>=0) al callback. */
static void pick_finish(GtkWindow *win, int idx)
{
   EcPick *p = g_object_get_data(G_OBJECT(win), "ec-pick");
   EcPickCb cb = p ? p->cb : NULL;
   gpointer ud = p ? p->user_data : NULL;
   g_object_set_data(G_OBJECT(win), "ec-pick", NULL);   /* libera p */
   gtk_window_destroy(win);
   if (cb && idx >= 0) cb(idx, ud);
}

/* Doble clic / Enter acepta la fila. */
static void pick_row_activated(GtkListBox *lb, GtkListBoxRow *row, gpointer data)
{
   (void)lb;
   pick_finish(GTK_WINDOW(data), GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "ec-idx")));
}

/* Accept aplica la fila SELECCIONADA (un clic simple sólo la marca). */
static void pick_accept(GtkButton *b, gpointer data)
{
   (void)b;
   GtkWindow *win = GTK_WINDOW(data);
   GtkListBox *lb = g_object_get_data(G_OBJECT(win), "ec-lb");
   GtkListBoxRow *row = lb ? gtk_list_box_get_selected_row(lb) : NULL;
   int idx = row ? GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "ec-idx")) : -1;
   if (idx < 0) return;   /* nada seleccionado: no cerrar */
   pick_finish(win, idx);
}

static void ec_module_picker(const char *title, int current, EcPickCb cb, gpointer user_data)
{
   GtkWidget *win = gtk_window_new();
   gtk_window_set_title(GTK_WINDOW(win), title);
   gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(g_window));
   gtk_window_set_modal(GTK_WINDOW(win), TRUE);
   gtk_window_set_default_size(GTK_WINDOW(win), 320, 440);

   EcPick *p = g_new0(EcPick, 1);
   p->cb = cb; p->user_data = user_data;
   g_object_set_data_full(G_OBJECT(win), "ec-pick", p, g_free);

   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   gtk_widget_set_margin_start(box, 10); gtk_widget_set_margin_end(box, 10);
   gtk_widget_set_margin_top(box, 10); gtk_widget_set_margin_bottom(box, 10);

   GtkWidget *lb = gtk_list_box_new();
   /* Un clic simple SÓLO selecciona: la elección se aplica con Accept o
      doble clic / Enter (activate_on_single_click = FALSE). */
   gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(lb), FALSE);
   for (int i = 0; i < g_status.nmods; i++) {
      GtkWidget *lbl = gtk_label_new(g_status.mods[i].name);
      gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
      gtk_widget_set_margin_start(lbl, 6); gtk_widget_set_margin_end(lbl, 6);
      GtkWidget *row = gtk_list_box_row_new();
      gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
      g_object_set_data(G_OBJECT(row), "ec-idx", GINT_TO_POINTER(i));
      gtk_list_box_append(GTK_LIST_BOX(lb), row);
      if (i == current) gtk_list_box_select_row(GTK_LIST_BOX(lb), GTK_LIST_BOX_ROW(row));
   }
   g_object_set_data(G_OBJECT(win), "ec-lb", lb);

   GtkWidget *sw = gtk_scrolled_window_new();
   gtk_widget_set_vexpand(sw, TRUE);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), lb);
   gtk_box_append(GTK_BOX(box), sw);

   GtkWidget *hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_widget_set_halign(hbtn, GTK_ALIGN_END);
   GtkWidget *cancel = gtk_button_new_with_label("Cancel");
   GtkWidget *accept = gtk_button_new_with_label("Accept");
   gtk_widget_add_css_class(accept, "suggested-action");
   gtk_box_append(GTK_BOX(hbtn), cancel);
   gtk_box_append(GTK_BOX(hbtn), accept);
   gtk_box_append(GTK_BOX(box), hbtn);

   gtk_window_set_child(GTK_WINDOW(win), box);
   g_signal_connect(lb, "row-activated", G_CALLBACK(pick_row_activated), win);
   g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), win);
   g_signal_connect(accept, "clicked", G_CALLBACK(pick_accept), win);

   gtk_window_present(GTK_WINDOW(win));
}

static void ec_log_pick(int idx, gpointer user_data)
{
   (void)user_data;
   if (idx < 0 || idx >= g_status.nmods) return;
   snprintf(g_log_name, sizeof(g_log_name), "%s", g_status.mods[idx].name);
   if (g_combo) gtk_button_set_label(GTK_BUTTON(g_combo), g_log_name);
   actualizar_log();
}

void on_log_combo_clicked(GtkWidget *w, gpointer data)
{
   (void)w; (void)data;
   int cur = -1;
   for (int i = 0; i < g_status.nmods; i++)
      if (!strcmp(g_status.mods[i].name, g_log_name)) { cur = i; break; }
   ec_module_picker("Select module (logs)", cur, ec_log_pick, NULL);
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
   int i, r = 0, focus_line = -1;

   /* Conservar el foco del campo que se estaba editando, si toca reconstruir.
      Se recorre hasta g_cfg_entries_n (tamano del array VIEJO), no g_cfg.nlines. */
   if (g_cfg_entries) {
      for (i = 0; i < g_cfg_entries_n; i++)
         if (g_cfg_entries[i] && gtk_widget_has_focus(g_cfg_entries[i])) { focus_line = i; break; }
   }

   cfg_vaciar_grid();
   if (g_cfg_entries) { free(g_cfg_entries); g_cfg_entries = NULL; g_cfg_entries_n = 0; }
   g_cfg_entries = calloc(g_cfg.nlines + 1, sizeof(GtkWidget *));
   g_cfg_entries_n = g_cfg.nlines + 1;

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

   if (focus_line >= 0 && focus_line < g_cfg.nlines && g_cfg_entries[focus_line])
      gtk_widget_grab_focus(g_cfg_entries[focus_line]);
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
   ec_cfg_set_label();
   for (i = 0; i < g_cfg.nlines; i++)
      if (g_cfg.lines[i].kind == 0) nedit++;
   gtk_label_set_text(GTK_LABEL(g_lbl_cfgpath), path);
   snprintf(msg, sizeof(msg), "Loaded: %s  (%d editable variables)", path, nedit);
   gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), msg);
   cfg_rebuild_grid();
   actualizar_botones_cfg();
}

static int g_cfg_pending_idx = -1;
static int g_cfg_confirm_idx = -1;

static void ec_cfg_confirm_discard(gboolean ok, gpointer data)
{
   (void)data;
   int idx = g_cfg_confirm_idx;
   g_cfg_confirm_idx = -1;
   if (ok && idx >= 0) cfg_cargar_modulo(idx);
}

static gboolean ec_cfg_combo_apply(gpointer data)
{
   (void)data;
   int idx = g_cfg_pending_idx;
   g_cfg_pending_idx = -1;
   if (idx < 0) return G_SOURCE_REMOVE;

   if (g_cfg_loaded && g_cfg_modidx >= 0 && g_cfg_modidx < g_status.nmods &&
       idx < g_status.nmods && !strcmp(g_status.mods[g_cfg_modidx].cfgfile, g_status.mods[idx].cfgfile)) {
      g_cfg_modidx = idx;
      ec_cfg_set_label();
      return G_SOURCE_REMOVE;
   }
   if (g_cfg_loaded && g_cfg_dirty) {
      char msg[300];
      snprintf(msg, sizeof(msg), "There are unsaved changes in '%s'. Discard them?",
               (g_cfg_modidx >= 0 && g_cfg_modidx < g_status.nmods) ? g_status.mods[g_cfg_modidx].name : "?");
      g_cfg_confirm_idx = idx;
      ec_confirmar(msg, ec_cfg_confirm_discard, NULL);
      return G_SOURCE_REMOVE;
   }
   cfg_cargar_modulo(idx);
   return G_SOURCE_REMOVE;
}

static void ec_cfg_pick(int idx, gpointer user_data)
{
   (void)user_data;
   /* Diferido: deja cerrar la ventana del selector antes de abrir la
      confirmacion (si hay cambios sin guardar). */
   g_cfg_pending_idx = idx;
   g_idle_add(ec_cfg_combo_apply, NULL);
}

void on_cfg_combo_clicked(GtkWidget *w, gpointer data)
{
   (void)w; (void)data;
   ec_module_picker("Select module (Configuration)", g_cfg_modidx, ec_cfg_pick, NULL);
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

static void do_cfg_reconfig(gboolean ok, gpointer data)
{
   EcModRef *r = data;
   if (ok) {
      if (!strcmp(r->name, "startstop")) {
         reconfigurar();
         gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Reconfig (startstop) sent...");
         gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), "startstop re-reads its config and applies the changes.");
      } else {
         reiniciar_mod(r->pid);
         gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Restart sent...");
         gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status),
            "Config saved. The module re-reads its config on restart.");
      }
      pedir_estado();
   }
   g_free(r);
}

void on_btn_cfg_reconfig(GtkWidget *w, gpointer data)
{
   char msg[300];
   if (!g_cfg_loaded || g_cfg_modidx < 0 || g_cfg_modidx >= g_status.nmods) return;
   on_btn_cfg_save(w, data);
   if (g_cfg_dirty) return;   /* could not be saved */
   EcModRef *r = g_new0(EcModRef, 1);
   r->pid = g_status.mods[g_cfg_modidx].pid;
   snprintf(r->name, sizeof(r->name), "%s", g_status.mods[g_cfg_modidx].name);
   snprintf(msg, sizeof(msg), "Restart '%s' to apply the saved config?", r->name);
   ec_confirmar(msg, do_cfg_reconfig, r);
}

static void do_cfg_reload(gboolean ok, gpointer data)
{
   (void)data;
   if (ok) cfg_cargar_modulo(g_cfg_modidx);
}

void on_btn_cfg_reload(GtkWidget *w, gpointer data)
{
   (void)w; (void)data;
   if (!g_cfg_loaded) return;
   if (g_cfg_dirty)
      ec_confirmar("There are unsaved changes. Reload the file and discard them?", do_cfg_reload, NULL);
   else
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
