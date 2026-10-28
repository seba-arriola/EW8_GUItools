/* Configuracion del modulo: lee el .d con kom, EXACTAMENTE como csntvp
 * (csntvp_data.c:29-107): las mismas 6 claves obligatorias, los mismos
 * defaults y las mismas claves opcionales; las desconocidas se ignoran en
 * silencio, que es lo que permite que csntvpqt.d y csntvp.d compartan formato.
 *
 * Cambios deliberados respecto al GTK:
 *   - el error de clave obligatoria FALTANTE nombra las que faltan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "earthworm.h"
#include "earthworm_simple_funcs.h"
#include "kom.h"

#include "csntvp_util.h"

#include "tvq_capi.h"
#include "tvq_internal.h"

static void copy_str(char *dst, size_t n, const char *src)
{
    if (!src)
        return;
    snprintf(dst, n, "%s", src);
}

static void set_defaults(TvqConfig *c)
{
    memset(c, 0, sizeof(*c));
    copy_str(c->my_module, sizeof c->my_module, "MOD_CSNTVPQT");
    c->module_id           = -1;
    c->heartbeat_int       = 30;
    c->log_file            = 1;
    c->stations_per_screen = 12;
    c->time_window_min     = 6;
    c->pick_replace_secs   = 15.0;

    /* Mismos defaults que csntvp_main.c:25-30. */
    const double black[3] = { 0.0, 0.0, 0.0 };
    const double white[3] = { 1.0, 1.0, 1.0 };
    const double red[3]   = { 1.0, 0.0, 0.0 };
    const double sep[3]   = { 217.0 / 255.0, 217.0 / 255.0, 217.0 / 255.0 };   /* D9D9D9 */

    for (int i = 0; i < 3; i++) {
        c->colour[TVQ_COLOUR_WAVE][i] = black[i];
        c->colour[TVQ_COLOUR_BG][i]   = white[i];
        c->colour[TVQ_COLOUR_FONT][i] = red[i];
        c->colour[TVQ_COLOUR_SEP][i]  = sep[i];
        c->colour[TVQ_COLOUR_P][i]    = red[i];
        c->colour[TVQ_COLOUR_S][i]    = black[i];
    }
}

void tvq_missing_keys(char *out, size_t n, const int have[6])
{
    static const char *names[6] = {
        "MyModuleId", "InRing", "OutRing", "HeartBeatInt", "LogFile", "StaFile"
    };
    size_t used = 0;

    if (!out || n == 0)
        return;
    out[0] = '\0';
    for (int i = 0; i < 6; i++) {
        if (have[i])
            continue;
        int w = snprintf(out + used, n - used, "%s%s", used ? " " : "", names[i]);
        if (w < 0 || (size_t)w >= n - used)
            break;
        used += (size_t)w;
    }
}

/* 0 = aplicado, -1 = color invalido (se mantiene el default). */
static int parse_colour(const char *str, double rgb[3], const char *key)
{
    if (!str || csntvp_parse_hexcolor(str, rgb) != 0) {
        fprintf(stderr, "csntvpQT: %s <%s> invalido, uso el default\n", key,
                str ? str : "");
        return -1;
    }
    return 0;
}

TvqConfig *tvq_config_load(const char *path, char *err, size_t errlen)
{
    TvqConfig *c;
    int have[6] = { 0, 0, 0, 0, 0, 0 };
    char *com, *str;

    if (err && errlen)
        err[0] = '\0';
    if (!path || !path[0]) {
        if (err)
            snprintf(err, errlen, "falta el archivo de configuracion");
        return NULL;
    }

    c = (TvqConfig *)malloc(sizeof(TvqConfig));
    if (!c) {
        if (err)
            snprintf(err, errlen, "sin memoria");
        return NULL;
    }
    set_defaults(c);

    if (!k_open((char *)path)) {
        if (err)
            snprintf(err, errlen, "no puedo abrir <%s>", path);
        free(c);
        return NULL;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#')
            continue;

        if (k_its("MyModuleId")) {
            str = k_str();
            if (str)
                copy_str(c->my_module, sizeof c->my_module, str);
            have[0] = 1;
        } else if (k_its("InRing")) {
            str = k_str();
            if (str)
                copy_str(c->in_ring, sizeof c->in_ring, str);
            have[1] = 1;
        } else if (k_its("OutRing")) {
            str = k_str();
            if (str)
                copy_str(c->out_ring, sizeof c->out_ring, str);
            have[2] = 1;
        } else if (k_its("HeartBeatInt")) {
            c->heartbeat_int = k_int();
            have[3] = 1;
        } else if (k_its("LogFile")) {
            c->log_file = k_int();
            have[4] = 1;
        } else if (k_its("StaFile")) {
            str = k_str();
            if (str)
                copy_str(c->sta_file, sizeof c->sta_file, str);
            have[5] = 1;
        } else if (k_its("waveformsColor")) {
            (void)parse_colour(k_str(), c->colour[TVQ_COLOUR_WAVE], "waveformsColor");
        } else if (k_its("backgroundColor")) {
            (void)parse_colour(k_str(), c->colour[TVQ_COLOUR_BG], "backgroundColor");
        } else if (k_its("fontColor")) {
            (void)parse_colour(k_str(), c->colour[TVQ_COLOUR_FONT], "fontColor");
        } else if (k_its("separatorColor")) {
            (void)parse_colour(k_str(), c->colour[TVQ_COLOUR_SEP], "separatorColor");
        } else if (k_its("pColor")) {
            (void)parse_colour(k_str(), c->colour[TVQ_COLOUR_P], "pColor");
        } else if (k_its("sColor")) {
            (void)parse_colour(k_str(), c->colour[TVQ_COLOUR_S], "sColor");
        } else if (k_its("StationsPerScreen")) {
            str = k_str();
            if (str) {
                int v = atoi(str);
                if (v >= 1 && v <= TVQ_MAX_STATIONS)
                    c->stations_per_screen = v;
                else
                    fprintf(stderr,
                            "csntvpQT: StationsPerScreen <%s> invalido, uso %d\n",
                            str, c->stations_per_screen);
            }
        } else if (k_its("TimeWindow")) {
            str = k_str();
            if (str) {
                int v = atoi(str);
                if (v >= 1 && v <= TVQ_MAX_MINUTES)
                    c->time_window_min = v;
                else
                    fprintf(stderr, "csntvpQT: TimeWindow <%s> invalido, uso %d\n",
                            str, c->time_window_min);
            }
        } else if (k_its("PickReplaceWindow")) {
            str = k_str();
            if (str) {
                double v = atof(str);
                if (v >= 0.0)
                    c->pick_replace_secs = v;
                else
                    fprintf(stderr,
                            "csntvpQT: PickReplaceWindow <%s> invalido, uso %.1f\n",
                            str, c->pick_replace_secs);
            }
        } else {
            /* Clave desconocida: se ignora (paridad con csntvp_data.c:92-93). */
            continue;
        }

        if (k_err()) {
            if (err)
                snprintf(err, errlen, "error parseando <%s> en <%s>", com, path);
            k_close();
            free(c);
            return NULL;
        }
    }
    k_close();

    if (!have[0] || !have[1] || !have[2] || !have[3] || !have[4] || !have[5]) {
        char miss[128];
        tvq_missing_keys(miss, sizeof miss, have);
        if (err)
            snprintf(err, errlen, "faltan parametros en <%s>: %s", path, miss);
        free(c);
        return NULL;
    }

    /* Identidad: el nombre debe existir en earthworm.d. Si no, el modulo no
     * puede publicar picadas ni latir con su logo; main lo reporta al arrancar
     * (y open_rings lo rechaza con la receta). */
    {
        unsigned char mid = 0;
        if (GetModId(c->my_module, &mid) == 0)
            c->module_id = (int)mid;
    }

    return c;
}

void tvq_config_free(TvqConfig *cfg)
{
    free(cfg);
}

static void colour_hex(const double rgb[3], char out[9])
{
    int r = (int)(rgb[0] * 255.0 + 0.5);
    int g = (int)(rgb[1] * 255.0 + 0.5);
    int b = (int)(rgb[2] * 255.0 + 0.5);

    if (r < 0)
        r = 0;
    if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    if (b > 255)
        b = 255;
    snprintf(out, 9, "#%02X%02X%02X", r, g, b);
}

void tvq_print_config(const TvqConfig *cfg, FILE *out)
{
    char w[9], bg[9], fo[9], sp[9], p[9], s[9];

    if (!cfg || !out)
        return;

    colour_hex(cfg->colour[TVQ_COLOUR_WAVE], w);
    colour_hex(cfg->colour[TVQ_COLOUR_BG], bg);
    colour_hex(cfg->colour[TVQ_COLOUR_FONT], fo);
    colour_hex(cfg->colour[TVQ_COLOUR_SEP], sp);
    colour_hex(cfg->colour[TVQ_COLOUR_P], p);
    colour_hex(cfg->colour[TVQ_COLOUR_S], s);

    fprintf(out, "module=%s\n", cfg->my_module);
    if (cfg->module_id >= 0)
        fprintf(out, "module_id=%d\n", cfg->module_id);
    else
        fprintf(out, "module_id=unresolved\n");
    fprintf(out, "in_ring=%s\n", cfg->in_ring);
    fprintf(out, "out_ring=%s\n", cfg->out_ring);
    fprintf(out, "heartbeat_int=%d\n", cfg->heartbeat_int);
    fprintf(out, "log_file=%d\n", cfg->log_file);
    fprintf(out, "sta_file=%s\n", cfg->sta_file);
    fprintf(out, "stations_per_screen=%d\n", cfg->stations_per_screen);
    fprintf(out, "time_window_min=%d\n", cfg->time_window_min);
    fprintf(out, "pick_replace_window=%.1f\n", cfg->pick_replace_secs);
    fprintf(out, "color_wave=%s\n", w);
    fprintf(out, "color_bg=%s\n", bg);
    fprintf(out, "color_font=%s\n", fo);
    fprintf(out, "color_sep=%s\n", sp);
    fprintf(out, "color_p=%s\n", p);
    fprintf(out, "color_s=%s\n", s);
}
