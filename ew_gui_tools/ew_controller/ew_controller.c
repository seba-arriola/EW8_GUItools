/***********************************************************************
 *                            ew_controller                            *
 *                                                                     *
 *  Graphical control panel for startstop, the Earthworm system        *
 *  manager. It connects to a dedicated CONTROL_RING and exchanges     *
 *  TYPE_REQSTATUS / TYPE_STATUS / TYPE_STOP / TYPE_RESTART /          *
 *  TYPE_RECONFIG / TYPE_HEARTBEAT messages to display the status of   *
 *  every module, stop/restart/start modules, show their logs and      *
 *  edit their .d configuration files.                                 *
 *                                                                     *
 *  Usage: ew_controller <configfile.d>                                *
 ***********************************************************************/

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
#include <kom.h>

#define MAX_STR 256
#define MAX_ROWS 256
#define STATUS_MAX 16384
#define LOG_TAIL 32768

/* Configuration parameters read from the .d file
 *************************************************/
char MyModName[MAX_STR] = "MOD_CONTROL";   /* Earthworm module name */
char RingName[MAX_STR] = "CONTROL_RING";   /* name of the dedicated control ring */
char LogDir[MAX_STR] = "";                 /* directory of the module logs (EW_LOG wins) */
int  HeartBeatInt = 30;                    /* heartbeat interval (seconds) */
int  LogFile = 1;                          /* 1 = write log to disk, 0 = console only */
int  PollInt = 5;                          /* status request interval to startstop (seconds) */

pid_t MyPid;                               /* this process id */
time_t timeLastBeat = 0;                   /* time of the last heartbeat sent */

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

/* --- DATA MODEL --- */
typedef struct {
   char  name[32];      /* module name */
   int   pid;           /* process id */
   char  status[16];    /* status word (Alive/Stop/Dead/...) */
   char  detalle[192];  /* rest of the status line (arguments) */
   char  config[64];    /* config file base name (for the log file) */
   char  cfgfile[64];   /* config file name (.d) */
} MODROW;

typedef struct {
   char  name[64];      /* ring name */
   int   key;           /* shared memory key */
   int   size;          /* size in KB */
} RINGROW;

/* --- CONFIGURATION EDITOR --- */
typedef struct {
   int   kind;      /* 0=name/value, 1=comment, 2=blank */
   char *prefix;    /* leading spaces */
   char *name;      /* variable name */
   char *gap1;      /* spaces between name and value */
   char *value;     /* value (keeps the original quotes) */
   char *gap2;      /* spaces between value and comment */
   char *comment;   /* comment from '#' to the end */
   char *raw;       /* original line (for kind 1/2) */
} CFGLINE;

typedef struct {
   char     path[512];
   int      nlines;
   int      last_newline;  /* 1 if the file ends with '\n' */
   CFGLINE *lines;
} CFGFILE;

MODROW g_mods[MAX_ROWS];    /* parsed module rows */
int    g_nmods = 0;         /* number of modules parsed */
RINGROW g_rings[MAX_ROWS];  /* parsed ring rows */
int    g_nrings = 0;        /* number of rings parsed */

/* Header fields extracted from the status message
 **************************************************/
char g_hostname[128] = "-";  /* Hostname-OS field */
char g_starttime[64] = "-";  /* Start time (UTC) */
char g_curtime[64] = "-";    /* Current time (UTC) */
char g_disk[64] = "-";       /* Disk space available */
char g_version[64] = "-";    /* Startstop Version */

time_t g_last_status = 0;    /* time of the last status message received */

/* --- GTK --- */
GtkWidget *g_tree, *g_rings_tree;            /* tree views for modules and rings */
GtkListStore *g_store, *g_rings_store;       /* tree models */
GtkWidget *g_btn_start, *g_btn_restart, *g_btn_stop, *g_btn_reconfig, *g_btn_refresh;  /* action buttons */
GtkWidget *g_lbl_header, *g_lbl_statusbar;   /* header and status bar labels */
GtkWidget *g_combo, *g_logview;              /* module combo and log text view */
GtkTextBuffer *g_logbuf;                     /* log text buffer */

/* Configuration tab */
GtkWidget *g_cfg_combo, *g_cfg_grid, *g_lbl_cfgpath, *g_lbl_cfg_status;   /* config tab widgets */
GtkWidget *g_btn_cfg_save, *g_btn_cfg_reconfig, *g_btn_cfg_reload;        /* config buttons */
CFGFILE g_cfg;                 /* configuration file being edited */
GtkWidget **g_cfg_entries;     /* entry widgets, one per editable line */
int g_cfg_loaded = 0;          /* 1 once a config file has been loaded */
int g_cfg_modidx = -1;         /* index of the module whose config is loaded */
int g_cfg_dirty = 0;           /* 1 if there are unsaved changes */
int g_cfg_suppress = 0;        /* 1 to suppress the combo "changed" handler */

int g_sel_idx = -1;            /* index of the selected module row */
int g_sel_pid = -1;            /* pid of the selected module (kept across refreshes) */
time_t g_last_user_click = 0;  /* time of the last user click on the tree */
GtkWidget *g_window;           /* main window */

static void aplicar_botones(void);

/* ***********************************************************
 *  Ring I/O helpers                                          *
 * ***********************************************************/
 /***********************************************************************
  *                              enviar()                               *
  *             Sends a message of the given type to the control        *
  *             ring, with the payload followed by a trailing newline.  *
  *               Nothing; logs an error if the write fails.            *
  ***********************************************************************/

static void enviar(unsigned char type, const char *payload)
{
   MSG_LOGO logo;
   char msg[512];
   logo.instid = MyInstId;
   logo.mod    = MyModId;
   logo.type   = type;
   strncpy(msg, payload, sizeof(msg) - 2);
   msg[sizeof(msg) - 2] = '\0';
   strcat(msg, "\n");
   if (tport_putmsg(&Region, &logo, strlen(msg), msg) != PUT_OK)
      logit("t", "ew_controller: Error sending message type %d to the ring.\n", (int) type);
}

 /***********************************************************************
  *                           pedir_estado()                            *
  *             Asks startstop for a status message by sending '?'      *
  *             with the TYPE_REQSTATUS message type.                   *
  *               Nothing.                                              *
  ***********************************************************************/

void pedir_estado(void)
{
   enviar(TypeReqStatus, "?");
}

 /***********************************************************************
  *                            detener_mod()                            *
  *             Asks startstop to stop the module with the given pid    *
  *             by sending a TYPE_STOP message containing the pid.      *
  *               Nothing.                                              *
  ***********************************************************************/

void detener_mod(int pid)
{
   char buf[32];
   snprintf(buf, sizeof(buf), "%d", pid);
   enviar(TypeStop, buf);
}

 /***********************************************************************
  *                           reiniciar_mod()                           *
  *             Asks startstop to restart the module with the given     *
  *             pid by sending a TYPE_RESTART message with that pid.    *
  *               Nothing.                                              *
  ***********************************************************************/

void reiniciar_mod(int pid)
{
   char buf[32];
   snprintf(buf, sizeof(buf), "%d", pid);
   enviar(TypeRestart, buf);
}

 /***********************************************************************
  *                           reconfigurar()                            *
  *             Requests startstop to re-read its configuration and     *
  *             apply the changes, sending a TYPE_RECONFIG message.     *
  *               Nothing.                                              *
  ***********************************************************************/

void reconfigurar(void)
{
   enviar(TypeReconfig, "");
}

/* ***********************************************************
 *  Config                                                    *
 * ***********************************************************/
 /***********************************************************************
  *                            ReadConfig()                             *
  *             Processes the command file with kom.c, filling the      *
  *             module configuration globals (MyModName, RingName, ...).*
  *               0 on success, -1 if any errors are encountered.       *
  ***********************************************************************/

int ReadConfig(char *configfile)
{
   int ncommand = 5, nmiss = 0, i;
   char init[8] = {0};
   char *com, *str;

   if (!k_open(configfile)) {
      fprintf(stderr, "ew_controller: Error opening config <%s>\n", configfile);
      return -1;
   }

   while (k_rd()) {
      com = k_str();
      if (!com || com[0] == '#') continue;

      if (k_its("MyModuleId")) { str = k_str(); if (str) strcpy(MyModName, str); init[0] = 1; }
      else if (k_its("Ring")) { str = k_str(); if (str) strcpy(RingName, str); init[1] = 1; }
      else if (k_its("HeartBeatInt")) { HeartBeatInt = k_int(); init[2] = 1; }
      else if (k_its("LogFile")) { LogFile = k_int(); init[3] = 1; }
      else if (k_its("PollInt")) { PollInt = k_int(); init[4] = 1; }
      else if (k_its("LogDir")) { str = k_str(); if (str) strcpy(LogDir, str); init[5] = 1; }
      else continue;

      if (k_err()) {
         fprintf(stderr, "ew_controller: Error parsing <%s> in <%s>\n", com, configfile);
         return -1;
      }
   }
   for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
   k_close();
   if (nmiss > 0) {
      fprintf(stderr, "ew_controller: ERROR, missing parameters in <%s>\n", configfile);
      return -1;
   }
   return 0;
}

 /***********************************************************************
  *                        ConnectToEarthworm()                         *
  *             Resolves the ring key, the message types and the        *
  *             installation and module ids from the tables.            *
  *               Nothing; exits if the ring is not registered.         *
  ***********************************************************************/

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

 /***********************************************************************
  *                           on_try_attach()                           *
  *             GTK timeout callback that attaches to the control ring  *
  *             once startstop has created it, then requests the first  *
  *             status.                                                 *
  *               G_SOURCE_REMOVE on attach, else G_SOURCE_CONTINUE.    *
  ***********************************************************************/

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

/* ***********************************************************
 *  Parsing the startstop STATUS                              *
 * ***********************************************************/
 /***********************************************************************
  *                           campo_despues()                           *
  *             Extracts the value of a labeled field from a status     *
  *             line: the text after '<label>:' trimmed of blanks and   *
  *             CR/LF.                                                  *
  *               Nothing; out is left empty if the label is not found. *
  ***********************************************************************/

static void campo_despues(char *line, const char *label, char *out, size_t n)
{
   char *c = strstr(line, label);
   if (!c) { out[0] = '\0'; return; }
   c = strchr(c, ':');
   if (!c) { out[0] = '\0'; return; }
   c++;
   while (*c == ' ') c++;
   strncpy(out, c, n - 1);
   out[n - 1] = '\0';
   char *nl = strchr(out, '\n');
   if (nl) *nl = '\0';
   char *e = out + strlen(out) - 1;
   while (e >= out && (*e == ' ' || *e == '\r')) { *e = '\0'; e--; }
}

 /***********************************************************************
  *                      extraer_config_de_args()                       *
  *             Scans the detail string for a '.d' argument and returns *
  *             its base name without directory or extension.           *
  *               Nothing; config is filled (empty if not found).       *
  ***********************************************************************/

static void extraer_config_de_args(char *detalle, char *config, size_t n)
{
   char tmp[192], *tok;
   config[0] = '\0';
   strncpy(tmp, detalle, sizeof(tmp) - 1);
   tmp[sizeof(tmp) - 1] = '\0';
   tok = strtok(tmp, " ");
   while (tok) {
      int len = strlen(tok);
      if (len > 3 && !strcmp(&tok[len - 2], ".d")) {
         /* log base = config without extension (same as get_prog_name2) */
         char *slash = strrchr(tok, '/');
         if (slash) tok = slash + 1;
         char *dot = strchr(tok, '.');
         if (dot) *dot = '\0';
         strncpy(config, tok, n - 1);
      }
      tok = strtok(NULL, " ");
   }
   config[n - 1] = '\0';
}

 /***********************************************************************
  *                      extraer_cfgfile_de_args()                      *
  *             Scans the detail string for a '.d' argument and returns *
  *             the file name without the directory part.               *
  *               Nothing; cfgfile is filled (empty if not found).      *
  ***********************************************************************/

static void extraer_cfgfile_de_args(char *detalle, char *cfgfile, size_t n)
{
   char tmp[192], *tok;
   cfgfile[0] = '\0';
   strncpy(tmp, detalle, sizeof(tmp) - 1);
   tmp[sizeof(tmp) - 1] = '\0';
   tok = strtok(tmp, " ");
   while (tok) {
      int len = strlen(tok);
      if (len > 3 && !strcmp(&tok[len - 2], ".d")) {
         char *slash = strrchr(tok, '/');
         if (slash) tok = slash + 1;
         strncpy(cfgfile, tok, n - 1);
      }
      tok = strtok(NULL, " ");
   }
   cfgfile[n - 1] = '\0';
}

 /***********************************************************************
  *                           parse_status()                            *
  *             Parses a startstop status message, filling the module   *
  *             ring arrays plus the header fields (host, times, disk). *
  *               Nothing; g_mods/g_rings are repopulated.              *
  ***********************************************************************/

void parse_status(char *buf)
{
   char *save = NULL;
   char *line;

   g_nmods = 0;
   g_nrings = 0;

   for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
      char name[32], status[16], rest[192];

      if (strstr(line, "name/key/size")) {
         char *v = strchr(line, ':');
         if (v) {
            RINGROW r;
            if (sscanf(v + 1, "%63s / %d / %d", r.name, &r.key, &r.size) == 3) {
               r.name[sizeof(r.name) - 1] = '\0';
               if (g_nrings < MAX_ROWS) g_rings[g_nrings++] = r;
            }
         }
         continue;
      }
      if (strstr(line, "Hostname-OS")) { campo_despues(line, "Hostname-OS", g_hostname, sizeof(g_hostname)); continue; }
      if (strstr(line, "Start time (UTC)")) { campo_despues(line, "Start time (UTC)", g_starttime, sizeof(g_starttime)); continue; }
      if (strstr(line, "Current time (UTC)")) { campo_despues(line, "Current time (UTC)", g_curtime, sizeof(g_curtime)); continue; }
      if (strstr(line, "Disk space avail")) { campo_despues(line, "Disk space avail", g_disk, sizeof(g_disk)); continue; }
      if (strstr(line, "Stopstart Version")) { campo_despues(line, "Stopstart Version", g_version, sizeof(g_version)); continue; }
      if (strstr(line, "Process  Process") || strstr(line, "Name      Id")) continue;
      if (strstr(line, "-------")) continue;

      if (g_nmods >= MAX_ROWS) break;
      if (sscanf(line, "%31s %d %15s", name, &g_mods[g_nmods].pid, status) == 3) {
         if (g_mods[g_nmods].pid <= 0) continue;
         strncpy(g_mods[g_nmods].name, name, 31);
         g_mods[g_nmods].name[31] = '\0';
         strncpy(g_mods[g_nmods].status, status, 15);
         g_mods[g_nmods].status[15] = '\0';

         /* rest of the line = detail */
         char *reststart = strstr(line, status) + strlen(status);
         while (*reststart == ' ') reststart++;
         strncpy(rest, reststart, sizeof(rest) - 1);
         rest[sizeof(rest) - 1] = '\0';
         strncpy(g_mods[g_nmods].detalle, rest, sizeof(g_mods[g_nmods].detalle) - 1);
         g_mods[g_nmods].detalle[sizeof(g_mods[g_nmods].detalle) - 1] = '\0';

         extraer_config_de_args(rest, g_mods[g_nmods].config, sizeof(g_mods[g_nmods].config));
         if (g_mods[g_nmods].config[0] == '\0') {
            /* if no .d file found, use the process name */
            strncpy(g_mods[g_nmods].config, g_mods[g_nmods].name, sizeof(g_mods[g_nmods].config) - 1);
         }

         extraer_cfgfile_de_args(rest, g_mods[g_nmods].cfgfile, sizeof(g_mods[g_nmods].cfgfile));
         if (g_mods[g_nmods].cfgfile[0] == '\0') {
            /* fallback: derive from the process name */
            strncpy(g_mods[g_nmods].cfgfile, g_mods[g_nmods].name, sizeof(g_mods[g_nmods].cfgfile) - 1);
            strncat(g_mods[g_nmods].cfgfile, ".d", sizeof(g_mods[g_nmods].cfgfile) - 1);
            g_mods[g_nmods].cfgfile[sizeof(g_mods[g_nmods].cfgfile) - 1] = '\0';
         }
         if (!strcmp(g_mods[g_nmods].name, "startstop"))
            strncpy(g_mods[g_nmods].cfgfile, "startstop_unix.d", sizeof(g_mods[g_nmods].cfgfile) - 1);
         g_nmods++;
      }
   }
}

/* ***********************************************************
 *  Configuration editor: parser/writer of .d files          *
 * ***********************************************************/
 /***********************************************************************
  *                             cfg_libre()                             *
  *             Frees all the allocated fields of a CFGFILE (lines and  *
  *             their strings) and resets the line count.               *
  *               Nothing.                                              *
  ***********************************************************************/

static void cfg_libre(CFGFILE *cf)
{
   int i;
   if (!cf) return;
   for (i = 0; i < cf->nlines; i++) {
      CFGLINE *l = &cf->lines[i];
      free(l->prefix); free(l->name); free(l->gap1); free(l->value);
      free(l->gap2); free(l->comment); free(l->raw);
   }
   free(cf->lines);
   cf->lines = NULL;
   cf->nlines = 0;
}

 /***********************************************************************
  *                          cfg_parse_linea()                          *
  *             Splits a config line into its fields (prefix, name, gap,*
  *             value, comment) honoring quotes around the value.       *
  *               Nothing; the CFGLINE is filled in.                    *
  ***********************************************************************/

static void cfg_parse_linea(char *line, CFGLINE *l)
{
   char *p = line, *q;
   char *name, *vstart, *vaux;
   int in_q = 0;
   char *hash = NULL;

   memset(l, 0, sizeof(*l));
   l->prefix = strdup(p);
   l->prefix[0] = '\0';

   /* prefix */
   p = line;
   while (*p == ' ' || *p == '\t') p++;
   strncpy(l->prefix, line, p - line);
   l->prefix[p - line] = '\0';

   /* blank line or comment */
   if (*p == '\0') { l->kind = 2; l->raw = strdup(line); return; }
   if (*p == '#')  { l->kind = 1; l->raw = strdup(line); return; }

   /* name = first token */
   name = p;
   while (*p && *p != ' ' && *p != '\t') p++;
   l->name = malloc(p - name + 1);
   memcpy(l->name, name, p - name);
   l->name[p - name] = '\0';

   /* gap1 */
   q = p;
   while (*q == ' ' || *q == '\t') q++;
   l->gap1 = malloc(q - p + 1);
   memcpy(l->gap1, p, q - p);
   l->gap1[q - p] = '\0';

   /* value = up to the first '#' outside quotes */
   vstart = q;
   vaux = q;
   while (*vaux) {
      if (*vaux == '"') in_q = !in_q;
      else if (*vaux == '#' && !in_q) { hash = vaux; break; }
      vaux++;
   }
   if (hash) {
      /* gap2 = spaces between value and '#', comment = from '#' */
      q = hash;
      while (q > vstart && (q[-1] == ' ' || q[-1] == '\t')) q--;
      l->value = malloc(q - vstart + 1);
      memcpy(l->value, vstart, q - vstart);
      l->value[q - vstart] = '\0';
      l->gap2 = malloc(hash - q + 1);
      memcpy(l->gap2, q, hash - q);
      l->gap2[hash - q] = '\0';
      /* full comment (includes trailing spaces/tabs) */
      l->comment = strdup(hash);
   } else {
      char *vend = vstart + strlen(vstart);
      char *ve = vend;
      while (ve > vstart && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
      l->value = malloc(ve - vstart + 1);
      memcpy(l->value, vstart, ve - vstart);
      l->value[ve - vstart] = '\0';
      /* trailing spaces of the line are kept in gap2 */
      l->gap2 = malloc(vend - ve + 1);
      memcpy(l->gap2, ve, vend - ve);
      l->gap2[vend - ve] = '\0';
      l->comment = strdup("");
   }
   l->kind = 0;
}

 /***********************************************************************
  *                             cfg_leer()                              *
  *             Reads a .d file into a CFGFILE, one CFGLINE per line,   *
  *             and records whether the file ends with a newline.       *
  *               0 on success, -1 if the file cannot be opened.        *
  ***********************************************************************/

static int cfg_leer(const char *path, CFGFILE *cf)
{
   FILE *f = fopen(path, "r");
   char line[1024];
   int cap = 0;
   int last_nl = 1;

   if (!f) return -1;
   memset(cf, 0, sizeof(*cf));
   snprintf(cf->path, sizeof(cf->path), "%s", path);

   while (fgets(line, sizeof(line), f)) {
      char *nl = strchr(line, '\n');
      last_nl = (nl != NULL);
      if (nl) *nl = '\0';
      if (cf->nlines == cap) {
         cap = cap ? cap * 2 : 32;
         CFGLINE *n = realloc(cf->lines, cap * sizeof(CFGLINE));
         if (!n) { fclose(f); cfg_libre(cf); return -1; }
         cf->lines = n;
      }
      cfg_parse_linea(line, &cf->lines[cf->nlines]);
      cf->nlines++;
   }
   cf->last_newline = last_nl;
   fclose(f);
   return 0;
}

 /***********************************************************************
  *                          copiar_archivo()                           *
  *             Copies a file byte by byte (makes the .bak backup).     *
  *               0 on success, -1 on failure.                          *
  ***********************************************************************/

static int copiar_archivo(const char *src, const char *dst)
{
   FILE *in = fopen(src, "rb"), *out;
   char buf[4096];
   size_t n;
   if (!in) return -1;
   out = fopen(dst, "wb");
   if (!out) { fclose(in); return -1; }
   while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
      fwrite(buf, 1, n, out);
   fclose(in);
   fclose(out);
   return 0;
}

 /***********************************************************************
  *                            cfg_guardar()                            *
  *             Writes the CFGFILE to disk with a .bak backup and an    *
  *             atomic rename, preserving formatting and comments.      *
  *               0 on success, -1 on failure.                          *
  ***********************************************************************/

static int cfg_guardar(CFGFILE *cf)
{
   char bak[600], tmp[600];
   FILE *f;
   int i;

   snprintf(bak, sizeof(bak), "%s.bak", cf->path);
   if (copiar_archivo(cf->path, bak) != 0) return -1;

   snprintf(tmp, sizeof(tmp), "%s.tmp", cf->path);
   f = fopen(tmp, "w");
   if (!f) return -1;
   for (i = 0; i < cf->nlines; i++) {
      CFGLINE *l = &cf->lines[i];
      if (l->kind == 0) {
         fputs(l->prefix, f);
         fputs(l->name, f);
         fputs(l->gap1, f);
         fputs(l->value, f);
         fputs(l->gap2, f);
         fputs(l->comment, f);
      } else {
         fputs(l->raw, f);
      }
      if (!(i == cf->nlines - 1 && !cf->last_newline))
         fputc('\n', f);
   }
   if (fclose(f) != 0) return -1;
   if (rename(tmp, cf->path) != 0) return -1;
   return 0;
}

 /***********************************************************************
  *                            ruta_config()                            *
  *             Builds the full path of a config file, prefixing it with*
  *             EW_PARAMS unless it is already absolute.                *
  *               Nothing; out is filled.                               *
  ***********************************************************************/

static void ruta_config(const char *cfgfile, char *out, size_t n)
{
   const char *ep = getenv("EW_PARAMS");
   if (cfgfile[0] == '/') { snprintf(out, n, "%s", cfgfile); return; }
   if (ep && *ep) snprintf(out, n, "%s/%s", ep, cfgfile);
   else snprintf(out, n, "%s", cfgfile);
}

/* ***********************************************************
 *  GTK: populate lists                                        *
 * ***********************************************************/
 /***********************************************************************
  *                           poblar_listas()                           *
  *             Refreshes module and ring trees from the parsed arrays, *
  *             restores the previous selection by pid, and updates the *
  *             header label and the button state.                      *
  *               Nothing.                                              *
  ***********************************************************************/

void poblar_listas(void)
{
   GtkTreeIter iter;
   int i;

   /* keep the target module (by PID) across the refresh */
   if (g_sel_idx >= 0 && g_sel_idx < g_nmods) g_sel_pid = g_mods[g_sel_idx].pid;
   int intent_pid = g_sel_pid;

   gtk_list_store_clear(g_store);
   for (i = 0; i < g_nmods; i++) {
      gtk_list_store_append(g_store, &iter);
      gtk_list_store_set(g_store, &iter,
         0, g_mods[i].name,
         1, g_mods[i].pid,
         2, g_mods[i].status,
         3, g_mods[i].detalle,
         -1);
   }

   gtk_list_store_clear(g_rings_store);
   for (i = 0; i < g_nrings; i++) {
      gtk_list_store_append(g_rings_store, &iter);
      gtk_list_store_set(g_rings_store, &iter, 0, g_rings[i].name, 1, g_rings[i].key, 2, g_rings[i].size, -1);
   }

   /* relocate the target module in the new list */
   g_sel_idx = -1;
   for (i = 0; i < g_nmods; i++)
      if (g_mods[i].pid == intent_pid) { g_sel_idx = i; break; }

   /* restore the visual selection only if the user has just not interacted,
      so as not to "fight" his click every time a refresh arrives */
   if (g_sel_idx >= 0 && time(NULL) - g_last_user_click > 3) {
      gboolean ok = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(g_store), &iter);
      while (ok) {
         int pid;
         gtk_tree_model_get(GTK_TREE_MODEL(g_store), &iter, 1, &pid, -1);
         if (pid == intent_pid) {
            GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(g_tree));
            gtk_tree_selection_select_iter(sel, &iter);
            break;
         }
         ok = gtk_tree_model_iter_next(GTK_TREE_MODEL(g_store), &iter);
      }
   }
   aplicar_botones();

   char hdr[512];
   snprintf(hdr, sizeof(hdr),
            "EARTHWORM SYSTEM STATUS\n%s   Start: %s   Current: %s   Disk: %s   Version: %s",
            g_hostname, g_starttime, g_curtime, g_disk, g_version);
   gtk_label_set_text(GTK_LABEL(g_lbl_header), hdr);
}

/* ***********************************************************
 *  Acciones GTK                                              *
 * ***********************************************************/
 /***********************************************************************
  *                          aplicar_botones()                          *
  *             Enables/disables the Stop/Restart/Start buttons based on*
  *             the selected module and status (startstop is protected).*
  *               Nothing.                                              *
  ***********************************************************************/

static void aplicar_botones(void)
{
   gboolean sel_ok = (g_sel_idx >= 0 && g_sel_idx < g_nmods);
   gboolean es_startstop = sel_ok && !strcmp(g_mods[g_sel_idx].name, "startstop");
   const char *st = sel_ok ? g_mods[g_sel_idx].status : "";

   gtk_widget_set_sensitive(g_btn_stop, sel_ok && !es_startstop && !strcmp(st, "Alive"));
   gtk_widget_set_sensitive(g_btn_restart, sel_ok && !es_startstop);
   gtk_widget_set_sensitive(g_btn_start, sel_ok && !es_startstop &&
                            (!strcmp(st, "Stop") || !strcmp(st, "Dead")));
}

 /***********************************************************************
  *                          on_row_selected()                          *
  *             GTK handler for a change of the module tree selection;  *
  *             updates the selected index and syncs the config combo.  *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_row_selected(GtkTreeSelection *sel, gpointer data)
{
   GtkTreeModel *model;
   GtkTreeIter iter;
   gboolean sel_ok;

   sel_ok = gtk_tree_selection_get_selected(sel, &model, &iter);
   g_sel_idx = -1;
   if (sel_ok) {
      gint pid = -1;
      gtk_tree_model_get(model, &iter, 1, &pid, -1);
      for (int i = 0; i < g_nmods; i++)
         if (g_mods[i].pid == pid) { g_sel_idx = i; break; }
      if (g_sel_idx >= 0) g_sel_pid = g_mods[g_sel_idx].pid;
   }
   aplicar_botones();

   /* sync the selection with the combo of the Configuration tab */
   if (g_cfg_combo && g_sel_idx >= 0 && g_sel_idx < g_nmods) {
      gint cur = gtk_combo_box_get_active(GTK_COMBO_BOX(g_cfg_combo));
      if (cur != g_sel_idx)
         gtk_combo_box_set_active(GTK_COMBO_BOX(g_cfg_combo), g_sel_idx);
   }
}

 /***********************************************************************
  *                       on_tree_button_press()                        *
  *             GTK handler that timestamps a user click on the tree so *
  *             the next refresh does not fight the selection.          *
  *               FALSE (event not handled).                            *
  ***********************************************************************/

static gboolean on_tree_button_press(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
   time(&g_last_user_click);
   return FALSE;
}

 /***********************************************************************
  *                             confirmar()                             *
  *             Shows a modal Yes/No warning dialog with a message.     *
  *               TRUE if the user answered Yes.                        *
  ***********************************************************************/

static gboolean confirmar(const char *msg)
{
   GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(g_window), GTK_DIALOG_MODAL,
                     GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s", msg);
   gint r = gtk_dialog_run(GTK_DIALOG(dlg));
   gtk_widget_destroy(dlg);
   return r == GTK_RESPONSE_YES;
}

 /***********************************************************************
  *                            on_btn_stop()                            *
  *             Asks for confirmation and sends a Stop request for the  *
  *             selected module, then requests a status refresh.        *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_btn_stop(GtkWidget *w, gpointer data)
{
   if (g_sel_idx >= 0) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Stop the module '%s' (pid %d)?",
               g_mods[g_sel_idx].name, g_mods[g_sel_idx].pid);
      if (!confirmar(msg)) return;
      detener_mod(g_mods[g_sel_idx].pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Stop sent...");
      pedir_estado();
   }
}

 /***********************************************************************
  *                          on_btn_restart()                           *
  *             Asks confirmation and sends a Restart request for the   *
  *             selected module, then requests a status refresh.        *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_btn_restart(GtkWidget *w, gpointer data)
{
   if (g_sel_idx >= 0) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Restart the module '%s' (pid %d)?",
               g_mods[g_sel_idx].name, g_mods[g_sel_idx].pid);
      if (!confirmar(msg)) return;
      reiniciar_mod(g_mods[g_sel_idx].pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Restart sent...");
      pedir_estado();
   }
}

 /***********************************************************************
  *                           on_btn_start()                            *
  *             Asks confirmation and sends a Restart request (used to  *
  *             start) for the selected module, then requests a status. *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_btn_start(GtkWidget *w, gpointer data)
{
   if (g_sel_idx >= 0) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Start the module '%s'?",
               g_mods[g_sel_idx].name);
      if (!confirmar(msg)) return;
      reiniciar_mod(g_mods[g_sel_idx].pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Start sent...");
      pedir_estado();
   }
}

 /***********************************************************************
  *                          on_btn_reconfig()                          *
  *             Sends a Reconfig request to startstop, then a status.   *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_btn_reconfig(GtkWidget *w, gpointer data)
{
   reconfigurar();
   gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Reconfig sent...");
   pedir_estado();
}

 /***********************************************************************
  *                          on_btn_refresh()                           *
  *             Requests a new status and shows 'Updating...'.          *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_btn_refresh(GtkWidget *w, gpointer data)
{
   pedir_estado();
   gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Updating...");
}

/* ***********************************************************
 *  Log viewer                                               *
 * ***********************************************************/
 /***********************************************************************
  *                          populate_combo()                           *
  *             Repopulates the module and config combos from the module*
  *             array, preserving the previous selection when possible. *
  *               Nothing.                                              *
  ***********************************************************************/

static void populate_combo(void)
{
   gchar *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(g_combo));
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(g_combo));
   for (int i = 0; i < g_nmods; i++)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_combo), g_mods[i].name);
   if (sel) {
      int idx = -1;
      for (int i = 0; i < g_nmods; i++)
         if (!strcmp(g_mods[i].name, sel)) { idx = i; break; }
      gtk_combo_box_set_active(GTK_COMBO_BOX(g_combo), idx);
      g_free(sel);
   } else if (g_nmods > 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(g_combo), 0);
   }

   if (!g_cfg_combo) return;
   /* configuration combo: same population; the handler avoids reloading
      if the selected module has not changed */
   gchar *csel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(g_cfg_combo));
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(g_cfg_combo));
   for (int i = 0; i < g_nmods; i++)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_cfg_combo), g_mods[i].name);
   if (csel) {
      int idx = -1;
      for (int i = 0; i < g_nmods; i++)
         if (!strcmp(g_mods[i].name, csel)) { idx = i; break; }
      gtk_combo_box_set_active(GTK_COMBO_BOX(g_cfg_combo), idx);
      g_free(csel);
   } else if (g_nmods > 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(g_cfg_combo), 0);
   }
}

 /***********************************************************************
  *                             tail_file()                             *
  *             Reads the last maxbytes of a file, starting at a line   *
  *             boundary, and returns them as a NUL-terminated string.  *
  *               A malloc'ed string, or NULL if it cannot be opened.   *
  ***********************************************************************/

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

 /***********************************************************************
  *                          actualizar_log()                           *
  *             Finds the most recent .log file for the selected module *
  *             and shows its tail in the log text view.                *
  *               Nothing.                                              *
  ***********************************************************************/

static void actualizar_log(void)
{
   gchar *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(g_combo));
   if (!sel || !*sel) return;
   char base[64];
   strncpy(base, sel, sizeof(base) - 1);
   base[sizeof(base) - 1] = '\0';
   g_free(sel);

   /* startstop names its log with the executable name (argv[0]),
      not with the config file (it makes a second logit_init with argv[0]) */
   if (!strcmp(base, "startstop")) strcpy(base, "startstop");
   else {
      int k = -1;
      for (int i = 0; i < g_nmods; i++)
         if (!strcmp(g_mods[i].name, base)) { k = i; break; }
      if (k >= 0 && g_mods[k].config[0])
         strncpy(base, g_mods[k].config, sizeof(base) - 1);
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

 /***********************************************************************
  *                         on_combo_changed()                          *
  *             GTK handler that reloads the log when the module combo  *
  *             selection changes.                                      *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_combo_changed(GtkComboBox *combo, gpointer data)
{
   actualizar_log();
}

/* ***********************************************************
 *  Configuration tab                                          *
 * ***********************************************************/
 /***********************************************************************
  *                      actualizar_botones_cfg()                       *
  *             Enables/disables the config buttons (Save/Reconfig/     *
  *             according to whether a config file is loaded.           *
  *               Nothing.                                              *
  ***********************************************************************/

static void actualizar_botones_cfg(void)
{
   gboolean ok = (g_cfg_loaded == 1);
   gtk_widget_set_sensitive(g_btn_cfg_save, ok);
   gtk_widget_set_sensitive(g_btn_cfg_reconfig, ok);
   gtk_widget_set_sensitive(g_btn_cfg_reload, ok);
}

 /***********************************************************************
  *                       on_cfg_entry_changed()                        *
  *             GTK handler for edits in the config grid; it marks the  *
  *             the file as dirty and updates the status label.         *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_cfg_entry_changed(GtkWidget *entry, gpointer data)
{
   g_cfg_dirty = 1;
   gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), "There are unsaved changes...");
}

 /***********************************************************************
  *                          cfg_vaciar_grid()                          *
  *             Destroys all the widgets currently in the config grid.  *
  *               Nothing.                                              *
  ***********************************************************************/

static void cfg_vaciar_grid(void)
{
   GList *ch, *l;
   ch = gtk_container_get_children(GTK_CONTAINER(g_cfg_grid));
   for (l = ch; l; l = l->next)
      gtk_widget_destroy(GTK_WIDGET(l->data));
   g_list_free(ch);
}

 /***********************************************************************
  *                         cfg_rebuild_grid()                          *
  *             Rebuilds the configuration grid: one bold label and one *
  *             entry per editable line of the loaded CFGFILE.          *
  *               Nothing.                                              *
  ***********************************************************************/

static void cfg_rebuild_grid(void)
{
   int i, r = 0;
   cfg_vaciar_grid();
   if (g_cfg_entries) { free(g_cfg_entries); g_cfg_entries = NULL; }
   g_cfg_entries = calloc(g_cfg.nlines + 1, sizeof(GtkWidget *));

   for (i = 0; i < g_cfg.nlines; i++) {
      CFGLINE *l = &g_cfg.lines[i];
      g_cfg_entries[i] = NULL;
      if (l->kind != 0) continue;

      gchar *mark = g_markup_printf_escaped("<b>%s</b>", l->name);
      GtkWidget *lbl = gtk_label_new(NULL);
      gtk_label_set_markup(GTK_LABEL(lbl), mark);
      g_free(mark);
      gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
      gtk_widget_set_hexpand(lbl, FALSE);

      GtkWidget *entry = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(entry), l->value);
      gtk_widget_set_hexpand(entry, TRUE);

      gtk_grid_attach(GTK_GRID(g_cfg_grid), lbl, 0, r, 1, 1);
      gtk_grid_attach(GTK_GRID(g_cfg_grid), entry, 1, r, 1, 1);
      g_cfg_entries[i] = entry;
      g_signal_connect(entry, "changed", G_CALLBACK(on_cfg_entry_changed), NULL);
      r++;
   }
   gtk_widget_show_all(g_cfg_grid);
}

 /***********************************************************************
  *                         cfg_cargar_modulo()                         *
  *             Loads the .d config file of the given module into the   *
  *             configuration editor and rebuilds the grid.             *
  *               Nothing; the status label shows the result.           *
  ***********************************************************************/

static void cfg_cargar_modulo(int idx)
{
   char path[512];
   char msg[512];
   int nedit = 0, i;

   if (idx < 0 || idx >= g_nmods) return;
   ruta_config(g_mods[idx].cfgfile, path, sizeof(path));

   cfg_libre(&g_cfg);
   if (cfg_leer(path, &g_cfg) != 0) {
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

 /***********************************************************************
  *                       on_cfg_combo_changed()                        *
  *             GTK handler for the config module combo; avoids a reload*
  *             when the file is unchanged and warns of unsaved changes.*
  *               Nothing.                                              *
  ***********************************************************************/

static void on_cfg_combo_changed(GtkComboBox *combo, gpointer data)
{
   gint idx;
   if (g_cfg_suppress) return;
   idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
   if (idx < 0) return;

   /* same file (even if the position in the list changes): do not reload */
   if (g_cfg_loaded && g_cfg_modidx >= 0 && g_cfg_modidx < g_nmods &&
       idx < g_nmods && !strcmp(g_mods[g_cfg_modidx].cfgfile, g_mods[idx].cfgfile)) {
      g_cfg_modidx = idx;
      return;
   }

   if (g_cfg_loaded && g_cfg_dirty) {
      char msg[300];
      snprintf(msg, sizeof(msg), "There are unsaved changes in '%s'. Discard them?",
               (g_cfg_modidx >= 0 && g_cfg_modidx < g_nmods) ? g_mods[g_cfg_modidx].name : "?");
      if (!confirmar(msg)) {
         g_cfg_suppress = 1;
         gtk_combo_box_set_active(GTK_COMBO_BOX(combo), g_cfg_modidx);
         g_cfg_suppress = 0;
         return;
      }
   }
   cfg_cargar_modulo(idx);
}

 /***********************************************************************
  *                          on_btn_cfg_save()                          *
  *             Copies the entry contents into the model and writes the *
  *             config file to disk (with a .bak backup).               *
  *               Nothing; the status label shows the result.           *
  ***********************************************************************/

static void on_btn_cfg_save(GtkWidget *w, gpointer data)
{
   char msg[512];
   int i;

   if (!g_cfg_loaded) return;
   /* dump the entries into the model */
   for (i = 0; i < g_cfg.nlines; i++) {
      if (!g_cfg_entries[i]) continue;
      const gchar *txt = gtk_entry_get_text(GTK_ENTRY(g_cfg_entries[i]));
      free(g_cfg.lines[i].value);
      g_cfg.lines[i].value = g_strdup(txt);
   }
   if (cfg_guardar(&g_cfg) == 0) {
      g_cfg_dirty = 0;
      snprintf(msg, sizeof(msg), "Saved to %s (backup .bak)", g_cfg.path);
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), msg);
   } else {
      snprintf(msg, sizeof(msg), "ERROR saving %s", g_cfg.path);
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), msg);
   }
}

 /***********************************************************************
  *                        on_btn_cfg_reconfig()                        *
  *             Saves the edited config and then asks startstop (or the *
  *             module) to apply it by reconfiguring or restarting.     *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_btn_cfg_reconfig(GtkWidget *w, gpointer data)
{
   char msg[300];
   if (!g_cfg_loaded || g_cfg_modidx < 0) return;
   on_btn_cfg_save(w, data);
   if (g_cfg_dirty) return;   /* could not be saved */
   snprintf(msg, sizeof(msg), "Restart '%s' to apply the saved config?",
            g_mods[g_cfg_modidx].name);
   if (!confirmar(msg)) return;
   if (!strcmp(g_mods[g_cfg_modidx].name, "startstop")) {
      reconfigurar();
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Reconfig (startstop) sent...");
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status), "startstop re-reads its config and applies the changes.");
   } else {
      reiniciar_mod(g_mods[g_cfg_modidx].pid);
      gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), "Restart sent...");
      gtk_label_set_text(GTK_LABEL(g_lbl_cfg_status),
         "Config saved. The module re-reads its config on restart.");
   }
   pedir_estado();
}

 /***********************************************************************
  *                         on_btn_cfg_reload()                         *
  *             Reloads the config file from disk, discarding the       *
  *             unsaved changes after confirmation.                     *
  *               Nothing.                                              *
  ***********************************************************************/

static void on_btn_cfg_reload(GtkWidget *w, gpointer data)
{
   if (!g_cfg_loaded) return;
   if (g_cfg_dirty && !confirmar("There are unsaved changes. Reload the file and discard them?"))
      return;
   cfg_cargar_modulo(g_cfg_modidx);
}

/* ***********************************************************
 *  Background loops (GTK main loop)                           *
 * ***********************************************************/
 /***********************************************************************
  *                              on_poll()                              *
  *             Periodic timeout: sends heartbeats and status requests, *
  *             and disables the buttons if startstop stops responding. *
  *               G_SOURCE_CONTINUE always.                             *
  ***********************************************************************/

static gboolean on_poll(gpointer data)
{
   if (!g_attached) return G_SOURCE_CONTINUE;
   time_t now;
   time(&now);
   if (now - timeLastBeat >= HeartBeatInt) {
      timeLastBeat = now;
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

 /***********************************************************************
  *                              on_read()                              *
  *             GTK timeout that drains TYPE_STATUS from the ring and   *
  *             parses them and refreshes the whole interface.          *
  *               G_SOURCE_CONTINUE always.                             *
  ***********************************************************************/

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
         parse_status(msg);
         poblar_listas();
         populate_combo();
         time(&g_last_status);
         char sb[256];
         if (g_sel_idx >= 0 && g_sel_idx < g_nmods)
            snprintf(sb, sizeof(sb), "Updated %s UTC - %d modules - target: %s (%s)",
                     g_curtime, g_nmods, g_mods[g_sel_idx].name, g_mods[g_sel_idx].status);
         else
            snprintf(sb, sizeof(sb), "Updated %s UTC - %d modules", g_curtime, g_nmods);
         gtk_label_set_text(GTK_LABEL(g_lbl_statusbar), sb);
         gtk_widget_set_sensitive(g_btn_reconfig, TRUE);
         actualizar_log();
      }
   } while (res == GET_OK);

   return G_SOURCE_CONTINUE;
}

/* ***********************************************************
 *  Main                                                      *
 * ***********************************************************/
 /***********************************************************************
  *                               main()                                *
  *             Entry point: reads the config, connects to the ring,    *
  *             builds the GTK interface and runs the main loop.        *
  *               0 on clean exit.                                      *
  ***********************************************************************/

int main(int argc, char *argv[])
{
   if (argc != 2) {
      fprintf(stderr, "Usage: %s <configfile.d>\n", argv[0]);
      exit(1);
   }
   if (ReadConfig(argv[1]) != 0) exit(1);
   /* LogDir: priority EW_LOG (environment variable) > config > "logs" */
   {
      const char *ewlog = getenv("EW_LOG");
      if (ewlog && *ewlog)
         strncpy(LogDir, ewlog, sizeof(LogDir) - 1);
      else if (LogDir[0] == '\0')
         strncpy(LogDir, "logs", sizeof(LogDir) - 1);
      LogDir[sizeof(LogDir) - 1] = '\0';
   }

   logit_init(argv[1], 0, 1024, LogFile);
   MyPid = getpid();

   gtk_init(&argc, &argv);
   setlocale(LC_NUMERIC, "C");

   ConnectToEarthworm();

   GtkCssProvider *provider = gtk_css_provider_new();
   gtk_css_provider_load_from_data(provider,
      "treeview grid-line { border-color: #555555; }\n"
      "#btn_stop { background-image: none; background-color: #dc3545; color: #ffffff; font-weight: bold; padding: 5px 18px; border-radius: 4px; border: 1px solid #bd2130; }\n"
      "#btn_stop:hover { background-color: #c82333; }\n"
      "#btn_start { background-image: none; background-color: #28a745; color: #ffffff; font-weight: bold; padding: 5px 18px; border-radius: 4px; border: 1px solid #218838; }\n"
      "#btn_start:hover { background-color: #218838; }\n"
      "#btn_restart { background-image: none; background-color: #ffc107; color: #212529; font-weight: bold; padding: 5px 18px; border-radius: 4px; border: 1px solid #e0a800; }\n"
      "#btn_restart:hover { background-color: #e0a800; }\n"
      "#btn_reconfig { background-image: none; background-color: #17a2b8; color: #ffffff; font-weight: bold; padding: 5px 18px; border-radius: 4px; border: 1px solid #117a8b; }\n"
      "#btn_reconfig:hover { background-color: #138496; }\n"
      "#btn_refresh { background-image: none; background-color: #6c757d; color: #ffffff; font-weight: bold; padding: 5px 18px; border-radius: 4px; border: 1px solid #5a6268; }\n"
      "#btn_refresh:hover { background-color: #5a6268; }\n"
      "textview { font-family: monospace; font-size: 11px; }\n", -1, NULL);
   gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
       GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   g_window = window;
   gtk_window_set_title(GTK_WINDOW(window), "Earthworm Controller");
   gtk_window_set_default_size(GTK_WINDOW(window), 980, 720);
   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_add(GTK_CONTAINER(window), vbox);

   g_lbl_header = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(g_lbl_header), 0.0);
   gtk_widget_set_margin_top(g_lbl_header, 6);
   gtk_widget_set_margin_bottom(g_lbl_header, 2);
   gtk_box_pack_start(GTK_BOX(vbox), g_lbl_header, FALSE, FALSE, 0);

   /* ---- Notebook: Modules / Configuration tabs ---- */
   GtkWidget *notebook = gtk_notebook_new();
   gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

   /* ================= Tab 1: MODULES ================= */
   GtkWidget *page_mod = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_mod, gtk_label_new("Modules"));

   GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_box_pack_start(GTK_BOX(page_mod), paned, TRUE, TRUE, 0);

   GtkWidget *box_mod = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   gtk_paned_pack1(GTK_PANED(paned), box_mod, TRUE, FALSE);

   g_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
   g_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_store));
   gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(g_tree), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);

   GtkCellRenderer *renderer;
   GtkTreeViewColumn *col;
   GtkTreeSelection *sel;

   renderer = gtk_cell_renderer_text_new();
   col = gtk_tree_view_column_new_with_attributes("Module", renderer, "text", 0, NULL);
   gtk_tree_view_column_set_expand(GTK_TREE_VIEW_COLUMN(col), FALSE);
   gtk_tree_view_append_column(GTK_TREE_VIEW(g_tree), col);

   renderer = gtk_cell_renderer_text_new();
   col = gtk_tree_view_column_new_with_attributes("PID", renderer, "text", 1, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(g_tree), col);

   renderer = gtk_cell_renderer_text_new();
   col = gtk_tree_view_column_new_with_attributes("Status", renderer, "text", 2, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(g_tree), col);

   renderer = gtk_cell_renderer_text_new();
   col = gtk_tree_view_column_new_with_attributes("Details", renderer, "text", 3, NULL);
   gtk_tree_view_column_set_expand(GTK_TREE_VIEW_COLUMN(col), TRUE);
   gtk_tree_view_append_column(GTK_TREE_VIEW(g_tree), col);

   GtkWidget *sw_mod = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(sw_mod, 620, 300);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_mod), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(sw_mod), g_tree);
   gtk_box_pack_start(GTK_BOX(box_mod), sw_mod, TRUE, TRUE, 0);

   /* buttons */
   GtkWidget *hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   gtk_box_pack_start(GTK_BOX(box_mod), hbtn, FALSE, FALSE, 0);

   g_btn_stop = gtk_button_new_with_label("Stop");
   gtk_widget_set_name(g_btn_stop, "btn_stop");
   g_signal_connect(g_btn_stop, "clicked", G_CALLBACK(on_btn_stop), NULL);
   gtk_box_pack_start(GTK_BOX(hbtn), g_btn_stop, FALSE, FALSE, 0);

   g_btn_restart = gtk_button_new_with_label("Restart");
   gtk_widget_set_name(g_btn_restart, "btn_restart");
   g_signal_connect(g_btn_restart, "clicked", G_CALLBACK(on_btn_restart), NULL);
   gtk_box_pack_start(GTK_BOX(hbtn), g_btn_restart, FALSE, FALSE, 0);

   g_btn_start = gtk_button_new_with_label("Start");
   gtk_widget_set_name(g_btn_start, "btn_start");
   g_signal_connect(g_btn_start, "clicked", G_CALLBACK(on_btn_start), NULL);
   gtk_box_pack_start(GTK_BOX(hbtn), g_btn_start, FALSE, FALSE, 0);

   g_btn_reconfig = gtk_button_new_with_label("Reconfig");
   gtk_widget_set_name(g_btn_reconfig, "btn_reconfig");
   g_signal_connect(g_btn_reconfig, "clicked", G_CALLBACK(on_btn_reconfig), NULL);
   gtk_box_pack_start(GTK_BOX(hbtn), g_btn_reconfig, FALSE, FALSE, 0);

   g_btn_refresh = gtk_button_new_with_label("Refresh");
   gtk_widget_set_name(g_btn_refresh, "btn_refresh");
   g_signal_connect(g_btn_refresh, "clicked", G_CALLBACK(on_btn_refresh), NULL);
   gtk_box_pack_start(GTK_BOX(hbtn), g_btn_refresh, FALSE, FALSE, 0);

   gtk_widget_set_sensitive(g_btn_stop, FALSE);
   gtk_widget_set_sensitive(g_btn_restart, FALSE);
   gtk_widget_set_sensitive(g_btn_start, FALSE);
   gtk_widget_set_sensitive(g_btn_reconfig, FALSE);

   sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(g_tree));
   g_signal_connect(sel, "changed", G_CALLBACK(on_row_selected), NULL);
   g_signal_connect(g_tree, "button-press-event", G_CALLBACK(on_tree_button_press), NULL);

   /* ---- Right panel: rings + logs ---- */
   GtkWidget *box_der = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   gtk_paned_pack2(GTK_PANED(paned), box_der, TRUE, FALSE);

   GtkWidget *lbl_rings = gtk_label_new("RINGS");
   gtk_label_set_xalign(GTK_LABEL(lbl_rings), 0.0);
   gtk_box_pack_start(GTK_BOX(box_der), lbl_rings, FALSE, FALSE, 0);

   g_rings_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
   g_rings_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_rings_store));
   gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(g_rings_tree), TRUE);

   renderer = gtk_cell_renderer_text_new();
   col = gtk_tree_view_column_new_with_attributes("Ring", renderer, "text", 0, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(g_rings_tree), col);
   renderer = gtk_cell_renderer_text_new();
   col = gtk_tree_view_column_new_with_attributes("Key", renderer, "text", 1, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(g_rings_tree), col);
   renderer = gtk_cell_renderer_text_new();
   col = gtk_tree_view_column_new_with_attributes("Size KB", renderer, "text", 2, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(g_rings_tree), col);

   GtkWidget *sw_rings = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(sw_rings, 340, 120);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_rings), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(sw_rings), g_rings_tree);
   gtk_box_pack_start(GTK_BOX(box_der), sw_rings, FALSE, FALSE, 0);

   GtkWidget *lbl_log = gtk_label_new("LOGS (tail)");
   gtk_label_set_xalign(GTK_LABEL(lbl_log), 0.0);
   gtk_box_pack_start(GTK_BOX(box_der), lbl_log, FALSE, FALSE, 0);

   g_combo = gtk_combo_box_text_new();
   g_signal_connect(g_combo, "changed", G_CALLBACK(on_combo_changed), NULL);
   gtk_box_pack_start(GTK_BOX(box_der), g_combo, FALSE, FALSE, 0);

   g_logview = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(g_logview), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_logview), GTK_WRAP_NONE);
   g_logbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_logview));
   GtkWidget *sw_log = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_log), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
   gtk_container_add(GTK_CONTAINER(sw_log), g_logview);
   gtk_box_pack_start(GTK_BOX(box_der), sw_log, TRUE, TRUE, 0);

   /* ================= Tab 2: CONFIGURATION ================= */
   GtkWidget *page_cfg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_cfg, gtk_label_new("Configuration"));

   GtkWidget *cfg_h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   gtk_box_pack_start(GTK_BOX(page_cfg), cfg_h, FALSE, FALSE, 0);
   GtkWidget *lbl_cfg_mod = gtk_label_new("Module:");
   gtk_box_pack_start(GTK_BOX(cfg_h), lbl_cfg_mod, FALSE, FALSE, 0);

   g_cfg_combo = gtk_combo_box_text_new();
   g_signal_connect(g_cfg_combo, "changed", G_CALLBACK(on_cfg_combo_changed), NULL);
   gtk_box_pack_start(GTK_BOX(cfg_h), g_cfg_combo, FALSE, FALSE, 0);

   g_lbl_cfgpath = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(g_lbl_cfgpath), 0.0);
   gtk_label_set_ellipsize(GTK_LABEL(g_lbl_cfgpath), PANGO_ELLIPSIZE_MIDDLE);
   gtk_box_pack_start(GTK_BOX(cfg_h), g_lbl_cfgpath, TRUE, TRUE, 0);

   g_cfg_grid = gtk_grid_new();
   gtk_grid_set_column_spacing(GTK_GRID(g_cfg_grid), 8);
   gtk_grid_set_row_spacing(GTK_GRID(g_cfg_grid), 4);
   GtkWidget *sw_cfg = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_cfg), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(sw_cfg), g_cfg_grid);
   gtk_box_pack_start(GTK_BOX(page_cfg), sw_cfg, TRUE, TRUE, 0);

   GtkWidget *cfg_btn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(page_cfg), cfg_btn, FALSE, FALSE, 0);

   g_btn_cfg_save = gtk_button_new_with_label("Save");
   g_signal_connect(g_btn_cfg_save, "clicked", G_CALLBACK(on_btn_cfg_save), NULL);
   gtk_box_pack_start(GTK_BOX(cfg_btn), g_btn_cfg_save, FALSE, FALSE, 0);

   g_btn_cfg_reconfig = gtk_button_new_with_label("Reconfigure");
   g_signal_connect(g_btn_cfg_reconfig, "clicked", G_CALLBACK(on_btn_cfg_reconfig), NULL);
   gtk_box_pack_start(GTK_BOX(cfg_btn), g_btn_cfg_reconfig, FALSE, FALSE, 0);

   g_btn_cfg_reload = gtk_button_new_with_label("Reload");
   g_signal_connect(g_btn_cfg_reload, "clicked", G_CALLBACK(on_btn_cfg_reload), NULL);
   gtk_box_pack_start(GTK_BOX(cfg_btn), g_btn_cfg_reload, FALSE, FALSE, 0);

   g_lbl_cfg_status = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(g_lbl_cfg_status), 0.0);
   gtk_box_pack_start(GTK_BOX(cfg_btn), g_lbl_cfg_status, TRUE, TRUE, 0);

   gtk_widget_set_sensitive(g_btn_cfg_save, FALSE);
   gtk_widget_set_sensitive(g_btn_cfg_reconfig, FALSE);
   gtk_widget_set_sensitive(g_btn_cfg_reload, FALSE);

   g_lbl_statusbar = gtk_label_new("Waiting for startstop response...");
   gtk_label_set_xalign(GTK_LABEL(g_lbl_statusbar), 0.0);
   gtk_box_pack_start(GTK_BOX(vbox), g_lbl_statusbar, FALSE, FALSE, 0);

   gtk_widget_show_all(window);

   g_timeout_add_seconds((guint) PollInt, on_poll, NULL);
   g_timeout_add(500, on_read, NULL);
   g_timeout_add(2000, on_try_attach, NULL);

   gtk_main();
   return 0;
}