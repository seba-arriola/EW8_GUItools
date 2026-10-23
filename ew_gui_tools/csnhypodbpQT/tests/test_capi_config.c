/*
 * test_capi_config.c - Verifica el port de ReadConfig() contra los valores del
 * .d real del sistema (run_working_v8/params/csnhypodbp.d).
 *
 * Sin Qt y sin GTK: enlaza solo bpq_config.o + los nucleos puros de csnhypodbp
 * y libew_mt.a (kom). Se ejecuta con cwd = run_working_v8/params, igual que el
 * modulo real, para que las rutas relativas del .d (csnmags.d, ../wstore, ...)
 * resuelvan como en produccion.
 *
 * Uso:  test_capi_config [archivo.d]      (por defecto csnhypodbp.d)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                                    \
    g_checks++;                                                             \
    if (!(cond)) {                                                          \
        g_fail++;                                                           \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                       \
} while (0)

#define CHECK_STR(got, want) do {                                           \
    g_checks++;                                                             \
    if (!(got) || strcmp((got), (want)) != 0) {                             \
        g_fail++;                                                           \
        fprintf(stderr, "FALLA %s:%d  %s = <%s>, esperaba <%s>\n",          \
                __FILE__, __LINE__, #got, (got) ? (got) : "(null)", (want));\
    }                                                                       \
} while (0)

#define CHECK_DBL(got, want, tol) do {                                      \
    double _g = (got), _w = (want), _t = (tol);                             \
    g_checks++;                                                             \
    if (!(_g - _w <= _t && _w - _g <= _t)) {                                \
        g_fail++;                                                           \
        fprintf(stderr, "FALLA %s:%d  %s = %.6f, esperaba %.6f (+-%g)\n",   \
                __FILE__, __LINE__, #got, _g, _w, _t);                      \
    }                                                                       \
} while (0)

/* Claves obligatorias ausentes: el error debe NOMBRAR la clave (E3 del plan). */
static void test_missing_required(void)
{
    const char *path = "/tmp/bpq_test_missing.d";
    FILE *fp = fopen(path, "w");
    char  err[512] = "";
    BpqConfig *c;

    if (!fp) {
        fprintf(stderr, "AVISO: no pude crear %s; salto esa prueba\n", path);
        return;
    }
    fputs("MyModuleId MOD_X\n", fp);
    fclose(fp);

    c = bpq_config_load(path, NULL, err, sizeof(err));
    g_checks++;
    if (c != NULL) {
        g_fail++;
        fprintf(stderr, "FALLA: config incompleta devolvio un BpqConfig\n");
        bpq_config_free(c);
    }
    g_checks++;
    if (!strstr(err, "InRing")) {
        g_fail++;
        fprintf(stderr, "FALLA: el error no nombra InRing: <%s>\n", err);
    }
    remove(path);
}

/* --state-dir debe reescribir las tres rutas de runtime y nada mas. */
static void test_state_dir(const char *td)
{
    const char *dir = "/tmp/bpq_state_dir";
    char  err[512] = "";
    BpqConfig *c = bpq_config_load(td, dir, err, sizeof(err));

    CHECK(c != NULL);
    if (!c) return;

    CHECK_STR(c->state_file, "/tmp/bpq_state_dir/csnloc.events");
    CHECK_STR(c->manual_state_file, "/tmp/bpq_state_dir/csnhypodbp_manual.events");
    CHECK_STR(c->mww_workdir, "/tmp/bpq_state_dir/mww");
    CHECK_STR(bpq_config_state_dir(c), dir);
    /* lo que NO debe cambiar */
    CHECK_STR(c->in_ring, "HYPO_RING_REF");
    CHECK_STR(c->store_root, "../wstore");

    bpq_config_free(c);
}

/* Crea un .d minimo valido; `extra` se anade al final (puede ser NULL). */
static int write_min_config(const char *path, const char *extra)
{
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fputs("MyModuleId MOD_X\n"
          "InRing HYPO_RING_REF\n"
          "OutRing PICK_RING\n"
          "HeartBeatInt 30\n"
          "LogFile 1\n"
          "StaFile estaciones_107.txt\n"
          "StateFile csnloc.events\n", fp);
    if (extra) fputs(extra, fp);
    fclose(fp);
    return 0;
}

/*
 * Paridad de tolerancia con ReadConfig (csnhypodbp_main.c:445-457): un MapSplit
 * invalido SOLO avisa y conserva el valor previo; no aborta la carga.
 */
static void test_tolerated_line(void)
{
    const char *path = "/tmp/bpq_test_tolerated.d";
    char  err[512] = "";
    BpqConfig *c;

    if (write_min_config(path, "MapSplit NO-ES-UNA-PROPORCION\n") != 0) return;
    c = bpq_config_load(path, NULL, err, sizeof(err));
    CHECK(c != NULL);
    if (c) {
        CHECK_DBL(c->map_split_frac, 0.7, 1e-9);
        bpq_config_free(c);
    }
    remove(path);
}

/*
 * Paridad de fatalidad (csnhypodbp_main.c:507-519): una linea `Filter` invalida
 * aborta la lectura del .d.
 */
static void test_fatal_line(void)
{
    const char *path = "/tmp/bpq_test_fatal.d";
    char  err[512] = "";
    BpqConfig *c;

    if (write_min_config(path, "FilterPhase sideways\n") != 0) return;
    c = bpq_config_load(path, NULL, err, sizeof(err));
    CHECK(c == NULL);
    CHECK(err[0] != '\0');
    if (c) bpq_config_free(c);
    remove(path);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "csnhypodbp.d";
    char err[512] = "";
    BpqConfig *c;

    printf("== test_capi_config: %s ==\n", path);

    /* --- 1. carga completa del .d real --- */
    c = bpq_config_load(path, NULL, err, sizeof(err));
    if (!c) {
        fprintf(stderr, "FALLA: bpq_config_load(%s): %s\n", path, err);
        return 1;
    }

    /* identidad y anillos */
    CHECK_STR(c->my_mod_name, "MOD_CSNHYPODBP");
    CHECK_STR(c->in_ring, "HYPO_RING_REF");
    CHECK_STR(c->in_ring_raw, "HYPO_RING");
    CHECK_STR(c->out_ring, "PICK_RING");
    CHECK_STR(c->in_ring_pick, "PICK_RING");
    CHECK_STR(c->conf_ring, "HYPO_RING_REF2");
    CHECK(c->heartbeat_s == 30);
    CHECK(c->logfile == 1);

    /* picking / locate */
    CHECK_STR(c->tau_model, "iasp91");
    CHECK_STR(c->sta_file, "estaciones_107.txt");
    CHECK_STR(c->state_file, "csnloc.events");
    CHECK_DBL(c->pick_capture_window_s, 600.0, 1e-9);
    CHECK_DBL(c->max_dist_km, 500.0, 1e-9);
    CHECK_DBL(c->mag_fetch_s, 700.0, 1e-9);
    CHECK(c->locator_timeout_s == 600);
    CHECK(c->prefetch_neighbors == 2);
    CHECK_STR(c->manual_state_file, "../csnhypodbp/csnhypodbp_manual.events");

    /* agrupamiento */
    CHECK_DBL(c->group_pick_tol_s, 0.1, 1e-9);
    CHECK(c->group_min_match_picks == 3);
    CHECK(c->group_max_solutions == 100);

    /* vista de ondas */
    CHECK_STR(c->channels_order, "HH BH HN LH");
    CHECK_STR(c->components_order, "Z N E");
    CHECK(c->traces_per_screen == 1);
    CHECK(c->n_locators == 3);
    if (c->n_locators == 3) {
        CHECK_STR(c->locators[0].name, "csnloc");
        CHECK_STR(c->locators[1].name, "hyp2000_ring");
        CHECK_STR(c->locators[2].name, "nlloc_ring");
        CHECK_STR(c->locators[0].bin, "csnloc");
        CHECK_STR(c->locators[0].cfg, "csnloc.d");
    }

    /* BD / store / busqueda / SDS */
    CHECK_STR(c->store_root, "../wstore");
    CHECK_STR(c->db_env_file, "db.env");
    CHECK(c->db_history_hours == 24);
    CHECK(c->fetch_threads == 4);
    CHECK(c->search_max_events == 1000);
    CHECK(c->sds_enabled == 1);
    CHECK_STR(c->sds_root, "/mnt/sds/archive");
    CHECK(c->sds_fallback_hours == 168);
    CHECK(c->hide_sta_without_data == 1);

    /* mapa / overlays / perfiles */
    CHECK_DBL(c->map_min_span_deg, 5.0, 1e-9);
    CHECK_DBL(c->map_zoom_factor, 1.0, 1e-9);
    CHECK_DBL(c->map_split_frac, 0.7, 1e-9);      /* "7/3" */
    CHECK(c->n_overlay_files == 4);
    CHECK(c->n_overlay_hides == 0);
    CHECK_DBL(c->overlay_width, 0.0, 1e-9);
    CHECK_STR(c->profile_data_dir, "/mnt/d/resources/profiles");
    CHECK(c->profile_lon_col == 3);
    CHECK(c->profile_val_col == 7);
    CHECK_DBL(c->profile_depth_min_km, 200.0, 1e-9);
    CHECK(c->profiles.n == 13);
    if (c->profiles.n > 0) {
        CHECK_STR(c->profiles.p[0].name, "P01");
        CHECK_DBL(c->profiles.p[0].lon1, -72.0, 1e-9);
        /* slab/topo por convencion <ProfileDataDir>/slab<name>.tmp */
        CHECK(strstr(c->profiles.p[0].slab_file, "/slabP01.tmp") != NULL);
        CHECK(strstr(c->profiles.p[0].topo_file, "/topoP01.tmp") != NULL);
    }

    /* filtros: 7 presets del .d, fase cero, teclas y STA/LTA */
    CHECK(c->filtercfg.nf == 7);
    CHECK(c->filtercfg.phase_zero == 1);
    CHECK(c->filtercfg.menu_key == 'f');
    CHECK(c->filtercfg.raw_key == '0');
    CHECK(c->filtercfg.stalta_declared == 1);
    CHECK(c->filtercfg.stalta_key == 't');
    CHECK_DBL(c->filtercfg.stalta_sta, 0.5, 1e-9);
    CHECK_DBL(c->filtercfg.stalta_lta, 10.0, 1e-9);
    if (c->filtercfg.nf == 7) {
        /* El nucleo ordena los presets por categoria (los de distancia primero),
         * no en el orden del .d: se busca cada uno por (tipo, tecla). */
        int has_hp = 0, has_lp = 0, has_bp = 0, has_plocal = 0, fi;
        for (fi = 0; fi < c->filtercfg.nf; fi++) {
            const CsnhypodbpFilterPreset *fp = &c->filtercfg.f[fi];
            if (fp->type == 1 && fp->key == 'r') has_hp = 1;          /* Filter HP r */
            if (fp->type == 2 && fp->key == 'k') has_lp = 1;          /* Filter LP k */
            if (fp->type == 3 && fp->key == 'd') has_bp = 1;          /* Filter BP d */
            if (strcmp(fp->name, "P_local") == 0 && fp->key == '1') has_plocal = 1;
        }
        CHECK(has_hp);
        CHECK(has_lp);
        CHECK(has_bp);
        CHECK(has_plocal);
    }

    /* colores: pColor/sColor no declarados -> defaults rojo/verde, sin overrides */
    CHECK_DBL(c->colors_p.manual[0], 1.0, 1e-9);
    CHECK_DBL(c->colors_p.manual[1], 0.0, 1e-9);
    CHECK(c->colors_p.has_auto == 0);
    CHECK(c->colors_p.has_theo == 0);
    CHECK_DBL(c->colors_s.manual[1], 0.6, 1e-9);
    CHECK(c->colors_s.has_auto == 0);

    /* ModuleLabel */
    CHECK(c->n_modlabels == 3);
    if (c->n_modlabels == 3) {
        CHECK(c->modlabels[0].id == 162);
        CHECK_STR(c->modlabels[0].label, "csnloc");
        CHECK(c->modlabels[2].id == 164);
        CHECK_STR(c->modlabels[2].label, "nlloc");
    }

    /* magcfg se lee de csnmags.d (mismo cwd=params): defaults sensatos */
    CHECK(c->magcfg.ml_ta > 0.0);
    CHECK(c->magcfg.ml_tb > c->magcfg.ml_ta);
    CHECK(c->magcfg.mb_window > 0.0);

    /* --- 2. volcado headless --- */
    CHECK(bpq_config_print(c, stdout) == 0);

    /* --- 3. has_key --- */
    CHECK(bpq_config_has_key(c, "InRing") == 1);
    CHECK(bpq_config_has_key(c, "Profile") == 1);
    CHECK(bpq_config_has_key(c, "StaLta") == 1);
    CHECK(bpq_config_has_key(c, "NoExiste") == 0);

    bpq_config_free(c);

    /* --- 4. errores, tolerancias y overrides --- */
    test_missing_required();
    test_state_dir(path);
    test_tolerated_line();
    test_fatal_line();

    printf("== checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
