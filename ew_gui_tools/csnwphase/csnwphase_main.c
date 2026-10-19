#include "csnwphase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * CLI del módulo (headless, sin anillos):
 *
 *   csnwphase <csnwphase.d> --print-config
 *   csnwphase <csnwphase.d> --prep <rundir> <YYYY-MM-DDTHH:MM:SS> <lat> <lon>
 *                         <depth_km> <mag> [location]
 *
 * `--prep` deja el run dir listo para el motor (i_master + CMTSOLUTION) y
 * muestra la tabla de parámetros que la GUI presenta antes de ejecutar.
 */

static void usage(void)
{
    fprintf(stderr,
            "uso: csnwphase <csnwphase.d> --print-config\n"
            "     csnwphase <csnwphase.d> --prep <rundir> <YYYY-MM-DDTHH:MM:SS> "
            "<lat> <lon> <depth_km> <mag> [location]\n"
            "     csnwphase <csnwphase.d> --parse <rundir>\n"
            "     csnwphase <csnwphase.d> --fetch <net> <sta> <chan> <t0> <t1> [loc]\n"
            "     csnwphase <csnwphase.d> --run <rundir> <t0> <lat> <lon> <prof> "
            "<mag> [location] [--dry-run]\n");
}

static void print_config(const CsnwphaseCfg *c)
{
    printf("WphaseHome %s\n", c->wphase_home);
    printf("WphaseBin %s\n", c->wphase_bin);
    printf("GfDir %s\n", c->gf_dir);
    printf("StoreRoot %s\n", c->store_root);
    printf("SdsRoot %s\n", c->sds_root);
    printf("SdsFallbackHours %d\n", c->sds_fallback_hours);
    printf("StaFile %s\n", c->sta_file);
    printf("ResponseDir %s\n", c->resp_dir);
    printf("ResponsePattern %s\n", c->resp_pattern);
    printf("Dmin %g\n", c->dmin_deg);
    printf("Dmax %g\n", c->dmax_deg);
    printf("MinMagnitude %g\n", c->min_magnitude);
    printf("WinPre %d\n", c->win_pre_sec);
    printf("WinPost %d\n", c->win_post_sec);
    printf("ScreenTh %g\n", c->screen_th);
    printf("ScreenNr %g\n", c->screen_nr);
    printf("GridSearch %d\n", c->grid_search);
    printf("GsTsMin %g\n", c->gs_ts_min);
    printf("GsTsDt %g\n", c->gs_ts_dt);
    printf("GsTsMax %g\n", c->gs_ts_max);
    printf("GsTsNit %d\n", c->gs_ts_nit);
    printf("GsXyDx %g\n", c->gs_xy_dx);
    printf("GsXyNx %d\n", c->gs_xy_nx);
    printf("GsXyNopt %d\n", c->gs_xy_nopt);
    printf("GsXyNit %d\n", c->gs_xy_nit);
    printf("GsXyzDz %g\n", c->gs_xyz_dz);
    printf("GsXyzMinz %g\n", c->gs_xyz_minz);
    printf("GsXyzDx %g\n", c->gs_xyz_dx);
    printf("GsXyzNx %d\n", c->gs_xyz_nx);
    printf("GsXyzNopt %d\n", c->gs_xyz_nopt);
    printf("GsXyzNit %d\n", c->gs_xyz_nit);
    printf("GsHdsafe %d\n", c->gs_hdsafe);
    printf("GsOld %d\n", c->gs_old);
    printf("GsTsAuto %d\n", c->gs_ts_auto);
    printf("CorpusMinNused %d\n", c->corpus_min_nused);
    printf("TimeoutSec %d\n", c->timeout_sec);
    printf("KeepTmp %d\n", c->keep_tmp);
    printf("MaxConcurrent %d\n", c->max_concurrent);
    printf("Debug %d\n", c->debug);
    printf("Evname %s\n", c->evname);
    printf("SeedRef %s\n", c->seed_ref);
    printf("CmtFile %s\n", c->cmtfile);
    printf("GfdirRel %s\n", c->gfdir_rel);
    printf("FiltOrder %d\n", c->filt_order);
    printf("FiltPass %d\n", c->filt_pass);
    printf("Idec2 %g %g %g\n", c->idec2_idtr, c->idec2_preevent, c->idec2_fend);
    printf("Idec3 %g %g %d %g\n", c->idec3_fl, c->idec3_fh, c->idec3_nf,
           c->idec3_tol);
    printf("WpWin %g %g %g\n", c->wpwin[0], c->wpwin[1], c->wpwin[2]);
}

static int parse_time(const char *s, CsnwphaseInput *in)
{
    return sscanf(s, "%d-%d-%dT%d:%d:%lf", &in->year, &in->month, &in->day,
                  &in->hour, &in->minute, &in->second) == 6
               ? 0
               : -1;
}

/* "YYYY-MM-DDTHH:MM:SS" -> epoch UTC. */
static double epoch_of(const char *s)
{
    struct tm tm;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    double sec = 0.0;

    if (sscanf(s, "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) != 6)
        return 0.0;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = (int)sec;
    return (double)timegm(&tm) + (sec - (int)sec);
}

int main(int argc, char **argv)
{
    CsnwphaseCfg cfg;
    CsnwphaseInput in;
    CsnwphaseParamRow rows[CSNWPHASE_MAX_PARAMS];
    char err[256];
    int i, n;

    if (argc < 3) {
        usage();
        return 2;
    }

    if (csnwphase_cfg_load(&cfg, argv[1]) != 0)
        return 1;

    if (strcmp(argv[2], "--print-config") == 0) {
        print_config(&cfg);
        return 0;
    }

    if (strcmp(argv[2], "--parse") == 0) {
        CsnwphaseOut out;
        char json[1024], logf[1024];

        if (argc < 4) {
            usage();
            return 2;
        }
        if (csnwphase_parse(argv[3], &out) != 0) {
            fprintf(stderr, "csnwphase: %s\n", out.err);
            return 1;
        }

        snprintf(json, sizeof(json), "%s/wphase.json", argv[3]);
        snprintf(logf, sizeof(logf), "%s/full_output.txt", argv[3]);
        if (csnwphase_json_write(json, &out) != 0 ||
            csnwphase_log_write(argv[3], logf) < 0) {
            fprintf(stderr, "csnwphase: no se pudo escribir %s/%s\n", json, logf);
            return 1;
        }

        printf("ok=%d converged=%d Mw=%.2f M0=%.3e n_used=%d n_sta=%d\n",
               out.ok, out.converged, out.mw, out.m0, out.n_used, out.n_sta);
        printf("centroide lat=%.4f lon=%.4f prof=%.1f km (t_shift=%.1f s)\n",
               out.lat, out.lon, out.depth_km, out.t_shift_s);
        if (out.have_planes)
            printf("planos nodales: (%.1f %.1f %.1f) (%.1f %.1f %.1f)\n",
                   out.strike1, out.dip1, out.rake1, out.strike2, out.dip2,
                   out.rake2);
        printf("json: %s\nsalida completa: %s\n", json, logf);
        return 0;
    }

    if (strcmp(argv[2], "--fetch") == 0) {
        CsnwphaseWave w;
        CsnwphaseTrace tr;
        double t0, t1;
        int rc;

        /* --fetch <net> <sta> <chan> <t0> <t1> [loc] */
        if (argc < 8) {
            usage();
            return 2;
        }
        t0 = epoch_of(argv[6]);
        t1 = epoch_of(argv[7]);
        if (t0 <= 0.0 || t1 <= t0) {
            fprintf(stderr, "csnwphase: ventana invalida (usa YYYY-MM-DDTHH:MM:SS)\n");
            return 2;
        }

        if (csnwphase_wave_open(&w, &cfg) != 0)
            return 1;

        rc = csnwphase_wave_get(&w, argv[3], argv[4], argv[5],
                                (argc > 8) ? argv[8] : "", t0, t1, &tr);
        if (rc > 0) {
            long i;
            int32_t mn = tr.samples[0], mx = tr.samples[0];

            for (i = 1; i < tr.n; i++) {
                if (tr.samples[i] < mn)
                    mn = tr.samples[i];
                if (tr.samples[i] > mx)
                    mx = tr.samples[i];
            }
            printf("GET ok n=%ld rate=%.6f t0=%.3f min=%d max=%d\n", tr.n,
                   tr.samprate, tr.t0, mn, mx);
            csnwphase_trace_free(&tr);
        } else {
            printf("GET sin datos (rc=%d)\n", rc);
        }
        csnwphase_wave_close(&w);
        return rc > 0 ? 0 : 1;
    }

    if (strcmp(argv[2], "--run") == 0) {
        CsnwphaseOut out;
        char err[512], json[1024], logf[1024];
        int dry = 0, ai;

        for (ai = 3; ai < argc; ai++)
            if (strcmp(argv[ai], "--dry-run") == 0)
                dry = 1;

        /* --run <rundir> <t0> <lat> <lon> <prof> <mag> [location] [--dry-run] */
        if (argc < 9) {
            usage();
            return 2;
        }
        memset(&in, 0, sizeof(in));
        in.origin_epoch = epoch_of(argv[4]);
        if (in.origin_epoch <= 0.0) {
            fprintf(stderr, "csnwphase: fecha invalida '%s'\n", argv[4]);
            return 2;
        }
        {
            struct tm tm;
            time_t t = (time_t)in.origin_epoch;

            gmtime_r(&t, &tm);
            in.year = tm.tm_year + 1900;
            in.month = tm.tm_mon + 1;
            in.day = tm.tm_mday;
            in.hour = tm.tm_hour;
            in.minute = tm.tm_min;
            in.second = tm.tm_sec;
        }
        in.latitude = atof(argv[5]);
        in.longitude = atof(argv[6]);
        in.depth_km = atof(argv[7]);
        in.magnitude = atof(argv[8]);
        snprintf(in.location, sizeof(in.location), "%s",
                 (argc > 9 && argv[9][0] != '-') ? argv[9] : cfg.evname);
        snprintf(in.event_id, sizeof(in.event_id), "manual");
        snprintf(in.origin_id, sizeof(in.origin_id), "manual");

        if (in.magnitude < cfg.min_magnitude) {
            fprintf(stderr, "csnwphase: magnitud %.1f < umbral %.1f\n",
                    in.magnitude, cfg.min_magnitude);
            return 1;
        }

        if (dry) {
            int nsta = 0, ntr = 0;

            if (csnwphase_prep_write(&cfg, &in, argv[3], err, sizeof(err)) != 0) {
                fprintf(stderr, "csnwphase: %s\n", err);
                return 1;
            }
            if (csnwphase_run(&cfg, argv[3], 1, in.magnitude, err,
                              sizeof(err)) != 0) {
                fprintf(stderr, "csnwphase: %s\n", err);
                return 1;
            }
            printf("dry run: %s/run.sh escrito (sin traer ondas)\n", argv[3]);
            (void)nsta;
            (void)ntr;
            return 0;
        }

        {
            int nsta = 0, ntr = 0;

            if (csnwphase_input_prep(&cfg, &in, argv[3], &nsta, &ntr, err,
                                     sizeof(err)) != 0) {
                fprintf(stderr, "csnwphase: %s\n", err);
                return 1;
            }
            printf("entrada lista: %d estaciones en [%g,%g] grados, %d trazas\n",
                   nsta, cfg.dmin_deg, cfg.dmax_deg, ntr);
        }

        if (csnwphase_run(&cfg, argv[3], 0, in.magnitude, err, sizeof(err)) != 0) {
            fprintf(stderr, "csnwphase: %s\n", err);
            return 1;
        }

        if (csnwphase_parse(argv[3], &out) != 0) {
            fprintf(stderr, "csnwphase: sin solucion: %s\n", out.err);
            return 1;
        }
        snprintf(json, sizeof(json), "%s/wphase.json", argv[3]);
        snprintf(logf, sizeof(logf), "%s/full_output.txt", argv[3]);
        csnwphase_json_write(json, &out);
        csnwphase_log_write(argv[3], logf);
        printf("ok=%d converged=%d Mw=%.2f n_used=%d\n", out.ok, out.converged,
               out.mw, out.n_used);
        printf("json: %s\nsalida completa: %s\n", json, logf);
        return 0;
    }

    if (strcmp(argv[2], "--prep") != 0) {
        usage();
        return 2;
    }

    if (argc < 9) {
        usage();
        return 2;
    }

    memset(&in, 0, sizeof(in));
    if (parse_time(argv[4], &in) != 0) {
        fprintf(stderr, "csnwphase: fecha invalida '%s' (usa YYYY-MM-DDTHH:MM:SS)\n",
                argv[4]);
        return 2;
    }
    in.latitude = atof(argv[5]);
    in.longitude = atof(argv[6]);
    in.depth_km = atof(argv[7]);
    in.magnitude = atof(argv[8]);
    snprintf(in.location, sizeof(in.location), "%s",
             (argc > 9) ? argv[9] : cfg.evname);
    snprintf(in.event_id, sizeof(in.event_id), "manual");
    snprintf(in.origin_id, sizeof(in.origin_id), "manual");

    if (in.magnitude < cfg.min_magnitude) {
        fprintf(stderr,
                "csnwphase: magnitud %.1f por debajo del umbral %.1f "
                "(MinMagnitude)\n", in.magnitude, cfg.min_magnitude);
        return 1;
    }

    if (csnwphase_prep_write(&cfg, &in, argv[3], err, sizeof(err)) != 0) {
        fprintf(stderr, "csnwphase: %s\n", err);
        return 1;
    }

    printf("run dir: %s\n", argv[3]);
    if (csnwphase_params_build(&cfg, &in, rows, CSNWPHASE_MAX_PARAMS, &n) != 0)
        return 1;
    for (i = 0; i < n; i++)
        printf("%-34s %-46s %s\n", rows[i].name, rows[i].value, rows[i].source);

    return 0;
}
