#include "csnwphase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kom.h"

/*
 * Lectura del .d. Se usa kom directamente (y no ewgui_config_load) porque
 * varias claves del motor llevan VARIOS valores en la misma línea
 * (IDEC_2, IDEC_3, WP_WIN) y el helper de common sólo lee el primer
 * token de cada clave.
 */

/*
 * Campos separados por blancos de una línea cruda. Hace falta para `ScreenTh`:
 * kom no expone "¿quedan tokens?" y `k_val()` sobre un token ausente devuelve
 * 0 sin avisar, así que la detección del formato viejo (tres valores) se hace
 * sobre el texto de la línea (`k_com()`).
 */
static int count_fields(const char *line)
{
    int n = 0;
    const char *p = line;

    if (!p)
        return 0;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (*p == '\0')
            break;
        n++;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
    }
    return n;
}

static void copy_str(char *dst, size_t n, const char *src)
{
    size_t len;

    if (!dst || n == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    len = strlen(src);
    if (len >= n)
        len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void csnwphase_resolve_path(char *out, size_t n, const char *in)
{
    const char *params;

    if (!out || n == 0)
        return;
    if (!in || in[0] == '\0') {
        out[0] = '\0';
        return;
    }
    if (in[0] == '/') {
        snprintf(out, n, "%s", in);
        return;
    }

    params = getenv("EW_PARAMS");
    if (params && params[0])
        snprintf(out, n, "%s/%s", params, in);
    else
        snprintf(out, n, "./%s", in);
}

void csnwphase_cfg_defaults(CsnwphaseCfg *cfg)
{
    if (!cfg)
        return;

    memset(cfg, 0, sizeof(*cfg));

    copy_str(cfg->wphase_home, sizeof(cfg->wphase_home),
             "/home/seba/Dev/wphase/CSNop/wphase-prod");
    copy_str(cfg->wphase_bin, sizeof(cfg->wphase_bin),
             "/home/seba/Dev/wphase/CSNop/wphase-prod/bin");
    copy_str(cfg->gf_dir, sizeof(cfg->gf_dir),
             "/home/seba/Dev/wphase/CSNop/wphase-prod/GF_0.1d");

    copy_str(cfg->store_root, sizeof(cfg->store_root), "../wstore");
    copy_str(cfg->sds_root, sizeof(cfg->sds_root), "/mnt/sds/archive");
    cfg->sds_fallback_hours = 168;
    copy_str(cfg->sta_file, sizeof(cfg->sta_file), "estaciones_107.txt");
    copy_str(cfg->resp_dir, sizeof(cfg->resp_dir),
             "../../resources/csnmags/responses");
    copy_str(cfg->resp_pattern, sizeof(cfg->resp_pattern), "%S_%C_%N.pz");
    cfg->resp_in_nm = 1;

    cfg->dmin_deg = 5.0;          /* i_master.py:4 */
    cfg->dmax_deg = 50.0;
    cfg->min_magnitude = 6.0;     /* task_wphase/config.py:12-14 */
    cfg->win_pre_sec = 300;       /* run.py:230-233 (duration=1200) */
    cfg->win_post_sec = 900;
    cfg->screen_th = 2.0;         /* -th: un solo valor (antes 5.0 3.0 1.0) */
    cfg->screen_nr = 2.0;         /* -nr */
    cfg->grid_search = 1;
    cfg->gs_ts_min = 1.0;         /* wp_grid_search.py:174-183 (ts1 = 1.) */
    cfg->gs_ts_dt = 4.0;          /* TS_DT */
    cfg->gs_ts_max = 30.0;        /* ts2 = 3*half_dur, recortado (aqui editable) */
    cfg->gs_ts_nit = 3;           /* TS_NIT */
    cfg->gs_xy_dx = 0.4;          /* XY_DX */
    cfg->gs_xy_nx = 3;            /* XY_NX */
    cfg->gs_xy_nopt = 5;          /* XY_NOPT */
    cfg->gs_xy_nit = 3;           /* XY_NIT */
    cfg->gs_xyz_dz = 50.0;        /* DDEP   (wp_grid_search.py:54) */
    cfg->gs_xyz_minz = 11.5;      /* MINDEP (wp_grid_search.py:55) */
    cfg->gs_xyz_dx = 0.6;         /* XYZ_DX */
    cfg->gs_xyz_nx = 1;           /* XYZ_NX */
    cfg->gs_xyz_nopt = 4;         /* XYZ_NOPT */
    cfg->gs_xyz_nit = 1;          /* XYZ_NIT */
    cfg->gs_hdsafe = 1;           /* `-s` del wrapper -> `-hdsafe` */
    cfg->gs_old = 1;              /* WPINV_XY += ' -old' (linea 298) */
    cfg->gs_ts_auto = 1;          /* ts2 = f(mag) (lineas 173-185) */
    cfg->corpus_min_nused = 7;    /* umbral del corpus de validacion */
    cfg->timeout_sec = 1800;
    cfg->keep_tmp = 0;
    cfg->max_concurrent = 1;
    cfg->debug = 0;

    /* i_master: mismos defaults que i_master.py:4-8 */
    copy_str(cfg->evname, sizeof(cfg->evname), "Chile");
    copy_str(cfg->seed_ref, sizeof(cfg->seed_ref), "-");
    copy_str(cfg->cmtfile, sizeof(cfg->cmtfile), "CMTSOLUTION");
    copy_str(cfg->gfdir_rel, sizeof(cfg->gfdir_rel), "./GF");
    cfg->filt_order = 4;
    cfg->filt_pass = 1;
    cfg->idec2_idtr = 2.0;
    cfg->idec2_preevent = 280.0;
    cfg->idec2_fend = 0.1;
    cfg->idec3_fl = 0.001;
    cfg->idec3_fh = 0.1;
    cfg->idec3_nf = 100;
    cfg->idec3_tol = 0.03;
    cfg->wpwin[0] = 0.0;
    cfg->wpwin[1] = 15.0;
    cfg->wpwin[2] = 12.0;
}

int csnwphase_cfg_load(CsnwphaseCfg *cfg, const char *path)
{
    int rc = 0;

    if (!cfg || !path)
        return -1;

    csnwphase_cfg_defaults(cfg);

    if (!k_open(path)) {
        fprintf(stderr, "csnwphase: no se pudo abrir %s\n", path);
        return -1;
    }

    while (k_rd()) {
        char *com = k_str();

        /* kom no salta líneas en blanco ni comentarios: los descartamos. */
        if (!com || com[0] == '#' || com[0] == '\0')
            continue;

        if (k_its("WphaseHome")) {
            copy_str(cfg->wphase_home, sizeof(cfg->wphase_home), k_str());
        } else if (k_its("WphaseBin")) {
            copy_str(cfg->wphase_bin, sizeof(cfg->wphase_bin), k_str());
        } else if (k_its("GfDir")) {
            copy_str(cfg->gf_dir, sizeof(cfg->gf_dir), k_str());
        } else if (k_its("StoreRoot")) {
            copy_str(cfg->store_root, sizeof(cfg->store_root), k_str());
        } else if (k_its("SdsRoot")) {
            copy_str(cfg->sds_root, sizeof(cfg->sds_root), k_str());
        } else if (k_its("SdsFallbackHours")) {
            cfg->sds_fallback_hours = k_int();
        } else if (k_its("StaFile")) {
            copy_str(cfg->sta_file, sizeof(cfg->sta_file), k_str());
        } else if (k_its("ResponseDir")) {
            copy_str(cfg->resp_dir, sizeof(cfg->resp_dir), k_str());
        } else if (k_its("ResponsePattern")) {
            copy_str(cfg->resp_pattern, sizeof(cfg->resp_pattern), k_str());
        } else if (k_its("ResponseInNm")) {
            cfg->resp_in_nm = k_int();
        } else if (k_its("Dmin")) {
            cfg->dmin_deg = k_val();
        } else if (k_its("Dmax")) {
            cfg->dmax_deg = k_val();
        } else if (k_its("MinMagnitude")) {
            cfg->min_magnitude = k_val();
        } else if (k_its("WinPre")) {
            cfg->win_pre_sec = k_int();
        } else if (k_its("WinPost")) {
            cfg->win_post_sec = k_int();
        } else if (k_its("ScreenTh")) {
            cfg->screen_th = k_val();
            /* Formato viejo (5.0 3.0 1.0): se usa el primero y se avisa; las
             * tres pasadas encadenadas ya no se generan (una sola ejecucion). */
            if (count_fields(k_com()) > 2)
                fprintf(stderr, "csnwphase: aviso: ScreenTh con varios valores "
                        "en %s; se usa el primero (%g)\n", path, cfg->screen_th);
        } else if (k_its("ScreenNr")) {
            cfg->screen_nr = k_val();
        } else if (k_its("GridSearch")) {
            cfg->grid_search = k_int();
        } else if (k_its("GsTsMin")) {
            cfg->gs_ts_min = k_val();
        } else if (k_its("GsTsDt")) {
            cfg->gs_ts_dt = k_val();
        } else if (k_its("GsTsMax")) {
            cfg->gs_ts_max = k_val();
        } else if (k_its("GsTsNit")) {
            cfg->gs_ts_nit = k_int();
        } else if (k_its("GsXyDx")) {
            cfg->gs_xy_dx = k_val();
        } else if (k_its("GsXyNx")) {
            cfg->gs_xy_nx = k_int();
        } else if (k_its("GsXyNopt")) {
            cfg->gs_xy_nopt = k_int();
        } else if (k_its("GsXyNit")) {
            cfg->gs_xy_nit = k_int();
        } else if (k_its("GsXyzDz")) {
            cfg->gs_xyz_dz = k_val();
        } else if (k_its("GsXyzMinz")) {
            cfg->gs_xyz_minz = k_val();
        } else if (k_its("GsXyzDx")) {
            cfg->gs_xyz_dx = k_val();
        } else if (k_its("GsXyzNx")) {
            cfg->gs_xyz_nx = k_int();
        } else if (k_its("GsXyzNopt")) {
            cfg->gs_xyz_nopt = k_int();
        } else if (k_its("GsXyzNit")) {
            cfg->gs_xyz_nit = k_int();
        } else if (k_its("GsHdsafe")) {
            cfg->gs_hdsafe = k_int();
        } else if (k_its("GsOld")) {
            cfg->gs_old = k_int();
        } else if (k_its("GsTsAuto")) {
            cfg->gs_ts_auto = k_int();
        } else if (k_its("CorpusMinNused")) {
            cfg->corpus_min_nused = k_int();
        } else if (k_its("TimeoutSec")) {
            cfg->timeout_sec = k_int();
        } else if (k_its("KeepTmp")) {
            cfg->keep_tmp = k_int();
        } else if (k_its("MaxConcurrent")) {
            cfg->max_concurrent = k_int();
        } else if (k_its("Debug")) {
            cfg->debug = k_int();
        } else if (k_its("Evname")) {
            copy_str(cfg->evname, sizeof(cfg->evname), k_str());
        } else if (k_its("SeedRef")) {
            copy_str(cfg->seed_ref, sizeof(cfg->seed_ref), k_str());
        } else if (k_its("CmtFile")) {
            copy_str(cfg->cmtfile, sizeof(cfg->cmtfile), k_str());
        } else if (k_its("GfdirRel")) {
            copy_str(cfg->gfdir_rel, sizeof(cfg->gfdir_rel), k_str());
        } else if (k_its("FiltOrder")) {
            cfg->filt_order = k_int();
        } else if (k_its("FiltPass")) {
            cfg->filt_pass = k_int();
        } else if (k_its("Idec2")) {
            cfg->idec2_idtr = k_val();
            cfg->idec2_preevent = k_val();
            cfg->idec2_fend = k_val();
        } else if (k_its("Idec3")) {
            cfg->idec3_fl = k_val();
            cfg->idec3_fh = k_val();
            cfg->idec3_nf = k_int();
            cfg->idec3_tol = k_val();
        } else if (k_its("WpWin")) {
            cfg->wpwin[0] = k_val();
            cfg->wpwin[1] = k_val();
            cfg->wpwin[2] = k_val();
        }
    }

    if (k_err()) {
        fprintf(stderr, "csnwphase: error de parseo en %s\n", path);
        rc = -1;
    }
    k_close();

    if (!cfg->wphase_home[0] || !cfg->wphase_bin[0] || !cfg->gf_dir[0]) {
        fprintf(stderr, "csnwphase: WphaseHome/WphaseBin/GfDir son obligatorias\n");
        rc = -1;
    }

    /* Rutas de datos relativas al .d -> relativas a $EW_PARAMS. */
    {
        char tmp[512];

        csnwphase_resolve_path(tmp, sizeof(tmp), cfg->sta_file);
        copy_str(cfg->sta_file, sizeof(cfg->sta_file), tmp);
        csnwphase_resolve_path(tmp, sizeof(tmp), cfg->resp_dir);
        copy_str(cfg->resp_dir, sizeof(cfg->resp_dir), tmp);
        csnwphase_resolve_path(tmp, sizeof(tmp), cfg->store_root);
        copy_str(cfg->store_root, sizeof(cfg->store_root), tmp);
    }

    return rc;
}
