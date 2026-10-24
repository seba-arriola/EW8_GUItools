/*
 * bpq_config.c - Port de ReadConfig()/maps_print_config()/maps_check_profiles()
 * de csnhypodbp_main.c (lineas 321-541, 562-652 y 656-702) a un struct BpqConfig
 * sin globales y sin GTK.
 *
 * Se mantiene la semantica EXACTA del original:
 *   - mismo conjunto de claves, mismos defaults y mismos tipos;
 *   - clave desconocida -> se ignora (continue);
 *   - clave obligatoria ausente -> error (aqui, con la lista de las que faltan);
 *   - linea `Profile` invalida o `Filter*`/`StaLta` invalida -> error fatal;
 *   - ProfileFile (si se declara) REEMPLAZA a las claves Profile;
 *   - los parametros de magnitud salen del .d de csnmags (MagConfigFile); si no
 *     se puede leer, quedan los defaults de fabrica y se avisa (no es fatal).
 *
 * Diferencias deliberadas (documentadas):
 *   - los avisos van a stderr en lugar de logit(): esto corre ANTES de
 *     logit_init (ver AGENTS §6: logit con flag 0 es mudo y sin EW_LOG mata).
 *   - --state-dir sobrescribe StateFile/ManualStateFile/MwwWorkDir para que el
 *     prototipo no pise el estado de la GUI GTK.
 */
#include "bpq_capi.h"

#include <stdlib.h>
#include <string.h>

#include <kom.h>

#include "ewgui/overlay.h"   /* EWGUI_OVERLAY_MAX_FILES/NAME_MAX (contrato) */

/* Los .h puros que declaran las funciones de las claves con lista. */
#include "csnhypodbp_filtercfg.h"
#include "csnhypodbp_locator.h"
#include "csnhypodbp_magcfg.h"
#include "csnhypodbp_profile.h"
#include "csnhypodbp_traces.h"
#include "csnhypodbp_view.h"

/* Comprobacion de contrato: si cambian las constantes del repo, falla el build. */
typedef char bpq_chk_overlay_files[(BPQ_MAX_OVERLAY_FILES == EWGUI_OVERLAY_MAX_FILES) ? 1 : -1];
typedef char bpq_chk_overlay_name [(BPQ_MAX_OVERLAY_NAME  == EWGUI_OVERLAY_NAME_MAX ) ? 1 : -1];

/*
 * Las globales `g_sds_enabled`/`g_sds_root` que csnhypodbp_sds.c declara
 * `extern` viven en bpq_sds_globals.c: asi los tests que enlazan la capa de
 * ondas no arrastran todo este port de configuracion. Aqui solo se fijan.
 */
extern int  g_sds_enabled;
extern char g_sds_root[512];

/* Nombres de las claves que este parser conoce (para el test de paridad). */
static const char *const kKeys[] = {
    "MyModuleId", "InRing", "OutRing", "HeartBeatInt", "LogFile", "StaFile",
    "StateFile", "InRingRaw", "InRingPick", "ConfRing", "TauModel",
    "PickCaptureWindowSec", "MaxDistKm", "MagFetchSeconds", "LocateKeepTmp",
    "GroupPickTolSec", "GroupMinMatchPicks", "GroupMaxSolutions", "DbEnvFile",
    "MagConfigFile", "MwwConfigFile", "MwwWorkDir", "DbHistoryHours",
    "StoreRoot", "FetchThreads", "ModuleLabel", "channelsOrder",
    "componentsOrder", "tracesPerScreen", "locator", "ManualStateFile",
    "LocatorTimeoutSec", "PrefetchNeighbors", "HideStationsWithoutData",
    "WaveCacheMB", "TheoModel", "TheoMaxDeltaKm",
    "SearchMaxEvents", "SdsEnabled", "SdsRoot", "SdsFallbackHours",
    "pColor", "sColor", "pColorAuto", "pColorTheo", "sColorAuto", "sColorTheo",
    "MapImageFile", "MapMinSpanDeg", "MapZoom", "MapSplit", "OverlayFile",
    "OverlayColor", "OverlayWidth", "OverlayHide", "ProfileDataDir",
    "ProfileFile", "ProfileCols", "ProfileDepthMinKm", "Profile",
    "Filter", "FilterMenu", "FilterRaw", "FilterPhase", "StaLta",
    /* no son del .d de csnhypodbp, pero el prototipo las acepta: */
    "Fix", "FixedDepthKm",
    NULL
};

/* kom no tiene k_rest(): k_str() devuelve UN token. Las listas separadas por
 * espacios se concatenan aqui. El ultimo k_str() deja ierr=-17 (fin de linea) y
 * se limpia para que el `if (k_err())` del bucle no aborte (igual que
 * csnhypodbp_main.c:288-300). */
static void k_rest_join(char *out, size_t n)
{
    char  *t;
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

/* Lee un color RRGGBB y lo aplica al canal/Origen indicado. Un valor invalido no
 * toca el destino (sigue heredando del manual). csnhypodbp_main.c:304-316. */
static void read_pick_color(const char *key, const char *str,
                            CsnhypodbpColorSet *set, CsnhypodbpPickOrigin origin)
{
    double rgb[3];
    if (!str) return;
    if (csnhypodbp_view_parse_color(str, rgb) != 0) {
        fprintf(stderr, "csnhypodbpqt: %s <%s> invalido, uso el color heredado\n",
                key, str);
        return;
    }
    csnhypodbp_color_set_override(set, origin, rgb);
}

/* Copia un token en un destino de tamano fijo, truncando con NUL. */
static void set_str(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0 || !src) return;
    snprintf(dst, n, "%s", src);
}

/* Defaults de fabrica: los mismos inicializadores de csnhypodbp_main.c:13-272. */
static void config_defaults(BpqConfig *c)
{
    static const double red[3]   = { 1.0, 0.0, 0.0 };   /* pColor: rojo */
    static const double green[3] = { 0.0, 0.6, 0.0 };   /* sColor: verde */

    memset(c, 0, sizeof(*c));
    set_str(c->mag_config_file, sizeof(c->mag_config_file), "csnmags.d");
    set_str(c->mww_config_file, sizeof(c->mww_config_file), "csnwphase.d");
    set_str(c->mww_workdir,     sizeof(c->mww_workdir),     "/tmp/csnhypodbp_mww");
    set_str(c->manual_state_file, sizeof(c->manual_state_file),
            "csnhypodbp_manual.events");
    /* Modelo de tiempos de viaje de la P teorica (default de la GTK). */
    set_str(c->tau_model, sizeof(c->tau_model), "iasp91");
    c->db_history_hours        = 24;
    c->pick_capture_window_s   = 600.0;
    c->group_pick_tol_s        = 0.1;
    c->group_min_match_picks   = 3;
    c->group_max_solutions     = 100;
    set_str(c->channels_order,   sizeof(c->channels_order),   "HH BH HN LH");
    set_str(c->components_order, sizeof(c->components_order), "Z N E");
    c->traces_per_screen       = CSNHYPODBP_TRACES_PER_SCREEN_MIN;

    /* locators/colores */
    c->n_locators = 0;
    csnhypodbp_color_set_init(&c->colors_p, red);
    csnhypodbp_color_set_init(&c->colors_s, green);

    c->max_dist_km             = 500.0;
    c->mag_fetch_s             = 700.0;   /* Ms necesita ~400 s de ventana */
    c->locator_timeout_s       = 600;
    c->prefetch_neighbors      = 2;
    c->wave_cache_mb           = 64;   /* ~450 canales de 120 s a 100 Hz */
    c->theo_model              = 1;    /* capas de hyp2000 (dominio regional) */
    /* 500 km = el MaxDistKm del `locate`: el dominio en que la solucion se
     * refina con este mismo modelo de capas. Mas alla manda IASP91. */
    c->theo_max_delta_km       = 500.0;
    c->hide_sta_without_data   = 0;
    c->fetch_threads           = 4;

    /* mapas */
    c->map_min_span_deg        = 5.0;
    c->map_zoom_factor         = 1.0;
    c->map_split_frac          = 0.7;
    c->overlay_width           = 0.0;

    /* perfiles */
    c->profile_lon_col         = 3;
    c->profile_val_col         = 7;
    c->profile_depth_min_km    = 200.0;

    /* busqueda / SDS */
    c->search_max_events       = CSNHYPODBP_SEARCH_DEFAULT_LIMIT;
    c->sds_enabled             = 1;
    set_str(c->sds_root, sizeof(c->sds_root), "/mnt/sds/archive");
    c->sds_fallback_hours      = 168;

    /* profundidad fija (estado inicial de la GUI) */
    c->fix_depth_on            = 0;
    c->fix_depth_km            = -1.0;

    csnhypodbp_filtercfg_defaults(&c->filtercfg);
    csnhypodbp_magcfg_defaults(&c->magcfg);
}

/* Aplica el override --state-dir sobre las tres rutas de runtime. */
static void apply_state_dir(BpqConfig *c, const char *dir)
{
    if (!dir || !dir[0]) return;
    set_str(c->state_dir, sizeof(c->state_dir), dir);
    snprintf(c->state_file, sizeof(c->state_file), "%s/csnloc.events", dir);
    snprintf(c->manual_state_file, sizeof(c->manual_state_file),
             "%s/csnhypodbp_manual.events", dir);
    snprintf(c->mww_workdir, sizeof(c->mww_workdir), "%s/mww", dir);
}

BpqConfig *bpq_config_load(const char *path, const char *state_dir_or_null,
                           char *err, size_t errn)
{
    BpqConfig *c;
    int  ncommand = 7, i, nmiss = 0;
    char init[8] = {0};
    char *com, *str;

    if (err && errn) err[0] = '\0';
    if (!path || !path[0]) {
        if (err) snprintf(err, errn, "falta la ruta del archivo .d");
        return NULL;
    }
    c = (BpqConfig *)calloc(1, sizeof(*c));
    if (!c) {
        if (err) snprintf(err, errn, "sin memoria para BpqConfig");
        return NULL;
    }
    config_defaults(c);

    if (!k_open(path)) {
        if (err) snprintf(err, errn, "no se pudo abrir el archivo config <%s>", path);
        free(c);
        return NULL;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if (k_its("MyModuleId")) {
            str = k_str(); if (str) set_str(c->my_mod_name, sizeof(c->my_mod_name), str);
            init[0] = 1;
        }
        else if (k_its("InRing")) {
            str = k_str(); if (str) set_str(c->in_ring, sizeof(c->in_ring), str);
            init[1] = 1;
        }
        else if (k_its("OutRing")) {
            str = k_str(); if (str) set_str(c->out_ring, sizeof(c->out_ring), str);
            init[2] = 1;
        }
        else if (k_its("HeartBeatInt")) { c->heartbeat_s = k_int(); init[3] = 1; }
        else if (k_its("LogFile"))      { c->logfile = k_int();     init[4] = 1; }
        else if (k_its("StaFile")) {
            str = k_str(); if (str) set_str(c->sta_file, sizeof(c->sta_file), str);
            init[5] = 1;
        }
        else if (k_its("StateFile")) {
            str = k_str(); if (str) set_str(c->state_file, sizeof(c->state_file), str);
            init[6] = 1;
        }
        else if (k_its("InRingRaw"))  { str = k_str(); if (str) set_str(c->in_ring_raw,  sizeof(c->in_ring_raw),  str); }
        else if (k_its("InRingPick")) { str = k_str(); if (str) set_str(c->in_ring_pick, sizeof(c->in_ring_pick), str); }
        else if (k_its("ConfRing"))   { str = k_str(); if (str) set_str(c->conf_ring,    sizeof(c->conf_ring),    str); }
        else if (k_its("TauModel"))   { str = k_str(); if (str) set_str(c->tau_model,    sizeof(c->tau_model),    str); }
        else if (k_its("PickCaptureWindowSec")) { c->pick_capture_window_s = k_val(); }
        else if (k_its("MaxDistKm")) {
            double v = k_val(); if (v > 0.0) c->max_dist_km = v;
        }
        else if (k_its("MagFetchSeconds")) {
            double v = k_val(); if (v > 0.0) c->mag_fetch_s = v;
        }
        else if (k_its("LocateKeepTmp")) {
            /* Solo se guarda: el setter del nucleo (csnhypodbp_locate_keep_tmp)
             * lo aplica el wrapper de `locate` antes de correr la cadena, para
             * no arrastrar csnhypodbp_locate.o al test de configuracion. */
            c->locate_keep_tmp = k_int() ? 1 : 0;
        }
        else if (k_its("GroupPickTolSec"))    { c->group_pick_tol_s = k_val(); }
        else if (k_its("GroupMinMatchPicks")) { c->group_min_match_picks = k_int(); }
        else if (k_its("GroupMaxSolutions"))  { c->group_max_solutions = k_int(); }
        else if (k_its("DbEnvFile"))     { str = k_str(); if (str) set_str(c->db_env_file,     sizeof(c->db_env_file),     str); }
        else if (k_its("MagConfigFile")) { str = k_str(); if (str) set_str(c->mag_config_file, sizeof(c->mag_config_file), str); }
        else if (k_its("MwwConfigFile")) { str = k_str(); if (str) set_str(c->mww_config_file, sizeof(c->mww_config_file), str); }
        else if (k_its("MwwWorkDir"))    { str = k_str(); if (str) set_str(c->mww_workdir,     sizeof(c->mww_workdir),     str); }
        else if (k_its("DbHistoryHours")) { c->db_history_hours = k_int(); }
        else if (k_its("StoreRoot")) {
            str = k_str(); if (str) set_str(c->store_root, sizeof(c->store_root), str);
        }
        else if (k_its("FetchThreads")) {
            c->fetch_threads = k_int();
            if (c->fetch_threads < 1) c->fetch_threads = 1;
        }
        else if (k_its("ModuleLabel")) {
            int mid = k_int();
            str = k_str();
            if (str && c->n_modlabels < BPQ_MAX_MODLABELS) {
                c->modlabels[c->n_modlabels].id = mid;
                snprintf(c->modlabels[c->n_modlabels].label,
                         sizeof(c->modlabels[0].label), "%s", str);
                c->n_modlabels++;
            }
        }
        else if (k_its("channelsOrder")) {
            char buf[128];
            k_rest_join(buf, sizeof(buf));
            if (buf[0]) set_str(c->channels_order, sizeof(c->channels_order), buf);
        }
        else if (k_its("componentsOrder")) {
            char buf[64];
            k_rest_join(buf, sizeof(buf));
            if (buf[0]) set_str(c->components_order, sizeof(c->components_order), buf);
        }
        else if (k_its("tracesPerScreen")) {
            /* Solo 1..3 (un digito). Cualquier otra cosa no se aplica y se avisa
             * conservando el valor previo (csnhypodbp_main.c:369-387). */
            char *tok = k_str();
            int   v = 0;
            (void)k_err();
            if (tok && tok[0] != '\0' && tok[1] == '\0')
                v = csnhypodbp_traces_per_screen_norm(tok[0] - '0');
            if (v > 0)
                c->traces_per_screen = v;
            else
                fprintf(stderr, "csnhypodbpqt: tracesPerScreen <%s> fuera de [%d,%d], uso %d\n",
                        tok && tok[0] ? tok : "(none)",
                        CSNHYPODBP_TRACES_PER_SCREEN_MIN,
                        CSNHYPODBP_TRACES_PER_SCREEN_MAX, c->traces_per_screen);
        }
        else if (k_its("locator")) {
            char *nm = k_str(), *bn = k_str(), *cf = k_str();
            if (nm && bn && cf)
                csnhypodbp_locator_add(c->locators, &c->n_locators, BPQ_MAX_LOCATORS,
                                       nm, bn, cf);
        }
        else if (k_its("ManualStateFile")) {
            str = k_str(); if (str) set_str(c->manual_state_file, sizeof(c->manual_state_file), str);
        }
        else if (k_its("LocatorTimeoutSec"))  { c->locator_timeout_s = k_int(); }
        else if (k_its("PrefetchNeighbors"))  { c->prefetch_neighbors = k_int(); }
        else if (k_its("WaveCacheMB")) {
            c->wave_cache_mb = k_int();
            if (c->wave_cache_mb < 0) c->wave_cache_mb = 0;
            if (c->wave_cache_mb > 4096) c->wave_cache_mb = 4096;
        }
        else if (k_its("TheoModel")) {
            char *v = k_str();

            if (v) c->theo_model = (strcmp(v, "iasp91") == 0) ? 0 : 1;
        }
        else if (k_its("TheoMaxDeltaKm")) {
            double v = k_val();

            if (v > 0.0) c->theo_max_delta_km = v;
        }
        else if (k_its("HideStationsWithoutData")) { c->hide_sta_without_data = k_int() ? 1 : 0; }
        else if (k_its("SearchMaxEvents")) {
            c->search_max_events = k_int();
            if (c->search_max_events <= 0)
                c->search_max_events = CSNHYPODBP_SEARCH_DEFAULT_LIMIT;
        }
        else if (k_its("SdsEnabled")) { c->sds_enabled = k_int() ? 1 : 0; }
        else if (k_its("SdsRoot")) {
            str = k_str(); if (str) set_str(c->sds_root, sizeof(c->sds_root), str);
        }
        else if (k_its("SdsFallbackHours")) {
            c->sds_fallback_hours = k_int();
            if (c->sds_fallback_hours < 0) c->sds_fallback_hours = 0;
        }
        else if (k_its("pColor")) {
            str = k_str();
            read_pick_color("pColor", str, &c->colors_p, CSNHYPODBP_PICK_MANUAL);
        }
        else if (k_its("sColor")) {
            str = k_str();
            read_pick_color("sColor", str, &c->colors_s, CSNHYPODBP_PICK_MANUAL);
        }
        else if (k_its("pColorAuto")) {
            str = k_str();
            read_pick_color("pColorAuto", str, &c->colors_p, CSNHYPODBP_PICK_AUTO);
        }
        else if (k_its("pColorTheo")) {
            str = k_str();
            read_pick_color("pColorTheo", str, &c->colors_p, CSNHYPODBP_PICK_THEO);
        }
        else if (k_its("sColorAuto")) {
            str = k_str();
            read_pick_color("sColorAuto", str, &c->colors_s, CSNHYPODBP_PICK_AUTO);
        }
        else if (k_its("sColorTheo")) {
            str = k_str();
            read_pick_color("sColorTheo", str, &c->colors_s, CSNHYPODBP_PICK_THEO);
        }
        /* --- mapa de planta --- */
        else if (k_its("MapImageFile")) {
            str = k_str(); if (str) set_str(c->map_image_file, sizeof(c->map_image_file), str);
        }
        else if (k_its("MapMinSpanDeg")) {
            double v = k_val(); if (v > 0.0) c->map_min_span_deg = v;
        }
        else if (k_its("MapZoom")) {
            double v = k_val();
            if (v > 0.0 && v <= 50.0) {
                c->map_zoom_factor = v;
            } else {
                fprintf(stderr, "csnhypodbpqt: MapZoom <%.4g> fuera de (0,50], uso %.2f\n",
                        v, c->map_zoom_factor);
            }
        }
        else if (k_its("MapSplit")) {
            double f = 0.0;
            str = k_str();
            if (csnhypodbp_map_split_parse(str, &f)) {
                if (f < 0.1) f = 0.1;
                if (f > 0.9) f = 0.9;
                c->map_split_frac = f;
            } else {
                fprintf(stderr, "csnhypodbpqt: MapSplit <%s> invalido (usa `a/b`, "
                                "p.ej. 7/3 sin espacios); uso %.3f\n",
                        str ? str : "(none)", c->map_split_frac);
            }
        }
        else if (k_its("OverlayFile")) {
            str = k_str();
            if (str && c->n_overlay_files < BPQ_MAX_OVERLAY_FILES)
                set_str(c->overlay_files[c->n_overlay_files++], BPQ_MAX_STR, str);
        }
        else if (k_its("OverlayColor")) {
            char *lyr = k_str(), *hex = k_str();
            if (lyr && hex && c->n_overlay_colors < BPQ_MAX_OVERLAY_FILES) {
                set_str(c->overlay_color_layer[c->n_overlay_colors],
                        BPQ_MAX_OVERLAY_NAME, lyr);
                set_str(c->overlay_color_hex[c->n_overlay_colors], 8, hex);
                c->n_overlay_colors++;
            }
        }
        else if (k_its("OverlayWidth")) { c->overlay_width = k_val(); }
        else if (k_its("OverlayHide")) {
            str = k_str();
            if (str && c->n_overlay_hides < BPQ_MAX_OVERLAY_FILES)
                set_str(c->overlay_hide_layer[c->n_overlay_hides++],
                        BPQ_MAX_OVERLAY_NAME, str);
        }
        /* --- perfiles de profundidad --- */
        else if (k_its("ProfileDataDir")) {
            str = k_str(); if (str) set_str(c->profile_data_dir, sizeof(c->profile_data_dir), str);
        }
        else if (k_its("ProfileFile")) {
            str = k_str(); if (str) set_str(c->profile_file, sizeof(c->profile_file), str);
        }
        else if (k_its("ProfileCols")) {
            int lc = k_int(), vc = k_int();
            if (lc > 0 && vc > 0) { c->profile_lon_col = lc; c->profile_val_col = vc; }
        }
        else if (k_its("ProfileDepthMinKm")) {
            double v = k_val(); if (v > 0.0) c->profile_depth_min_km = v;
        }
        else if (k_its("Profile")) {
            /* Profile <name> <lon1> <lat1> <lon2> <lat2> [slab] [topo] */
            char *nm = k_str();
            char  rest[512] = "", perr[256] = "";
            k_rest_join(rest, sizeof(rest));
            if (!csnhypodbp_profile_add_tokens(nm, rest, &c->profiles, perr, sizeof(perr))) {
                if (err) snprintf(err, errn, "linea Profile invalida: %s",
                                  perr[0] ? perr : "tokens invalidos");
                else fprintf(stderr, "csnhypodbpqt: linea Profile invalida: %s\n",
                             perr[0] ? perr : "tokens invalidos");
                k_close();
                free(c);
                return NULL;
            }
        }
        else if (k_its("Filter") || k_its("FilterMenu") || k_its("FilterRaw") ||
                 k_its("FilterPhase") || k_its("StaLta")) {
            char rest[256] = "", ferr[256] = "";
            k_rest_join(rest, sizeof(rest));
            if (csnhypodbp_filtercfg_add(&c->filtercfg, com, rest, ferr, sizeof(ferr)) != 0) {
                if (err) snprintf(err, errn, "linea %s invalida: %s", com,
                                  ferr[0] ? ferr : "valor invalido");
                k_close();
                free(c);
                return NULL;
            }
        }
        /* Claves del prototipo (no del .d original): profundidad fija inicial. */
        else if (k_its("Fix"))          { c->fix_depth_on = k_int() ? 1 : 0; }
        else if (k_its("FixedDepthKm")) { c->fix_depth_km = k_val(); }
        else { continue; }

        if (k_err()) {
            if (err) snprintf(err, errn, "error parseando <%s>", com);
            k_close();
            free(c);
            return NULL;
        }
    }

    /* El archivo de declaraciones (si se usa) reemplaza a las claves Profile. */
    if (c->profile_file[0]) {
        char perr[256] = "";
        if (csnhypodbp_profiles_parse(c->profile_file, &c->profiles,
                                      perr, sizeof(perr)) < 0) {
            if (err) snprintf(err, errn, "ProfileFile <%s>: %s",
                              c->profile_file, perr);
            k_close();
            free(c);
            return NULL;
        }
    }
    csnhypodbp_profiles_default_files(&c->profiles, c->profile_data_dir);

    /* Parametros de magnitud: defaults + lo que diga el .d de csnmags. */
    csnhypodbp_magcfg_defaults(&c->magcfg);
    if (csnhypodbp_magcfg_load(&c->magcfg, c->mag_config_file) != 0)
        fprintf(stderr, "csnhypodbpqt: aviso: no se pudo leer MagConfigFile <%s>; "
                        "se usan los defaults de magnitud\n", c->mag_config_file);

    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close();

    /* Los globals de csnhypodbp_sds.c reflejan la config (single-thread: la
     * carga ocurre antes de arrancar hilos). */
    g_sds_enabled = c->sds_enabled;
    set_str(g_sds_root, sizeof(g_sds_root), c->sds_root);

    apply_state_dir(c, state_dir_or_null);

    if (nmiss > 0) {
        static const char *const req[7] = {
            "MyModuleId", "InRing", "OutRing", "HeartBeatInt", "LogFile",
            "StaFile", "StateFile"
        };
        char miss[256] = "";
        size_t off = 0;
        for (i = 0; i < ncommand; i++) {
            if (init[i]) continue;
            int w = snprintf(miss + off, sizeof(miss) - off, "%s%s",
                             off ? "," : "", req[i]);
            if (w < 0 || (size_t)w >= sizeof(miss) - off) break;
            off += (size_t)w;
        }
        if (err) snprintf(err, errn, "faltan claves obligatorias: %s", miss);
        free(c);
        return NULL;
    }
    return c;
}

void bpq_config_free(BpqConfig *c)
{
    free(c);
}

int bpq_config_has_key(const BpqConfig *c, const char *key)
{
    int i;
    (void)c;
    if (!key) return 0;
    for (i = 0; kKeys[i]; i++)
        if (strcmp(kKeys[i], key) == 0) return 1;
    return 0;
}

const char *bpq_config_state_dir(const BpqConfig *c)
{
    return c ? c->state_dir : "";
}

/* --- modos headless (port literal de csnhypodbp_main.c:547-702) ------------ */

/* Una linea de color ya resuelta; marca las entradas que aun heredan del manual. */
static void print_pick_color(FILE *out, const char *key,
                             const CsnhypodbpColorSet *set, CsnhypodbpPickOrigin origin)
{
    double rgb[3];
    char   hex[7];
    const char *sfx = "";

    csnhypodbp_color_resolve(set, origin, rgb);
    csnhypodbp_color_to_hex(rgb, hex);
    if (origin == CSNHYPODBP_PICK_AUTO && !set->has_auto)
        sfx = " (derived 40%)";
    else if (origin == CSNHYPODBP_PICK_THEO && !set->has_theo)
        sfx = " (derived 70%)";
    fprintf(out, "%s %s%s\n", key, hex, sfx);
}

int bpq_config_print(const BpqConfig *c, FILE *out)
{
    int i;

    if (!c || !out) return -1;

    fprintf(out, "HideStationsWithoutData %d\n", c->hide_sta_without_data ? 1 : 0);
    fprintf(out, "tracesPerScreen %d\n", c->traces_per_screen);
    fprintf(out, "MapImageFile %s\n", c->map_image_file[0] ? c->map_image_file : "(none)");
    if (c->map_image_file[0])
        fprintf(out, "MapCanvas image\n");
    else
        fprintf(out, "MapCanvas virtual %.0fx%.0f\n",
                CSNHYPODBP_MAP_VIRTUAL_W, CSNHYPODBP_MAP_VIRTUAL_H);
    fprintf(out, "MapMinSpanDeg %.1f\n", c->map_min_span_deg);
    fprintf(out, "MapZoom %.3f\n", c->map_zoom_factor);
    fprintf(out, "MapSplit %.4f\n", c->map_split_frac);

    fprintf(out, "OverlayFiles %d\n", c->n_overlay_files);
    for (i = 0; i < c->n_overlay_files; i++)
        fprintf(out, "OverlayFile %s\n", c->overlay_files[i]);
    fprintf(out, "OverlayColors %d\n", c->n_overlay_colors);
    for (i = 0; i < c->n_overlay_colors; i++)
        fprintf(out, "OverlayColor %s %s\n",
                c->overlay_color_layer[i], c->overlay_color_hex[i]);
    fprintf(out, "OverlayWidth %.3f\n", c->overlay_width);
    fprintf(out, "OverlayHides %d\n", c->n_overlay_hides);
    for (i = 0; i < c->n_overlay_hides; i++)
        fprintf(out, "OverlayHide %s\n", c->overlay_hide_layer[i]);

    fprintf(out, "FilterPhase %s\n", c->filtercfg.phase_zero ? "zero" : "causal");
    fprintf(out, "FilterMenu %c\n", c->filtercfg.menu_key);
    fprintf(out, "FilterRaw %c\n", c->filtercfg.raw_key);
    fprintf(out, "Filters %d\n", c->filtercfg.nf);
    for (i = 0; i < c->filtercfg.nf; i++) {
        const CsnhypodbpFilterPreset *fp = &c->filtercfg.f[i];
        if (fp->type == 3)
            fprintf(out, "Filter %s BP %c %.4g %.4g %d\n", fp->name, fp->key,
                    fp->f1, fp->f2, fp->order);
        else
            fprintf(out, "Filter %s %s %c %.4g %d\n", fp->name,
                    fp->type == 1 ? "HP" : "LP", fp->key, fp->f1, fp->order);
    }
    if (c->filtercfg.stalta_declared)
        fprintf(out, "StaLta %c %.3f %.3f %.2f %.2f %.3f\n",
                c->filtercfg.stalta_key, c->filtercfg.stalta_sta,
                c->filtercfg.stalta_lta, c->filtercfg.stalta_on_thr,
                c->filtercfg.stalta_off_thr, c->filtercfg.stalta_corr);

    print_pick_color(out, "pColor",     &c->colors_p, CSNHYPODBP_PICK_MANUAL);
    print_pick_color(out, "pColorAuto", &c->colors_p, CSNHYPODBP_PICK_AUTO);
    print_pick_color(out, "pColorTheo", &c->colors_p, CSNHYPODBP_PICK_THEO);
    print_pick_color(out, "sColor",     &c->colors_s, CSNHYPODBP_PICK_MANUAL);
    print_pick_color(out, "sColorAuto", &c->colors_s, CSNHYPODBP_PICK_AUTO);
    print_pick_color(out, "sColorTheo", &c->colors_s, CSNHYPODBP_PICK_THEO);

    fprintf(out, "ProfileDataDir %s\n",
            c->profile_data_dir[0] ? c->profile_data_dir : "(none)");

    fprintf(out, "MagConfigFile %s\n",
            c->mag_config_file[0] ? c->mag_config_file : "(none)");
    fprintf(out, "MwwConfigFile %s\n",
            c->mww_config_file[0] ? c->mww_config_file : "(none)");
    fprintf(out, "MwwWorkDir %s\n", c->mww_workdir[0] ? c->mww_workdir : "(none)");
    fprintf(out, "MagWindow ML %.3f %.3f\n", c->magcfg.ml_ta, c->magcfg.ml_tb);
    fprintf(out, "MagWindow Mwp %.3f %.3f\n", c->magcfg.mwp_start,
            c->magcfg.mwp_start + c->magcfg.mwp_t0);
    fprintf(out, "MagWindow Mb %.3f %.3f\n", 0.0, c->magcfg.mb_window);
    fprintf(out, "MagWindow Ms %.3f %.3f\n", 0.0, c->magcfg.ms_window);
    fprintf(out, "MagWoodAnderson %.3f %.3f %.1f\n", c->magcfg.wa_period,
            c->magcfg.wa_damp, c->magcfg.wa_gain);
    fprintf(out, "MagBand Mb %.3f %.3f\n", c->magcfg.mb_f1, c->magcfg.mb_f2);
    fprintf(out, "MagBand Ms %.3f %.3f\n", c->magcfg.ms_f1, c->magcfg.ms_f2);
    fprintf(out, "MagFetchSeconds %.1f\n", c->mag_fetch_s);
    {
        int gi;
        for (gi = 0; gi < CSNHYPODBP_MAG_N; gi++) {
            double lo = 0.0, hi = 0.0, km = 0.0;
            csnhypodbp_magcfg_gate_limits(&c->magcfg, gi, &lo, &hi, &km);
            fprintf(out, "MagGate %s %.1f %.1f %.1f\n",
                    csnhypodbp_magcfg_name(gi), lo, hi, km);
        }
    }
    fprintf(out, "ProfileFile %s\n",
            c->profile_file[0] ? c->profile_file : "(none)");
    fprintf(out, "ProfileCols %d %d\n", c->profile_lon_col, c->profile_val_col);
    fprintf(out, "ProfileDepthMinKm %.1f\n", c->profile_depth_min_km);
    fprintf(out, "Profiles %d\n", c->profiles.n);
    for (i = 0; i < c->profiles.n; i++) {
        const CsnhypodbpProfile *p = &c->profiles.p[i];
        fprintf(out, "Profile %s %.6f %.6f %.6f %.6f %s %s\n", p->name,
                p->lon1, p->lat1, p->lon2, p->lat2, p->slab_file, p->topo_file);
    }
    return 0;
}

int bpq_config_check_profiles(const BpqConfig *c, FILE *out)
{
    int i, bad = 0;

    if (!c || !out) return -1;
    fprintf(out, "profiles=%d\n", c->profiles.n);
    for (i = 0; i < c->profiles.n; i++) {
        const CsnhypodbpProfile *p = &c->profiles.p[i];
        CsnhypodbpProfileLine slab, topo, traza;
        char   err[256];
        double dev = 0.0, devmax = 0.0;
        int    k, nfin = 0;

        if (csnhypodbp_profile_line_read(p->slab_file, c->profile_lon_col,
                                         c->profile_val_col, &slab,
                                         err, sizeof(err)) != 0) {
            fprintf(out, "profile %s: slab FALLA (%s)\n", p->name, err);
            bad++;
            continue;
        }
        if (csnhypodbp_profile_line_read(p->topo_file, c->profile_lon_col,
                                         c->profile_val_col, &topo,
                                         err, sizeof(err)) != 0) {
            fprintf(out, "profile %s: topo FALLA (%s)\n", p->name, err);
            bad++;
            csnhypodbp_profile_line_free(&slab);
            continue;
        }
        /* Traza del slab: columnas 3/4 = lon/lat, para compararla con la recta. */
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
        fprintf(out, "profile %s slab=%d topo=%d traza=%d dev_media=%.4f dev_max=%.4f\n",
                p->name, slab.n, topo.n, nfin, dev, devmax);
        if (dev < 0.0 || dev > 0.05) {
            fprintf(out, "profile %s: la traza no sigue la recta declarada\n", p->name);
            bad++;
        }
        csnhypodbp_profile_line_free(&slab);
        csnhypodbp_profile_line_free(&topo);
    }
    fprintf(out, "perfiles=%d fallas=%d\n", c->profiles.n, bad);
    return bad ? 2 : 0;
}
