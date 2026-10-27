#include <math.h>

#include "csnhypodbp.h"
#include "csnhypodbp_locator.h"
#include "csnhypodbp_mww.h"
#include "ewgui/table_ui.h"

/* Hook de diagnostico del maximizado: se define junto a `main` y lo usa el
   constructor de la ventana (ver AGENTS §7). */
static void dbg_maximize_hook(void);

/* Definicion de Variables Globales */
char MyModName[MAX_STR];
char InRingName[MAX_STR];
char InRingRawName[MAX_STR];   /* 2.º anillo de entrada (csnloc crudo, opcional) */
char OutRingName[MAX_STR];
char InRingPickName[MAX_STR];  /* anillo de picks a leer (TYPE_PICK_SCNL) */
char ConfRingName[MAX_STR];    /* anillo de confirmaciones (ARC confirmado) */
char DbEnvFile[MAX_STR];       /* credenciales PostgreSQL (historial) */
char MagConfigFile[MAX_STR] = "csnmags.d";  /* .d de csnmags: defaults de magnitud */
char MwwConfigFile[MAX_STR] = "csnwphase.d";          /* .d de csnwphase (panel Mww) */
char MwwWorkDir[MAX_STR]    = "/tmp/csnhypodbp_mww";  /* run dirs de las corridas Mww */
CsnhypodbpMagCfg g_magcfg;     /* parametros de magnitud (se llenan al leer el .d) */
int  g_db_hours = 24;          /* ventana de historial de la BD (horas) */
char TauModel[MAX_STR];        /* modelo de tiempos de viaje (default iasp91) */
char StaFile[MAX_STR];
char StateFile[MAX_STR];   /* archivo de estado de csnloc (recuperacion) */
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
SHM_INFO CRegion;        /* confirmaciones: ARC confirmado -> hypodb */
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
/* 1 mientras una llegada del anillo recompone la seleccion: la re-seleccion
 * interna NO debe reiniciar la vista del usuario (ver should_reset). */
int     g_ring_sync = 0;
double  selected_otime = 0;
double  selected_depth = 0;
char    selected_id[32] = "None";
/* `origin_id` de la BD del origen seleccionado (vacio si la fila aun no esta en
 * la base, p.ej. un candidato recien localizado). Lo usa el panel `Mww` para
 * engancharle su magnitud y su tensor al confirmar. */
char    selected_origin_id[64] = "";
double  g_pick_window_sec = 600.0;   /* PickCaptureWindowSec */
char    g_pick_phase = 'P';          /* fase a picar en modo Repick */

EwGuiCanvas *canvas_global = NULL;
GtkWidget *g_sw_ondas = NULL;   /* scrolled de las trazas: su alto asignado es el viewport */
GListStore *g_store_hypo = NULL;
GListStore *g_store_events = NULL;
GListStore *g_store_detail = NULL;
GtkSingleSelection *g_selection_hypo = NULL;
GtkSingleSelection *g_selection_events = NULL;
GtkSingleSelection *g_selection_detail = NULL;
GtkWidget *g_notebook = NULL;
GtkWidget *g_page_procesar = NULL;            /* pagina raiz "Procesar" (sub-notebook) */
GtkWidget *g_subnotebook = NULL;              /* sub-pestañas: picking / magnitudes */
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
int  g_traces_per_screen = CSNHYPODBP_TRACES_PER_SCREEN_MIN; /* tracesPerScreen */
CsnhypodbpLocator g_locators[MAX_LOCATORS];
int  g_nlocators = 0;
char g_active_locator[32] = "";
char g_manual_state_file[MAX_STR] = "csnhypodbp_manual.events";
int  g_locator_timeout_sec = 600;
int  g_prefetch_neighbors = 2;
int  g_hide_empty = 0;   /* HideStationsWithoutData: ocultar estaciones sin datos */
char g_store_root[512] = "";
int  g_fetch_threads = 4;
/* Colores de las picadas (pColor/sColor + pColorAuto/pColorTheo/sColorAuto/
   sColorTheo del .d): la traza siempre es negra; auto y teorica, si no se
   declaran, se derivan del manual en el momento de dibujar (aclarado 40 % /
   70 %, csnhypodbp_color_resolve). */
CsnhypodbpColorSet g_colors_p = { { 1.0, 0.0, 0.0 }, 0, { 0.0, 0.0, 0.0 },
                                  0, { 0.0, 0.0, 0.0 } };   /* P: rojo */
CsnhypodbpColorSet g_colors_s = { { 0.0, 0.6, 0.0 }, 0, { 0.0, 0.0, 0.0 },
                                  0, { 0.0, 0.0, 0.0 } };   /* S: verde */
GtkWidget *window_global = NULL; 

double g_max_dist_km = 500.0; 
double g_mag_fetch_sec = 700.0;   /* ventana pedida por solucion (s; Ms necesita ~400) */
double   g_zoom_factor = 1.0; 
double   g_dWindowStart = 0;
double   g_dScreenTime = 120.0; 
int      g_margin_left = 44;   /* etiqueta de componente + aire, nada mas */ 
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

/* Sub-pestaña `magnitudes`: tabla con UNA fila por estacion (misma lista de
   estaciones que la de picking, con el valor de la magnitud activa). */
int g_mag_kind = CSNHYPODBP_MAG_ML;          /* magnitud activa (0=ML..3=Ms) */
CsnhypodbpMagRow g_mag_rows[MAX_ESTA];
int g_mag_sta_idx[MAX_ESTA];              /* fila de magnitudes -> fila de Ondas */
int g_mag_rows_n = 0;
EwGuiCanvas *g_mag_canvas = NULL;
EwGuiCanvas *g_mag_axis_canvas = NULL;
double       g_mag_cursor_x = 0.0;      /* puntero en el canvas de magnitudes */
int          g_mag_cursor_valid = 0;    /* 1 = sobre el canvas */
char g_mag_station[16] = "";
CsnhypodbpMagMarks g_mag_marks;   /* ventanas ajustadas por (estacion, magnitud) */
/* Filtro A/M/Teo de `magnitudes`: que ventanas entran al forzado (por defecto
   M y A si, Teo no). */
int g_magf_auto = 1, g_magf_manual = 1, g_magf_theo = 0;
CsnhypodbpMagGateOv g_mag_gate_ov[CSNHYPODBP_MAG_NKIND];   /* override de distancia */
GtkWidget *g_magf_chk[3] = { NULL, NULL, NULL };
GtkWidget *g_mag_ov_min = NULL, *g_mag_ov_max = NULL;
GtkWidget *g_mag_ov_dmin = NULL, *g_mag_ov_depth = NULL;
GtkWidget *g_mag_ov_reset = NULL;
GtkWidget *g_mag_btn_calc = NULL, *g_mag_btn_calc_all = NULL;
GtkWidget *g_mag_view = NULL;
GListStore *g_mag_store = NULL;
GtkSingleSelection *g_mag_sel = NULL;
GtkSortListModel *g_mag_sort = NULL;
GtkWidget *g_mag_radio[CSNHYPODBP_MAG_NKIND];
GtkWidget *g_mag_status = NULL;

/* Vista de ondas: zoom por dos clics, modo de clic y eje de tiempo UTC. */
CsnhypodbpView      g_view;
/* Zoom por dos clics del canvas de `magnitudes` (misma vista que la de ondas). */
CsnhypodbpView g_mag_zoomview;
CsnhypodbpClickMode g_click_mode = CSNHYPODBP_CLICK_ZOOM;
EwGuiCanvas        *g_axis_canvas = NULL;
double              g_win_start_cur = 0.0;
/* Ancla de la ventana del picking: P de referencia CONGELADA (tiempo absoluto).
 * Picar no la mueve; solo se refresca al deshacer el zoom o cambiar de
 * estacion/solucion/evento. `has_p` = 1 si ya es una P real (no el respaldo). */
double              g_win_ref_pick = 0.0;
int                 g_win_ref_has_p = 0;
double              g_cursor_x = 0.0, g_cursor_y = 0.0;  /* puntero en el canvas */
int                 g_cursor_valid = 0;                  /* 1 = sobre el canvas */

/* Preview de locate (solucion candidata). */
GtkWidget *g_preview_box = NULL;
GtkWidget *g_preview_title = NULL;
GtkWidget *g_preview_otime = NULL;
GtkWidget *g_preview_spinner = NULL;
GtkWidget *g_preview_status = NULL;
GtkWidget *g_preview_val[CSNHYPODBP_PREVIEW_NVALS];
GtkWidget *g_preview_btn_confirm = NULL, *g_preview_btn_discard = NULL;
GtkWidget *g_preview_radio[CSNHYPODBP_PREVIEW_MAGS];   /* magnitud preferida */
int        g_preview_pref_kind = -1;                   /* 0=ML..3=Ms, -1 = ninguna */
int        g_preview_pref_sync = 0;                    /* 1 = la GUI sincroniza */
CsnhypodbpSolution g_preview_sol;
CsnhypodbpMagOut g_preview_mag;
gboolean g_preview_valid = FALSE;
int g_filter_auto = 0;
int g_filter_manual = 1;
int g_filter_theo = 0;
GtkWidget *g_chk_a = NULL, *g_chk_m = NULL, *g_chk_teo = NULL;
GtkWidget *g_locator_label = NULL;
GtkWidget *g_fix_depth_label = NULL, *g_fix_depth_entry = NULL, *g_fix_depth_chk = NULL;
GtkWidget *g_btn_locate = NULL;
gboolean   g_fix_depth_on = FALSE;
double     g_fix_depth_km = -1.0;

/* --- Mapas: panel de planta (contrato de csnrv) y panel de perfil --- */
char  MapImageFile[MAX_STR] = "";            /* mapa base equirectangular (2:1) */
char  ProfileDataDir[MAX_STR] = "";          /* dir de slab<name>.tmp/topo<name>.tmp */
char  ProfileFile[MAX_STR] = "";             /* archivo de declaraciones Profile */
CsnhypodbpProfiles g_profiles;               /* perfiles de profundidad declarados */
int    g_profile_lon_col = 3;                /* ProfileCols: columnas 1-based */
int    g_profile_val_col = 7;
double g_profile_depth_min_km = 200.0;       /* ProfileDepthMinKm */
double g_map_min_span_deg = 5.0;             /* MapMinSpanDeg (auto-encuadre) */
double g_map_zoom_factor = 1.0;              /* MapZoom: factor sobre el auto-encuadre */
double g_map_split_frac  = 0.7;              /* MapSplit: altura de la planta (default 7/3) */
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
CsnhypodbpFilterCfg g_filtercfg; /* presets y atajos (defaults + .d) */
int      g_active_filter = -1;   /* -1 = Raw; -2 = filtro manual del dialogo */
GtkWidget *g_filter_label = NULL;
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

/* Etiqueta del modulo origen: se deriva del binario de la tabla de
 * localizadores (csnhypodbpqt.d); no hay ModuleLabel en el .d. */
const char *ModLabel(int mod)
{
    static char buf[16];
    int i;
    for (i = 0; i < g_nlocators; i++)
        if (csnhypodbp_locator_mod(&g_locators[i]) == mod)
            return g_locators[i].name;
    /* Fallback para origenes historicos de la BD si la tabla no esta. */
    if (mod == 162) return "csnloc";
    if (mod == 163) return "hyp2000_ring";
    if (mod == 164) return "nlloc_ring";
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

/* Lee un color RRGGBB del .d y lo aplica a `set`. Un valor invalido no toca
 * el destino (la entrada sigue heredando del manual) y deja aviso en el log. */
static void read_pick_color(const char *key, const char *str,
                            CsnhypodbpColorSet *set, CsnhypodbpPickOrigin origin)
{
    double rgb[3];
    if (!str)
        return;
    if (csnhypodbp_view_parse_color(str, rgb) != 0) {
        logit("et", "csnhypodbp: %s <%s> invalido, uso el color heredado\n",
              key, str);
        return;
    }
    csnhypodbp_color_set_override(set, origin, rgb);
}

/* --------------------------------------------------------------------
 * LECTURA DE CONFIG Y CONEXION EW
 * -------------------------------------------------------------------- */
int ReadConfig(char *configfile) {
    int ncommand = 7, nmiss = 0, i; char init[8] = {0}; char *com, *str;
    csnhypodbp_filtercfg_defaults(&g_filtercfg);
    if (!k_open(configfile)) { fprintf(stderr, "Error abriendo archivo config <%s>\n", configfile); return -1; }
    while (k_rd()) {
        com = k_str(); if (!com || com[0] == '#') continue;
        if (k_its("MyModuleId")) { str = k_str(); if (str) strcpy(MyModName, str); init[0] = 1; } 
        else if (k_its("InRing")) { str = k_str(); if (str) strcpy(InRingName, str); init[1] = 1; } 
        else if (k_its("OutRing")) { str = k_str(); if (str) strcpy(OutRingName, str); init[2] = 1; } 
        else if (k_its("HeartBeatInt")) { HeartBeatInt = k_int(); init[3] = 1; } 
        else if (k_its("LogFile")) { LogFile = k_int(); init[4] = 1; } 
        else if (k_its("StaFile")) { str = k_str(); if (str) strcpy(StaFile, str); init[5] = 1; } 
        else if (k_its("StateFile")) { str = k_str(); if (str) strcpy(StateFile, str); init[6] = 1; }
        else if (k_its("InRingRaw")) { str = k_str(); if (str) strcpy(InRingRawName, str); }
        else if (k_its("InRingPick")) { str = k_str(); if (str) strcpy(InRingPickName, str); }
        else if (k_its("ConfRing")) { str = k_str(); if (str) strcpy(ConfRingName, str); }
        else if (k_its("TauModel")) { str = k_str(); if (str) strcpy(TauModel, str); }
        else if (k_its("PickCaptureWindowSec")) { g_pick_window_sec = k_val(); }
        else if (k_its("MaxDistKm")) { double v = k_val(); if (v > 0.0) g_max_dist_km = v; }
        else if (k_its("MagFetchSeconds")) { double v = k_val(); if (v > 0.0) g_mag_fetch_sec = v; }
        else if (k_its("LocateKeepTmp")) { csnhypodbp_locate_keep_tmp(k_int()); }
        else if (k_its("GroupPickTolSec")) { g_group_pick_tol_sec = k_val(); }
        else if (k_its("GroupMinMatchPicks")) { g_group_min_match_picks = k_int(); }
        else if (k_its("GroupMaxSolutions")) { g_group_max_solutions = k_int(); }
        else if (k_its("DbEnvFile")) { str = k_str(); if (str) strcpy(DbEnvFile, str); }
        else if (k_its("MagConfigFile")) { str = k_str(); if (str) strcpy(MagConfigFile, str); }
        else if (k_its("MwwConfigFile")) { str = k_str(); if (str) strcpy(MwwConfigFile, str); }
        else if (k_its("MwwWorkDir")) { str = k_str(); if (str) strcpy(MwwWorkDir, str); }
        else if (k_its("DbHistoryHours")) { g_db_hours = k_int(); }
        else if (k_its("StoreRoot")) { str = k_str(); if (str) snprintf(g_store_root, sizeof(g_store_root), "%s", str); }
        else if (k_its("FetchThreads")) { g_fetch_threads = k_int(); if (g_fetch_threads < 1) g_fetch_threads = 1; }
        else if (k_its("channelsOrder")) {
            char buf[128]; k_rest_join(buf, sizeof(buf));
            if (buf[0]) snprintf(g_channels_order, sizeof(g_channels_order), "%s", buf);
        }
        else if (k_its("componentsOrder")) {
            char buf[64]; k_rest_join(buf, sizeof(buf));
            if (buf[0]) snprintf(g_components_order, sizeof(g_components_order), "%s", buf);
        }
        else if (k_its("tracesPerScreen")) {
            /* Solo 1, 2 o 3 (un digito): mas trazas por pantalla saturan la vista.
               Cualquier otra cosa ("0", "4", "-1", "2.7", "abc") no se aplica:
               se avisa con el TEXTO recibido y se conserva el previo (patron de
               MapZoom). Se lee como token y se parsea aqui para no dejar k_err()
               (abortaria la lectura del .d) y para no truncar "2.7" a 2. */
            char *tok = k_str();
            int v = 0;
            (void)k_err();
            if (tok && tok[0] != '\0' && tok[1] == '\0')
                v = csnhypodbp_traces_per_screen_norm(tok[0] - '0');
            if (v > 0)
                g_traces_per_screen = v;
            else
                logit("et", "csnhypodbp: tracesPerScreen <%s> fuera de [%d,%d], uso %d\n",
                      tok && tok[0] ? tok : "(none)",
                      CSNHYPODBP_TRACES_PER_SCREEN_MIN,
                      CSNHYPODBP_TRACES_PER_SCREEN_MAX, g_traces_per_screen);
        }
        else if (k_its("locator")) {
            /* locator <bin> <cfg> <live_usage> <time_recalc_live> <N_retry> */
            char *bn = k_str(), *cf = k_str(), *ls = k_str();
            char *rs = k_str(), *ns = k_str();
            if (bn && cf)
                csnhypodbp_locator_add_full(g_locators, &g_nlocators,
                                            MAX_LOCATORS, bn, cf,
                                            ls ? atoi(ls) : 0,
                                            rs ? atoi(rs) : 0,
                                            ns ? atoi(ns) : 0);
            (void)k_err();
        }
        else if (k_its("ManualStateFile")) { str = k_str(); if (str) snprintf(g_manual_state_file, sizeof(g_manual_state_file), "%s", str); }
        else if (k_its("LocatorTimeoutSec")) { g_locator_timeout_sec = k_int(); }
        else if (k_its("PrefetchNeighbors")) { g_prefetch_neighbors = k_int(); }
        else if (k_its("HideStationsWithoutData")) { g_hide_empty = k_int() ? 1 : 0; }
        else if (k_its("SearchMaxEvents")) {
            g_search_max_events = k_int();
            if (g_search_max_events <= 0) g_search_max_events = CSNHYPODBP_SEARCH_DEFAULT_LIMIT;
        }
        else if (k_its("SdsEnabled")) { g_sds_enabled = k_int() ? 1 : 0; }
        else if (k_its("SdsRoot")) { str = k_str(); if (str) snprintf(g_sds_root, sizeof(g_sds_root), "%s", str); }
        else if (k_its("SdsFallbackHours")) { g_sds_fallback_hours = k_int(); if (g_sds_fallback_hours < 0) g_sds_fallback_hours = 0; }
        else if (k_its("pColor")) {
            str = k_str();
            read_pick_color("pColor", str, &g_colors_p, CSNHYPODBP_PICK_MANUAL);
        }
        else if (k_its("sColor")) {
            str = k_str();
            read_pick_color("sColor", str, &g_colors_s, CSNHYPODBP_PICK_MANUAL);
        }
        else if (k_its("pColorAuto")) {
            str = k_str();
            read_pick_color("pColorAuto", str, &g_colors_p, CSNHYPODBP_PICK_AUTO);
        }
        else if (k_its("pColorTheo")) {
            str = k_str();
            read_pick_color("pColorTheo", str, &g_colors_p, CSNHYPODBP_PICK_THEO);
        }
        else if (k_its("sColorAuto")) {
            str = k_str();
            read_pick_color("sColorAuto", str, &g_colors_s, CSNHYPODBP_PICK_AUTO);
        }
        else if (k_its("sColorTheo")) {
            str = k_str();
            read_pick_color("sColorTheo", str, &g_colors_s, CSNHYPODBP_PICK_THEO);
        }
        /* --- Mapas: panel de planta (mismo contrato que csnrv) --- */
        else if (k_its("MapImageFile")) { str = k_str(); if (str) snprintf(MapImageFile, sizeof(MapImageFile), "%s", str); }
        else if (k_its("MapMinSpanDeg")) {
            double v = k_val();
            if (v > 0.0) g_map_min_span_deg = v;
        }
        /* Zoom por defecto del panel de planta (factor sobre el auto-encuadre)
           y reparto vertical planta/perfil (p.ej. `7/3`). */
        else if (k_its("MapZoom")) {
            double v = k_val();
            if (v > 0.0 && v <= 50.0) {
                g_map_zoom_factor = v;
            } else {
                logit("et", "csnhypodbp: MapZoom <%.4g> fuera de (0,50], uso %.2f\n",
                      v, g_map_zoom_factor);
            }
        }
        else if (k_its("MapSplit")) {
            double f = 0.0;

            str = k_str();
            if (csnhypodbp_map_split_parse(str, &f)) {
                if (f < 0.1) f = 0.1;
                if (f > 0.9) f = 0.9;
                g_map_split_frac = f;
            } else {
                logit("et", "csnhypodbp: MapSplit <%s> invalido (usa `a/b`, p.ej. 7/3 "
                            "sin espacios); uso %.3f\n",
                      str ? str : "(none)", g_map_split_frac);
            }
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
        else if (k_its("Filter") || k_its("FilterMenu") || k_its("FilterRaw") ||
                 k_its("FilterPhase") || k_its("StaLta")) {
            /* Presets de filtro y atajos. El parseo y la validacion viven en el
               nucleo puro csnhypodbp_filtercfg (test_filtercfg.c). Una linea
               invalida NO se ignora: aborta la lectura, como Profile. */
            char rest[256] = "", err[256] = "";
            k_rest_join(rest, sizeof(rest));
            if (csnhypodbp_filtercfg_add(&g_filtercfg, com, rest, err, sizeof(err)) != 0) {
                fprintf(stderr, "Error en la linea %s: %s\n", com,
                        err[0] ? err : "valor invalido");
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
    /* Parametros de magnitud: defaults de fabrica y, encima, lo que diga el .d de
       csnmags (la MISMA fuente que usa el motor: la GUI no duplica valores). */
    csnhypodbp_magcfg_defaults(&g_magcfg);
    if (csnhypodbp_magcfg_load(&g_magcfg, MagConfigFile) != 0)
        fprintf(stderr, "Aviso: no se pudo leer MagConfigFile <%s>; se usan los "
                        "defaults de magnitud\n", MagConfigFile);
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close();
    /* La tabla de localizadores vive SOLO en csnhypodbpqt.d. La GTK no la
     * duplica en su .d: si no declaro `locator`, la carga de ahi. */
    if (g_nlocators == 0)
        csnhypodbp_locator_load_default(g_locators, &g_nlocators, MAX_LOCATORS);
    return (nmiss > 0) ? -1 : 0;
}

/* --- Modos headless (patron de csnrv): no arrancan GTK ni se conectan a EW --- */

/* Una linea de color ya resuelta; marca las entradas que aun heredan del
 * manual (auto 40 % / teorica 70 %). */
static void print_pick_color(const char *key, const CsnhypodbpColorSet *set,
                             CsnhypodbpPickOrigin origin)
{
    double rgb[3];
    char hex[7];
    const char *sfx = "";
    csnhypodbp_color_resolve(set, origin, rgb);
    csnhypodbp_color_to_hex(rgb, hex);
    if (origin == CSNHYPODBP_PICK_AUTO && !set->has_auto)
        sfx = " (derived 40%)";
    else if (origin == CSNHYPODBP_PICK_THEO && !set->has_theo)
        sfx = " (derived 70%)";
    printf("%s %s%s\n", key, hex, sfx);
}

static int maps_print_config(void) {
    int i;
    /* Estado inicial de la tabla de estaciones en la sesion (el menu lo alterna
       sin reescribir el .d). */
    printf("HideStationsWithoutData %d\n", g_hide_empty ? 1 : 0);
    printf("tracesPerScreen %d\n", g_traces_per_screen);
    printf("MapImageFile %s\n", MapImageFile[0] ? MapImageFile : "(none)");
    /* Espacio de proyeccion efectivo, sin abrir GTK: sin MapImageFile el mapa de
       planta se dibuja igual sobre el lienzo virtual (grilla, capas y puntos). */
    if (MapImageFile[0])
        printf("MapCanvas image\n");
    else
        printf("MapCanvas virtual %.0fx%.0f\n",
               CSNHYPODBP_MAP_VIRTUAL_W, CSNHYPODBP_MAP_VIRTUAL_H);
    printf("MapMinSpanDeg %.1f\n", g_map_min_span_deg);
    printf("MapZoom %.3f\n", g_map_zoom_factor);
    printf("MapSplit %.4f\n", g_map_split_frac);
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
    /* Filtros de la vista de ondas: fase, atajos y presets vigentes. */
    printf("FilterPhase %s\n", g_filtercfg.phase_zero ? "zero" : "causal");
    printf("FilterMenu %c\n", g_filtercfg.menu_key);
    printf("FilterRaw %c\n", g_filtercfg.raw_key);
    printf("Filters %d\n", g_filtercfg.nf);
    for (i = 0; i < g_filtercfg.nf; i++) {
        const CsnhypodbpFilterPreset *fp = &g_filtercfg.f[i];
        if (fp->type == 3)
            printf("Filter %s BP %c %.4g %.4g %d\n", fp->name, fp->key,
                   fp->f1, fp->f2, fp->order);
        else
            printf("Filter %s %s %c %.4g %d\n", fp->name,
                   fp->type == 1 ? "HP" : "LP", fp->key, fp->f1, fp->order);
    }
    if (g_filtercfg.stalta_declared)
        printf("StaLta %c %.3f %.3f %.2f %.2f %.3f\n", g_filtercfg.stalta_key,
               g_filtercfg.stalta_sta, g_filtercfg.stalta_lta,
               g_filtercfg.stalta_on_thr, g_filtercfg.stalta_off_thr,
               g_filtercfg.stalta_corr);
    /* Colores de las picadas (la traza es siempre negra): auto y teorica
       heredan del manual aclarado si el .d no les da valor propio. */
    print_pick_color("pColor", &g_colors_p, CSNHYPODBP_PICK_MANUAL);
    print_pick_color("pColorAuto", &g_colors_p, CSNHYPODBP_PICK_AUTO);
    print_pick_color("pColorTheo", &g_colors_p, CSNHYPODBP_PICK_THEO);
    print_pick_color("sColor", &g_colors_s, CSNHYPODBP_PICK_MANUAL);
    print_pick_color("sColorAuto", &g_colors_s, CSNHYPODBP_PICK_AUTO);
    print_pick_color("sColorTheo", &g_colors_s, CSNHYPODBP_PICK_THEO);
    printf("ProfileDataDir %s\n", ProfileDataDir[0] ? ProfileDataDir : "(none)");
    /* Parametros de magnitud: los lee la sub-pestaña `magnitudes` para dibujar la
       traza de vista y las franjas de ventana. */
    printf("MagConfigFile %s\n", MagConfigFile[0] ? MagConfigFile : "(none)");
    /* Panel `Mww` (inversion de fase W): mismo .d que el CLI csnwphase y su
       directorio de corridas. */
    printf("MwwConfigFile %s\n", MwwConfigFile[0] ? MwwConfigFile : "(none)");
    printf("MwwWorkDir %s\n", MwwWorkDir[0] ? MwwWorkDir : "(none)");
    printf("MagWindow ML %.3f %.3f\n", g_magcfg.ml_ta, g_magcfg.ml_tb);
    printf("MagWindow Mwp %.3f %.3f\n", g_magcfg.mwp_start,
           g_magcfg.mwp_start + g_magcfg.mwp_t0);
    printf("MagWindow Mb %.3f %.3f\n", 0.0, g_magcfg.mb_window);
    printf("MagWindow Ms %.3f %.3f\n", 0.0, g_magcfg.ms_window);
    printf("MagWoodAnderson %.3f %.3f %.1f\n", g_magcfg.wa_period,
           g_magcfg.wa_damp, g_magcfg.wa_gain);
    printf("MagBand Mb %.3f %.3f\n", g_magcfg.mb_f1, g_magcfg.mb_f2);
    printf("MagBand Ms %.3f %.3f\n", g_magcfg.ms_f1, g_magcfg.ms_f2);
    printf("MagFetchSeconds %.1f\n", g_mag_fetch_sec);
    {
        int gi;
        for (gi = 0; gi < CSNHYPODBP_MAG_NKIND; gi++) {
            double lo = 0.0, hi = 0.0, km = 0.0;
            csnhypodbp_magcfg_gate_limits(&g_magcfg, gi, &lo, &hi, &km);
            printf("MagGate %s %.1f %.1f %.1f\n", csnhypodbp_magcfg_name(gi), lo, hi, km);
        }
    }
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
    if (!g_map_pixbuf)
        logit("t", "csnhypodbp: mapa de planta sobre lienzo virtual %.0fx%.0f "
                   "(grilla, perfiles y capas .lines se dibujan igual)\n",
              CSNHYPODBP_MAP_VIRTUAL_W, CSNHYPODBP_MAP_VIRTUAL_H);
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
    /* Anillo de CONFIRMACIONES: por ahi viaja el ARC de cada solucion
     * confirmada a mano, para que `hypodb` le de una fila `origin` propia. */
    if (ConfRingName[0]) {
        long ConfKey = GetKey(ConfRingName);
        if (ConfKey != -1) tport_attach(&CRegion, ConfKey);
        else {
            logit("e", "csnhypodbp: ConfRing %s no existe: las confirmaciones no se guardaran\n",
                  ConfRingName);
            ConfRingName[0] = '\0';
        }
    }
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
        /* Resizable: el usuario ajusta anchos a mano. El minimo de la ventana no
           lo fijan estas columnas porque el scrolled que las contiene lleva
           `min_content_width` (AGENTS §7). */
        gtk_column_view_column_set_resizable(col, TRUE);
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

/* -------------------------------------------------------------------------
 * Tabla de MAGNITUDES (sub-pestaña `magnitudes`): UNA fila por estacion con
 * station | dist[km] | mag | res | stat. La casilla de `station` marca si esa
 * estacion entra en el recalculo de la magnitud de red; su estado vive en la
 * tabla de filas (g_mag_rows[i].used), no en el modelo, porque las celdas se
 * reciclan al hacer scroll.
 * ------------------------------------------------------------------------- */

#define MAG_CELL_CHK "mag-cell-chk"
#define MAG_CELL_LBL "mag-cell-lbl"

static CsnhypodbpMagItem *mag_item_of(GtkListItem *item)
{
    gpointer it = gtk_list_item_get_item(item);
    return (it && CSNHYPODBP_IS_MAG_ITEM(it)) ? CSNHYPODBP_MAG_ITEM(it) : NULL;
}

/* Casilla de uso: entra o no en el calculo de la magnitud de red. */
static void on_mag_check_toggled(GtkButton *b, gpointer data)
{
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "mag_idx"));
    (void)data;
    if (idx < 0 || idx >= g_mag_rows_n) return;
    g_mag_rows[idx].used = gtk_check_button_get_active(GTK_CHECK_BUTTON(b)) ? 1 : 0;
    csnhypodbp_magmarks_set_used(&g_mag_marks, g_mag_rows[idx].sta, g_mag_kind,
                                 g_mag_rows[idx].used);
    csnhypodbp_mag_status_update();
}

/* Columna `station`: [casilla de uso] nombre. */
static void mag_col_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    GtkWidget *box, *chk, *lbl;
    (void)f; (void)data;
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    chk = gtk_check_button_new();
    lbl = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(lbl, TRUE);
    g_signal_connect(chk, "toggled", G_CALLBACK(on_mag_check_toggled), NULL);
    gtk_box_append(GTK_BOX(box), chk);
    gtk_box_append(GTK_BOX(box), lbl);
    g_object_set_data(G_OBJECT(box), MAG_CELL_CHK, chk);
    g_object_set_data(G_OBJECT(box), MAG_CELL_LBL, lbl);
    gtk_list_item_set_child(item, box);
}

static void mag_col_bind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    CsnhypodbpMagItem *it;
    GtkWidget *box, *chk, *lbl;
    char *name = NULL;
    gboolean used = FALSE;
    GBinding *b;
    int idx;
    (void)f; (void)data;
    it = mag_item_of(item);
    if (!it) return;
    box = gtk_list_item_get_child(item);
    chk = g_object_get_data(G_OBJECT(box), MAG_CELL_CHK);
    lbl = g_object_get_data(G_OBJECT(box), MAG_CELL_LBL);
    idx = csnhypodbp_mag_item_idx(it);
    g_object_get(it, "sta", &name, "used", &used, NULL);
    b = g_object_bind_property(it, "sta", lbl, "label", G_BINDING_SYNC_CREATE);
    g_object_set_data_full(G_OBJECT(item), "cell-binding", b, g_object_unref);
    g_object_set_data(G_OBJECT(chk), "mag_idx", GINT_TO_POINTER(idx));
    g_signal_handlers_block_by_func(chk, G_CALLBACK(on_mag_check_toggled), NULL);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk), used);
    g_signal_handlers_unblock_by_func(chk, G_CALLBACK(on_mag_check_toggled), NULL);
    /* Sin traza descargada no hay nada que medir ni que forzar: la casilla
       queda inerte (marcar una estacion sin ondas no cambiaria el calculo). */
    gtk_widget_set_sensitive(chk, (idx >= 0 && idx < g_mag_rows_n &&
                                   g_mag_rows[idx].has_data) ? TRUE : FALSE);
    g_free(name);
}

/* Columnas de texto (dist/mag/res/stat): bind por nombre de propiedad. */
static void mag_txt_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    GtkWidget *lbl;
    (void)f; (void)data;
    lbl = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_list_item_set_child(item, lbl);
}

static void mag_txt_bind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    const char *prop = (const char *)data;
    GtkWidget *lbl;
    CsnhypodbpMagItem *it;
    GBinding *b;
    (void)f;
    it = mag_item_of(item);
    lbl = gtk_list_item_get_child(item);
    if (!it || !lbl || !prop) return;
    b = g_object_bind_property(it, prop, lbl, "label", G_BINDING_SYNC_CREATE);
    g_object_set_data_full(G_OBJECT(item), "cell-binding", b, g_object_unref);
}

static void mag_txt_unbind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
    (void)f; (void)data;
    g_object_set_data(G_OBJECT(item), "cell-binding", NULL);
}

static void add_mag_station_column(GtkWidget *view, const char *title)
{
    GtkListItemFactory *f = gtk_signal_list_item_factory_new();
    GtkColumnViewColumn *col;
    g_signal_connect(f, "setup",  G_CALLBACK(mag_col_setup), NULL);
    g_signal_connect(f, "bind",   G_CALLBACK(mag_col_bind), NULL);
    g_signal_connect(f, "unbind", G_CALLBACK(mag_txt_unbind), NULL);
    col = gtk_column_view_column_new(title, f);
    gtk_column_view_column_set_expand(col, TRUE);
    gtk_column_view_column_set_resizable(col, TRUE);
    ewgui_table_column_sortable(col, "sta", NULL, EWGUI_TABLE_ASC);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(view), col);
}

/* `sort_prop` es una propiedad NUMERICA del item (las celdas son texto). */
static void add_mag_text_column(GtkWidget *view, const char *title, const char *prop,
                                const char *sort_prop, int width, int dir)
{
    GtkListItemFactory *f = gtk_signal_list_item_factory_new();
    GtkColumnViewColumn *col;
    g_signal_connect(f, "setup",  G_CALLBACK(mag_txt_setup), NULL);
    g_signal_connect(f, "bind",   G_CALLBACK(mag_txt_bind), (gpointer)prop);
    g_signal_connect(f, "unbind", G_CALLBACK(mag_txt_unbind), NULL);
    col = gtk_column_view_column_new(title, f);
    gtk_column_view_column_set_fixed_width(col, width);
    gtk_column_view_column_set_resizable(col, TRUE);
    ewgui_table_column_sortable(col, sort_prop, NULL, dir);
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
   filtra la lista, no borra filas (una estacion con datos reaparece). El valor
   del .d solo fija el estado inicial; la casilla del menu alterna la sesion. */
static void hide_item_sync(void);
static void on_toggle_hide_empty(GSimpleAction *action, GVariant *param, gpointer user_data) {
    GVariant *st;
    gboolean active;
    (void)param; (void)user_data;
    st = g_action_get_state(G_ACTION(action));
    active = !g_variant_get_boolean(st);
    g_variant_unref(st);
    g_simple_action_set_state(action, g_variant_new_boolean(active));
    g_hide_empty = active ? 1 : 0;
    hide_item_sync();            /* la casilla del menu sigue a la sesion */
    rebuild_station_list();
    actualizar_altura_canvas();
    if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
}

/* Divide el panel maestro/detalle por la mitad en el primer mapeo (una sola
   vez, para que luego el usuario pueda arrastrar el separador).
   El flag por widget evita re-aplicar el reparto en cada `map`/remapeo: cada
   re-aplicacion mueve la geometria (y el minimo) de la ventana justo cuando el
   compositor esta negociando el maximizado, y eso es lo que hacia fallar la
   conexion Wayland (ver AGENTS §7). */
static gboolean paned_set_half_idle(gpointer data)
{
    GtkWidget *paned = GTK_WIDGET(data);
    int h = gtk_widget_get_height(paned);
    if (h > 0 && !g_object_get_data(G_OBJECT(paned), "csn-split-half")) {
        g_object_set_data(G_OBJECT(paned), "csn-split-half", GINT_TO_POINTER(1));
        gtk_paned_set_position(GTK_PANED(paned), h / 2);
    }
    return G_SOURCE_REMOVE;
}
static void paned_map_half(GtkWidget *paned, gpointer data)
{
    (void)data;
    if (g_object_get_data(G_OBJECT(paned), "csn-split-half"))
        return;
    g_idle_add(paned_set_half_idle, paned);
}

/* --- Menú del headerbar dependiente de la vista (Eventos vs Ondas) --- */
static const EwMenuItem view_items[] = {
    { "Export view...", "win.export", NULL, 0, 0, 0 },
    { "Dark mode", "win.dark-mode", NULL, 0, 0, 0 },
};
/* La casilla es estado de sesion, no una accion del grupo de formas de onda: va
   en su propio grupo, PRIMERO del menu de Procesar. El menu no lee el
   GSimpleAction, asi que el estado viaja en el item: `hide_items` NO es const y
   `hide_item_sync` lo mantiene igual a `g_hide_empty`. */
static EwMenuItem hide_items[] = {
    { "Hide stations without data", "win.hide-empty", NULL, 0, 1, 0 },
};

static void hide_item_sync(void) {
    hide_items[0].checked = g_hide_empty ? 1 : 0;
}

static const EwMenuItem wave_items[] = {
    { "Filters...", "win.filters", NULL, 0, 0, 0 },
    { "Fetch waveforms", "win.fetch", NULL, 0, 0, 0 },
    { "Time window...", "win.timewin", NULL, 0, 0, 0 },
};

static const EwMenuItem colour_items[] = {
    { "P manual", "win.colour", "i", 1, 0, 0 },
    { "P automatic", "win.colour", "i", 2, 0, 0 },
    { "P theoretical", "win.colour", "i", 3, 0, 0 },
    { "S manual", "win.colour", "i", 4, 0, 0 },
    { "S automatic", "win.colour", "i", 5, 0, 0 },
    { "S theoretical", "win.colour", "i", 6, 0, 0 },
};
static const EwMenuItem help_items[] = {
    { "Commands...", "win.help", NULL, 0, 0, 0 },
};
static const EwMenuGroup wave_groups[] = {
    { "Stations",  hide_items, 1 },
    { "Waveforms", wave_items, 3 },
    { "Colours", colour_items, 6 },
    { "View", view_items, 2 },
    { "Help", help_items, 1 },
};
static const EwMenuItem search_items[] = {
    { "Search events...", "win.search", NULL, 0, 0, 0 },
    { "Clear search", "win.clear-search", NULL, 0, 0, 0 },
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

/* En la pestaña raiz Procesar el ☰ muestra el grupo Waveforms. Se compara el
   WIDGET de la pagina, no su indice: añadir o reordenar pestañas no cambia el
   menu. */
static void on_notebook_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer ud) {
    (void)nb; (void)num; (void)ud;
    if (!g_menu_btn) return;
    if (page == g_page_procesar) ewgui_menu_button_set_groups(g_menu_btn, wave_groups, 5);
    else                         ewgui_menu_button_set_groups(g_menu_btn, base_groups, 2);
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
static gboolean paned_maps_log_idle(gpointer data);
static gboolean paned_maps_set_idle(gpointer data) {
    GtkWidget *p = data;
    int w = gtk_widget_get_width(p);

    if (g_object_get_data(G_OBJECT(p), "csn-split-maps"))
        return G_SOURCE_REMOVE;
    /* El `map` puede llegar con tamano 0 y entonces el reparto se perdia en
       silencio: se reintenta cada 50 ms hasta que haya ancho. */
    if (w <= 0) return G_SOURCE_CONTINUE;
    g_object_set_data(G_OBJECT(p), "csn-split-maps", GINT_TO_POINTER(1));
    {
        int pos = w - 340;
        /* Suelo: si el ancho medido aun es pequeno, no dejar los mapas en nada. */
        if (pos < 260) pos = 260;
        gtk_paned_set_position(GTK_PANED(p), pos);
        g_idle_add(paned_maps_log_idle, NULL);   /* ya con la posicion aplicada */
    }
    return G_SOURCE_REMOVE;
}

/* Una linea con la geometria al arrancar: si la columna de mapas quedara sin
   ancho (el fallo que la dejo invisible), se ve aqui sin abrir un inspector. */
static gboolean paned_maps_log_idle(gpointer data) {
    (void)data;
    logit("et", "csnhypodbp: ventana=%d px, columna de mapas=%d px\n",
          window_global ? gtk_widget_get_width(GTK_WIDGET(window_global)) : 0,
          g_maps_col ? gtk_widget_get_width(g_maps_col) : 0);
    return G_SOURCE_REMOVE;
}
static void paned_maps_map(GtkWidget *paned, gpointer data) {
    (void)data;
    if (g_object_get_data(G_OBJECT(paned), "csn-split-maps"))
        return;   /* ya repartido: no volver a moverlo en cada remapeo */
    g_timeout_add(50, paned_maps_set_idle, paned);
}

/* Reparte la columna de mapas planta/perfil en el primer mapeo, con la fraccion
   de `MapSplit` (default 7/3 = 0.70 de planta arriba). Se aplica UNA vez: un
   reparto posterior pisaria el que el usuario haya hecho a mano. */
static int g_map_split_done = 0;
static gboolean paned_vmaps_set_idle(gpointer data) {
    GtkWidget *p = data;
    int h = gtk_widget_get_height(p);

    if (h <= 0) return G_SOURCE_CONTINUE;      /* aun sin alto: reintenta */
    if (!g_map_split_done) {
        g_map_split_done = 1;
        gtk_paned_set_position(GTK_PANED(p), (int)(h * g_map_split_frac));
    }
    return G_SOURCE_REMOVE;
}
static void paned_vmaps_map(GtkWidget *paned, gpointer data) {
    (void)data;
    g_timeout_add(50, paned_vmaps_set_idle, paned);
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

    /* Suelos PEQUENOS: cada pixel de minimo se suma al de la ventana y, si el
       total supera la geometria maximizada que da el compositor, GTK adjunta un
       buffer mas grande que el configurado y Wayland corta la conexion (Error 71
       al maximizar; ver AGENTS §7). 160x140 basta para ver el encuadre. */
    plan_frame = map_panel_new(&g_map_plan, &g_map_plan_cap, on_draw_map_plan,
                               &plan_w, 160, 140);
    prof_frame = map_panel_new(&g_map_prof, &g_map_prof_cap, on_draw_map_profile,
                               &prof_w, 160, 100);

    /* Los dos cuadros se manejan igual: rueda = zoom, arrastre boton 1 = paneo. */
    map_panel_connect(plan_w, G_CALLBACK(on_map_plan_scroll), G_CALLBACK(on_map_plan_press),
                      G_CALLBACK(on_map_plan_release), G_CALLBACK(on_map_plan_motion));
    map_panel_connect(prof_w, G_CALLBACK(on_map_prof_scroll), G_CALLBACK(on_map_prof_press),
                      G_CALLBACK(on_map_prof_release), G_CALLBACK(on_map_prof_motion));

    gtk_paned_set_start_child(GTK_PANED(paned), plan_frame);
    gtk_paned_set_end_child(GTK_PANED(paned), prof_frame);
    /* La planta (arriba) se queda con el alto sobrante: crece con la ventana y el
       perfil no se estira mas alla de su tamano. */
    gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 320);
    gtk_widget_set_vexpand(paned, TRUE);
    g_signal_connect(paned, "map", G_CALLBACK(paned_vmaps_map), NULL);

    g_maps_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    /* Sin `size_request`: su minimo ya lo dan los dos canvas (160 px) y un minimo
       propio se SUMA al de la ventana, que es lo que dejaba la columna sin ancho
       (el notebook no cede por debajo de su minimo). El paned deja encoger
       (`shrink_*` en TRUE) para que el minimo de la ventana quepa en el
       maximizado del compositor (AGENTS §7). */
    gtk_widget_set_hexpand(g_maps_col, TRUE);
    gtk_widget_set_vexpand(g_maps_col, TRUE);
    gtk_box_append(GTK_BOX(g_maps_col), paned);
    csnhypodbp_maps_refresh();   /* estado inicial (sin seleccion) */
}

/* El eje de tiempo y la barra inferior son de *magnitudes*: al mirar la pagina
   `Mww` se ocultan, para que sus botones no se confundan con los de fase W. */
static GtkWidget *g_mag_bar_w = NULL;

static void on_mag_stack_changed(GObject *stack, GParamSpec *pspec, gpointer data)
{
    const char *nm = gtk_stack_get_visible_child_name(GTK_STACK(stack));
    int mww = (nm != NULL && strcmp(nm, "mww") == 0);

    (void)pspec;
    (void)data;
    if (g_mag_axis_canvas)
        gtk_widget_set_visible(ewgui_canvas_widget(g_mag_axis_canvas), !mww);
    if (g_mag_bar_w)
        gtk_widget_set_visible(g_mag_bar_w, !mww);
}

/* Caja de texto de un campo de la barra de overrides (barra inferior de la
   sub-pestaña `magnitudes`). Es `GtkEntry` y no `GtkSpinButton`: los botones +/-
   no aportaban y su marco se llevaba el ancho de la barra. Rango, decimales y
   ancho (el del valor canonico del campo + margen) salen del nucleo puro, y el
   valor se aplica al CONFIRMAR (Enter o salir del campo): ver
   `on_mag_ov_commit`. */
static GtkWidget *mag_ov_entry_new(int field, const char *tip)
{
    GtkWidget *e = gtk_entry_new();
    GtkEventController *fc;
    int n = csnhypodbp_mag_ov_width_chars(field);

    /* GTK4 da a TODO `GtkEntry` un ancho natural de 150 px y `width-chars` es solo
       un suelo, asi que hay que acotarlo con `max-width-chars` o la caja se queda
       ancha por mucho que su texto pida 5 caracteres. Se fijan los dos al mismo
       valor: la caja no baila al cambiar el valor. */
    if (n < 1) n = 4;                  /* campo invalido: ancho de emergencia */
    gtk_entry_set_input_purpose(GTK_ENTRY(e), GTK_INPUT_PURPOSE_NUMBER);
    gtk_entry_set_max_length(GTK_ENTRY(e), 8);
    gtk_editable_set_width_chars(GTK_EDITABLE(e), n);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(e), n);
    gtk_widget_set_tooltip_text(e, tip);
    g_signal_connect(e, "changed", G_CALLBACK(on_mag_ov_changed), NULL);
    g_signal_connect(e, "activate", G_CALLBACK(on_mag_ov_commit),
                     GINT_TO_POINTER(field));
    fc = gtk_event_controller_focus_new();
    g_signal_connect(fc, "leave", G_CALLBACK(on_mag_ov_focus_leave),
                     GINT_TO_POINTER(field));
    gtk_widget_add_controller(e, fc);
    return e;
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
    csnhypodbp_pick_anchor_refresh();   /* arranque: ancla sin P (respaldo) */

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
            /* Radios de magnitud del preview: sin el relleno/minimo del tema,
               que imponia una fila alta y dejaba una franja vacia a su lado. */
            ".preview-radio { min-height: 0; min-width: 0; padding: 0; margin: 0; }\n"
            ".preview-busy { font-size: 0.85em; color: alpha(@theme_fg_color, 0.75); }\n"
            ".preview-busy.preview-error { color: #b00020; }\n"
            ".preview-hdr { font-size: 0.75em; color: alpha(@theme_fg_color, 0.7); }\n"
            ".preview-val { font-weight: bold; }\n"
            /* Caption de los paneles de mapa (columna derecha). */
            ".map-cap { font-size: 0.8em; font-weight: bold; padding: 1px 4px; }\n"
            /* Campos de fecha y hora del buscador: fuente un punto mas chica y
               ancho minimo para que "YYYY-MM-DD HH:MM:SS" entre completo. */
            ".time-entry { font-size: 0.95em; min-width: 11em; }\n"
            /* Panel `Mww`: la tabla de parametros es editable con cajas de TEXTO
               (sin los +/- del spin); el cuadro de resultados va al 134% y la salida
               del motor al 91% (79% + 15%). */
            ".mww-param-name { font-size: 0.85em; }\n"
            ".mww-param-src { font-size: 0.8em; color: alpha(@theme_fg_color, 0.7); }\n"
            ".mww-param-value { font-weight: bold; }\n"
            ".mww-param-cell { font-size: 0.85em; min-height: 0; padding: 0 2px; }\n"
            ".mww-param-cell > text { min-height: 0; }\n"
            ".mww-cell-bad { background-image: none; background-color: #fde7e7; }\n"
            ".mww-summary { font-size: 134%; }\n"
            ".mww-output { font-size: 91%; }\n"
            /* Barra inferior de la pestaña *magnitudes*: los rotulos de los grupos
               y las cajas de la puerta de distancia van al 72% (60% + 20%). Las
               cajas son `GtkEntry` (sin los +/- del spin), asi que no hay
               subbotones que recortar; `mag-bar-bad` marca la caja cuyo texto
               todavia no es un numero valido. Los botones reset/calculate van al
               90% (`mag-bar-btn`) y el resto de la barra no cambia. */
            ".mag-bar-cell { font-size: 72%; min-height: 0; padding: 0 1px; }\n"
            ".mag-bar-cell > text { min-height: 0; min-width: 0; }\n"
            ".mag-bar-bad { background-image: none; background-color: #fde7e7; }\n"
            ".mag-bar-label { font-size: 72%; }\n"
            ".mag-bar-btn { font-size: 90%; min-height: 0; padding: 1px 6px; }");
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

    hide_item_sync();   /* estado inicial: HideStationsWithoutData del .d */
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
        /* Ambos `resize_*` en TRUE: el ancho sobrante se reparte y los mapas
           crecen con la ventana. Los `shrink_*` quedan en TRUE (el default): con
           FALSE el hijo no baja de su minimo y ese minimo se SUMA al de la
           ventana; si el total supera la geometria maximizada que da el
           compositor, GTK adjunta un buffer mas grande que el configurado y
           Wayland corta la conexion (Error 71 al maximizar, AGENTS §7). */
        gtk_paned_set_resize_start_child(GTK_PANED(paned_maps), TRUE);
        gtk_paned_set_resize_end_child(GTK_PANED(paned_maps), TRUE);
        gtk_paned_set_shrink_start_child(GTK_PANED(paned_maps), TRUE);
        gtk_paned_set_shrink_end_child(GTK_PANED(paned_maps), TRUE);
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
    /* min_content_width acota el MINIMO aunque el contenido pida mas: sin esto
       las 16 columnas de la tabla fijan el minimo de TODA la ventana y el
       maximizado del compositor no lo satisface (AGENTS §7). */
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw_master), 320);
    gtk_widget_set_vexpand(sw_master, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_master), tree_global);

    GtkWidget *sw_detail = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_detail), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw_detail), 320);
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
    g_sw_ondas = sw_ondas;   /* el canvas de trazas mide su rejilla con este alto */
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
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw_list), 240);
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

    /* Pestaña raiz "Procesar": la tablita de la solucion es COMPARTIDA por las dos
       sub-pestañas (no se pierde al cambiar de `picking` a `magnitudes`), asi que
       va por encima del sub-notebook. */
    GtkWidget *page_procesar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Preview de la solucion: una grilla de 4 filas dividida en dos mitades
       (parametros a la izquierda, magnitudes a la derecha) para no dejar una
       franja vacia encima de Lat..RMS. Confirm/Discard a la derecha. Muestra la
       elegida en el detalle y, tras `locate`, la candidata (fondo rojo leve).
       SIEMPRE visible: sin seleccion muestra guiones, asi no salta el layout. */
    g_preview_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(g_preview_box, "preview-box");
    gtk_widget_set_margin_start(g_preview_box, 8);
    gtk_widget_set_margin_end(g_preview_box, 8);
    gtk_widget_set_margin_top(g_preview_box, 4);
    gtk_widget_set_margin_bottom(g_preview_box, 2);
    {
        GtkWidget *grid = gtk_grid_new();
        GtkWidget *pc = gtk_button_new_with_label("Confirm");
        GtkWidget *pd = gtk_button_new_with_label("Discard");
        GtkWidget *mags_title;
        GtkWidget *sep;
        GtkWidget *otime_row;
        int i, k;

        gtk_widget_set_hexpand(grid, TRUE);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
        /* Dos mitades en UNA grilla para que las filas queden alineadas:
             fila 0: titulo      | sep | "Magnitude"
             fila 1: otime       | sep | radios
             fila 2: cabeceras   | sep | cabeceras
             fila 3: valores     | sep | valores
           El separador ocupa su propia columna entre ambos bloques. */
        g_preview_title = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(g_preview_title), 0.0);
        gtk_widget_add_css_class(g_preview_title, "preview-title");
        gtk_grid_attach(GTK_GRID(grid), g_preview_title, 0, 0,
                        CSNHYPODBP_PREVIEW_NPARAMS, 1);
        /* Fila 1 izquierda: Origin time + indicador de calculo (spinner + texto).
           El indicador usa el mismo patron que el panel Mww: timer de 1 s y
           segundos transcurridos; vale para TODOS los localizadores. */
        g_preview_otime = gtk_label_new("Origin time: --");
        gtk_label_set_xalign(GTK_LABEL(g_preview_otime), 0.0);
        gtk_widget_add_css_class(g_preview_otime, "preview-otime");
        g_preview_spinner = gtk_spinner_new();
        gtk_widget_set_valign(g_preview_spinner, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(g_preview_spinner, FALSE);
        g_preview_status = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(g_preview_status), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(g_preview_status), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(g_preview_status), 48);
        gtk_widget_add_css_class(g_preview_status, "preview-busy");
        gtk_widget_set_visible(g_preview_status, FALSE);
        otime_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_hexpand(otime_row, TRUE);
        gtk_box_append(GTK_BOX(otime_row), g_preview_otime);
        gtk_box_append(GTK_BOX(otime_row), g_preview_spinner);
        gtk_box_append(GTK_BOX(otime_row), g_preview_status);
        gtk_grid_attach(GTK_GRID(grid), otime_row, 0, 1,
                        CSNHYPODBP_PREVIEW_NPARAMS, 1);
        mags_title = gtk_label_new("Magnitude");
        gtk_label_set_xalign(GTK_LABEL(mags_title), 0.0);
        gtk_widget_add_css_class(mags_title, "preview-title");
        gtk_grid_attach(GTK_GRID(grid), mags_title,
                        CSNHYPODBP_PREVIEW_NPARAMS + 1, 0,
                        CSNHYPODBP_PREVIEW_MAGS, 1);
        sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
        gtk_widget_set_margin_start(sep, 4);
        gtk_widget_set_margin_end(sep, 4);
        gtk_grid_attach(GTK_GRID(grid), sep, CSNHYPODBP_PREVIEW_NPARAMS, 0, 1, 4);

        for (i = 0; i < CSNHYPODBP_PREVIEW_NPARAMS; i++) {
            GtkWidget *h = gtk_label_new(csnhypodbp_preview_headers[i]);

            gtk_label_set_xalign(GTK_LABEL(h), 0.0);
            gtk_widget_set_hexpand(h, TRUE);
            gtk_widget_add_css_class(h, "preview-hdr");
            g_preview_val[i] = gtk_label_new("-");
            gtk_label_set_xalign(GTK_LABEL(g_preview_val[i]), 0.0);
            gtk_label_set_width_chars(GTK_LABEL(g_preview_val[i]), 9);
            gtk_widget_set_hexpand(g_preview_val[i], TRUE);
            gtk_widget_add_css_class(g_preview_val[i], "preview-val");
            gtk_grid_attach(GTK_GRID(grid), h, i, 2, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), g_preview_val[i], i, 3, 1, 1);
        }
        for (k = 0; k < CSNHYPODBP_PREVIEW_MAGS; k++) {
            GtkWidget *h;
            int col = CSNHYPODBP_PREVIEW_NPARAMS + 1 + k;

            i = CSNHYPODBP_PREVIEW_NPARAMS + k;
            g_preview_radio[k] = gtk_check_button_new();
            if (k > 0)   /* en grupo: se comportan como radios */
                gtk_check_button_set_group(GTK_CHECK_BUTTON(g_preview_radio[k]),
                                           GTK_CHECK_BUTTON(g_preview_radio[0]));
            gtk_widget_set_halign(g_preview_radio[k], GTK_ALIGN_CENTER);
            gtk_widget_add_css_class(g_preview_radio[k], "preview-radio");
            gtk_widget_set_tooltip_text(g_preview_radio[k],
                "Marca esta magnitud como la preferida al confirmar");
            gtk_widget_set_sensitive(g_preview_radio[k], FALSE);
            g_signal_connect(g_preview_radio[k], "toggled",
                             G_CALLBACK(on_preview_pref_toggled), GINT_TO_POINTER(k));
            gtk_grid_attach(GTK_GRID(grid), g_preview_radio[k], col, 1, 1, 1);
            h = gtk_label_new(csnhypodbp_preview_headers[i]);
            gtk_label_set_xalign(GTK_LABEL(h), 0.0);
            gtk_widget_set_hexpand(h, TRUE);
            gtk_widget_add_css_class(h, "preview-hdr");
            g_preview_val[i] = gtk_label_new("-");
            gtk_label_set_xalign(GTK_LABEL(g_preview_val[i]), 0.0);
            gtk_label_set_width_chars(GTK_LABEL(g_preview_val[i]), 9);
            gtk_widget_set_hexpand(g_preview_val[i], TRUE);
            gtk_widget_add_css_class(g_preview_val[i], "preview-val");
            gtk_grid_attach(GTK_GRID(grid), h, col, 2, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), g_preview_val[i], col, 3, 1, 1);
        }
        gtk_box_append(GTK_BOX(g_preview_box), grid);
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
    gtk_box_append(GTK_BOX(page_procesar), g_preview_box);
    csnhypodbp_preview_update();   /* estado inicial (sin seleccion: guiones) */

    gtk_box_append(GTK_BOX(page_waves), waves_hbox);

    canvas_global = ewgui_canvas_new();
    ewgui_canvas_set_draw(canvas_global, on_draw_signal, NULL);
    GtkWidget *canvas_widget = ewgui_canvas_widget(canvas_global);
    /* Minimo, no altura fija: el canvas llena el area disponible y on_draw_signal
       reparte el alto real entre las bandas (ver actualizar_altura_canvas). */
    gtk_widget_set_hexpand(canvas_widget, TRUE);
    gtk_widget_set_vexpand(canvas_widget, TRUE);
    gtk_widget_set_size_request(canvas_widget, -1, CSNHYPODBP_BAND_MIN + 60);
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
    /* La pagina NO se añade aqui: va dentro del sub-notebook de "Procesar", que
       se arma al final (cuando page_waves ya tiene todo su contenido). */

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

        /* HUD del filtro activo: nombre, banda, orden y tecla. Se acota con
           ellipsize + max-width-chars para no inflar el minimo de la ventana
           (AGENTS §7: el minimo de un GtkLabel es su texto completo). */
        g_filter_label = gtk_label_new("");
        gtk_label_set_ellipsize(GTK_LABEL(g_filter_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(g_filter_label), 48);
        gtk_label_set_xalign(GTK_LABEL(g_filter_label), 0.0);
        gtk_widget_add_css_class(g_filter_label, "dim-label");
        gtk_box_append(GTK_BOX(hbox_bottom), g_filter_label);
        csnhypodbp_filter_hud_update();

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

        /* Profundidad fija: la caja solo pesa con `Fix` marcado. El valor se
           aplica al confirmar (Enter o salir del campo), como las cajas de
           magnitudes; un texto invalido no se aplica y bloquea `locate`. */
        g_fix_depth_label = gtk_label_new("depth");
        gtk_box_append(GTK_BOX(hbox_bottom), g_fix_depth_label);
        g_fix_depth_entry = gtk_entry_new();
        gtk_editable_set_max_width_chars(GTK_EDITABLE(g_fix_depth_entry), 7);
        gtk_entry_set_placeholder_text(GTK_ENTRY(g_fix_depth_entry), "km");
        gtk_entry_set_input_purpose(GTK_ENTRY(g_fix_depth_entry), GTK_INPUT_PURPOSE_NUMBER);
        gtk_editable_set_alignment(GTK_EDITABLE(g_fix_depth_entry), 1.0);
        gtk_widget_set_sensitive(g_fix_depth_entry, FALSE);
        g_signal_connect(g_fix_depth_entry, "activate",
                         G_CALLBACK(on_fix_depth_activate), NULL);
        {
            GtkEventController *fc = gtk_event_controller_focus_new();
            g_signal_connect(fc, "leave",
                             G_CALLBACK(on_fix_depth_focus_leave), NULL);
            gtk_widget_add_controller(g_fix_depth_entry, fc);
        }
        gtk_box_append(GTK_BOX(hbox_bottom), g_fix_depth_entry);
        g_fix_depth_chk = gtk_check_button_new_with_label("Fix");
        g_signal_connect(g_fix_depth_chk, "toggled",
                         G_CALLBACK(on_fix_depth_toggled), NULL);
        gtk_widget_set_tooltip_text(g_fix_depth_chk,
            "Fija la profundidad de la solucion: el localizador no la resuelve");
        gtk_box_append(GTK_BOX(hbox_bottom), g_fix_depth_chk);

        g_locator_label = gtk_label_new("");
        gtk_box_append(GTK_BOX(hbox_bottom), g_locator_label);
        csnhypodbp_update_locator_label();

        gtk_box_append(GTK_BOX(hbox_bottom), btn_locators);
        gtk_box_append(GTK_BOX(hbox_bottom), btn_locate);
        g_btn_locate = btn_locate;
    }
    gtk_box_append(GTK_BOX(page_waves), hbox_bottom);

    /* Pestaña raiz "Procesar": agrupa dos sub-pestañas. `picking` es el contenido
       de siempre; `magnitudes` (F2/F3) es el procesado de magnitudes. */
    g_subnotebook = gtk_notebook_new();
    gtk_widget_set_vexpand(g_subnotebook, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(g_subnotebook), page_waves,
                             gtk_label_new("picking"));
    {
        /* Sub-pestaña `magnitudes`: barra con el selector de magnitud y la linea
           de estado, tabla (una fila por estacion) y, a la derecha, la traza
           procesada con las ventanas (F3). */
        GtkWidget *page_mags = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget *hbox_mag = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *mag_frame = gtk_frame_new(NULL);
        GtkWidget *sw_mag = gtk_scrolled_window_new();
        GtkWidget *mag_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget *mag_stack = gtk_stack_new();
        int k;

        gtk_widget_set_vexpand(mag_stack, TRUE);

        gtk_widget_set_margin_start(hbox_mag, 10);
        gtk_widget_set_margin_end(hbox_mag, 10);
        gtk_widget_set_margin_bottom(hbox_mag, 5);
        /* Selector del area central: `Estaciones` (tabla + traza por magnitud) y
           `Mww` (inversion de fase W, ON-DEMAND: no corre nada hasta Calculate). */
        {
            GtkWidget *mag_sw = gtk_stack_switcher_new();
            gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(mag_sw), GTK_STACK(mag_stack));
            gtk_widget_add_css_class(mag_sw, "mww-switcher");
            gtk_box_append(GTK_BOX(hbox_mag), mag_sw);
        }
        gtk_box_append(GTK_BOX(hbox_mag), gtk_label_new("magnitud:"));
        for (k = 0; k < CSNHYPODBP_MAG_NKIND; k++) {
            GtkWidget *r = gtk_check_button_new_with_label(csnhypodbp_magcfg_name(k));
            if (k > 0)
                gtk_check_button_set_group(GTK_CHECK_BUTTON(r),
                                           GTK_CHECK_BUTTON(g_mag_radio[0]));
            gtk_check_button_set_active(GTK_CHECK_BUTTON(r),
                                        k == g_mag_kind ? TRUE : FALSE);
            g_signal_connect(r, "toggled", G_CALLBACK(on_mag_kind_toggled),
                             GINT_TO_POINTER(k));
            g_mag_radio[k] = r;
            gtk_box_append(GTK_BOX(hbox_mag), r);
        }
        g_mag_status = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(g_mag_status), 0.0);
        gtk_widget_set_hexpand(g_mag_status, TRUE);
        gtk_box_append(GTK_BOX(hbox_mag), g_mag_status);
        gtk_box_append(GTK_BOX(page_mags), hbox_mag);

        gtk_widget_add_css_class(mag_frame, "detail-frame");
        gtk_widget_set_vexpand(mag_frame, TRUE);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_mag),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw_mag), 240);
        gtk_widget_set_vexpand(sw_mag, TRUE);
        g_mag_store = g_list_store_new(CSNHYPODBP_TYPE_MAG_ITEM);
        g_mag_sort = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(g_mag_store)), NULL);
        g_mag_sel = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(g_mag_sort)));
        gtk_single_selection_set_autoselect(g_mag_sel, FALSE);
        gtk_single_selection_set_can_unselect(g_mag_sel, TRUE);
        g_signal_connect(g_mag_sel, "notify::selected",
                         G_CALLBACK(on_mag_row_selected), NULL);
        g_mag_view = gtk_column_view_new(GTK_SELECTION_MODEL(g_mag_sel));
        gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(g_mag_view), TRUE);
        gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(g_mag_view), TRUE);
        gtk_widget_add_css_class(g_mag_view, "sta-table");
        add_mag_station_column(g_mag_view, "station");
        add_mag_text_column(g_mag_view, "dist[km]", "dist", "distv", 56,
                            EWGUI_TABLE_DESC);
        add_mag_text_column(g_mag_view, "ancla", "anchor", "anchor", 44,
                            EWGUI_TABLE_ASC);
        add_mag_text_column(g_mag_view, "t0[s]", "t0", "t0v", 44, EWGUI_TABLE_DESC);
        add_mag_text_column(g_mag_view, "t1[s]", "t1", "t1v", 44, EWGUI_TABLE_DESC);
        add_mag_text_column(g_mag_view, "mag", "mag", "magv", 46, EWGUI_TABLE_DESC);
        add_mag_text_column(g_mag_view, "res", "res", "resv", 46, EWGUI_TABLE_DESC);
        add_mag_text_column(g_mag_view, "stat", "stat", "stat", 30, EWGUI_TABLE_ASC);
        ewgui_table_enable_sorting(GTK_COLUMN_VIEW(g_mag_view), g_mag_sort, NULL, 0);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_mag), g_mag_view);
        gtk_frame_set_child(GTK_FRAME(mag_frame), sw_mag);
        gtk_box_append(GTK_BOX(mag_hbox), mag_frame);
        /* Traza procesada de la estacion elegida con la franja de la ventana de
           la magnitud activa: se arrastra un borde (o la franja entera) para
           ajustarla. La cadena por magnitud la construye csnhypodbp_magview. */
        g_mag_canvas = ewgui_canvas_new();
        ewgui_canvas_set_draw(g_mag_canvas, on_draw_mag, NULL);
        {
            GtkWidget *mw = ewgui_canvas_widget(g_mag_canvas);
            GtkWidget *sw_magcv = gtk_scrolled_window_new();
            GtkGesture *dg = gtk_gesture_drag_new();

            /* El alto lo fija csnhypodbp_mag_canvas_fit() segun el numero de
               canales de la estacion elegida: sin el scrolled window las filas de
               abajo quedaban fuera del area visible (se veia solo la primera). */
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw_magcv),
                                           GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
            gtk_widget_set_hexpand(sw_magcv, TRUE);
            gtk_widget_set_vexpand(sw_magcv, TRUE);
            /* Ancho minimo del canvas: sin el, la tabla de 8 columnas se lo comia
               y el grafico quedaba en unos pocos pixeles. */
            gtk_widget_set_size_request(sw_magcv, CSNHYPODBP_CANVAS_MIN_W, -1);
            gtk_widget_set_hexpand(mw, TRUE);
            gtk_widget_set_vexpand(mw, TRUE);
            gtk_widget_set_size_request(mw, CSNHYPODBP_CANVAS_MIN_W,
                                        CSNHYPODBP_BAND_MIN + 60);
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw_magcv), mw);
            gtk_box_append(GTK_BOX(mag_hbox), sw_magcv);
            g_signal_connect(dg, "drag-begin",  G_CALLBACK(on_mag_drag_begin), NULL);
            g_signal_connect(dg, "drag-update", G_CALLBACK(on_mag_drag_update), NULL);
            g_signal_connect(dg, "drag-end",    G_CALLBACK(on_mag_drag_end), NULL);
            gtk_widget_add_controller(mw, GTK_EVENT_CONTROLLER(dg));
            /* Zoom por dos clics sobre la traza (el arrastre vive en el handle). */
            {
                GtkGesture *mc = gtk_gesture_click_new();
                gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mc), 1);
                g_signal_connect(mc, "pressed", G_CALLBACK(on_mag_canvas_clicked), NULL);
                gtk_widget_add_controller(mw, GTK_EVENT_CONTROLLER(mc));
            }
            /* Puntero: dice si el clic va a arrastrar la franja (grab), mover un
               borde (ew-resize) o hacer zoom (crosshair). */
            {
                GtkEventController *mmo = gtk_event_controller_motion_new();
                g_signal_connect(mmo, "motion", G_CALLBACK(on_mag_canvas_motion), NULL);
                g_signal_connect(mmo, "leave", G_CALLBACK(on_mag_canvas_leave), NULL);
                gtk_widget_add_controller(mw, mmo);
            }
        }
        gtk_widget_set_vexpand(mag_hbox, TRUE);
        /* Area central = pila: la pagina `Mww` trae su propio panel (parametros,
           Calculate/Cancel, resumen + playera y la salida completa del motor). */
        gtk_stack_add_titled(GTK_STACK(mag_stack), mag_hbox, "mag", "Estaciones");
        gtk_stack_add_titled(GTK_STACK(mag_stack), csnhypodbp_mww_panel_new(), "mww", "Mww");
        gtk_box_append(GTK_BOX(page_mags), mag_stack);

        /* Eje temporal abajo (mismo patron que el de `picking`): ticks UTC y
           arrastre para acercar; doble clic recentra la franja entera. */
        g_mag_axis_canvas = ewgui_canvas_new();
        ewgui_canvas_set_draw(g_mag_axis_canvas, on_draw_mag_axis, NULL);
        {
            GtkWidget *axw = ewgui_canvas_widget(g_mag_axis_canvas);
            GtkGesture *adg = gtk_gesture_drag_new();
            GtkGesture *acl = gtk_gesture_click_new();

            gtk_widget_set_size_request(axw, -1, 26);
            g_signal_connect(adg, "drag-begin", G_CALLBACK(on_mag_axis_drag_begin), NULL);
            g_signal_connect(adg, "drag-update", G_CALLBACK(on_mag_axis_drag_update), NULL);
            g_signal_connect(adg, "drag-end", G_CALLBACK(on_mag_axis_drag_end), NULL);
            gtk_widget_add_controller(axw, GTK_EVENT_CONTROLLER(adg));
            g_signal_connect(acl, "pressed", G_CALLBACK(on_mag_axis_clicked), NULL);
            gtk_widget_add_controller(axw, GTK_EVENT_CONTROLLER(acl));
            gtk_box_append(GTK_BOX(page_mags), axw);
        }

        /* Barra inferior, mismo patron que la de `picking` (spacer con hexpand y
           todo el grupo a la DERECHA). Las UNIDADES van solo en la cabecera de
           cada grupo; cada caja se rotula solo `min`/`max`, para que no haya duda
           de a que caja pertenece cada texto. Sin casilla: el rango que se ve ES
           el que se aplica (confirmar un campo con Enter, o salir de el, activa
           el override de la magnitud activa) y `reset default` devuelve las
           cuatro al .d.
           Va dentro de un scrolled window: asi su ancho no infla el minimo de la
           ventana, que era lo que dejaba la columna de mapas sin ancho. */
        {
            GtkWidget *barsw = gtk_scrolled_window_new();
            GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            int j;

            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(barsw),
                                           GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(barsw), bar);
            /* La barra (y el eje, mas arriba) se ocultan en la pagina Mww. */
            g_mag_bar_w = barsw;
            g_signal_connect(mag_stack, "notify::visible-child-name",
                             G_CALLBACK(on_mag_stack_changed), NULL);
            gtk_widget_set_margin_start(bar, 10);
            gtk_widget_set_margin_end(bar, 10);
            gtk_widget_set_margin_bottom(bar, 6);
            gtk_widget_set_hexpand(spacer, TRUE);
            gtk_box_append(GTK_BOX(bar), spacer);
            gtk_box_append(GTK_BOX(bar), gtk_label_new("ventanas:"));
            for (j = 0; j < 3; j++) {
                const char *lbl = (j == 0) ? "A" : (j == 1) ? "M" : "Teo";

                g_magf_chk[j] = gtk_check_button_new_with_label(lbl);
                gtk_check_button_set_active(GTK_CHECK_BUTTON(g_magf_chk[j]),
                                            (j == 2) ? FALSE : TRUE);
                gtk_widget_set_tooltip_text(g_magf_chk[j],
                    "Origen de la ventana que entra al calculo: A = default del .d "
                    "con P observada, M = ajustada a mano, Teo = ancla teorica.");
                g_signal_connect(g_magf_chk[j], "toggled",
                                 G_CALLBACK(on_mag_filter_toggled), GINT_TO_POINTER(j));
                gtk_box_append(GTK_BOX(bar), g_magf_chk[j]);
            }
            gtk_box_append(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

            /* Grupo distancia: `distancia [°]  min [ ]  max [ ]`. Las cajas son
               `GtkEntry` (sin los +/- del spin): ver `mag_ov_entry_new`. */
            gtk_box_append(GTK_BOX(bar), gtk_label_new("distancia [°]"));
            gtk_box_append(GTK_BOX(bar), gtk_label_new("min"));
            g_mag_ov_min = mag_ov_entry_new(CSNHYPODBP_MAG_OV_MIN_DELTA,
                "Distancia epicentral MINIMA en grados. Al confirmar el campo "
                "(Enter o salir de el) la barra se aplica a la magnitud activa.");
            gtk_box_append(GTK_BOX(bar), g_mag_ov_min);
            gtk_box_append(GTK_BOX(bar), gtk_label_new("max"));
            g_mag_ov_max = mag_ov_entry_new(CSNHYPODBP_MAG_OV_MAX_DELTA,
                "Distancia epicentral MAXIMA en grados.");
            gtk_box_append(GTK_BOX(bar), g_mag_ov_max);
            gtk_box_append(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

            /* Grupo profundidad del EVENTO: `prof. [km]  min [ ]  max [ ]`. */
            gtk_box_append(GTK_BOX(bar), gtk_label_new("prof. [km]"));
            gtk_box_append(GTK_BOX(bar), gtk_label_new("min"));
            g_mag_ov_dmin = mag_ov_entry_new(CSNHYPODBP_MAG_OV_MIN_DEPTH,
                "Profundidad MINIMA del evento para esta magnitud, en km "
                "(0 = sin piso).");
            gtk_box_append(GTK_BOX(bar), g_mag_ov_dmin);
            gtk_box_append(GTK_BOX(bar), gtk_label_new("max"));
            g_mag_ov_depth = mag_ov_entry_new(CSNHYPODBP_MAG_OV_MAX_DEPTH,
                "Profundidad MAXIMA del evento para esta magnitud, en km (0 = sin "
                "corte, 999 = sin corte practico). El veto por defecto de Ms es "
                "60 km.");
            gtk_box_append(GTK_BOX(bar), g_mag_ov_depth);
            gtk_box_append(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

            g_mag_ov_reset = gtk_button_new_with_label("reset default");
            gtk_widget_set_tooltip_text(g_mag_ov_reset,
                "Devuelve las cuatro magnitudes a la puerta del .d de csnmags "
                "(tambien se resetea al elegir otra solucion en Origins).");
            g_signal_connect(g_mag_ov_reset, "clicked",
                             G_CALLBACK(on_mag_ov_reset_clicked), NULL);
            gtk_box_append(GTK_BOX(bar), g_mag_ov_reset);

            /* `calculate` = la magnitud activa; `calculate all` = las cuatro.
               Una sola corrida de csnmags en los dos casos (ver locate_start). */
            g_mag_btn_calc = gtk_button_new_with_label("calculate");
            gtk_widget_set_tooltip_text(g_mag_btn_calc,
                "Recalcula la magnitud ACTIVA con las ventanas que ajustaste "
                "(no relocaliza). El resultado entra como candidata.");
            g_signal_connect(g_mag_btn_calc, "clicked",
                             G_CALLBACK(on_mag_calc_clicked), NULL);
            gtk_box_append(GTK_BOX(bar), g_mag_btn_calc);
            g_mag_btn_calc_all = gtk_button_new_with_label("calculate all");
            gtk_widget_set_tooltip_text(g_mag_btn_calc_all,
                "Recalcula las CUATRO magnitudes con las ventanas ajustadas "
                "(no relocaliza).");
            g_signal_connect(g_mag_btn_calc_all, "clicked",
                             G_CALLBACK(on_mag_calc_all_clicked), NULL);
            gtk_box_append(GTK_BOX(bar), g_mag_btn_calc_all);

            /* Cada caja se aplica al confirmar: las señales las conecta
               `mag_ov_entry_new`. Compacta la barra: rotulos y cajas al 72% (el
               usuario la vio demasiado chica despues del 60%) y los tres botones
               al 90%. Se hace con clases CSS propias (`.mag-bar*`, ver la hoja
               del modulo) y NO con Pango. Los checkboxes A/M/Teo no se tocan. */
            {
                GtkWidget *cw2;

                gtk_widget_add_css_class(bar, "mag-bar");
                for (cw2 = gtk_widget_get_first_child(bar); cw2;
                     cw2 = gtk_widget_get_next_sibling(cw2)) {
                    if (GTK_IS_EDITABLE(cw2)) {
                        gtk_widget_add_css_class(cw2, "mag-bar-cell");
                    } else if (GTK_IS_LABEL(cw2)) {
                        gtk_widget_add_css_class(cw2, "mag-bar-label");
                    }
                }
                gtk_widget_add_css_class(g_mag_ov_reset, "mag-bar-btn");
                gtk_widget_add_css_class(g_mag_btn_calc, "mag-bar-btn");
                gtk_widget_add_css_class(g_mag_btn_calc_all, "mag-bar-btn");
            }
            gtk_box_append(GTK_BOX(page_mags), barsw);
            csnhypodbp_mag_ov_sync();   /* la barra arranca con la puerta del .d */
        }

        gtk_notebook_append_page(GTK_NOTEBOOK(g_subnotebook), page_mags,
                                 gtk_label_new("magnitudes"));
    }
    gtk_box_append(GTK_BOX(page_procesar), g_subnotebook);
    g_page_procesar = page_procesar;
    gtk_notebook_append_page(GTK_NOTEBOOK(g_notebook), page_procesar,
                             gtk_label_new("Procesar"));

    cargar_sismos_iniciales(tree_global);
    cargar_sismos_db();
    csnhypodbp_load_manual_state();
    csnhypodbp_sta_sel_reset();
    rebuild_station_list();
    gtk_window_present(GTK_WINDOW(window_global));
    dbg_maximize_hook();

    g_timeout_add(1000, ew_background_tasks, NULL);
    g_timeout_add(500, escuchar_anillo_earthworm, tree_global);
    g_timeout_add(1000, waveform_reload_timer, NULL);
}

/* -------------------------------------------------------------------------
 * Hook de diagnostico del maximizado (ver AGENTS §7).
 * `CSNHYPODBP_MAXIMIZE_MS=<ms>` maximiza/des-maximiza 5 ciclos y deja la ventana
 * maximizada: reproduce y verifica, sin clics, el cierre por `Error 71` que
 * sufren los modulos GTK4 bajo WSLg cuando el minimo de la ventana no cabe en la
 * geometria maximizada. Sin la variable no hace absolutamente nada.
 * ------------------------------------------------------------------------- */
static int g_dbg_max_toggles;
static gboolean dbg_maximize_toggle(gpointer data)
{
    GtkWindow *w = GTK_WINDOW(data);

    if (g_dbg_max_toggles++ >= 10) {          /* 5 ciclos = 10 toggles */
        gtk_window_maximize(w);               /* termina en el estado peligroso */
        return G_SOURCE_REMOVE;
    }
    if (gtk_window_is_maximized(w))
        gtk_window_unmaximize(w);
    else
        gtk_window_maximize(w);
    return G_SOURCE_CONTINUE;
}
static void dbg_maximize_hook(void)
{
    const char *ms = getenv("CSNHYPODBP_MAXIMIZE_MS");
    int v = ms ? atoi(ms) : 0;

    if (v > 0 && window_global)
        g_timeout_add((guint)v, dbg_maximize_toggle, window_global);
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
    logit("t", "csnhypodbp: config channelsOrder=[%s] componentsOrder=[%s] tracesPerScreen=%d HideStationsWithoutData=%d\n",
          g_channels_order, g_components_order, g_traces_per_screen, g_hide_empty);
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
