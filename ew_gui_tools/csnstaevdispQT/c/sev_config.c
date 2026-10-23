/* Lectura de csnstaevdisp.d: replica `ReadConfig` de csnstaevdisp.c:136-199.
 *
 * No usa la tabla plana de `ewgui/config.h` porque aqui hay claves REPETIBLES
 * (`OverlayFile`/`OverlayColor`/`OverlayHide`) y `OverlayColor` lleva DOS
 * valores en la misma linea.
 *
 * Paridad que se conserva:
 *   - 8 claves obligatorias (incluida `StationSize`);
 *   - las claves desconocidas se ignoran en silencio (`QuakeFile` incluida);
 *   - los avisos de overlays van a stderr, que es donde los busca el arnes;
 *   - el reporte de `--check-overlays` es byte a byte el del GTK.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kom.h>

#include "ewgui/config.h"
#include "ewgui/overlay.h"

#include "sev_capi.h"
#include "sev_internal.h"

#define N_REQUIRED 8

SevConfig *sev_config_load(const char *path, char *err, size_t errlen)
{
    SevConfig *cfg;
    char init[N_REQUIRED] = {0};
    char pathbuf[1024];
    char *com, *str;
    int i, nmiss = 0;

    if (err && errlen)
        err[0] = '\0';
    if (!path || !*path) {
        if (err)
            snprintf(err, errlen, "falta el archivo de configuracion (.d)");
        return NULL;
    }
    /* k_open recibe char*: copia para no castear un literal del llamante. */
    snprintf(pathbuf, sizeof(pathbuf), "%s", path);

    cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        if (err)
            snprintf(err, errlen, "sin memoria");
        return NULL;
    }
    snprintf(cfg->my_module, sizeof(cfg->my_module), "MOD_STAEVDISP");
    snprintf(cfg->ring, sizeof(cfg->ring), "HYPO_RING");
    cfg->heart_beat_int = 30;
    cfg->log_file = 1;
    cfg->db_hours = 24;
    cfg->station_size = 5.0;

    if (!k_open(pathbuf)) {
        if (err)
            snprintf(err, errlen, "no se pudo abrir <%s>", path);
        free(cfg);
        return NULL;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#')
            continue;

        if (k_its("MyModuleId")) {
            str = k_str();
            if (str)
                snprintf(cfg->my_module, sizeof(cfg->my_module), "%s", str);
            init[0] = 1;
        } else if (k_its("RingName")) {
            str = k_str();
            if (str)
                snprintf(cfg->ring, sizeof(cfg->ring), "%s", str);
            init[1] = 1;
        } else if (k_its("HeartBeatInt")) {
            cfg->heart_beat_int = k_int();
            init[2] = 1;
        } else if (k_its("LogFile")) {
            cfg->log_file = k_int();
            init[3] = 1;
        } else if (k_its("DbEnvFile")) {
            str = k_str();
            if (str)
                snprintf(cfg->db_env_file, sizeof(cfg->db_env_file), "%s", str);
            init[4] = 1;
        } else if (k_its("DbHistoryHours")) {
            cfg->db_hours = k_int();
        } else if (k_its("MapImageFile")) {
            str = k_str();
            if (str)
                snprintf(cfg->map_image, sizeof(cfg->map_image), "%s", str);
            init[5] = 1;
        } else if (k_its("StaFile")) {
            str = k_str();
            if (str)
                snprintf(cfg->sta_file, sizeof(cfg->sta_file), "%s", str);
            init[6] = 1;
        } else if (k_its("StationSize")) {
            cfg->station_size = k_val();
            init[7] = 1;
        } else if (k_its("OverlayFile")) {
            str = k_str();
            if (str && cfg->n_overlay_files < SEV_MAX_OVERLAYS)
                snprintf(cfg->overlay_files[cfg->n_overlay_files++],
                         SEV_MAX_STR, "%s", str);
        } else if (k_its("OverlayColor")) {
            str = k_str();
            if (str && cfg->n_overlay_colors < SEV_MAX_OVERLAYS) {
                char *hex = k_str();
                if (hex) {
                    snprintf(cfg->overlay_colors[cfg->n_overlay_colors].layer,
                             sizeof(cfg->overlay_colors[0].layer), "%s", str);
                    snprintf(cfg->overlay_colors[cfg->n_overlay_colors].hex,
                             sizeof(cfg->overlay_colors[0].hex), "%s", hex);
                    cfg->n_overlay_colors++;
                }
            }
        } else if (k_its("OverlayWidth")) {
            cfg->overlay_width = k_val();
        } else if (k_its("OverlayHide")) {
            str = k_str();
            if (str && cfg->n_overlay_hides < SEV_MAX_OVERLAYS)
                snprintf(cfg->overlay_hides[cfg->n_overlay_hides++],
                         sizeof(cfg->overlay_hides[0]), "%s", str);
        } else {
            continue;   /* clave desconocida: se ignora (paridad) */
        }

        if (k_err()) {
            if (err)
                snprintf(err, errlen, "error parseando <%s> en <%s>", com, path);
            k_close();
            free(cfg);
            return NULL;
        }
    }

    for (i = 0; i < N_REQUIRED; i++)
        if (!init[i])
            nmiss++;
    k_close();

    if (nmiss > 0) {
        if (err)
            snprintf(err, errlen,
                     "ERROR, missing parameters (e.g. StationSize) in <%s>",
                     path);
        free(cfg);
        return NULL;
    }
    return cfg;
}

void sev_config_free(SevConfig *cfg)
{
    free(cfg);
}

void sev_print_config(const SevConfig *cfg, FILE *out)
{
    int i;

    if (!cfg)
        return;
    if (!out)
        out = stdout;
    fprintf(out, "my_module=%s\n", cfg->my_module);
    fprintf(out, "ring=%s\n", cfg->ring);
    fprintf(out, "heartbeat=%d\n", cfg->heart_beat_int);
    fprintf(out, "log_file=%d\n", cfg->log_file);
    fprintf(out, "db_env=%s\n", cfg->db_env_file);
    fprintf(out, "db_hours=%d\n", cfg->db_hours);
    fprintf(out, "map_image=%s\n", cfg->map_image);
    fprintf(out, "sta_file=%s\n", cfg->sta_file);
    fprintf(out, "station_size=%.6f\n", cfg->station_size);
    fprintf(out, "overlay_files=%d\n", cfg->n_overlay_files);
    fprintf(out, "overlay_width=%.6f\n", cfg->overlay_width);
    fprintf(out, "overlay_hides=%d\n", cfg->n_overlay_hides);
    for (i = 0; i < cfg->n_overlay_hides; i++)
        fprintf(out, "overlay_hide=%s\n", cfg->overlay_hides[i]);
}

/* --- Overlays ------------------------------------------------------------ */

/* Carga/re-carga lo declarado en el .d y aplica los overrides. Misma
 * tolerancia que `LoadOverlays` (csnstaevdisp.c:203-227). */
EwOverlay *sev_overlays_load(const SevConfig *cfg, FILE *err)
{
    EwOverlay *ov;
    int i;

    if (!cfg || cfg->n_overlay_files == 0)
        return NULL;

    ov = ewgui_overlay_new();
    if (!ov)
        return NULL;

    for (i = 0; i < cfg->n_overlay_files; i++) {
        if (ewgui_overlay_append(ov, cfg->overlay_files[i]) != 0)
            fprintf(err ? err : stderr,
                    "csnstaevdispQT: no se pudo cargar overlay %s\n",
                    cfg->overlay_files[i]);
    }
    if (cfg->overlay_width > 0.0)
        ewgui_overlay_set_global_width(ov, cfg->overlay_width);

    for (i = 0; i < cfg->n_overlay_colors; i++) {
        double rgb[3];
        if (ewgui_parse_hex_color(cfg->overlay_colors[i].hex, rgb) == 0)
            ewgui_overlay_set_color(ov, cfg->overlay_colors[i].layer, rgb);
        else
            fprintf(err ? err : stderr,
                    "csnstaevdispQT: OverlayColor %s: hex invalido '%s'\n",
                    cfg->overlay_colors[i].layer,
                    cfg->overlay_colors[i].hex);
    }
    for (i = 0; i < cfg->n_overlay_hides; i++) {
        if (ewgui_overlay_set_visible(ov, cfg->overlay_hides[i], 0) == 0)
            fprintf(err ? err : stderr,
                    "csnstaevdispQT: OverlayHide %s: no existe esa capa\n",
                    cfg->overlay_hides[i]);
    }
    return ov;
}

int sev_check_overlays(const SevConfig *cfg, FILE *out, FILE *err)
{
    EwOverlay *ov;
    size_t i;
    int nl = 0, np = 0;

    if (!out)
        out = stdout;
    if (!err)
        err = stderr;

    ov = sev_overlays_load(cfg, err);
    if (ov) {
        nl = (int)ov->nlayers;
        for (i = 0; i < ov->nlayers; i++)
            np += (int)ov->layers[i].nlines;
    }
    fprintf(out, "overlay_layers=%d overlay_polylines=%d\n", nl, np);
    if (ov) {
        for (i = 0; i < ov->nlayers; i++) {
            const EwOverlayLayer *L = &ov->layers[i];
            size_t k, npt = 0;
            for (k = 0; k < L->nlines; k++)
                npt += L->lines[k].npts;
            fprintf(out,
                    "layer %s visible=%d color=%02X%02X%02X width=%.1f "
                    "polylines=%d vertices=%d\n",
                    L->name, L->visible,
                    (int)(L->color[0] * 255.0 + 0.5),
                    (int)(L->color[1] * 255.0 + 0.5),
                    (int)(L->color[2] * 255.0 + 0.5),
                    L->width, (int)L->nlines, (int)npt);
        }
        ewgui_overlay_free(ov);
    }
    return 0;
}
