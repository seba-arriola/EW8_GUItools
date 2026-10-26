#define _GNU_SOURCE
#include "wstore.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char g_root[64];

/*
 * Contrato de `wstore_prune` (wstore.c): borra los segmentos `YYYYMMDD.ws`
 * (y su `.idx`) anteriores a la medianoche UTC de hoy menos `keep_days` dias.
 * El test calcula los dias con el MISMO reloj y la MISMA regla (UTC), nunca con
 * fechas fijas: tiene que valer cualquier dia del ano.
 */
static void dayname(char *out, size_t n, int days_ago)
{
    time_t    now = time(NULL), t;
    struct tm tm;

    gmtime_r(&now, &tm);
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
    t = timegm(&tm) - (time_t)days_ago * 86400;
    gmtime_r(&t, &tm);
    snprintf(out, n, "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static int mkdir_p(const char *path)
{
    return mkdir(path, 0777) == 0 || access(path, F_OK) == 0;
}

/* `wstore_prune` solo mira el NOMBRE del fichero: jamas abre el contenido.
 * Se escriben 4 bytes para poder verificar `bytes_freed`. */
static void make_seg(const char *scnl, int days_ago)
{
    char day[64], p[1400];
    FILE *f;

    dayname(day, sizeof(day), days_ago);
    snprintf(p, sizeof(p), "%s/%s/%s.ws", g_root, scnl, day);
    f = fopen(p, "w"); if (f) { fputs("wxyz", f); fclose(f); }
    snprintf(p, sizeof(p), "%s/%s/%s.idx", g_root, scnl, day);
    f = fopen(p, "w"); if (f) { fputs("wxyz", f); fclose(f); }
}

static int seg_exists(const char *scnl, int days_ago, const char *ext)
{
    char day[64], p[1400];

    dayname(day, sizeof(day), days_ago);
    snprintf(p, sizeof(p), "%s/%s/%s.%s", g_root, scnl, day, ext);
    return access(p, F_OK) == 0;
}

static int rel_exists(const char *rel)
{
    char p[1400];

    snprintf(p, sizeof(p), "%s/%s", g_root, rel);
    return access(p, F_OK) == 0;
}

static void touch_rel(const char *rel)
{
    char p[1400];
    FILE *f;

    snprintf(p, sizeof(p), "%s/%s", g_root, rel);
    f = fopen(p, "w"); if (f) fclose(f);
}

static void rm_tree(const char *path)
{
    DIR *d = opendir(path);

    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char p[1400];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
            rm_tree(p);
        }
        closedir(d);
    }
    remove(path);
}

static double day_epoch(int days_ago)
{
    time_t    now = time(NULL), t;
    struct tm tm;

    gmtime_r(&now, &tm);
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
    t = timegm(&tm) - (time_t)days_ago * 86400;
    return (double)t + 3600.0;   /* dentro del dia UTC */
}

int main(void)
{
    WStore      *s = NULL;
    const char  *SCNL0 = "CX.ST00.--.HHZ";
    const char  *SCNL1 = "CX.ST01.--.HHZ";
    WStoreTrace  tr;
    int          k;

    strcpy(g_root, "/tmp/wstore_prune_XXXXXX");
    if (!mkdtemp(g_root)) { perror("mkdtemp"); return 1; }
    CHECK(mkdir_p(g_root));

    /* --- Escenario A: RetentionDays 7 ----------------------------------- */
    char d0[1400];
    snprintf(d0, sizeof(d0), "%s/%s", g_root, SCNL0);
    CHECK(mkdir_p(d0));
    for (k = 0; k <= 10; k++) make_seg(SCNL0, k);

    CHECK(wstore_open(&s, g_root, NULL) == 0);
    CHECK(wstore_prune(s, 7) == 0);

    /* El dia del corte (7 dias atras) NO se borra; del 8 en adelante, si. */
    for (k = 0; k <= 7; k++) {
        CHECK(seg_exists(SCNL0, k, "ws"));
        CHECK(seg_exists(SCNL0, k, "idx"));
    }
    for (k = 8; k <= 10; k++) {
        CHECK(!seg_exists(SCNL0, k, "ws"));
        CHECK(!seg_exists(SCNL0, k, "idx"));   /* CA3: el .idx se va con el .ws */
    }

    /* CA5: un rango que cae entero en dias borrados = "sin datos" (1), no error. */
    memset(&tr, 0, sizeof(tr));
    k = wstore_get(s, "ST00", "CX", "HHZ", "--",
                   day_epoch(9), day_epoch(8), &tr);
    CHECK(k == 1);
    if (tr.samples) wstore_trace_free(&tr);
    wstore_close(s);

    /* --- CA4: nada que no sea exactamente `YYYYMMDD.ws` (fecha real) ---- */
    {
        char old[64];
        dayname(old, sizeof(old), 9);
        touch_rel("CX.ST00.--.HHZ/notaseg.ws");
        touch_rel("CX.ST00.--.HHZ/20260101.txt");
        touch_rel("CX.ST00.--.HHZ/LEEME");
        touch_rel("CX.ST00.--.HHZ/2026010.ws");        /* 7 digitos */
        touch_rel("CX.ST00.--.HHZ/20260100.ws");       /* dia 00: timegm normaliza */
        touch_rel("CX.ST00.--.HHZ/20260231.ws");       /* 31 de febrero */
        touch_rel("CX.ST00.--.HHZ/20261301.ws");       /* mes 13 */
        touch_rel("CX.ST00.--.HHZ/202610160.ws");      /* 9 digitos */
        touch_rel("CX.ST00.--.HHZ/20261016.ws.bak");   /* respaldo, no segmento */
        char rel[256];
        snprintf(rel, sizeof(rel), "%s/%s.idx", SCNL0, old);   /* .idx huerfano */
        touch_rel(rel);

        CHECK(wstore_open(&s, g_root, NULL) == 0);
        CHECK(wstore_prune(s, 7) == 0);
        wstore_close(s);

        CHECK(rel_exists("CX.ST00.--.HHZ/notaseg.ws"));
        CHECK(rel_exists("CX.ST00.--.HHZ/20260101.txt"));
        CHECK(rel_exists("CX.ST00.--.HHZ/LEEME"));
        CHECK(rel_exists("CX.ST00.--.HHZ/2026010.ws"));
        CHECK(rel_exists("CX.ST00.--.HHZ/20260100.ws"));
        CHECK(rel_exists("CX.ST00.--.HHZ/20260231.ws"));
        CHECK(rel_exists("CX.ST00.--.HHZ/20261301.ws"));
        CHECK(rel_exists("CX.ST00.--.HHZ/202610160.ws"));
        CHECK(rel_exists("CX.ST00.--.HHZ/20261016.ws.bak"));
        CHECK(rel_exists(rel));
    }

    /* --- CA2: keep_days <= 0 no borra nada ------------------------------- */
    {
        char d1[1400];
        snprintf(d1, sizeof(d1), "%s/%s", g_root, SCNL1);
        CHECK(mkdir_p(d1));
        for (k = 20; k <= 30; k++) make_seg(SCNL1, k);

        CHECK(wstore_open(&s, g_root, NULL) == 0);
        CHECK(wstore_prune(s, 0) == 0);
        CHECK(wstore_prune(s, -1) == 0);
        wstore_close(s);

        for (k = 20; k <= 30; k++) {
            CHECK(seg_exists(SCNL1, k, "ws"));
            CHECK(seg_exists(SCNL1, k, "idx"));
        }
    }

    /* --- CA3/CA6: conteo exacto y dry-run ------------------------------- */
    {
        char              r2[] = "/tmp/wstore_prune_st_XXXXXX";
        char              d2[1400];
        WStorePruneStats  st, dry;

        CHECK(mkdtemp(r2) != NULL);
        snprintf(g_root, sizeof(g_root), "%s", r2);
        snprintf(d2, sizeof(d2), "%s/%s", g_root, SCNL0);
        CHECK(mkdir_p(d2));
        for (k = 0; k <= 5; k++) make_seg(SCNL0, k + 30);   /* 30..35 dias: todos viejos */

        CHECK(wstore_open(&s, g_root, NULL) == 0);

        /* CA6: el dry-run cuenta lo mismo y NO borra nada */
        CHECK(wstore_prune_ex(s, 7, WSTORE_PRUNE_DRY_RUN, &dry) == 0);
        CHECK(dry.segments_removed == 6);
        CHECK(dry.idx_removed == 6);
        CHECK(dry.bytes_freed == 48);                        /* 6 x (4+4) bytes */
        for (k = 0; k <= 5; k++) CHECK(seg_exists(SCNL0, k + 30, "ws"));

        /* CA3: la purga real da el MISMO conteo y ahora si borra */
        CHECK(wstore_prune_stats(s, 7, &st) == 0);
        CHECK(st.segments_removed == dry.segments_removed);
        CHECK(st.idx_removed == dry.idx_removed);
        CHECK(st.bytes_freed == dry.bytes_freed);
        for (k = 0; k <= 5; k++) {
            CHECK(!seg_exists(SCNL0, k + 30, "ws"));
            CHECK(!seg_exists(SCNL0, k + 30, "idx"));
        }

        /* Sin nada que borrar el conteo queda a cero */
        CHECK(wstore_prune_stats(s, 7, &st) == 0);
        CHECK(st.segments_removed == 0 && st.idx_removed == 0 && st.bytes_freed == 0);

        /* keep_days <= 0 no borra ni cuenta */
        make_seg(SCNL0, 40);
        CHECK(wstore_prune_stats(s, 0, &st) == 0);
        CHECK(st.segments_removed == 0 && st.idx_removed == 0);
        CHECK(seg_exists(SCNL0, 40, "ws"));
        wstore_close(s);

        rm_tree(r2);
    }

    rm_tree(g_root);

    if (failures) { printf("%d FALLOS\n", failures); return 1; }
    printf("test_prune OK\n");
    return 0;
}
