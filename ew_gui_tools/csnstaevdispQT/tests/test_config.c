/* C1: configuracion de csnstaevdispQT (mismas claves/defaults que el GTK). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sev_capi.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);             \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

static char g_dir[256];

static void write_cfg(const char *name, const char *extra1, const char *extra2)
{
    char path[600];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "MyModuleId      MOD_STAEVDISP\n");
    fprintf(f, "RingName        HYPO_RING\n");
    fprintf(f, "HeartBeatInt    30\n");
    fprintf(f, "LogFile         1\n");
    fprintf(f, "DbEnvFile       db.env\n");
    fprintf(f, "MapImageFile    world_map.jpg\n");
    fprintf(f, "StaFile         estaciones_107.txt\n");
    fprintf(f, "StationSize     15\n");
    fprintf(f, "QuakeFile       csnhypodbp_hist.txt\n");   /* obsoleta: ignorar */
    if (extra1 && *extra1)
        fprintf(f, "%s\n", extra1);
    if (extra2 && *extra2)
        fprintf(f, "%s\n", extra2);
    fclose(f);
}

static char *slurp(FILE *f)
{
    static char buf[8192];
    size_t n;

    fflush(f);
    rewind(f);
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    return buf;
}

int main(void)
{
    SevConfig *cfg;
    char path[600];
    char err[512];
    char ov1[700], ov2[700];
    FILE *out, *errf;

    snprintf(g_dir, sizeof g_dir, "/tmp/sev_cfg_XXXXXX");
    if (!mkdtemp(g_dir)) {
        printf("FAIL: mkdtemp\n");
        return 1;
    }

    /* Defaults: DbHistoryHours 24, StationSize 15 y claves ignoradas. */
    write_cfg("t1.d", "", "");
    snprintf(path, sizeof path, "%s/t1.d", g_dir);
    cfg = sev_config_load(path, err, sizeof err);
    CHECK(cfg != NULL, "config minima debe cargar");
    if (cfg) {
        CHECK(cfg->db_hours == 24, "DbHistoryHours default 24");
        CHECK(cfg->station_size == 15.0, "StationSize leido");
        CHECK(cfg->heart_beat_int == 30, "HeartBeatInt leido");
        CHECK(cfg->log_file == 1, "LogFile leido");
        CHECK(strcmp(cfg->my_module, "MOD_STAEVDISP") == 0, "MyModuleId");
        CHECK(strcmp(cfg->ring, "HYPO_RING") == 0, "RingName");
        CHECK(strcmp(cfg->map_image, "world_map.jpg") == 0, "MapImageFile");
        CHECK(strcmp(cfg->sta_file, "estaciones_107.txt") == 0, "StaFile");
        CHECK(cfg->n_overlay_files == 0, "sin overlays");

        out = tmpfile();
        sev_print_config(cfg, out);
        CHECK(strstr(slurp(out), "station_size=15.000000") != NULL,
              "--print-config station_size");
        CHECK(strstr(slurp(out), "overlay_files=0") != NULL,
              "--print-config overlay_files");
        fclose(out);
        sev_config_free(cfg);
    }

    /* Falta StationSize: el GTK falla y el port tambien. */
    {
        FILE *f;
        char p2[600];
        snprintf(p2, sizeof p2, "%s/t2.d", g_dir);
        f = fopen(p2, "wb");
        fprintf(f, "MyModuleId      MOD_STAEVDISP\n");
        fprintf(f, "RingName        HYPO_RING\n");
        fprintf(f, "HeartBeatInt    30\n");
        fprintf(f, "LogFile         1\n");
        fprintf(f, "StaFile         estaciones_107.txt\n");
        fprintf(f, "DbEnvFile       db.env\n");
        fprintf(f, "MapImageFile    world_map.jpg\n");
        fclose(f);

        cfg = sev_config_load(p2, err, sizeof err);
        CHECK(cfg == NULL, "falta StationSize debe fallar");
        CHECK(strstr(err, "missing parameters") != NULL,
              "el error nombra missing parameters");
    }

    /* Overlays: repetibles, OverlayColor/Width/Hide. */
    snprintf(ov1, sizeof ov1, "%s/ov1.lines", g_dir);
    snprintf(ov2, sizeof ov2, "%s/ov2.lines", g_dir);
    {
        FILE *f = fopen(ov1, "wb");
        fprintf(f, "#layer name=visib color=FF0000 width=2\n");
        fprintf(f, "0 0\n1 1\n");
        fclose(f);
        f = fopen(ov2, "wb");
        fprintf(f, "#layer name=oculta color=0000FF width=2\n");
        fprintf(f, "2 2\n3 3\n");
        fclose(f);
    }

    {
        char e1[800], e2[800];
        snprintf(e1, sizeof e1, "OverlayFile     %s", ov1);
        snprintf(e2, sizeof e2, "OverlayFile     %s", ov2);
        write_cfg("t3.d", e1, e2);
        snprintf(path, sizeof path, "%s/t3.d", g_dir);
        cfg = sev_config_load(path, err, sizeof err);
        CHECK(cfg != NULL, "config con overlays");
        if (cfg) {
            CHECK(cfg->n_overlay_files == 2, "dos OverlayFile");
            out = tmpfile();
            errf = tmpfile();
            sev_check_overlays(cfg, out, errf);
            CHECK(strstr(slurp(out), "overlay_layers=2 overlay_polylines=2")
                      != NULL,
                  "reporte de capas/polilineas");
            fclose(out);
            fclose(errf);
            sev_config_free(cfg);
        }
    }

    /* OverlayHide apaga la capa pedida y deja la otra. */
    {
        char e1[800], e2[200];
        snprintf(e1, sizeof e1, "OverlayFile     %s", ov1);
        snprintf(e2, sizeof e2, "OverlayHide     visib");
        write_cfg("t4.d", e1, e2);
        snprintf(path, sizeof path, "%s/t4.d", g_dir);
        cfg = sev_config_load(path, err, sizeof err);
        CHECK(cfg != NULL, "config con OverlayHide");
        if (cfg) {
            out = tmpfile();
            errf = tmpfile();
            sev_check_overlays(cfg, out, errf);
            CHECK(strstr(slurp(out),
                         "layer visib visible=0 color=FF0000 width=2.0 "
                         "polylines=1 vertices=2") != NULL,
                  "OverlayHide aplicado");
            fclose(out);
            fclose(errf);
            sev_config_free(cfg);
        }
    }

    {
        char cmd[400];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
        if (system(cmd) != 0)
            printf("nota: no se pudo limpiar %s\n", g_dir);
    }

    if (g_fail == 0)
        printf("OK: config de csnstaevdispQT (8 claves, overlays y reporte)\n");
    return g_fail;
}
