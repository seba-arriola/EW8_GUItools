/* C1: configuracion de csnrvQT (mismas claves y defaults que csnrv.d).
 *
 * Cubre lo que el script del GTK no puede: clamps, claves repetibles,
 * OverlayColor con dos valores y el reporte de --check-overlays.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "crv_capi.h"

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
    fprintf(f, "MyModuleId      MOD_CSNRV\n");
    fprintf(f, "RingName        HYPO_RING_REF\n");
    fprintf(f, "HeartBeatInt    30\n");
    fprintf(f, "LogFile         1\n");
    fprintf(f, "QuakeFile       csnhypodbp_hist.txt\n");
    fprintf(f, "MapImageFile    world_map.jpg\n");
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
    CrvConfig *cfg;
    char path[600];
    char err[512];

    snprintf(g_dir, sizeof g_dir, "/tmp/crv_cfg_XXXXXX");
    if (!mkdtemp(g_dir)) {
        printf("FAIL: mkdtemp\n");
        return 1;
    }

    /* Defaults: InitialZoom 8.0 y DbHistoryHours 24. */
    write_cfg("t1.d", "", "");
    snprintf(path, sizeof path, "%s/t1.d", g_dir);
    cfg = crv_config_load(path, err, sizeof err);
    CHECK(cfg != NULL, "config minima debe cargar");
    if (cfg) {
        CHECK(cfg->initial_zoom == 8.0, "InitialZoom default 8.0");
        CHECK(cfg->db_hours == 24, "DbHistoryHours default 24");
        CHECK(strcmp(cfg->my_module, "MOD_CSNRV") == 0, "MyModuleId");
        CHECK(strcmp(cfg->ring, "HYPO_RING_REF") == 0, "RingName");
        crv_config_free(cfg);
    }

    /* Clamp alto y bajo. */
    write_cfg("t2.d", "InitialZoom     999", "");
    snprintf(path, sizeof path, "%s/t2.d", g_dir);
    cfg = crv_config_load(path, err, sizeof err);
    CHECK(cfg && cfg->initial_zoom == 50.0, "InitialZoom 999 -> 50.0");
    crv_config_free(cfg);

    write_cfg("t3.d", "InitialZoom     0.01", "");
    snprintf(path, sizeof path, "%s/t3.d", g_dir);
    cfg = crv_config_load(path, err, sizeof err);
    CHECK(cfg && cfg->initial_zoom == 0.2, "InitialZoom 0.01 -> 0.2");
    crv_config_free(cfg);

    /* Claves repetibles + OverlayColor con dos valores. */
    write_cfg("t4.d", "OverlayFile     a.lines",
              "OverlayFile     b.lines");
    snprintf(path, sizeof path, "%s/t4.d", g_dir);
    cfg = crv_config_load(path, err, sizeof err);
    CHECK(cfg && cfg->n_overlay_files == 2, "OverlayFile repetible");
    crv_config_free(cfg);

    write_cfg("t5.d", "OverlayColor    subduccion 112233", "OverlayWidth    2.0");
    snprintf(path, sizeof path, "%s/t5.d", g_dir);
    cfg = crv_config_load(path, err, sizeof err);
    CHECK(cfg && cfg->n_overlay_colors == 1, "OverlayColor con dos valores");
    CHECK(cfg && strcmp(cfg->overlay_colors[0].layer, "subduccion") == 0,
          "OverlayColor capa");
    CHECK(cfg && strcmp(cfg->overlay_colors[0].hex, "112233") == 0,
          "OverlayColor hex");
    CHECK(cfg && cfg->overlay_width == 2.0, "OverlayWidth");
    crv_config_free(cfg);

    /* Falta MapImageFile -> NULL. */
    snprintf(path, sizeof path, "%s/t6.d", g_dir);
    {
        FILE *f = fopen(path, "wb");
        fprintf(f, "MyModuleId MOD_CSNRV\nRingName HYPO_RING_REF\n");
        fprintf(f, "HeartBeatInt 30\nLogFile 1\n");
        fclose(f);
    }
    cfg = crv_config_load(path, err, sizeof err);
    CHECK(cfg == NULL, "falta MapImageFile debe fallar");
    crv_config_free(cfg);

    /* --print-config: bloque exacto. */
    write_cfg("t7.d", "", "");
    snprintf(path, sizeof path, "%s/t7.d", g_dir);
    cfg = crv_config_load(path, err, sizeof err);
    CHECK(cfg != NULL, "cfg t7");
    if (cfg) {
        FILE *f = tmpfile();
        char *out;
        CHECK(f != NULL, "tmpfile");
        if (f) {
            crv_print_config(cfg, f);
            out = slurp(f);
            CHECK(strstr(out, "InitialZoom=8.000000\n") != NULL,
                  "print InitialZoom");
            CHECK(strstr(out, "MapImageFile=world_map.jpg\n") != NULL,
                  "print MapImageFile");
            CHECK(strstr(out, "OverlayFiles=0\n") != NULL, "print OverlayFiles");
            fclose(f);
        }
        crv_config_free(cfg);
    }

    /* --check-overlays: 2 capas y 3 polilineas (T8 del GTK). */
    snprintf(path, sizeof path, "%s/ov.lines", g_dir);
    {
        FILE *f = fopen(path, "wb");
        fprintf(f, "#layer name=a color=FF0000 width=2\n");
        fprintf(f, "0 0\n1 1\n>\n2 2\n");
        fprintf(f, "#layer name=b\n-70 -30\n");
        fclose(f);
    }
    {
        char extra[600];
        snprintf(extra, sizeof extra, "OverlayFile     %s/ov.lines", g_dir);
        write_cfg("t8.d", extra, "OverlayColor    a 00FF00");
        snprintf(path, sizeof path, "%s/t8.d", g_dir);
        cfg = crv_config_load(path, err, sizeof err);
        CHECK(cfg != NULL, "cfg t8");
        if (cfg) {
            FILE *f = tmpfile();
            char *out;
            crv_check_overlays(cfg, f, stderr);
            out = slurp(f);
            CHECK(strstr(out, "overlay_layers=2") != NULL, "layers=2");
            CHECK(strstr(out, "overlay_polylines=3") != NULL, "polylines=3");
            CHECK(strstr(out, "layer a visible=1 color=00FF00 width=2.0 "
                              "polylines=2 vertices=3") != NULL,
                  "color/ancho aplicados");
            fclose(f);
        }
        crv_config_free(cfg);
    }

    /* OverlayHide inexistente: avisa y no aborta. */
    {
        char extra[600];
        snprintf(extra, sizeof extra, "OverlayFile     %s/ov.lines", g_dir);
        write_cfg("t9.d", extra, "OverlayHide     no_existe");
        snprintf(path, sizeof path, "%s/t9.d", g_dir);
        cfg = crv_config_load(path, err, sizeof err);
        CHECK(cfg != NULL, "cfg t9");
        if (cfg) {
            FILE *f = tmpfile();
            CHECK(f != NULL, "tmpfile t9");
            if (f) {
                crv_check_overlays(cfg, f, stderr);
                fclose(f);
            }
            CHECK(cfg->n_overlay_hides == 1, "OverlayHide registrado");
        }
        crv_config_free(cfg);
    }

    {
        char cmd[400];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
        if (system(cmd) != 0)
            printf("nota: no se pudo limpiar %s\n", g_dir);
    }

    if (g_fail == 0)
        printf("OK: config de csnrvQT (defaults, clamps, overlays y reporte)\n");
    return g_fail;
}
