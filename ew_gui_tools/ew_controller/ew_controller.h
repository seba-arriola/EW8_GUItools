#ifndef EW_CONTROLLER_H
#define EW_CONTROLLER_H

#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <earthworm.h>
#include <transport.h>
#include "ewgui/config.h"
#include "ewgui/ring.h"
#include "ewgui/cfgfile.h"
#include "ewgui/ctrl.h"
#include "ew_controller_row.h"

#define MAX_STR 256
#define MAX_ROWS 256
#define STATUS_MAX 16384
#define LOG_TAIL 32768

/* --- Configuración (definidas en ew_controller_main.c) --- */
extern char MyModName[MAX_STR];
extern char RingName[MAX_STR];
extern char LogDir[MAX_STR];
extern int  HeartBeatInt;
extern int  LogFile;
extern int  PollInt;
extern pid_t MyPid;

extern unsigned char TypeHeartBeat;
extern unsigned char TypeReqStatus;
extern unsigned char TypeStatus;
extern unsigned char TypeStop;
extern unsigned char TypeRestart;
extern unsigned char TypeReconfig;

extern unsigned char MyInstId;
extern unsigned char MyModId;
extern SHM_INFO Region;
extern long g_ring_key;
extern int  g_attached;

extern EwCtrlStatus g_status;
extern time_t g_last_status;

/* --- GTK --- */
extern GtkWidget *g_tree, *g_rings_tree;
extern GListStore *g_store, *g_rings_store;
extern GtkSingleSelection *g_mod_sel;
extern GtkWidget *g_btn_start, *g_btn_restart, *g_btn_stop, *g_btn_reconfig, *g_btn_refresh;
extern GtkWidget *g_lbl_header, *g_lbl_statusbar;
extern GtkWidget *g_combo, *g_logview;
extern GtkTextBuffer *g_logbuf;

extern GtkWidget *g_cfg_combo, *g_cfg_grid, *g_lbl_cfgpath, *g_lbl_cfg_status;
extern GtkWidget *g_btn_cfg_save, *g_btn_cfg_reconfig, *g_btn_cfg_reload;
extern EwCfgFile g_cfg;
extern GtkWidget **g_cfg_entries;
extern int g_cfg_loaded;
extern int g_cfg_modidx;
extern int g_cfg_dirty;
extern int g_cfg_suppress;

extern int g_sel_idx;
extern GtkWidget *g_window;

/* --- Prototipos --- */
int  ReadConfig(char *configfile);
void ConnectToEarthworm(void);

void pedir_estado(void);
void detener_mod(int pid);
void reiniciar_mod(int pid);
void reconfigurar(void);

void poblar_listas(void);
void populate_combo(void);
void actualizar_log(void);
void aplicar_botones(void);
void cfg_rebuild_grid(void);
void cfg_vaciar_grid(void);
void cfg_cargar_modulo(int idx);

void on_row_selected(GtkSingleSelection *sel, GParamSpec *pspec, gpointer data);
typedef void (*EcConfirmCb)(gboolean accepted, gpointer user_data);
void ec_confirmar(const char *msg, EcConfirmCb cb, gpointer user_data);
void on_btn_stop(GtkWidget *w, gpointer data);
void on_btn_restart(GtkWidget *w, gpointer data);
void on_btn_start(GtkWidget *w, gpointer data);
void on_btn_reconfig(GtkWidget *w, gpointer data);
void on_btn_refresh(GtkWidget *w, gpointer data);
void on_log_combo_clicked(GtkWidget *w, gpointer data);
void on_cfg_entry_changed(GtkWidget *entry, gpointer data);
void on_cfg_combo_clicked(GtkWidget *w, gpointer data);
void on_btn_cfg_save(GtkWidget *w, gpointer data);
void on_btn_cfg_reconfig(GtkWidget *w, gpointer data);
void on_btn_cfg_reload(GtkWidget *w, gpointer data);

#endif /* EW_CONTROLLER_H */
