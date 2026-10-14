#include "csnwphase.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/*
 * Tests del parser de la salida del motor y de la captura de la salida
 * completa. Headless y sin procesos: sólo archivos.
 *
 * Los fixtures son RECONSTRUCCIONES del formato del motor (ver la cabecera de
 * cada uno); F0.4 los reemplaza por la salida de una corrida real.
 */

#define ROOT "/tmp/csnwphase_parse"

static const char *data_dir(void)
{
    const char *d = getenv("CSNWPHASE_DATA_DIR");

    return (d && d[0]) ? d : "tests/data";
}

static void mkdir_p(const char *path)
{
    char tmp[1024];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[4096];
    size_t n;

    assert(in);
    out = fopen(dst, "wb");
    assert(out);
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        assert(fwrite(buf, 1, n, out) == n);
    fclose(in);
    assert(fclose(out) == 0);
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    assert(f);
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    assert(buf);
    assert(fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void setup_run(const char *run, const char *cmt_name, const char *cmt_src,
                      const char *log_src)
{
    char p[1024];

    snprintf(p, sizeof(p), "%s", run);
    mkdir_p(p);
    snprintf(p, sizeof(p), "%s/LOG", run);
    mkdir_p(p);
    snprintf(p, sizeof(p), "%s/%s", run, cmt_name);
    copy_file(cmt_src, p);
    snprintf(p, sizeof(p), "%s/LOG/gs_o_wpinversion.log", run);
    copy_file(log_src, p);
}

static void test_converged(void)
{
    CsnwphaseOut out;
    char run[512], src[512];
    double want_epoch;

    snprintf(run, sizeof(run), ROOT "/run1");
    snprintf(src, sizeof(src), "%s/xy_wcmtsolution.txt", data_dir());
    setup_run(run, "xy_WCMTSOLUTION", src,
              "tests/data/gs_o_wpinversion.log");

    assert(csnwphase_parse(run, &out) == 0);
    assert(out.ok == 1);
    assert(out.converged == 1);
    assert(fabs(out.mw - 8.83) < 1e-9);
    assert(fabs(out.m0 - 2.24e29) < 1e15);
    assert(out.n_used == 42);
    assert(out.n_sta == 2);
    assert(fabs(out.lat - (-30.9120)) < 1e-9);
    assert(fabs(out.lon - (-71.8900)) < 1e-9);
    assert(fabs(out.depth_km - 22.5) < 1e-9);
    assert(fabs(out.t_shift_s - 39.2809) < 1e-9);
    assert(fabs(out.half_dur_s - 39.2809) < 1e-9);
    assert(out.have_mt == 1);
    assert(out.have_planes == 1);
    assert(fabs(out.m_rr - 8.4e27) < 1e21);
    assert(fabs(out.m_tp - (-3.4e27)) < 1e21);
    assert(fabs(out.strike1 - 12.30) < 1e-9);
    assert(fabs(out.dip1 - 22.40) < 1e-9);
    assert(fabs(out.rake1 - (-85.60)) < 1e-9);
    assert(fabs(out.strike2 - 195.70) < 1e-9);
    assert(fabs(out.dip2 - 70.20) < 1e-9);
    assert(fabs(out.rake2 - (-95.30)) < 1e-9);
    assert(fabs(out.eig[0] - 8.4e27) < 1e21);
    assert(fabs(out.global_rms - 0.0421) < 1e-9);
    assert(strcmp(out.cmt_file, "xy_WCMTSOLUTION") == 0);
    assert(strcmp(out.log_file, "LOG/gs_o_wpinversion.log") == 0);

    {
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        tm.tm_year = 2015 - 1900;
        tm.tm_mon = 9 - 1;
        tm.tm_mday = 16;
        tm.tm_hour = 22;
        tm.tm_min = 54;
        tm.tm_sec = 32;
        want_epoch = (double)timegm(&tm);
        assert(fabs(out.origin_epoch - want_epoch) < 1e-6);
        assert(fabs(out.centroid_epoch - (want_epoch + 39.2809)) < 1e-3);
    }

    printf("  convergente ok (Mw %.2f, n_used %d, centroide %.4f/%.4f/%.1f)\n",
           out.mw, out.n_used, out.lat, out.lon, out.depth_km);
}

static void test_not_converged(void)
{
    CsnwphaseOut out;
    char run[512], src[512];

    snprintf(run, sizeof(run), ROOT "/run2");
    snprintf(src, sizeof(src), "%s/xy_wcmtsolution.txt", data_dir());
    setup_run(run, "WCMTSOLUTION", src,
              "tests/data/gs_o_wpinversion_noth.log");

    assert(csnwphase_parse(run, &out) == 0);
    assert(out.ok == 1);
    assert(out.converged == 0);
    assert(out.have_planes == 0);
    assert(out.n_used == 3);
    assert(strcmp(out.cmt_file, "WCMTSOLUTION") == 0);
    printf("  no convergente ok (converged=0, n_used=%d)\n", out.n_used);
}

static void test_empty(void)
{
    CsnwphaseOut out;
    char run[512];

    snprintf(run, sizeof(run), ROOT "/run3");
    mkdir_p(run);

    assert(csnwphase_parse(run, &out) == -1);
    assert(out.ok == 0);
    assert(out.err[0] != '\0');
    printf("  run dir vacio ok: %s\n", out.err);
}

static void test_json_and_log(void)
{
    CsnwphaseOut out;
    char run[512], json[512], logf[512];
    char *txt;

    snprintf(run, sizeof(run), ROOT "/run1");
    assert(csnwphase_parse(run, &out) == 0);

    snprintf(json, sizeof(json), ROOT "/wphase.json");
    assert(csnwphase_json_write(json, &out) == 0);
    txt = slurp(json);
    assert(strstr(txt, "\"mw\": 8.8300") != NULL);
    assert(strstr(txt, "\"converged\": 1") != NULL);
    assert(strstr(txt, "\"n_used_rec\": 42") != NULL);
    assert(strstr(txt, "\"strike\": 12.30") != NULL);
    assert(strstr(txt, "\"origin_time\": \"2015-09-16 22:54:32\"") != NULL);
    free(txt);
    printf("  wphase.json ok\n");

    snprintf(logf, sizeof(logf), ROOT "/full.txt");
    assert(csnwphase_log_write(run, logf) > 0);
    txt = slurp(logf);
    assert(strstr(txt, "===== i_master =====") != NULL);
    assert(strstr(txt, "===== xy_WCMTSOLUTION =====") != NULL);
    assert(strstr(txt, "===== LOG/gs_o_wpinversion.log =====") != NULL);
    assert(strstr(txt, "Wmag") != NULL);
    assert(strstr(txt, "Mrr:") != NULL);
    /* lo que no existe se declara ausente, no se omite en silencio */
    assert(strstr(txt, "===== o_wpinversion =====\n(ausente)") != NULL);
    free(txt);
    printf("  salida completa ok\n");
}

int main(void)
{
    printf("csnwphase: tests del parser de la salida\n");

    mkdir_p(ROOT);

    test_converged();
    test_not_converged();
    test_empty();
    test_json_and_log();

    printf("ALL CSNWPHASE PARSE TESTS PASSED\n");
    return 0;
}
