/* Contrato de configuracion: mismas 6 claves obligatorias, mismos defaults y
 * mismas claves opcionales que csntvp.d (csntvp_data.c:29-107), mas
 * zColor/nColor/eColor (que heredan de waveformsColor si faltan). */
#include <math.h>

#include "tvq_test.h"

static int near(double a, double b)
{
    return fabs(a - b) < 1e-6;
}

static void write_cfg(const char *name, const char *extra)
{
    char d[2048];
    char sta[512];

    snprintf(sta, sizeof sta, "%s", tvq_t_path("min.sta"));
    snprintf(d, sizeof d,
             "MyModuleId      MOD_CSNTVPQT\n"
             "InRing          SLINK_RING\n"
             "OutRing         PICK_RING\n"
             "HeartBeatInt    30\n"
             "LogFile         1\n"
             "StaFile         %s\n"
             "%s",
             sta, extra ? extra : "");
    tvq_t_write(name, d);
}

int main(void)
{
    char err[512];
    const char *p;

    tvq_t_write("min.sta", "AAA HHZ XX -- 0.005\n");

    /* --- 1. minimo: defaults = los del GTK ------------------------------- */
    write_cfg("min.d", NULL);
    p = tvq_t_path("min.d");
    {
        TvqConfig *cfg = tvq_config_load(p, err, sizeof err);

        T_CHECK(cfg != NULL, "carga el .d minimo");
        if (cfg) {
            T_CHECK(near(cfg->heartbeat_int, 30), "HeartBeatInt 30");
            T_CHECK(cfg->stations_per_screen == 12, "default StationsPerScreen=12");
            T_CHECK(cfg->time_window_min == 6, "default TimeWindow=6");
            T_CHECK(near(cfg->pick_replace_secs, 15.0),
                    "default PickReplaceWindow=15.0");
            T_CHECK(near(cfg->colour[TVQ_COLOUR_WAVE][0], 0.0) &&
                        near(cfg->colour[TVQ_COLOUR_WAVE][1], 0.0),
                    "default waveformsColor negro");
            T_CHECK(near(cfg->colour[TVQ_COLOUR_BG][0], 1.0),
                    "default backgroundColor blanco");
            T_CHECK(near(cfg->colour[TVQ_COLOUR_SEP][0], 217.0 / 255.0),
                    "default separatorColor D9D9D9");
            T_CHECK(near(cfg->colour[TVQ_COLOUR_N][0],
                         cfg->colour[TVQ_COLOUR_WAVE][0]),
                    "nColor sin declarar hereda de waveformsColor");
            T_CHECK(strcmp(cfg->sta_file, tvq_t_path("min.sta")) == 0,
                    "StaFile tomado del .d");

            /* print_config: una linea key=value por dato. */
            {
                FILE *f = tmpfile();

                if (f) {
                    char buf[4096];
                    size_t n;

                    tvq_print_config(cfg, f);
                    rewind(f);
                    n = fread(buf, 1, sizeof buf - 1, f);
                    buf[n] = '\0';
                    T_CHECK(strstr(buf, "module=MOD_CSNTVPQT") != NULL,
                            "print: module");
                    T_CHECK(strstr(buf, "in_ring=SLINK_RING") != NULL,
                            "print: in_ring");
                    T_CHECK(strstr(buf, "stations_per_screen=12") != NULL,
                            "print: stations_per_screen");
                    T_CHECK(strstr(buf, "color_z=#000000") != NULL,
                            "print: color_z resuelto");
                    T_CHECK(strstr(buf, "module_id=") != NULL,
                            "print: module_id (numero o unresolved)");
                    fclose(f);
                }
            }
            tvq_config_free(cfg);
        }
    }

    /* --- 2. clave desconocida: se ignora (no rompe) ---------------------- */
    write_cfg("unknown.d", "EstaClaveNoExiste 42\n");
    {
        TvqConfig *cfg = tvq_config_load(tvq_t_path("unknown.d"), err, sizeof err);

        T_CHECK(cfg != NULL, "clave desconocida ignorada");
        tvq_config_free(cfg);
    }

    /* --- 3. overrides + claves por componente --------------------------- */
    write_cfg("over.d",
              "StationsPerScreen 4\n"
              "TimeWindow        2\n"
              "PickReplaceWindow 0\n"
              "waveformsColor    #112233\n"
              "zColor            445566\n"
              "nColor            00FF00\n");
    {
        TvqConfig *cfg = tvq_config_load(tvq_t_path("over.d"), err, sizeof err);

        T_CHECK(cfg != NULL, "carga el .d con overrides");
        if (cfg) {
            T_CHECK(cfg->stations_per_screen == 4, "StationsPerScreen 4");
            T_CHECK(cfg->time_window_min == 2, "TimeWindow 2");
            T_CHECK(near(cfg->pick_replace_secs, 0.0), "PickReplaceWindow 0");
            T_CHECK(near(cfg->colour[TVQ_COLOUR_WAVE][0], 0x11 / 255.0),
                    "waveformsColor #112233");
            T_CHECK(near(cfg->colour[TVQ_COLOUR_Z][0], 0x44 / 255.0),
                    "zColor explicito");
            T_CHECK(near(cfg->colour[TVQ_COLOUR_N][1], 1.0), "nColor verde");
            T_CHECK(near(cfg->colour[TVQ_COLOUR_E][0], 0x11 / 255.0),
                    "eColor sin declarar sigue a waveformsColor");
            tvq_config_free(cfg);
        }
    }

    /* --- 4. valores fuera de rango: default + aviso, sin fallar --------- */
    write_cfg("range.d", "StationsPerScreen 9999\nTimeWindow 0\n");
    {
        TvqConfig *cfg = tvq_config_load(tvq_t_path("range.d"), err, sizeof err);

        T_CHECK(cfg != NULL, "valores fuera de rango no abortan");
        if (cfg) {
            T_CHECK(cfg->stations_per_screen == 12,
                    "StationsPerScreen invalido -> default");
            T_CHECK(cfg->time_window_min == 6, "TimeWindow invalido -> default");
            tvq_config_free(cfg);
        }
    }

    /* --- 5. color invalido: se mantiene el default ---------------------- */
    write_cfg("badcol.d", "pColor zzz\n");
    {
        TvqConfig *cfg = tvq_config_load(tvq_t_path("badcol.d"), err, sizeof err);

        T_CHECK(cfg != NULL, "color invalido no aborta");
        if (cfg) {
            T_CHECK(near(cfg->colour[TVQ_COLOUR_P][0], 1.0) &&
                        near(cfg->colour[TVQ_COLOUR_P][1], 0.0),
                    "pColor invalido -> default rojo");
            tvq_config_free(cfg);
        }
    }

    /* --- 6. obligatoria faltante: NULL y el error la nombra ------------- */
    tvq_t_write("miss.d", "MyModuleId MOD_CSNTVPQT\nInRing SLINK_RING\n");
    {
        TvqConfig *cfg = tvq_config_load(tvq_t_path("miss.d"), err, sizeof err);

        T_CHECK(cfg == NULL, "faltan obligatorias -> NULL");
        T_CHECK(strstr(err, "OutRing") != NULL &&
                    strstr(err, "StaFile") != NULL,
                "el error nombra las claves que faltan");
    }

    /* --- 7. .d inexistente ---------------------------------------------- */
    {
        TvqConfig *cfg = tvq_config_load(tvq_t_path("no_existe.d"), err,
                                         sizeof err);

        T_CHECK(cfg == NULL, ".d inexistente -> NULL");
        T_CHECK(strstr(err, "no puedo abrir") != NULL, "error de apertura claro");
    }

    printf(tvq_t_fail ? "FALLAS: test_config\n" : "OK: test_config\n");
    return tvq_t_fail ? 1 : 0;
}
