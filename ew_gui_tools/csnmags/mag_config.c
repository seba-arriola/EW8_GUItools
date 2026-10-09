#include "csnmags.h"

#include <string.h>
#include <kom.h>

/*
 * mag_config.c - Lectura de csnmags.d.
 *
 * Todas las claves son opcionales salvo StaFile y ResponseDir; los valores por
 * defecto son estándar (IASPEI/Hutton-Boore/Gutenberg-Richter) para calibrar
 * después sin recomponer. El modo anillo exige además MyModuleId/InRing/OutRing.
 */

void mag_config_defaults(MagConfig *c)
{
    memset(c, 0, sizeof(*c));

    strcpy(c->my_mod_id, "MOD_MAGNITUDES");
    strcpy(c->in_ring, "HYPO_RING_REF");
    strcpy(c->out_ring, "HYPO_RING_REF");
    c->heartbeat = 30;
    c->logfile   = 1;
    c->debug     = 0;

    strcpy(c->resp_dir, "responses");
    strcpy(c->resp_pattern, "%S_%C_%N.pz");
    c->resp_in_meters = 0;
    strcpy(c->sta_file, "estaciones_107.txt");
    strcpy(c->calib_file, "calib/calib_map.txt");
    strcpy(c->sta_corr_file, "");
    strcpy(c->tau_model, "iasp91");

    strcpy(c->ws_ip, "127.0.0.1");
    strcpy(c->ws_port, "16022");
    c->ws_timeout = 10000;

    /* ML: Wood-Anderson Uhrhammer & Collins + Hutton-Boore (IASPEI IS 3.3) */
    c->wa_period = 0.8; c->wa_damp = 0.7; c->wa_gain = 2080.0;
    c->ml_dist_type = 1;          /* epicentral (Hutton-Boore) */
    c->ml_amp_mode  = 1;          /* peak-to-peak (mitad) */
    c->ml_slide_len = 0.8;
    c->ml_allow_vert = 0; c->ml_require2h = 0;
    c->ml_c1 = 1.11; c->ml_c2 = 0.00189; c->ml_c3 = -2.09;
    c->ml_snr = 3.5;
    c->ml_sg_speed = 3.98; c->ml_t_a = 10.0; c->ml_t_b = 45.0;
    c->ml_max_dist = 600.0;
    c->ml_min_sta = 1;
    c->ml.enabled = 1; c->ml.min_delta = 0.0; c->ml.max_delta = 8.0;
    c->ml.win_pre = 0.0; c->ml.win_post = 0.0;
    c->ml.band_lo = 0.0; c->ml.band_hi = 0.0;

    /* Mwp */
    c->mwp_t0 = 95.0; c->mwp_start_off = 0.0; c->mwp_highpass = 0.01;
    c->mwp_rho = 2700.0; c->mwp_alpha = 6000.0; c->mwp_fp = 0.6;
    c->mwp_min_sta = 1;
    c->mwp.enabled = 1; c->mwp.min_delta = 3.0; c->mwp.max_delta = 90.0;
    c->mwp.win_pre = 0.0; c->mwp.win_post = 0.0;
    c->mwp.band_lo = 0.0; c->mwp.band_hi = 0.0;

    /* Mb */
    c->mb_period = 1.0; c->mb_window = 30.0; c->mb_snr = 3.0;
    strcpy(c->mb_q_table, "calib/mb_Q.tab");
    c->mb_min_sta = 1;
    c->mb.enabled = 1; c->mb.min_delta = 5.0; c->mb.max_delta = 105.0;
    c->mb.win_pre = 0.0; c->mb.win_post = 0.0;
    c->mb.band_lo = 0.5; c->mb.band_hi = 2.0;

    /* Ms */
    c->ms_variant = 0;            /* Ms_20 */
    c->ms_t = 20.0; c->ms_band_lo = 0.045; c->ms_band_hi = 0.056;
    c->ms_k1 = 1.66; c->ms_k2 = 0.3;
    c->ms_deep_corr = 0;
    c->ms_min_sta = 1;
    c->ms.enabled = 1; c->ms.min_delta = 20.0; c->ms.max_delta = 160.0;
    c->ms.win_pre = 0.0; c->ms.win_post = 0.0;
    c->ms.band_lo = 0.045; c->ms.band_hi = 0.056;

    c->use_median = 0;
    c->trunc_k = 2.0;
    c->n_grids = 0;
}

static void add_grid(MagConfig *c, int level, const char *file)
{
    if (c->n_grids >= MAG_MAX_GRIDS || !file || !file[0]) return;
    MagGridRef *g = &c->grids[c->n_grids++];
    snprintf(g->name, sizeof(g->name), "%s", file);
    g->level = level;
    snprintf(g->file, sizeof(g->file), "%s", file);
}

int mag_config_load(const char *path, MagConfig *c)
{
    char *str;
    int   have_sta = 0, have_resp = 0;

    mag_config_defaults(c);

    if (!k_open(path)) return -1;
    while (k_rd()) {
        char *com = k_str();
        if (!com || com[0] == '#') continue;

        if      (k_its("MyModuleId"))  { str = k_str(); if (str) snprintf(c->my_mod_id, sizeof(c->my_mod_id), "%s", str); }
        else if (k_its("InRing"))      { str = k_str(); if (str) snprintf(c->in_ring, sizeof(c->in_ring), "%s", str); }
        else if (k_its("OutRing"))     { str = k_str(); if (str) snprintf(c->out_ring, sizeof(c->out_ring), "%s", str); }
        else if (k_its("HeartBeatInt")) c->heartbeat = k_int();
        else if (k_its("LogFile"))      c->logfile = k_int();
        else if (k_its("Debug"))        c->debug = k_int();

        else if (k_its("WsIP"))    { str = k_str(); if (str) snprintf(c->ws_ip, sizeof(c->ws_ip), "%s", str); }
        else if (k_its("WsPort"))  { str = k_str(); if (str) snprintf(c->ws_port, sizeof(c->ws_port), "%s", str); }
        else if (k_its("WsTimeout")) c->ws_timeout = k_int();

        else if (k_its("ResponseDir"))     { str = k_str(); if (str) snprintf(c->resp_dir, sizeof(c->resp_dir), "%s", str); have_resp = 1; }
        else if (k_its("ResponsePattern")) { str = k_str(); if (str) snprintf(c->resp_pattern, sizeof(c->resp_pattern), "%s", str); }
        else if (k_its("ResponseInMeters")) c->resp_in_meters = k_int();
        else if (k_its("StaFile"))     { str = k_str(); if (str) snprintf(c->sta_file, sizeof(c->sta_file), "%s", str); have_sta = 1; }
        else if (k_its("CalibFile"))   { str = k_str(); if (str) snprintf(c->calib_file, sizeof(c->calib_file), "%s", str); }
        else if (k_its("StaCorrFile")) { str = k_str(); if (str) snprintf(c->sta_corr_file, sizeof(c->sta_corr_file), "%s", str); }
        else if (k_its("TauModel"))    { str = k_str(); if (str) snprintf(c->tau_model, sizeof(c->tau_model), "%s", str); }

        else if (k_its("WoodAndersonCoefs")) { c->wa_period = k_val(); c->wa_damp = k_val(); c->wa_gain = k_val(); }
        else if (k_its("Ml_DistType"))   c->ml_dist_type = k_int();
        else if (k_its("Ml_AmpMode"))    c->ml_amp_mode = k_int();
        else if (k_its("Ml_SlideLen"))   c->ml_slide_len = k_val();
        else if (k_its("Ml_AllowVerticals")) c->ml_allow_vert = k_int();
        else if (k_its("Ml_Require2H"))  c->ml_require2h = k_int();
        else if (k_its("Ml_Coeffs"))     { c->ml_c1 = k_val(); c->ml_c2 = k_val(); c->ml_c3 = k_val(); }
        else if (k_its("Ml_SNR"))        c->ml_snr = k_val();
        else if (k_its("Ml_SgSpeed"))    c->ml_sg_speed = k_val();
        else if (k_its("Ml_TA"))         c->ml_t_a = k_val();
        else if (k_its("Ml_TB"))         c->ml_t_b = k_val();
        else if (k_its("Ml_MaxDist"))    c->ml_max_dist = k_val();
        else if (k_its("Ml_MinSta"))     c->ml_min_sta = k_int();
        else if (k_its("Ml_Enable"))     c->ml.enabled = k_int();
        else if (k_its("Ml_MinDelta"))   c->ml.min_delta = k_val();
        else if (k_its("Ml_MaxDelta"))   c->ml.max_delta = k_val();
        else if (k_its("Ml_WindowPre"))  c->ml.win_pre = k_val();
        else if (k_its("Ml_WindowPost")) c->ml.win_post = k_val();

        else if (k_its("Mwp_T0"))        c->mwp_t0 = k_val();
        else if (k_its("Mwp_StartOff"))  c->mwp_start_off = k_val();
        else if (k_its("Mwp_HighPass"))  c->mwp_highpass = k_val();
        else if (k_its("Mwp_Rho"))       c->mwp_rho = k_val();
        else if (k_its("Mwp_Alpha"))     c->mwp_alpha = k_val();
        else if (k_its("Mwp_Fp"))        c->mwp_fp = k_val();
        else if (k_its("Mwp_MinSta"))    c->mwp_min_sta = k_int();
        else if (k_its("Mwp_Enable"))    c->mwp.enabled = k_int();
        else if (k_its("Mwp_MinDelta"))  c->mwp.min_delta = k_val();
        else if (k_its("Mwp_MaxDelta"))  c->mwp.max_delta = k_val();
        else if (k_its("Mwp_WindowPre")) c->mwp.win_pre = k_val();
        else if (k_its("Mwp_WindowPost")) c->mwp.win_post = k_val();

        else if (k_its("Mb_Period"))     c->mb_period = k_val();
        else if (k_its("Mb_Window"))     c->mb_window = k_val();
        else if (k_its("Mb_SNR"))        c->mb_snr = k_val();
        else if (k_its("Mb_QTable"))     { str = k_str(); if (str) snprintf(c->mb_q_table, sizeof(c->mb_q_table), "%s", str); }
        else if (k_its("Mb_MinSta"))     c->mb_min_sta = k_int();
        else if (k_its("Mb_Enable"))     c->mb.enabled = k_int();
        else if (k_its("Mb_MinDelta"))   c->mb.min_delta = k_val();
        else if (k_its("Mb_MaxDelta"))   c->mb.max_delta = k_val();
        else if (k_its("Mb_Band"))       { c->mb.band_lo = k_val(); c->mb.band_hi = k_val(); }

        else if (k_its("Ms_Variant"))    c->ms_variant = k_int();
        else if (k_its("Ms_T"))          c->ms_t = k_val();
        else if (k_its("Ms_Band"))       { c->ms_band_lo = k_val(); c->ms_band_hi = k_val(); }
        else if (k_its("Ms_Coeffs"))     { c->ms_k1 = k_val(); c->ms_k2 = k_val(); }
        else if (k_its("Ms_DeepCorr"))   c->ms_deep_corr = k_int();
        else if (k_its("Ms_MinSta"))     c->ms_min_sta = k_int();
        else if (k_its("Ms_Enable"))     c->ms.enabled = k_int();
        else if (k_its("Ms_MinDelta"))   c->ms.min_delta = k_val();
        else if (k_its("Ms_MaxDelta"))   c->ms.max_delta = k_val();

        else if (k_its("UseMedian"))     c->use_median = k_int();
        else if (k_its("TruncK"))        c->trunc_k = k_val();

        else if (k_its("GlobalGrid"))    { str = k_str(); add_grid(c, 0, str); }
        else if (k_its("RegionalGrid"))  { str = k_str(); add_grid(c, 1, str); }
        else if (k_its("LocalGrid"))     { str = k_str(); add_grid(c, 2, str); }

        else continue;
        if (k_err()) { k_close(); return -1; }
    }
    k_close();

    if (!have_sta || !have_resp) return -1;
    return 0;
}
