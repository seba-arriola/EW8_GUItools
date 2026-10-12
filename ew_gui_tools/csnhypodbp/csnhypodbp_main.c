#include "csnhypodbp.h"
#include "csnhypodbp_locator.h"
#include "ewgui/table_ui.h"

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
int g_pick_on[MAX_ESTA][2];
CsnhypodbpStaRow g_sta_rows[MAX_ESTA];
int g_sta_rows_n = 0;
CsnhypodbpPick g_base_picks[CSNHYPODBP_MAX_PICKS];
int g_base_npicks = 0;
CsnhypodbpSolution g_base_sol;
GtkWidget *g_sta_view = NULL;
GListStore *g_sta_store = NULL;
GtkSingleSelection *g_sta_sel = NULL;
/* Modelos ordenados: GtkColumnView solo ordena si el sorter de la vista
   (gtk_column_view_get_sorter) esta puesto en un GtkSortListModel. */
GtkSortListModel *g_sort_events = NULL;
GtkSortListModel *g_sort_detail = NULL;
GtkSortListModel *g_sort_sta = NULL;
int  g_rebuilding_stations = 0;   /* 1 mientras rebuild_station_list repuebla */

/* Vista de ondas: zoom por dos clics, modo de clic y eje de tiempo UTC. */
CsnhypodbpView      g_view;
CsnhypodbpClickMode g_click_mode = CSNHYPODBP_CLICK_ZOOM;
EwGuiCanvas        *g_axis_canvas = NULL;
double              g_win_start_cur = 0.0;
double              g_cursor_x = 0.0, g_cursor_y = 0.0;  /* puntero en el canvas */
int                 g_cursor_valid = 0;                  /* 1 = sobre el canvas */

/* Preview de locate (solucion candidata). */
GtkWidget *g_preview_box = NULL;
GtkWidget *g_preview_title = NULL;
GtkWidget *g_preview_val[CSNHYPODBP_PREVIEW_NVALS];
GtkWidget *g_preview_btn_confirm = NULL, *g_preview_btn_discard = NULL;
CsnhypodbpSolution g_preview_sol;
CsnhypodbpMagOut g_preview_mag;
gboolean g_preview_valid = FALSE;
int g_filter_auto = 0;
int g_filter_manual = 1;
int g_filter_theo = 0;
GtkWidget *g_chk_a = NULL, *g_chk_m = NULL, *g_chk_teo = NULL;
GtkWidget *g_locator_label = NULL;

/* --- Mapas: panel de planta (contrato de csnrv) y panel de perfil --- */
char  MapImageFile[MAX_STR] = "";            /* mapa base equirectangular (2:1) */
char  ProfileDataDir[MAX_STR] = "";          /* dir de slab<name>.tmp/topo<name>.tmp */
char  ProfileFile[MAX_STR] = "";             /* archivo de declaraciones Profile */
CsnhypodbpProfiles g_profiles;               /* perfiles de profundidad declarados */
int    g_profile_lon_col = 3;                /* ProfileCols: columnas 1-based */
int    g_profile_val_col = 7;
double g_profile_depth_min_km = 200.0;       /* ProfileDepthMinKm */
double g_map_min_span_deg = 5.0;             /* MapMinSpanDeg (auto-encuadre) */
static char   g_overlay_files[EWGUI_OVERLAY_MAX_FILES][MAX_STR];
static int    g_overlay_nfiles = 0;
static char   g_overlay_color_layer[EWGUI_OVERLAY_MAX_FILES][EWGUI_OVERLAY_NAME_MAX];
static char   g_overlay_color_hex[EWGUI_OVERLAY_MAX_FILES][8];
static int    g_overlay_ncolors = 0;
static double g_overlay_width = 0.0;         /* 0 = sin override */
static char   g_overlay_hide_layer[EWGUI_OVERLAY_MAX_FILES][EWGUI_OVERLAY_NAME_MAX];
static int    g_overlay_nhides = 0;

/* Mapas: columna derecha (planta arriba, perfil abajo). */
GtkWidget   *g_maps_col = NULL;
EwGuiCanvas *g_map_plan = NULL;
EwGuiCanvas *g_map_prof = NULL;
GtkWidget   *g_map_plan_cap = NULL;
GtkWidget   *g_map_prof_cap = NULL;
GdkPixbuf   *g_map_pixbuf = NULL;
EwOverlay   *g_map_overlay = NULL;
CsnhypodbpMapPt g_map_pts[CSNHYPODBP_MAX_MAP_PTS];
int          g_map_npts = 0;
int          g_active_profile = -1;
CsnhypodbpProfileLine g_prof_slab, g_prof_topo;

int      g_filter_type = 0;      /* 0=Raw, 1=HP, 2=LP, 3=BP */
double   g_filter_f1 = 0.7;      /* F1 (Hz) */
double   g_filter_f2 = 2.0;      /* F2 (Hz) */
int      g_filter_order = 4;     /* orden IIR (2 o 4) */
double   g_align_lead = 20.0;    /* segundos antes de la onda P */

gboolean pending_waveform_reload = FALSE;
double   selected_lat = 0.0;
double   selected_lon = 0.0;

/* Busqueda hipocentral (pestaña Eventos): la maestra muestra el resultado. */
int g_search_active = 0;
int g_search_max_events = CSNHYPODBP_SEARCH_DEFAULT_LIMIT;
static GtkWidget *g_btn_clear_search = NULL;

/* Backend SDS (archivo SeisComP) para eventos mas antiguos que la retencion. */
int  g_sds_enabled = 1;
char g_sds_root[512] = "/mnt/sds/archive";
int  g_sds_fallback_hours = 168;

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
        else if (k_its("MaxDistKm")) { double v = k_val(); if (v > 0.0) g_max_dist_km = v; }
        else if (k_its("LocateKeepTmp")) { csnhypodbp_locate_keep_tmp(k_int()); }
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
        else if (k_its("SearchMaxEvents")) {
            g_search_max_events = k_int();
            if (g_search_max_events <= 0) g_search_max_events = CSNHYPODBP_SEARCH_DEFAULT_LIMIT;
        }
        else if (k_its("SdsEnabled")) { g_sds_enabled = k_int() ? 1 : 0; }
        else if (k_its("SdsRoot")) { str = k_str(); if (str) snprintf(g_sds_root, sizeof(g_sds_root), "%s", str); }
        else if (k_its("SdsFallbackHours")) { g_sds_fallback_hours = k_int(); if (g_sds_fallback_hours < 0) g_sds_fallback_hours = 0; }
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
        /* --- Mapas: panel de planta (mismo contrato que csnrv) --- */
        else if (k_its("MapImageFile")) { str = k_str(); if (str) snprintf(MapImageFile, sizeof(MapImageFile), "%s", str); }
        else if (k_its("MapMinSpanDeg")) {
            double v = k_val();
            if (v > 0.0) g_map_min_span_deg = v;
        }
        else if (k_its("OverlayFile")) {
            str = k_str();
            if (str && g_overlay_nfiles < EWGUI_OVERLAY_MAX_FILES)
                snprintf(g_overlay_files[g_overlay_nfiles++], MAX_STR, "%s", str);
        }
        else if (k_its("OverlayColor")) {
            char *lyr = k_str(), *hex = k_str();
            if (lyr && hex && g_overlay_ncolors < EWGUI_OVERLAY_MAX_FILES) {
                snprintf(g_overlay_color_layer[g_overlay_ncolors],
                         EWGUI_OVERLAY_NAME_MAX, "%s", lyr);
                snprintf(g_overlay_color_hex[g_overlay_ncolors], 8, "%s", hex);
                g_overlay_ncolors++;
            }
        }
        else if (k_its("OverlayWidth")) { g_overlay_width = k_val(); }
        else if (k_its("OverlayHide")) {
            /* OPCIONAL y repetible: <capa> que no se dibuja (el archivo se sigue
               cargando; el resto se controla con OverlayColor/OverlayWidth). */
            str = k_str();
            if (str && g_overlay_nhides < EWGUI_OVERLAY_MAX_FILES)
                snprintf(g_overlay_hide_layer[g_overlay_nhides++],
                         EWGUI_OVERLAY_NAME_MAX, "%s", str);
        }
        /* --- Perfiles de profundidad (panel inferior) --- */
        else if (k_its("ProfileDataDir")) { str = k_str(); if (str) snprintf(ProfileDataDir, sizeof(ProfileDataDir), "%s", str); }
        else if (k_its("ProfileFile")) { str = k_str(); if (str) snprintf(ProfileFile, sizeof(ProfileFile), "%s", str); }
        else if (k_its("ProfileCols")) {
            int lc = k_int(), vc = k_int();
            if (lc > 0 && vc > 0) { g_profile_lon_col = lc; g_profile_val_col = vc; }
        }
        else if (k_its("ProfileDepthMinKm")) {
            double v = k_val();
            if (v > 0.0) g_profile_depth_min_km = v;
        }
        else if (k_its("Profile")) {
            /* Profile <name> <lon1> <lat1> <lon2> <lat2> [slab] [topo].
               kom no tiene k_eol(): el resto de la linea se junta con
               k_rest_join (que limpia el ierr de fin de linea) y lo parsea el
               nucleo puro, el mismo que usa el archivo de declaraciones. */
            char *nm = k_str();
            char rest[512] = "", err[256] = "";
            k_rest_join(rest, sizeof(rest));
            if (!csnhypodbp_profile_add_tokens(nm, rest, &g_profiles, err, sizeof(err))) {
                fprintf(stderr, "Error en la linea Profile: %s\n",
                        err[0] ? err : "tokens invalidos");
                return -1;
            }
        }
        else { continue; }
        if (k_err()) { fprintf(stderr, "Error parseando <%s>\n", com); return -1; }
    }
    /* El archivo de declaraciones (si se usa) reemplaza a las claves Profile. */
    if (ProfileFile[0]) {
        char err[256] = "";
        if (csnhypodbp_profiles_parse(ProfileFile, &g_profiles, err, sizeof(err)) < 0) {
            fprintf(stderr, "Error leyendo ProfileFile <%s>: %s\n", ProfileFile, err);
            k_close();
            return -1;
        }
    }
    csnhypodbp_profiles_default_files(&g_profiles, ProfileDataDir);
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close(); return (nmiss > 0) ? -1 : 0;
}

/* --- Modos headless (patron de csnrv): no arrancan GTK ni se conectan a EW --- */

static int maps_print_config(void) {
    int i;
    printf("MapImageFile %s\n", MapImageFile[0] ? MapImageFile : "(none)");
    printf("MapMinSpanDeg %.1f\n", g_map_min_span_deg);
    printf("OverlayFiles %d\n", g_overlay_nfiles);
    for (i = 0; i < g_overlay_nfiles; i++)
        printf("OverlayFile %s\n", g_overlay_files[i]);
    printf("OverlayColors %d\n", g_overlay_ncolors);
    for (i = 0; i < g_overlay_ncolors; i++)
        printf("OverlayColor %s %s\n", g_overlay_color_layer[i], g_overlay_color_hex[i]);
    printf("OverlayWidth %.3f\n", g_overlay_width);
    printf("OverlayHides %d\n", g_overlay_nhides);
    for (i = 0; i < g_overlay_nhides; i++)
        printf("OverlayHide %s\n", g_overlay_hide_layer[i]);
    printf("ProfileDataDir %s\n", ProfileDataDir[0] ? ProfileDataDir : "(none)");
    printf("ProfileFile %s\n", ProfileFile[0] ? ProfileFile : "(none)");
    printf("ProfileCols %d %d\n", g_profile_lon_col, g_profile_val_col);
    printf("ProfileDepthMinKm %.1f\n", g_profile_depth_min_km);
    printf("Profiles %d\n", g_profiles.n);
    for (i = 0; i < g_profiles.n; i++) {
        const CsnhypodbpProfile *p = &g_profiles.p[i];
        printf("Profile %s %.6f %.6f %.6f %.6f %s %s\n", p->name,
               p->lon1, p->lat1, p->lon2, p->lat2, p->slab_file, p->topo_file);
    }
    return 0;
}

/* Valida los datos de cada perfil: los dos archivos legibles y la traza del slab
 * (columnas 3/4 = lon/lat) pegada a la recta declarada. */
static int maps_check_profiles(void) {
    int i, bad = 0;
    printf("profiles=%d\n", g_profiles.n);
    for (i = 0; i < g_profiles.n; i++) {
        const CsnhypodbpProfile *p = &g_profiles.p[i];
        CsnhypodbpProfileLine slab, topo, traza;
        char err[256];
        double dev = 0.0, devmax = 0.0;
        int k, nfin = 0;

        if (csnhypodbp_profile_line_read(p->slab_file, g_profile_lon_col,
                                         g_profile_val_col, &slab, err, sizeof(err)) != 0) {
            printf("profile %s: slab FALLA (%s)\n", p->name, err);
            bad++;
            continue;
        }
        if (csnhypodbp_profile_line_read(p->topo_file, g_profile_lon_col,
                                         g_profile_val_col, &topo, err, sizeof(err)) != 0) {
            printf("profile %s: topo FALLA (%s)\n", p->name, err);
            bad++;
            csnhypodbp_profile_line_free(&slab);
            continue;
        }
        if (csnhypodbp_profile_line_read(p->slab_file, 3, 4, &traza,
                                         err, sizeof(err)) == 0) {
            for (k = 0; k < traza.n; k++) {
                double d = csnhypodbp_profile_dist(p, traza.val[k], traza.lon[k]);
                if (d < 0.0) continue;
                dev += d;
                if (d > devmax) devmax = d;
                nfin++;
            }
            csnhypodbp_profile_line_free(&traza);
        }
        dev = (nfin > 0) ? dev / (double)nfin : -1.0;
        printf("profile %s slab=%d topo=%d traza=%d dev_media=%.4f dev_max=%.4f\n",
               p->name, slab.n, topo.n, nfin, dev, devmax);
        if (dev < 0.0 || dev > 0.05) {
            printf("profile %s: la traza no sigue la recta declarada\n", p->name);
            bad++;
        }
        csnhypodbp_profile_line_free(&slab);
        csnhypodbp_profile_line_free(&topo);
    }
    printf("perfiles=%d fallas=%d\n", g_profiles.n, bad);
    return bad ? 2 : 0;
}

/* Carga el mapa base y las capas .lines (una vez, tras ReadConfig). */
void csnhypodbp_maps_load(void) {
    int i;
    if (MapImageFile[0]) {
        GError *err = NULL;
        g_map_pixbuf = gdk_pixbuf_new_from_file(MapImageFile, &err);
        if (!g_map_pixbuf) {
            logit("et", "csnhypodbp: no se pudo cargar MapImageFile <%s>: %s\n",
                  MapImageFile, err ? err->message : "?");
            if (err) g_error_free(err);
        } else {
            logit("t", "csnhypodbp: mapa base %s (%dx%d)\n", MapImageFile,
                  gdk_pixbuf_get_width(g_map_pixbuf), gdk_pixbuf_get_height(g_map_pixbuf));
        }
    } else {
        logit("et", "csnhypodbp: sin MapImageFile; el mapa de planta va sin fondo\n");
    }
    if (g_overlay_nfiles > 0) {
        g_map_overlay = ewgui_overlay_new();
        for (i = 0; g_map_overlay && i < g_overlay_nfiles; i++) {
            if (ewgui_overlay_append(g_map_overlay, g_overlay_files[i]) != 0)
                logit("et", "csnhypodbp: no se pudo cargar overlay %s\n", g_overlay_files[i]);
        }
        if (g_map_overlay && g_overlay_width > 0.0)
            ewgui_overlay_set_global_width(g_map_overlay, g_overlay_width);
        for (i = 0; g_map_overlay && i < g_overlay_ncolors; i++) {
            double rgb[3];
            if (ewgui_parse_hex_color(g_overlay_color_hex[i], rgb) == 0)
                ewgui_overlay_set_color(g_map_overlay, g_overlay_color_layer[i], rgb);
            else
                logit("et", "csnhypodbp: OverlayColor %s: hex invalido '%s'\n",
                      g_overlay_color_layer[i], g_overlay_color_hex[i]);
        }
        for (i = 0; g_map_overlay && i < g_overlay_nhides; i++) {
            if (ewgui_overlay_set_visible(g_map_overlay, g_overlay_hide_layer[i], 0) == 0)
                logit("et", "csnhypodbp: OverlayHide %s: no existe esa capa\n",
                      g_overlay_hide_layer[i]);
        }
        logit("t", "csnhypodbp: overlays cargados=%d\n", g_overlay_nfiles);
    }
    logit("t", "csnhypodbp: perfiles declarados=%d datos=%s\n",
          g_profiles.n, ProfileDataDir[0] ? ProfileDataDir : "(cwd)");
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

/* Columna i-esima de la vista (0 = la primera). Devuelve una referencia nueva:
   el llamante la libera. */
static GtkColumnViewColumn *column_at(GtkWidget *view, guint i) {
    GListModel *cols = gtk_column_view_get_columns(GTK_COLUMN_VIEW(view));
    return cols ? g_list_model_get_item(cols, i) : NULL;
}

/* Como add_text_column, pero ademas ordenable: ordena por `sort_prop` (si es ""
 * usa el propio texto de la celda) y desempata por `tie` (puede ser NULL). */
static void add_text_column_sort(GtkWidget *view, const char *title, const char *prop,
                                 const char *sort_prop, const char *tie, int first_dir) {
    GtkColumnViewColumn *col;
    add_text_column(view, title, prop);
    col = column_at(view, g_list_model_get_n_items(
                                gtk_column_view_get_columns(GTK_COLUMN_VIEW(view))) - 1);
    if (col) {
        ewgui_table_column_sortable(col, sort_prop, tie, first_dir);
        g_object_unref(col);
    }
}

/* -------------------------------------------------------------------------
 * Tabla de estaciones (pestaña Ondas): GtkColumnView con separadores de fila y
 * columna, igual que la tabla de Eventos. Una fila por (estacion, fase) = 2 por
 * estacion: las dos llevan nombre, distancia y casilla. La casilla va junto al
 * NOMBRE y es la del pick de esa fila (cada fila es una fase suelta, asi que
 * desmarcarla desactiva esa fase, no la estacion); la columna `pick` es solo el
 * texto P|S.
 *
 * El estado de las casillas NO esta en el modelo: vive en g_pick_on[] (la fuente
 * que usa el localizador) y cada bind lo relee, porque las celdas se reciclan al
 * hacer scroll y guardarlo en el modelo revertiria el toggle.
 * ------------------------------------------------------------------------- */

#define STA_CELL_CHK "sta-cell-chk"
#define STA_CELL_LBL "sta-cell-lbl"
#define STA_CELL_STA "sta-cell-name"

static CsnhypodbpStaItem *sta_item_of(GtkListItem *item)
{
    gpointer it = gtk_list_item_get_item(item);
    return (it && CSNHYPODBP_IS_STA_ITEM(it)) ? CSNHYPODBP_STA_ITEM(it) : NULL;
}

/* Clic derecho sobre una celda: quita las picadas manuales de esa estacion. */
static void on_sta_cell_right_click(GtkGestureClick *g, int n_press, double x, double y,
                                    gpointer data)
{
    GtkWidget *w;
    const char *sta;
    int r;
    (void)n_press; (void)x; (void)y; (void)data;
    w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    sta = w ? g_object_get_data(G_OBJECT(w), STA_CELL_STA) : NULL;
    if (!sta || !sta[0]) return;
    r = csnhypodbp_manual_remove_station(sta);
    logit("t", "csnhypodbp: clic der: -%d picks de la estacion %s\n", r, sta);
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

static void sta_cell_ctx(GtkWidget *w)
{
    GtkGesture *g = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 3);
    g_signal_connect(g, "pressed", G_CALLBACK(on_sta_cell_right_click), NULL);
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(g));
}

/* Columna `station`: [casilla del pick] nombre, en las dos filas. */
static void sta_col_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    GtkWidget *box, *chk, *lbl;
    (void)f; (void)data;
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    chk = gtk_check_button_new();
    lbl = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(lbl, TRUE);
    g_signal_connect(chk, "toggled", G_CALLBACK(on_pick_check_toggled), NULL);
    gtk_box_append(GTK_BOX(box), chk);
    gtk_box_append(GTK_BOX(box), lbl);
    sta_cell_ctx(box);
    g_object_set_data(G_OBJECT(box), STA_CELL_CHK, chk);
    g_object_set_data(G_OBJECT(box), STA_CELL_LBL, lbl);
    gtk_list_item_set_child(item, box);
}

static void sta_col_bind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    CsnhypodbpStaItem *it;
    GtkWidget *box, *chk, *lbl;
    char *name = NULL;
    gboolean has = FALSE;
    GBinding *b;
    int idx, ph;
    (void)f; (void)data;
    it = sta_item_of(item);
    if (!it) return;
    box = gtk_list_item_get_child(item);
    chk = g_object_get_data(G_OBJECT(box), STA_CELL_CHK);
    lbl = g_object_get_data(G_OBJECT(box), STA_CELL_LBL);
    idx = csnhypodbp_sta_item_idx(it);
    ph  = csnhypodbp_sta_item_phase(it);
    g_object_get(it, "sta", &name, "has-pick", &has, NULL);

    /* Las DOS filas (P y S) muestran estacion, distancia y su casilla: cada fila
       se lee sola. La casilla es la del PICK de esa fase (no la de la estacion):
       el estado vive en g_pick_on[] y se relee en cada bind. */
    gtk_widget_set_visible(chk, TRUE);
    gtk_widget_set_visible(lbl, TRUE);
    b = g_object_bind_property(it, "sta", lbl, "label", G_BINDING_SYNC_CREATE);
    g_object_set_data_full(G_OBJECT(item), "cell-binding", b, g_object_unref);

    g_object_set_data(G_OBJECT(chk), "sta_idx", GINT_TO_POINTER(idx));
    g_object_set_data(G_OBJECT(chk), "phase",   GINT_TO_POINTER(ph));
    g_signal_handlers_block_by_func(chk, G_CALLBACK(on_pick_check_toggled), NULL);
    /* Fase ausente: casilla VACIA y no sensible (marcada pero inerte confundia). */
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk),
        (has && idx >= 0 && idx < MAX_ESTA && g_pick_on[idx][ph]) ? TRUE : FALSE);
    g_signal_handlers_unblock_by_func(chk, G_CALLBACK(on_pick_check_toggled), NULL);
    gtk_widget_set_sensitive(chk, has ? TRUE : FALSE);

    g_object_set_data_full(G_OBJECT(box), STA_CELL_STA, g_strdup(name ? name : ""), g_free);
    g_free(name);
}

/* Columnas de texto (dist/pick/stat/res): bind por nombre de propiedad. */
static void sta_txt_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    GtkWidget *lbl;
    (void)f; (void)data;
    lbl = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    sta_cell_ctx(lbl);
    gtk_list_item_set_child(item, lbl);
}

static void sta_txt_bind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    const char *prop = (const char *)data;
    GtkWidget *lbl;
    CsnhypodbpStaItem *it;
    char *name = NULL;
    GBinding *b;
    (void)f;
    it = sta_item_of(item);
    lbl = gtk_list_item_get_child(item);
    if (!it || !lbl || !prop) return;
    b = g_object_bind_property(it, prop, lbl, "label", G_BINDING_SYNC_CREATE);
    g_object_set_data_full(G_OBJECT(item), "cell-binding", b, g_object_unref);
    g_object_get(it, "sta", &name, NULL);
    g_object_set_data_full(G_OBJECT(lbl), STA_CELL_STA, g_strdup(name ? name : ""), g_free);
    g_free(name);
}

static void sta_txt_unbind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    (void)f; (void)data;
    g_object_set_data(G_OBJECT(item), "cell-binding", NULL);
    g_object_set_data(G_OBJECT(item), "cell-binding-chk", NULL);
}

static void add_sta_station_column(GtkWidget *view, const char *title)
{
    GtkListItemFactory *f = gtk_signal_list_item_factory_new();
    GtkColumnViewColumn *col;
    g_signal_connect(f, "setup", G_CALLBACK(sta_col_setup), NULL);
    g_signal_connect(f, "bind",  G_CALLBACK(sta_col_bind), NULL);
    g_signal_connect(f, "unbind", G_CALLBACK(sta_txt_unbind), NULL);
    col = gtk_column_view_column_new(title, f);
    gtk_column_view_column_set_expand(col, TRUE);
    gtk_column_view_column_set_resizable(col, TRUE);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), col);
}

static void add_sta_text_column(GtkWidget *view, const char *title, const char *prop,
                                int width)
{
    GtkListItemFactory *f = gtk_signal_list_item_factory_new();
    GtkColumnViewColumn *col;
    g_signal_connect(f, "setup",  G_CALLBACK(sta_txt_setup), NULL);
    g_signal_connect(f, "bind",   G_CALLBACK(sta_txt_bind), (gpointer)prop);
    g_signal_connect(f, "unbind", G_CALLBACK(sta_txt_unbind), NULL);
    col = gtk_column_view_column_new(title, f);
    gtk_column_view_column_set_fixed_width(col, width);
    gtk_column_view_column_set_resizable(col, TRUE);
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
static const EwMenuItem help_items[] = {
    { "Commands...", "win.help", NULL, 0 },
};
static const EwMenuGroup wave_groups[] = {
    { "Waveforms", wave_items, 4 },
    { "Colours", colour_items, 2 },
    { "View", view_items, 2 },
    { "Help", help_items, 1 },
};
static const EwMenuItem search_items[] = {
    { "Search events...", "win.search", NULL, 0 },
    { "Clear search", "win.clear-search", NULL, 0 },
};
static const EwMenuGroup base_groups[] = {
    { "Search", search_items, 2 },
    { "View", view_items, 2 },
};

static GtkWidget *g_menu_btn = NULL;

/* Adaptadores barra inferior -> menú ☰ (widget=NULL; los handlers solo usan
   `data`, mismo patrón que csntvp). */
static void act_filters(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; (void)ud; on_btn_filters_clicked(NULL, NULL); }
static void act_fetch(GSimpleAction *a, GVariant *p, gpointer ud)   { (void)a; (void)p; (void)ud; on_btn_fetch_clicked(NULL, NULL); }
static void act_timewin(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; (void)ud; on_btn_timewin_clicked(NULL, NULL); }
static void act_search(GSimpleAction *a, GVariant *p, gpointer ud)  { (void)a; (void)p; (void)ud; on_btn_search_clicked(NULL, NULL); }
static void act_clear_search(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; (void)ud; search_clear(); }
static void act_help(GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; (void)ud; csnhypodbp_help_dialog_run(window_global ? GTK_WINDOW(window_global) : NULL); }

/* El boton "Clear search" (junto al titulo "Events") solo tiene sentido cuando
   la maestra muestra un resultado de busqueda. */
void csnhypodbp_update_search_ui(void)
{
    if (g_btn_clear_search)
        gtk_widget_set_sensitive(g_btn_clear_search, g_search_active ? TRUE : FALSE);
}

/* En la pestaña Ondas (página 1) el ☰ muestra el grupo Waveforms. */
static void on_notebook_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer ud) {
    (void)nb; (void)page; (void)ud;
    if (!g_menu_btn) return;
    if (num == 1) ewgui_menu_button_set_groups(g_menu_btn, wave_groups, 4);
    else          ewgui_menu_button_set_groups(g_menu_btn, base_groups, 2);
}

/* Un cuadro de mapa: caption (texto visible por AT-SPI) + canvas. */
static GtkWidget *map_panel_new(EwGuiCanvas **cv, GtkWidget **cap, EwDrawFn draw,
                                GtkWidget **widget, int minw, int minh)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "detail-frame");
    *cap = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(*cap), 0.0);
    gtk_widget_add_css_class(*cap, "map-cap");
    /* El titulo del cuadro no se muestra (el usuario lo pidio fuera): la etiqueta
       queda oculta y solo sirve de asercion para el arnes GUI. */
    gtk_widget_set_visible(*cap, FALSE);
    *cv = ewgui_canvas_new();
    ewgui_canvas_set_draw(*cv, draw, NULL);
    *widget = ewgui_canvas_widget(*cv);
    gtk_widget_set_size_request(*widget, minw, minh);
    gtk_widget_set_vexpand(*widget, TRUE);
    gtk_box_append(GTK_BOX(box), *cap);
    gtk_box_append(GTK_BOX(box), *widget);
    gtk_frame_set_child(GTK_FRAME(frame), box);
    return frame;
}

/* Deja ~340 px para la columna de mapas cuando el paned ya tiene ancho. */
static gboolean paned_maps_set_idle(gpointer data) {
    GtkWidget *p = data;
    int w = gtk_widget_get_width(p);
    if (w > 0) gtk_paned_set_position(GTK_PANED(p), w - 340);
    return G_SOURCE_REMOVE;
}
static void paned_maps_map(GtkWidget *paned, gpointer data) {
    (void)data;
    g_idle_add(paned_maps_set_idle, paned);
}

/* Reparte la columna de mapas 65% planta / 35% perfil en el primer mapeo. */
static gboolean paned_vmaps_set_idle(gpointer data) {
    GtkWidget *p = data;
    int h = gtk_widget_get_height(p);
    if (h > 0) gtk_paned_set_position(GTK_PANED(p), (int)(h * 0.65));
    return G_SOURCE_REMOVE;
}
static void paned_vmaps_map(GtkWidget *paned, gpointer data) {
    (void)data;
    g_idle_add(paned_vmaps_set_idle, paned);
}

/* Los dos canvas de mapa se manejan igual: rueda = zoom, arrastre boton 1 =
   paneo (el clic sin arrastre selecciona el origen, ver los handlers). */
static void map_panel_connect(GtkWidget *w, GCallback scroll_cb, GCallback press_cb,
                              GCallback release_cb, GCallback motion_cb)
{
    GtkEventController *sc = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    GtkGesture *ck = gtk_gesture_click_new();
    GtkEventController *mo = gtk_event_controller_motion_new();

    g_signal_connect(sc, "scroll", scroll_cb, NULL);
    gtk_widget_add_controller(w, sc);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(ck), 1);
    g_signal_connect(ck, "pressed", press_cb, NULL);
    g_signal_connect(ck, "released", release_cb, NULL);
    gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(ck));
    g_signal_connect(mo, "motion", motion_cb, NULL);
    gtk_widget_add_controller(w, mo);
}

/* Columna derecha: planta arriba, perfil abajo (compartida por las pestañas). */
static void build_maps_column(void)
{
    GtkWidget *plan_frame, *prof_frame, *plan_w, *prof_w;
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);

    plan_frame = map_panel_new(&g_map_plan, &g_map_plan_cap, on_draw_map_plan,
                               &plan_w, 300, 240);
    prof_frame = map_panel_new(&g_map_prof, &g_map_prof_cap, on_draw_map_profile,
                               &prof_w, 300, 140);

    /* Los dos cuadros se manejan igual: rueda = zoom, arrastre boton 1 = paneo. */
    map_panel_connect(plan_w, G_CALLBACK(on_map_plan_scroll), G_CALLBACK(on_map_plan_press),
                      G_CALLBACK(on_map_plan_release), G_CALLBACK(on_map_plan_motion));
    map_panel_connect(prof_w, G_CALLBACK(on_map_prof_scroll), G_CALLBACK(on_map_prof_press),
                      G_CALLBACK(on_map_prof_release), G_CALLBACK(on_map_prof_motion));

    gtk_paned_set_start_child(GTK_PANED(paned), plan_frame);
    gtk_paned_set_end_child(GTK_PANED(paned), prof_frame);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_position(GTK_PANED(paned), 320);
    gtk_widget_set_vexpand(paned, TRUE);
    g_signal_connect(paned, "map", G_CALLBACK(paned_vmaps_map), NULL);

    g_maps_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(g_maps_col), paned);
    csnhypodbp_maps_refresh();   /* estado inicial (sin seleccion) */
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
            ".detail-solutions row:selected { background-image: none; background-color: #ffd54f; color: #000000; }\n"
            ".sta-table { font-size: 0.75em; }\n"
            ".sta-table row { background-image: none; min-height: 0; }\n"
            ".sta-table cell { padding: 1px 2px; }\n"
            /* Cabeceras: el relleno del tema (boton) + el hueco del indicador de
               orden son ~20 px por columna; sin recortarlos, "dist[km]" no cabe
               en una columna estrecha. Si el selector no existe, GTK lo ignora. */
            ".sta-table header { min-height: 0; }\n"
            ".sta-table header button, .sta-table > header > button {\n"
            "    padding: 0 4px; min-height: 0; }\n"
            ".detail-frame { border: 1px solid alpha(@theme_fg_color, 0.45); border-radius: 4px; }\n"
            /* Preview de la solucion: ambar suave para la elegida y ROJO MUY
               LEVE para la candidata del locate (la que hay que mirar). */
            ".preview-box { background-image: none; background-color: #fff8e1; border: 1px solid alpha(@theme_fg_color, 0.35); border-radius: 4px; padding: 2px 8px; }\n"
            ".preview-box.candidate { background-color: #fde7e7; border-color: #e8a0a0; }\n"
            ".preview-title { font-weight: bold; font-size: 0.9em; }\n"
            ".preview-hdr { font-size: 0.75em; color: alpha(@theme_fg_color, 0.7); }\n"
            ".preview-val { font-weight: bold; }\n"
            /* Caption de los paneles de mapa (columna derecha). */
            ".map-cap { font-size: 0.8em; font-weight: bold; padding: 1px 4px; }\n"
            /* Campos de fecha y hora del buscador: fuente un punto mas chica y
               ancho minimo para que "YYYY-MM-DD HH:MM:SS" entre completo. */
            ".time-entry { font-size: 0.95em; min-width: 11em; }");
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
    ewgui_action_add(G_ACTION_MAP(actions), "search", NULL, act_search, window_global);
    ewgui_action_add(G_ACTION_MAP(actions), "clear-search", NULL, act_clear_search, window_global);
    ewgui_action_add(G_ACTION_MAP(actions), "help", NULL, act_help, window_global);
    gtk_widget_insert_action_group(window_global, "win", G_ACTION_GROUP(actions));
    g_object_unref(actions);

    g_menu_btn = ewgui_menu_button_new(base_groups, 2);

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

    /* Columna derecha (planta + perfil), compartida por las dos pestañas. */
    build_maps_column();
    {
        GtkWidget *paned_maps = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_set_start_child(GTK_PANED(paned_maps), g_notebook);
        gtk_paned_set_end_child(GTK_PANED(paned_maps), g_maps_col);
        gtk_paned_set_resize_start_child(GTK_PANED(paned_maps), TRUE);
        gtk_paned_set_resize_end_child(GTK_PANED(paned_maps), TRUE);
        gtk_paned_set_shrink_start_child(GTK_PANED(paned_maps), FALSE);
        gtk_widget_set_vexpand(paned_maps, TRUE);
        g_signal_connect(paned_maps, "map", G_CALLBACK(paned_maps_map), NULL);
        gtk_box_append(GTK_BOX(vbox), paned_maps);
    }

    /* --- Pestaña "Eventos": maestro (eventos) + detalle (soluciones) --- */
    GtkWidget *page_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    g_store_hypo   = g_list_store_new(CSNHYPODBP_TYPE_ROW);
    g_store_events = g_list_store_new(CSNHYPODBP_TYPE_ROW);
    g_store_detail = g_list_store_new(CSNHYPODBP_TYPE_ROW);

    /* ---- Maestro: una fila por evento. El orden lo elige el usuario pulsando
       las cabeceras; por defecto por hora de origen DESC (la columna Date
       ordena por la propiedad numerica `otime`). ---- */
    g_sort_events = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(g_store_events)), NULL);
    g_selection_events = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(g_sort_events)));
    gtk_single_selection_set_autoselect(g_selection_events, FALSE);
    gtk_single_selection_set_can_unselect(g_selection_events, TRUE);

    tree_global = gtk_column_view_new(GTK_SELECTION_MODEL(g_selection_events));
    gtk_widget_add_css_class(tree_global, "master-events");
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(tree_global), TRUE);
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(tree_global), TRUE);
    g_signal_connect(g_selection_events, "notify::selected", G_CALLBACK(on_event_selected), NULL);
    {
        const char *mheaders[14] = {"Date", "Lat.", "Lon.", "Dep", "Res", "Azm", "#Stn", "ID", "Ml", "Mwp", "Mb", "Ms", "Mod", "Ver"};
        const char *mprops[14]   = {"c0", "c2", "c3", "c4", "c5", "c6", "c7", "c8",
                                    "c9", "c10", "c11", "c12", "c13", "c14"};
        /* Propiedad de orden de cada columna ("" = su propio texto) y direccion
           del primer clic: numeros y fechas descendente, texto alfabetico. */
        static const char *msort[14] = {"otime", "lat", "lon", "depth",
                                        "", "", "", "", "nmag0", "nmag1", "nmag2", "nmag3", "", ""};
        static const int   mdir[14]  = {EWGUI_TABLE_DESC, EWGUI_TABLE_DESC, EWGUI_TABLE_DESC,
                                        EWGUI_TABLE_DESC, EWGUI_TABLE_DESC, EWGUI_TABLE_DESC,
                                        EWGUI_TABLE_DESC, EWGUI_TABLE_DESC, EWGUI_TABLE_DESC,
                                        EWGUI_TABLE_DESC, EWGUI_TABLE_DESC, EWGUI_TABLE_DESC,
                                        EWGUI_TABLE_ASC, EWGUI_TABLE_DESC};
        GtkColumnViewColumn *cdate;
        for (int i = 0; i < 14; i++)
            add_text_column_sort(tree_global, mheaders[i], mprops[i],
                                 msort[i][0] ? msort[i] : mprops[i], NULL, mdir[i]);
        add_text_column_sort(tree_global, "N", "nsol", "nsol", NULL, EWGUI_TABLE_DESC);
        add_text_column_sort(tree_global, "Est.", "estado", "estado", NULL, EWGUI_TABLE_ASC);
        cdate = column_at(tree_global, 0);
        ewgui_table_enable_sorting(GTK_COLUMN_VIEW(tree_global), g_sort_events, cdate,
                                   EWGUI_TABLE_DESC);
        g_object_unref(cdate);
    }

    /* ---- Detalle: soluciones del evento seleccionado ---- */
    g_sort_detail = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(g_store_detail)), NULL);
    g_selection_detail = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(g_sort_detail)));
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
        const char *dheaders[] = {"Mod", "Ver", "Est.", "Created", "Date", "Lat.", "Lon.", "Dep", "Res", "#Stn", "ID", "Ml", "Mwp", "Mb", "Ms"};
        const char *dprops[]   = {"c13", "c14", "estado", "created", "c0", "c2", "c3", "c4", "c5", "c7", "c8", "c9", "c10", "c11", "c12"};
        static const char *dsort[] = {"", "", "", "", "otime", "lat", "lon", "depth",
                                      "", "", "", "nmag0", "nmag1", "nmag2", "nmag3"};
        static const int   ddir[]  = {EWGUI_TABLE_ASC, EWGUI_TABLE_DESC, EWGUI_TABLE_ASC,
                                      EWGUI_TABLE_DESC, EWGUI_TABLE_DESC, EWGUI_TABLE_DESC,
                                      EWGUI_TABLE_DESC, EWGUI_TABLE_DESC, EWGUI_TABLE_DESC,
                                      EWGUI_TABLE_DESC, EWGUI_TABLE_DESC, EWGUI_TABLE_DESC,
                                      EWGUI_TABLE_DESC, EWGUI_TABLE_DESC, EWGUI_TABLE_DESC};
        GtkColumnViewColumn *ccreated;
        for (int i = 0; i < 15; i++)
            add_text_column_sort(tree_detail, dheaders[i], dprops[i],
                                 dsort[i][0] ? dsort[i] : dprops[i], NULL, ddir[i]);
        /* Orden por defecto del detalle: la solucion mas reciente arriba. */
        ccreated = column_at(tree_detail, 3);   /* Created */
        ewgui_table_enable_sorting(GTK_COLUMN_VIEW(tree_detail), g_sort_detail, ccreated,
                                   EWGUI_TABLE_DESC);
        g_object_unref(ccreated);
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
    GtkWidget *lbl_master = gtk_label_new("Events");
    gtk_widget_add_css_class(lbl_master, "section-title");
    gtk_widget_set_halign(lbl_master, GTK_ALIGN_START);
    gtk_widget_set_margin_start(lbl_master, 6);
    gtk_widget_set_hexpand(lbl_master, TRUE);
    /* Fila de encabezado: titulo + boton "Clear search" (volver a vivo+historial). */
    GtkWidget *master_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_end(master_header, 6);
    gtk_box_append(GTK_BOX(master_header), lbl_master);
    g_btn_clear_search = gtk_button_new_with_label("Clear search");
    gtk_widget_add_css_class(g_btn_clear_search, "flat");
    g_signal_connect(g_btn_clear_search, "clicked",
                     G_CALLBACK(on_btn_clear_search_clicked), NULL);
    gtk_widget_set_sensitive(g_btn_clear_search, FALSE);
    gtk_box_append(GTK_BOX(master_header), g_btn_clear_search);
    GtkWidget *master_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_vexpand(master_box, TRUE);
    gtk_box_append(GTK_BOX(master_box), master_header);
    gtk_box_append(GTK_BOX(master_box), sw_master);

    GtkWidget *lbl_detail = gtk_label_new("Origins");
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

    /* Panel izquierdo: TABLA de estaciones (GtkColumnView con separadores de
       fila y columna, igual que la tabla de Eventos). Una fila por (estacion,
       fase) = 2 por estacion; columnas station | dist[km] | pick | stat | res. */
    GtkWidget *list_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(list_frame, "detail-frame");
    /* Ancho: el marco se dimensiona a lo que pide la tabla y el lienzo se queda
       el resto. Con GTK_POLICY_NEVER abajo no hay barra horizontal; y como los
       anchos de columna son un SUELO (si la cabecera pide mas, manda ella), la
       tabla nunca recorta sus titulos. */
    gtk_widget_set_vexpand(list_frame, TRUE);
    GtkWidget *sw_list = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_list), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw_list, TRUE);
    g_sta_store = g_list_store_new(CSNHYPODBP_TYPE_STA_ITEM);
    g_sort_sta = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(g_sta_store)), NULL);
    g_sta_sel = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(g_sort_sta)));
    gtk_single_selection_set_autoselect(g_sta_sel, FALSE);
    gtk_single_selection_set_can_unselect(g_sta_sel, TRUE);
    g_signal_connect(g_sta_sel, "notify::selected", G_CALLBACK(on_station_selected), NULL);
    g_sta_view = gtk_column_view_new(GTK_SELECTION_MODEL(g_sta_sel));
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(g_sta_view), TRUE);
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(g_sta_view), TRUE);
    gtk_widget_add_css_class(g_sta_view, "sta-table");
    /* Anchos AJUSTADOS a la cabecera con la fuente a 0.75em: si se bajan, GTK
       recorta el titulo ("dist[k…", "pic…"). El marco mide la suma de columnas
       (la `station` se estira); comprobado con capturas bajo el arnes GUI. */
    add_sta_station_column(g_sta_view, "station");
    add_sta_text_column(g_sta_view, "dist[km]", "dist", 56);
    add_sta_text_column(g_sta_view, "pick", "pick", 36);
    add_sta_text_column(g_sta_view, "stat", "stat", 28);
    add_sta_text_column(g_sta_view, "res", "res", 42);
    /* Orden por cabecera: texto alfabetico primero, numeros descendente. La
       estacion desempata por fase para que P y S salgan siempre en el mismo
       orden (el orden de un sorter no es estable). Sin orden inicial: se
       conserva el orden por distancia. */
    {
        static const char *sprop[5] = {"sta", "dist", "pick", "stat", "res"};
        static const char *stie[5]  = {"phase", NULL, NULL, NULL, NULL};
        static const int   sdir[5]  = {EWGUI_TABLE_ASC, EWGUI_TABLE_DESC,
                                       EWGUI_TABLE_ASC, EWGUI_TABLE_ASC,
                                       EWGUI_TABLE_DESC};
        for (int i = 0; i < 5; i++) {
            GtkColumnViewColumn *c = column_at(g_sta_view, (guint)i);
            if (c) {
                ewgui_table_column_sortable(c, sprop[i], stie[i], sdir[i]);
                g_object_unref(c);
            }
        }
        ewgui_table_enable_sorting(GTK_COLUMN_VIEW(g_sta_view), g_sort_sta, NULL, 0);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_list), g_sta_view);
    gtk_frame_set_child(GTK_FRAME(list_frame), sw_list);

    GtkWidget *waves_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(waves_hbox), list_frame);
    gtk_box_append(GTK_BOX(waves_hbox), waves_right);
    gtk_widget_set_vexpand(waves_hbox, TRUE);

    /* Preview de la solucion: tablita de 2 filas (cabecera de parametros +
       valores) con Confirm/Discard a la derecha. Muestra la solucion elegida en
       el detalle y, tras `locate`, la candidata (fondo rojo leve). SIEMPRE
       visible: sin seleccion muestra guiones, asi no salta el layout. */
    g_preview_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(g_preview_box, "preview-box");
    gtk_widget_set_margin_start(g_preview_box, 8);
    gtk_widget_set_margin_end(g_preview_box, 8);
    gtk_widget_set_margin_top(g_preview_box, 4);
    gtk_widget_set_margin_bottom(g_preview_box, 2);
    {
        GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *grid = gtk_grid_new();
        GtkWidget *pc = gtk_button_new_with_label("Confirm");
        GtkWidget *pd = gtk_button_new_with_label("Discard");
        int i;
        gtk_widget_set_hexpand(inner, TRUE);
        g_preview_title = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(g_preview_title), 0.0);
        gtk_widget_add_css_class(g_preview_title, "preview-title");
        gtk_box_append(GTK_BOX(inner), g_preview_title);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 0);
        /* Fila 0 = cabeceras, fila 1 = valores (mismo indice en las dos). */
        for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++) {
            GtkWidget *h = gtk_label_new(csnhypodbp_preview_headers[i]);
            gtk_label_set_xalign(GTK_LABEL(h), 0.0);
            gtk_widget_add_css_class(h, "preview-hdr");
            g_preview_val[i] = gtk_label_new("-");
            gtk_label_set_xalign(GTK_LABEL(g_preview_val[i]), 0.0);
            gtk_label_set_width_chars(GTK_LABEL(g_preview_val[i]), 9);
            gtk_widget_add_css_class(g_preview_val[i], "preview-val");
            gtk_grid_attach(GTK_GRID(grid), h, i, 0, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), g_preview_val[i], i, 1, 1, 1);
        }
        gtk_box_append(GTK_BOX(inner), grid);
        gtk_box_append(GTK_BOX(g_preview_box), inner);
        gtk_widget_add_css_class(pc, "suggested-action");
        gtk_widget_set_valign(pc, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(pd, GTK_ALIGN_CENTER);
        /* Sin candidata no hay nada que confirmar ni descartar. */
        gtk_widget_set_sensitive(pc, FALSE);
        gtk_widget_set_sensitive(pd, FALSE);
        g_signal_connect(pc, "clicked", G_CALLBACK(on_preview_confirm), NULL);
        g_signal_connect(pd, "clicked", G_CALLBACK(on_preview_discard), NULL);
        gtk_box_append(GTK_BOX(g_preview_box), pc);
        gtk_box_append(GTK_BOX(g_preview_box), pd);
        g_preview_btn_confirm = pc;
        g_preview_btn_discard = pd;
    }
    gtk_widget_set_visible(g_preview_box, TRUE);
    gtk_box_append(GTK_BOX(page_waves), g_preview_box);
    csnhypodbp_preview_update();   /* estado inicial (sin seleccion: guiones) */

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

    /* Barra inferior (Ondas): Cancel a la izquierda; a la DERECHA el filtro de
       picadas (A/M/Teo), el localizador activo y los botones locators/locate.
       Filters / Fetch waveforms / Time window / Hide-empty viven en el ☰. */
    {
        GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
        GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget *btn_locators = gtk_button_new_with_label("locators");
        GtkWidget *btn_locate = gtk_button_new_with_label("locate");

        g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_btn_cancel_clicked), NULL);
        g_signal_connect(btn_locators, "clicked", G_CALLBACK(on_btn_locators_clicked), NULL);
        gtk_widget_add_css_class(btn_locate, "suggested-action");
        g_signal_connect(btn_locate, "clicked", G_CALLBACK(on_btn_locate_clicked), NULL);

        gtk_box_append(GTK_BOX(hbox_bottom), btn_cancel);
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(GTK_BOX(hbox_bottom), spacer);

        g_chk_a = gtk_check_button_new_with_label("A");
        g_chk_m = gtk_check_button_new_with_label("M");
        g_chk_teo = gtk_check_button_new_with_label("Teo");
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_chk_a), g_filter_auto ? TRUE : FALSE);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_chk_m), g_filter_manual ? TRUE : FALSE);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_chk_teo), g_filter_theo ? TRUE : FALSE);
        g_signal_connect(g_chk_a, "toggled", G_CALLBACK(on_filter_toggled), GINT_TO_POINTER(0));
        g_signal_connect(g_chk_m, "toggled", G_CALLBACK(on_filter_toggled), GINT_TO_POINTER(1));
        g_signal_connect(g_chk_teo, "toggled", G_CALLBACK(on_filter_toggled), GINT_TO_POINTER(2));
        gtk_box_append(GTK_BOX(hbox_bottom), g_chk_a);
        gtk_box_append(GTK_BOX(hbox_bottom), g_chk_m);
        gtk_box_append(GTK_BOX(hbox_bottom), g_chk_teo);

        g_locator_label = gtk_label_new("");
        gtk_box_append(GTK_BOX(hbox_bottom), g_locator_label);
        csnhypodbp_update_locator_label();

        gtk_box_append(GTK_BOX(hbox_bottom), btn_locators);
        gtk_box_append(GTK_BOX(hbox_bottom), btn_locate);
    }
    gtk_box_append(GTK_BOX(page_waves), hbox_bottom);

    cargar_sismos_iniciales(tree_global);
    cargar_sismos_db();
    csnhypodbp_load_manual_state();
    csnhypodbp_sta_sel_reset();
    rebuild_station_list();
    gtk_window_present(GTK_WINDOW(window_global));

    g_timeout_add(1000, ew_background_tasks, NULL);
    g_timeout_add(500, escuchar_anillo_earthworm, tree_global);
    g_timeout_add(1000, waveform_reload_timer, NULL);
}

int main(int argc, char *argv[]) {
    /* Modos headless (patron de csnrv): no arrancan GTK ni Earthworm. */
    if (argc == 3 && strcmp(argv[1], "--print-config") == 0) {
        if (ReadConfig(argv[2]) != 0) {
            fprintf(stderr, "Error leyendo configuracion de %s\n", argv[2]);
            return 1;
        }
        return maps_print_config();
    }
    if (argc == 3 && strcmp(argv[1], "--check-profiles") == 0) {
        if (ReadConfig(argv[2]) != 0) {
            fprintf(stderr, "Error leyendo configuracion de %s\n", argv[2]);
            return 1;
        }
        return maps_check_profiles();
    }
    if (argc != 2) { fprintf(stderr, "Uso: %s <configfile.d>\n", argv[0]); exit(1); }
    if (ReadConfig(argv[1]) != 0) exit(1);
    setenv("TZ", "GMT", 1); tzset(); logit_init(argv[1], 0, 1024, LogFile); MyPid = getpid();
    logit("t", "csnhypodbp: config channelsOrder=[%s] componentsOrder=[%s] HideEmptyStations=%d\n",
          g_channels_order, g_components_order, g_hide_empty);
    logit("t", "csnhypodbp: busqueda SearchMaxEvents=%d  SDS enabled=%d root=%s fallbackHours=%d\n",
          g_search_max_events, g_sds_enabled, g_sds_root, g_sds_fallback_hours);
    /* La BD se abre DESPUES de logit_init: si falla (p. ej. falta db.env), el
       motivo queda en el log y no en un stderr silencioso. */
    if (DbEnvFile[0]) {
        if (csnhypodbp_db_init(DbEnvFile, g_db_hours) == 0)
            logit("t", "csnhypodbp: BD conectada (%s)\n", DbEnvFile);
        else
            logit("e", "csnhypodbp: BD NO disponible (%s): historial y busqueda deshabilitados\n",
                  DbEnvFile);
    } else {
        logit("e", "csnhypodbp: sin DbEnvFile: historial y busqueda deshabilitados\n");
    }
    csnhypodbp_log_colors();
    setlocale(LC_NUMERIC, "C");
    csnhypodbp_maps_load();   /* mapa base + capas .lines (usa logit) */

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
