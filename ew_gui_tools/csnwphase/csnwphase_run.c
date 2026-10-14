#include "csnwphase.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/*
 * Orquestación de una corrida. Se genera un `run.sh` POSIX AUDITABLE dentro del
 * run dir (lo que el usuario ve en la GUI es exactamente lo que se ejecuta) y
 * su salida completa se acumula en `run.log`.
 *
 * Replica los pasos de `prepare_wp.csh` + `RUNA3r.csh` (NO se usan los .csh:
 * ni csh ni python2 son necesarios) y salta `extract.csh`/rdseed porque
 * DATA_org ya se llena desde el SDS/store.
 *
 * El grid search del centroide lo hace `wpinversion_gs` (el wrapper del CSN,
 * `wp_grid_search.py`, es python2 y no se usa): deja `xy_WCMTSOLUTION`, que es
 * el archivo que este modulo prefiere al parsear.
 */

/* El `mkdir -p` vive en csnwphase_mkdir_p() (prep.c): lo comparten el run dir y
 * DATA_org. */

/* ------------------------------------------------------------------ */
/* Llenado de DATA_org desde el SDS/store                              */
/* ------------------------------------------------------------------ */

int csnwphase_input_prep(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                         const char *rundir, int *n_sta_out, int *n_traces_out,
                         char *err, size_t errlen)
{
    CsnwphaseStation stas[512];
    CsnwphaseWave w;
    char dir[1024], path[2048];
    double t0, t1;
    int nsta, i, ntrace = 0;

    if (err && errlen)
        err[0] = '\0';
    if (n_sta_out)
        *n_sta_out = 0;
    if (n_traces_out)
        *n_traces_out = 0;

    if (in->origin_epoch <= 0.0) {
        if (err && errlen)
            snprintf(err, errlen, "falta el tiempo de origen en epoch");
        return -1;
    }
    t0 = in->origin_epoch;

    snprintf(dir, sizeof(dir), "%s/DATA_org", rundir);
    if (csnwphase_mkdir_p(dir) != 0) {
        if (err && errlen)
            snprintf(err, errlen, "no se pudo crear %s", dir);
        return -1;
    }

    /* i_master + CMTSOLUTION (contrato del motor). */
    if (csnwphase_prep_write(cfg, in, rundir, err, errlen) != 0)
        return -1;

    nsta = csnwphase_sta_select(cfg, in->latitude, in->longitude, cfg->dmin_deg,
                                cfg->dmax_deg, stas, 512);
    if (nsta < 0) {
        if (err && errlen)
            snprintf(err, errlen, "no se pudo leer StaFile %s", cfg->sta_file);
        return -1;
    }

    if (csnwphase_wave_open(&w, cfg) != 0) {
        if (err && errlen)
            snprintf(err, errlen, "sin fuentes de ondas (store/SDS)");
        return -1;
    }

    t1 = t0 + (double)cfg->win_post_sec;

    for (i = 0; i < nsta; i++) {
        char chans[3][16];
        int nch = 0, c;

        csnwphase_chan_variants(stas[i].chan, chans, &nch);
        for (c = 0; c < nch; c++) {
            CsnwphaseTrace tr;
            CsnwphaseSAC sac;
            static float buf[CSNWPHASE_MAX_SAMPLES];
            long k;
            int rc;

            rc = csnwphase_wave_get(&w, stas[i].net, stas[i].sta, chans[c],
                                    stas[i].loc, t0 - (double)cfg->win_pre_sec,
                                    t1, &tr);
            if (rc <= 0)
                continue;
            if (tr.n > CSNWPHASE_MAX_SAMPLES) {
                csnwphase_trace_free(&tr);
                continue;
            }

            for (k = 0; k < tr.n; k++)
                buf[k] = (float)tr.samples[k];

            memset(&sac, 0, sizeof(sac));
            snprintf(sac.sta, sizeof(sac.sta), "%s", stas[i].sta);
            snprintf(sac.net, sizeof(sac.net), "%s", stas[i].net);
            snprintf(sac.chan, sizeof(sac.chan), "%s", chans[c]);
            snprintf(sac.loc, sizeof(sac.loc), "%s", stas[i].loc);
            sac.stla = stas[i].lat;
            sac.stlo = stas[i].lon;
            sac.stel_m = stas[i].elev_m;
            sac.evla = in->latitude;
            sac.evlo = in->longitude;
            sac.evdp_km = in->depth_km;
            sac.mag = in->magnitude;
            sac.rate = tr.samprate;
            sac.start_epoch = tr.t0;
            sac.origin_epoch = t0;
            sac.npts = tr.n;
            sac.data = buf;

            snprintf(path, sizeof(path), "%s/%s.%s.%s.SAC", dir, stas[i].net,
                     stas[i].sta, chans[c]);
            if (csnwphase_sac_write(path, &sac) == 0) {
                char pz[1200];

                /* Respuesta del canal: el motor la busca en DATA_org y luego
                   la copia a DATA/ (prepare_wp.csh). */
                if (csnwphase_pz_write(cfg, dir, stas[i].net, stas[i].sta,
                                       chans[c], stas[i].loc, pz,
                                       sizeof(pz)) != 0)
                    fprintf(stderr,
                            "csnwphase: aviso: sin .pz para %s.%s.%s.%s\n",
                            stas[i].net, stas[i].sta, chans[c], stas[i].loc);
                ntrace++;
            }
            csnwphase_trace_free(&tr);
        }
    }

    csnwphase_wave_close(&w);

    if (ntrace == 0) {
        if (err && errlen)
            snprintf(err, errlen,
                     "sin ondas para el evento (0 trazas; %d estaciones en [%g,%g] grados)",
                     nsta, cfg->dmin_deg, cfg->dmax_deg);
        return -1;
    }

    if (n_sta_out)
        *n_sta_out = nsta;
    if (n_traces_out)
        *n_traces_out = ntrace;
    return 0;
}

/* ------------------------------------------------------------------ */
/* run.sh                                                             */
/* ------------------------------------------------------------------ */

int csnwphase_run_script(const CsnwphaseCfg *cfg, const char *rundir,
                         const char *script)
{
    FILE *f = fopen(script, "w");

    if (!f)
        return -1;

    fprintf(f, "#!/bin/sh\n");
    fprintf(f, "# Generado por csnwphase: inversión de fase W (motor eost/wphase).\n");
    fprintf(f, "# Datos: DATA_org/*.SAC + DATA_org/SAC_PZs_*  ->  WCMTSOLUTION\n");
    fprintf(f, "set -e\n");
    fprintf(f, "cd \"%s\"\n", rundir);
    fprintf(f, "export WPHASE_HOME='%s'\n", cfg->wphase_home);
    fprintf(f, "export GF_PATH='%s'\n", cfg->gf_dir);
    fprintf(f, "export LC_ALL=C\n");
    fprintf(f, "export PATH='%s':$PATH\n", cfg->wphase_bin);
    fprintf(f, "WBIN='%s'\n", cfg->wphase_bin);
    fprintf(f, "\n");
    fprintf(f, "mkdir -p DATA LOG SYNTH\n");
    fprintf(f, "\n# 1. respuestas: se copian al directorio de trabajo del motor\n");
    fprintf(f, "cp -f DATA_org/SAC_PZs_* DATA/ 2>/dev/null || true\n");
    fprintf(f, "\n# 2. diezmado a 1 sps (trim_sac_files EXIGE delta == 1 s)\n");
    fprintf(f, "for f in DATA_org/*.SAC; do\n");
    fprintf(f, "  [ -e \"$f\" ] || continue\n");
    fprintf(f, "  \"$WBIN/decim_one_sac_file_to_1sps\" \"$f\" DATA/ || echo \"aviso: no se pudo diezmar $f\"\n");
    fprintf(f, "done\n");
    fprintf(f, "ls DATA/*.SAC > DATA/_sac_files_list\n");
    fprintf(f, "\n# 3. ventanas W (usa P_tt_JB de $WPHASE_HOME y DMIN/DMAX/WP_WIN de i_master)\n");
    fprintf(f, "\"$WBIN/trim_sac_files\" i_master DATA/_sac_files_list scr_dat_fil_list -u\n");
    fprintf(f, "\n# 4. LUT de respuesta (id NET_STA_LOC_CHAN)\n");
    fprintf(f, "ls DATA/SAC_PZs* > pz_fil_list\n");
    fprintf(f, "\"$WBIN/make_resp_lookup_table\" pz_fil_list i_master coeffs_rec_lut\n");
    fprintf(f, "\n# 5. deconvolucion + filtrado de datos, y kernels de las GF\n");
    fprintf(f, "\"$WBIN/rec_dec_filt\" coeffs_rec_lut i_master scr_dat_fil_list dec_bp_dat_fil_list\n");
    fprintf(f, "\"$WBIN/prep_kernels\" scr_dat_fil_list l\n");
    fprintf(f, "cut -d' ' -f1 dec_bp_dat_fil_list > i_wpinversion\n");
    fprintf(f, "\n# 6. inversion: screening de mediana y luego por RMS\n");
    fprintf(f, "\"$WBIN/wpinversion\" -log LOG/wpinversion.noth.log -osyndir SYNTH \\\n");
    fprintf(f, "  -pdata fort.15.noth -med\n");
    fprintf(f, "cp -f o_wpinversion o_wpinv\n");
    fprintf(f, "cp -f WCMTSOLUTION WCMTSOLUTION.noth 2>/dev/null || true\n");
    fprintf(f, "cp -f p_wpinversion p_wpinversion.noth 2>/dev/null || true\n");
    fprintf(f, "for th in %g %g %g; do\n", cfg->screen_th[0], cfg->screen_th[1],
            cfg->screen_th[2]);
    fprintf(f, "  \"$WBIN/wpinversion\" -th $th -ifil o_wpinversion -ofil o_wpinv.th_$th \\\n");
    fprintf(f, "    -log LOG/wpinversion.th_$th.log -osyndir SYNTH -pdata fort.15.th_$th \\\n");
    fprintf(f, "    -ocmtf WCMTSOLUTION.th_$th -old\n");
    fprintf(f, "  cp -f o_wpinv.th_$th o_wpinversion\n");
    fprintf(f, "done\n");
    fprintf(f, "\n# 7. screening por razon de amplitudes\n");
    fprintf(f, "\"$WBIN/wpinversion\" -nr %g -ifil o_wpinversion -ofil o_wpinv.r_%g \\\n",
            cfg->screen_nr, cfg->screen_nr);
    fprintf(f, "  -log LOG/wpinversion.r_%g.log -osyndir SYNTH -pdata fort.15.r_%g \\\n",
            cfg->screen_nr, cfg->screen_nr);
    fprintf(f, "  -ocmtf WCMTSOLUTION.r_%g -old\n", cfg->screen_nr);
    fprintf(f, "cp -f o_wpinv.r_%g o_wpinversion\n", cfg->screen_nr);
    fprintf(f, "cp -f WCMTSOLUTION.r_%g WCMTSOLUTION 2>/dev/null || true\n",
            cfg->screen_nr);
    fprintf(f, "cp -f p_wpinversion.r_%g p_wpinversion 2>/dev/null || true\n",
            cfg->screen_nr);
    if (cfg->grid_search) {
        /* El wrapper del CSN (wp_grid_search.py) es python2 y no se usa: se llama
         * al binario. Los `-ts` salen del .d (el wrapper los derivaba de la
         * duracion; aqui son visibles y editables). Salidas: `xy_WCMTSOLUTION`
         * (la que prefiere el parser), `LOG/gs_o_wpinversion.log` y `gs_SYNTH/`. */
        fprintf(f, "\n# 8. grid search del centroide (wpinversion_gs)\n");
        fprintf(f, "mkdir -p gs_SYNTH\n");
        fprintf(f, "\"$WBIN/wpinversion_gs\" -imas i_master -ifil o_wpinversion \\\n");
        fprintf(f, "  -icmtf %s -log LOG/gs_o_wpinversion.log -osyndir gs_SYNTH \\\n",
                cfg->cmtfile);
        fprintf(f, "  -ts %g %g %g -ts_Nit %d -otsgsf grid_search_ts_out \\\n",
                cfg->gs_ts_min, cfg->gs_ts_dt, cfg->gs_ts_max, cfg->gs_ts_nit);
        fprintf(f, "  -xy_Nit %d -dx %g -Nx %d -Nopt %d -oxygsf grid_search_xy_out\n",
                cfg->gs_xy_nit, cfg->gs_xy_dx, cfg->gs_xy_nx, cfg->gs_xy_nopt);
    }
    fprintf(f, "\necho \"csnwphase run: OK\"\n");

    return fclose(f) == 0 ? 0 : -1;
}

int csnwphase_run(const CsnwphaseCfg *cfg, const char *rundir, int dry_run,
                  char *err, size_t errlen)
{
    char script[1024], cmd[1200], line[2048];
    FILE *log, *p;
    long total = 0;
    double remain = (double)cfg->timeout_sec;

    if (err && errlen)
        err[0] = '\0';
    if (!cfg || !rundir) {
        if (err && errlen)
            snprintf(err, errlen, "argumentos invalidos");
        return -1;
    }

    snprintf(script, sizeof(script), "%s/run.sh", rundir);
    if (csnwphase_run_script(cfg, rundir, script) != 0) {
        if (err && errlen)
            snprintf(err, errlen, "no se pudo escribir %s", script);
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "%s/run.log", rundir);
    log = fopen(cmd, "a");
    if (!log) {
        if (err && errlen)
            snprintf(err, errlen, "no se pudo abrir %s", cmd);
        return -1;
    }

    fprintf(log, "\n##### run.sh (%s) #####\n", dry_run ? "dry run" : "ejecucion");
    p = fopen(script, "r");
    if (p) {
        while (fgets(line, sizeof(line), p))
            fputs(line, log);
        fclose(p);
    }
    fflush(log);

    if (dry_run) {
        fprintf(log, "##### fin dry run #####\n");
        fclose(log);
        return 0;
    }

    /* timeout absoluto: en este entorno `timeout` está ensombrecido por
       SeisComP, así que se usa la ruta absoluta. */
    snprintf(cmd, sizeof(cmd), "/usr/bin/timeout %.0f /bin/sh '%s' >> '%s/run.log' 2>&1",
             remain, script, rundir);

    p = popen(cmd, "r");
    if (!p) {
        fclose(log);
        if (err && errlen)
            snprintf(err, errlen, "no se pudo ejecutar run.sh");
        return -1;
    }
    while (fgets(line, sizeof(line), p))
        total += (long)strlen(line);
    {
        int rc = pclose(p);

        fprintf(log, "##### fin run.sh (rc=%d) #####\n", rc);
        fclose(log);
        if (rc != 0) {
            if (err && errlen)
                snprintf(err, errlen,
                         "run.sh termino con rc=%d (ver %s/run.log)", rc, rundir);
            return -1;
        }
    }

    return 0;
}
