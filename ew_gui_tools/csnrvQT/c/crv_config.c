/* Lectura de csnrv.d: replica `ReadConfig` de csnrv.c:113-208.
 *
 * No usa `ewgui_config_load` porque aqui hay claves REPETIBLES
 * (`OverlayFile`/`OverlayColor`/`OverlayHide`) y `OverlayColor` lleva DOS
 * valores en la misma linea; la tabla plana de ewgui/config.h solo admite una
 * clave -> un destino.
 *
 * Los avisos (clamp de InitialZoom, overlay que no carga) van a stderr, igual
 * que el GTK: el arnes `test_config.sh` los busca ahi, no en el log (en modo
 * headless no hay log).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kom.h>

#include "ewgui/config.h"
#include "ewgui/overlay.h"

#include "crv_capi.h"
#include "crv_internal.h"

#define ZOOM_MIN     0.2
#define ZOOM_MAX     50.0
#define ZOOM_DEFAULT 8.0

/* Claves obligatorias: MyModuleId/RingName/HeartBeatInt/LogFile/MapImageFile. */
#define N_REQUIRED 5

CrvConfig *crv_config_load(const char *path, char *err, size_t errlen)
{
    CrvConfig *cfg;
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
    cfg->initial_zoom = ZOOM_DEFAULT;
    cfg->db_hours = 24;

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
        } else if (k_its("QuakeFile")) {
            /* Obsoleta: se acepta y se ignora (test_config.sh la escribe). */
            (void)k_str();
        } else if (k_its("DbEnvFile")) {
            str = k_str();
            if (str)
                snprintf(cfg->db_env_file, sizeof(cfg->db_env_file), "%s", str);
        } else if (k_its("DbHistoryHours")) {
            cfg->db_hours = k_int();
        } else if (k_its("MapImageFile")) {
            str = k_str();
            if (str)
                snprintf(cfg->map_image, sizeof(cfg->map_image), "%s", str);
            init[4] = 1;
        } else if (k_its("InitialZoom")) {
            /* Opcional: no cuenta en N_REQUIRED. */
            cfg->initial_zoom = k_val();
            if (cfg->initial_zoom < ZOOM_MIN) {
                fprintf(stderr,
                        "csnrvQT: InitialZoom %.3f fuera de rango, se satura a %.1f\n",
                        cfg->initial_zoom, ZOOM_MIN);
                cfg->initial_zoom = ZOOM_MIN;
            } else if (cfg->initial_zoom > ZOOM_MAX) {
                fprintf(stderr,
                        "csnrvQT: InitialZoom %.3f fuera de rango, se satura a %.1f\n",
                        cfg->initial_zoom, ZOOM_MAX);
                cfg->initial_zoom = ZOOM_MAX;
            }
        } else if (k_its("OverlayFile")) {
            str = k_str();
            if (str && cfg->n_overlay_files < CRV_MAX_OVERLAYS)
                snprintf(cfg->overlay_files[cfg->n_overlay_files++],
                         CRV_MAX_STR, "%s", str);
        } else if (k_its("OverlayColor")) {
            str = k_str();
            if (str && cfg->n_overlay_colors < CRV_MAX_OVERLAYS) {
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
            if (str && cfg->n_overlay_hides < CRV_MAX_OVERLAYS)
                snprintf(cfg->overlay_hides[cfg->n_overlay_hides++],
                         sizeof(cfg->overlay_hides[0]), "%s", str);
        } else {
            continue;
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
            snprintf(err, errlen, "faltan parametros en <%s>", path);
        free(cfg);
        return NULL;
    }
    if (cfg->heart_beat_int <= 0)
        cfg->heart_beat_int = 30;
    if (cfg->db_hours <= 0)
        cfg->db_hours = 24;
    return cfg;
}

void crv_config_free(CrvConfig *cfg)
{
    free(cfg);
}

/* Bloque exacto de `csnrv --print-config` (csnrv.c:999-1008). */
void crv_print_config(const CrvConfig *cfg, FILE *out)
{
    int i;

    if (!cfg)
        return;
    if (!out)
        out = stdout;
    fprintf(out, "InitialZoom=%.6f\n", cfg->initial_zoom);
    fprintf(out, "MapImageFile=%s\n", cfg->map_image);
    fprintf(out, "OverlayFiles=%d\n", cfg->n_overlay_files);
    fprintf(out, "OverlayColors=%d\n", cfg->n_overlay_colors);
    fprintf(out, "OverlayHides=%d\n", cfg->n_overlay_hides);
    for (i = 0; i < cfg->n_overlay_hides; i++)
        fprintf(out, "OverlayHide=%s\n", cfg->overlay_hides[i]);
}

/* --- Overlays ------------------------------------------------------------ */

/* Carga/re-carga lo declarado en el .d y aplica los overrides. Misma
 * tolerancia que `LoadOverlays` (csnrv.c:212-236). */
EwOverlay *crv_overlays_load(const CrvConfig *cfg, FILE *err)
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
            fprintf(err ? err : stderr, "csnrvQT: no se pudo cargar overlay %s\n",
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
                    "csnrvQT: OverlayColor %s: hex invalido '%s'\n",
                    cfg->overlay_colors[i].layer, cfg->overlay_colors[i].hex);
    }
    for (i = 0; i < cfg->n_overlay_hides; i++) {
        if (ewgui_overlay_set_visible(ov, cfg->overlay_hides[i], 0) == 0)
            fprintf(err ? err : stderr,
                    "csnrvQT: OverlayHide %s: no existe esa capa\n",
                    cfg->overlay_hides[i]);
    }
    return ov;
}

int crv_check_overlays(const CrvConfig *cfg, FILE *out, FILE *err)
{
    EwOverlay *ov;
    size_t i;
    int nl = 0, np = 0;

    if (!out)
        out = stdout;
    if (!err)
        err = stderr;

    ov = crv_overlays_load(cfg, err);
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
