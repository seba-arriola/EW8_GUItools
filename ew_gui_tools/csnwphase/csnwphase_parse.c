#include "csnwphase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Parseo de la salida del motor. Lógica PURA (sin GTK, sin anillos, sin
 * procesos): recibe el run dir y devuelve CsnwphaseOut.
 *
 * Contrato (verificado en el motor y en el parser del CSN, report.py:22-95):
 *   xy_WCMTSOLUTION / WCMTSOLUTION:
 *     línea PDE por posiciones fijas (report.py:106-112),
 *     time shift:, half duration:, latitude:, longitude:, depth:,
 *     Mrr:, Mtt:, Mpp:, Mrt:, Mrp:, Mtp:
 *   LOG/gs_o_wpinversion.log (o LOG/wpinversion.noth.log):
 *     Wmag:<f> ;, ;Wmom <f> ;, n_used_rec:<int>,
 *     W_eigenvalues: <f> <f> <f>, W_bestnodal planes: <f>×6, `stat:` por estación
 */

static int grab_double(const char *line, const char *key, double *out)
{
    const char *p = strstr(line, key);
    char *end;
    double v;

    if (!p)
        return 0;
    p += strlen(key);
    while (*p == ':' || *p == ' ' || *p == '\t')
        p++;
    v = strtod(p, &end);
    if (end == p)
        return 0;
    *out = v;
    return 1;
}

static int grab_int(const char *line, const char *key, int *out)
{
    const char *p = strstr(line, key);
    char *end;
    long v;

    if (!p)
        return 0;
    p += strlen(key);
    while (*p == ':' || *p == ' ' || *p == '\t')
        p++;
    v = strtol(p, &end, 10);
    if (end == p)
        return 0;
    *out = (int)v;
    return 1;
}

static int grab_doubles(const char *line, const char *key, double *out, int n)
{
    const char *p = strstr(line, key);
    char *end;
    int i;

    if (!p)
        return 0;
    p += strlen(key);
    while (*p == ':' || *p == ' ' || *p == '\t')
        p++;
    for (i = 0; i < n; i++) {
        out[i] = strtod(p, &end);
        if (end == p)
            return 0;
        p = end;
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
    }
    return 1;
}

static void epoch_to_iso(double epoch, char *out, size_t n)
{
    time_t t = (time_t)epoch;
    struct tm tm;
    char buf[32];
    unsigned y, mo, d, h, mi, s;

    gmtime_r(&t, &tm);
    y = (unsigned)(tm.tm_year + 1900) % 10000u;
    mo = (unsigned)(tm.tm_mon + 1) % 100u;
    d = (unsigned)tm.tm_mday % 100u;
    h = (unsigned)tm.tm_hour % 100u;
    mi = (unsigned)tm.tm_min % 100u;
    s = (unsigned)tm.tm_sec % 100u;
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", y, mo, d, h, mi, s);
    snprintf(out, n, "%s", buf);
}

/* Línea PDE: " PDE YYYY MM DD HH MM SS.SS lat lon prof mag mag location". */
static void parse_pde(const char *line, CsnwphaseOut *out)
{
    int y, mo, d, h, mi;
    double sec;
    struct tm tm;

    if (sscanf(line, " PDE %d %d %d %d %d %lf", &y, &mo, &d, &h, &mi, &sec) != 6)
        return;

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = (int)sec;
    out->origin_epoch = (double)timegm(&tm) + (sec - (int)sec);
}

static int read_cmt(const char *path, CsnwphaseOut *out)
{
    FILE *f = fopen(path, "r");
    char line[1024];

    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "PDE") && strlen(line) > 27)
            parse_pde(line, out);

        grab_double(line, "time shift", &out->t_shift_s);
        grab_double(line, "half duration", &out->half_dur_s);
        grab_double(line, "latitude", &out->lat);
        grab_double(line, "longitude", &out->lon);
        grab_double(line, "depth", &out->depth_km);

        if (grab_double(line, "Mrr", &out->m_rr) |
            grab_double(line, "Mtt", &out->m_tt) |
            grab_double(line, "Mpp", &out->m_pp) |
            grab_double(line, "Mrt", &out->m_rt) |
            grab_double(line, "Mrp", &out->m_rp) |
            grab_double(line, "Mtp", &out->m_tp))
            out->have_mt = 1;
    }

    fclose(f);
    return 1;
}

static int read_log(const char *path, CsnwphaseOut *out)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    double planes[6];
    int have_mw = 0;

    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f)) {
        if (grab_double(line, "Wmag", &out->mw))
            have_mw = 1;
        grab_double(line, "Wmom", &out->m0);
        grab_int(line, "n_used_rec", &out->n_used);
        grab_doubles(line, "W_eigenvalues", out->eig, 3);
        grab_double(line, "global_rms", &out->global_rms);
        if (grab_doubles(line, "W_bestnodal planes", planes, 6)) {
            out->strike1 = planes[0];
            out->dip1 = planes[1];
            out->rake1 = planes[2];
            out->strike2 = planes[3];
            out->dip2 = planes[4];
            out->rake2 = planes[5];
            out->have_planes = 1;
        }
        if (strncmp(line, "stat:", 5) == 0)
            out->n_sta++;
    }

    fclose(f);
    out->converged = have_mw;
    return have_mw;
}

int csnwphase_parse(const char *rundir, CsnwphaseOut *out)
{
    static const char *cmt_names[] = { "xy_WCMTSOLUTION", "WCMTSOLUTION" };
    static const char *log_names[] = { "LOG/gs_o_wpinversion.log",
                                       "LOG/wpinversion.noth.log",
                                       "LOG/wpinversion.log" };
    char path[1024];
    int i, have_log = 0;

    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));

    if (!rundir || !rundir[0]) {
        snprintf(out->err, sizeof(out->err), "rundir vacio");
        return -1;
    }

    for (i = 0; i < (int)(sizeof(cmt_names) / sizeof(cmt_names[0])); i++) {
        snprintf(path, sizeof(path), "%s/%s", rundir, cmt_names[i]);
        if (read_cmt(path, out)) {
            snprintf(out->cmt_file, sizeof(out->cmt_file), "%s", cmt_names[i]);
            break;
        }
    }

    for (i = 0; i < (int)(sizeof(log_names) / sizeof(log_names[0])); i++) {
        snprintf(path, sizeof(path), "%s/%s", rundir, log_names[i]);
        if (read_log(path, out)) {
            snprintf(out->log_file, sizeof(out->log_file), "%s", log_names[i]);
            have_log = 1;
            break;
        }
    }

    out->centroid_epoch = out->origin_epoch + out->t_shift_s;
    out->ok = (out->cmt_file[0] != '\0') || have_log;

    if (!out->ok)
        snprintf(out->err, sizeof(out->err),
                 "sin xy_WCMTSOLUTION/WCMTSOLUTION ni LOG/*.log en %s", rundir);

    return out->ok ? 0 : -1;
}

int csnwphase_json_write(const char *path, const CsnwphaseOut *out)
{
    FILE *f;
    char iso[32], ciso[32];

    if (!path || !out)
        return -1;

    f = fopen(path, "w");
    if (!f)
        return -1;

    epoch_to_iso(out->origin_epoch, iso, sizeof(iso));
    epoch_to_iso(out->centroid_epoch, ciso, sizeof(ciso));

    fprintf(f, "{\n");
    fprintf(f, "  \"ok\": %d,\n", out->ok);
    fprintf(f, "  \"converged\": %d,\n", out->converged);
    fprintf(f, "  \"mw\": %.4f,\n", out->mw);
    fprintf(f, "  \"m0_nm\": %.6e,\n", out->m0);
    fprintf(f, "  \"n_used_rec\": %d,\n", out->n_used);
    fprintf(f, "  \"n_sta_log\": %d,\n", out->n_sta);
    fprintf(f, "  \"global_rms\": %.6g,\n", out->global_rms);
    fprintf(f, "  \"centroid\": { \"lat\": %.4f, \"lon\": %.4f, \"depth_km\": %.1f },\n",
            out->lat, out->lon, out->depth_km);
    fprintf(f, "  \"origin_time\": \"%s\",\n", iso);
    fprintf(f, "  \"centroid_time\": \"%s\",\n", ciso);
    fprintf(f, "  \"time_shift_s\": %.4f,\n", out->t_shift_s);
    fprintf(f, "  \"half_duration_s\": %.4f,\n", out->half_dur_s);
    fprintf(f, "  \"moment_tensor\": { \"m_rr\": %.6e, \"m_tt\": %.6e, \"m_pp\": %.6e, "
               "\"m_rt\": %.6e, \"m_rp\": %.6e, \"m_tp\": %.6e },\n",
            out->m_rr, out->m_tt, out->m_pp, out->m_rt, out->m_rp, out->m_tp);
    fprintf(f, "  \"eigenvalues\": [%.6e, %.6e, %.6e],\n", out->eig[0],
            out->eig[1], out->eig[2]);
    fprintf(f, "  \"nodal_planes\": [ { \"strike\": %.2f, \"dip\": %.2f, \"rake\": %.2f }, "
               "{ \"strike\": %.2f, \"dip\": %.2f, \"rake\": %.2f } ],\n",
            out->strike1, out->dip1, out->rake1, out->strike2, out->dip2, out->rake2);
    fprintf(f, "  \"have_mt\": %d,\n", out->have_mt);
    fprintf(f, "  \"have_planes\": %d,\n", out->have_planes);
    fprintf(f, "  \"cmt_file\": \"%s\",\n", out->cmt_file);
    fprintf(f, "  \"log_file\": \"%s\"\n", out->log_file);
    fprintf(f, "}\n");

    return fclose(f) == 0 ? 0 : -1;
}
