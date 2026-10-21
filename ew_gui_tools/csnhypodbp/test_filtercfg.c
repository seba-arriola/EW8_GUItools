/* Presets de filtro y atajos: lógica pura, sin GTK. */
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_filtercfg.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("  [PASS] %s\n", name); } \
    else      { printf("  [FAIL] %s\n", name); failures++; } \
} while (0)

static int add(CsnhypodbpFilterCfg *c, const char *kind, const char *rest)
{
    char err[128] = "";
    int rc = csnhypodbp_filtercfg_add(c, kind, rest, err, sizeof(err));
    if (rc != 0)
        printf("    (error esperado: %s)\n", err);
    return rc;
}

int main(void)
{
    CsnhypodbpFilterCfg c;
    csnhypodbp_filtercfg_defaults(&c);

    /* 1. Defaults */
    CHECK(c.nf == 7, "defaults: 7 presets");
    {
        int i = csnhypodbp_filtercfg_find_key(&c, 'r');
        CHECK(i >= 0 && c.f[i].type == 1 && c.f[i].f1 == 1.0 && c.f[i].order == 2,
              "defaults: r = HP 1.0 Hz orden 2");
        i = csnhypodbp_filtercfg_find_key(&c, '1');
        CHECK(i >= 0 && c.f[i].type == 3 && c.f[i].f1 == 1.0 && c.f[i].f2 == 20.0 &&
              !strcmp(c.f[i].name, "P_local"),
              "defaults: 1 = P_local BP 1-20 Hz");
        i = csnhypodbp_filtercfg_find_key(&c, '4');
        CHECK(i >= 0 && c.f[i].type == 3 && c.f[i].f2 == 8.0 &&
              !strcmp(c.f[i].name, "S_local"),
              "defaults: 4 = S_local BP 0.5-8 Hz");
        i = csnhypodbp_filtercfg_find_key(&c, 'd');
        CHECK(i >= 0 && c.f[i].f1 == 0.001 && c.f[i].f2 == 12.0 && c.f[i].order == 4,
              "defaults: d = BP 0.001-12 Hz orden 4");
    }
    CHECK(c.menu_key == 'f' && c.raw_key == '0', "defaults: menu f / raw 0");
    CHECK(c.phase_zero == 1, "defaults: fase cero");
    CHECK(c.stalta_declared && !c.stalta_on && c.stalta_key == 't',
          "defaults: StaLta declarada, apagada, tecla t");

    /* 2. Teclas: mayúsculas se normalizan */
    CHECK(csnhypodbp_filtercfg_find_key(&c, 'R') ==
          csnhypodbp_filtercfg_find_key(&c, 'r'),
          "find_key: R == r");
    CHECK(csnhypodbp_filtercfg_find_key(&c, 'z') < 0, "find_key: tecla libre -> -1");

    /* 2b. Dispatch de teclas (incluye el toggle) */
    {
        int r = csnhypodbp_filtercfg_find_key(&c, 'r');
        CHECK(csnhypodbp_filtercfg_key_action(&c, -1, 'r') == r,
              "tecla: r con Raw activa HP");
        CHECK(csnhypodbp_filtercfg_key_action(&c, r, 'r') ==
              CSNHYPODBP_FILTER_KEY_RAW, "tecla: r repetida apaga (Raw)");
        CHECK(csnhypodbp_filtercfg_key_action(&c, -1, 'R') == r,
              "tecla: R equivale a r");
        CHECK(csnhypodbp_filtercfg_key_action(&c, -1, '0') ==
              CSNHYPODBP_FILTER_KEY_RAW, "tecla: 0 -> Raw");
        CHECK(csnhypodbp_filtercfg_key_action(&c, -1, 'f') ==
              CSNHYPODBP_FILTER_KEY_MENU, "tecla: f abre Filters");
        CHECK(csnhypodbp_filtercfg_key_action(&c, -1, 'z') ==
              CSNHYPODBP_FILTER_KEY_NONE, "tecla: libre -> NONE");
        int p2 = csnhypodbp_filtercfg_find_key(&c, '2');
        CHECK(csnhypodbp_filtercfg_key_action(&c, r, '2') == p2,
              "tecla: cambiar de preset");
    }

    /* 3. Alta y reemplazo de presets */
    CHECK(add(&c, "Filter", "BP x 2.0 5.0 2") == 0, "alta: Filter BP x");
    CHECK(c.nf == 8 && csnhypodbp_filtercfg_find_key(&c, 'x') >= 0, "alta: 8 presets");
    CHECK(add(&c, "Filter", "HP r 2.0 4") == 0, "reemplazo: Filter HP r");
    {
        int i = csnhypodbp_filtercfg_find_key(&c, 'r');
        CHECK(c.nf == 8 && c.f[i].f1 == 2.0 && c.f[i].order == 4,
              "reemplazo: no duplica y actualiza");
    }

    /* 4. Nombre opcional */
    CHECK(add(&c, "Filter", "MiBP BP y 0.5 4.0 2") == 0, "alta con nombre");
    {
        int i = csnhypodbp_filtercfg_find_key(&c, 'y');
        CHECK(i >= 0 && !strcmp(c.f[i].name, "MiBP") && c.f[i].f2 == 4.0,
              "nombre opcional conservado");
    }

    /* 5. Errores de parseo */
    CHECK(add(&c, "Filter", "HP q 0 2") != 0, "error: f1 <= 0");
    CHECK(add(&c, "Filter", "HP q 1 3") != 0, "error: orden 3");
    CHECK(add(&c, "Filter", "BP q 5 2 2") != 0, "error: f1 >= f2");
    CHECK(add(&c, "Filter", "HP") != 0, "error: faltan argumentos");
    CHECK(add(&c, "Filter", "XX q 1 2") != 0, "error: tipo invalido");
    CHECK(add(&c, "FilterPhase", "foo") != 0, "error: FilterPhase foo");
    CHECK(add(&c, "FilterMenu", "p") != 0, "error: tecla reservada p");
    CHECK(add(&c, "FilterMenu", "1") != 0, "error: tecla ya usada por preset");
    CHECK(add(&c, "StaLta", "t 0 10 3 1.5") != 0, "error: sta <= 0");
    CHECK(add(&c, "StaLta", "t 0.5 0.2 3 1.5") != 0, "error: lta < sta");
    CHECK(add(&c, "Filter", "HP q 1 2 extra") != 0, "error: argumentos de mas");
    CHECK(add(&c, "Filter", "HP q inf 2") != 0, "error: frecuencia no finita");
    CHECK(add(&c, "Filter", "nombre_demasiado_largo BP q 1 5 2") != 0,
          "error: nombre demasiado largo");
    CHECK(add(&c, "StaLta", "u 0.5 10 3 1.5 extra") != 0,
          "error: StaLta argumentos de mas");

    /* 6. Duplicado dentro del .d */
    CHECK(add(&c, "Filter", "LP z 0.5 2") == 0, "alta: LP z");
    CHECK(add(&c, "Filter", "HP z 1.0 2") != 0, "error: tecla duplicada en el .d");

    /* 7. FilterPhase causal */
    CHECK(add(&c, "FilterPhase", "causal") == 0 && c.phase_zero == 0,
          "FilterPhase causal");
    CHECK(add(&c, "FilterPhase", "zero") == 0 && c.phase_zero == 1,
          "FilterPhase zero");

    /* 8. StaLta completa */
    CHECK(add(&c, "StaLta", "u 1.0 20.0 4 2 -0.5") == 0, "StaLta parse");
    CHECK(c.stalta_key == 'u' && c.stalta_sta == 1.0 && c.stalta_lta == 20.0 &&
          c.stalta_on_thr == 4.0 && c.stalta_off_thr == 2.0 &&
          c.stalta_corr == -0.5,
          "StaLta valores");
    CHECK(add(&c, "Filter", "BP u 1 2 2") != 0, "error: tecla ya usada por StaLta");

    /* 9. Etiquetas del HUD */
    {
        char buf[192];
        csnhypodbp_filtercfg_defaults(&c);
        csnhypodbp_filtercfg_format_active(&c, -1, 0, 0.0, 0.0, 0, buf, sizeof(buf));
        CHECK(!strcmp(buf, "Raw (0)"), "HUD: Raw (0)");

        int i = csnhypodbp_filtercfg_find_key(&c, 'r');
        csnhypodbp_filtercfg_format_active(&c, i, 0, 0.0, 0.0, 0, buf, sizeof(buf));
        CHECK(!strcmp(buf, "HP 1.0 Hz · n=2 (r)"), "HUD: HP 1.0 Hz · n=2 (r)");

        i = csnhypodbp_filtercfg_find_key(&c, 'd');
        csnhypodbp_filtercfg_format_active(&c, i, 0, 0.0, 0.0, 0, buf, sizeof(buf));
        CHECK(!strcmp(buf, "BP 0.001-12 Hz · n=4 (d)"),
              "HUD: BP 0.001-12 Hz · n=4 (d)");

        i = csnhypodbp_filtercfg_find_key(&c, '2');
        csnhypodbp_filtercfg_format_active(&c, i, 0, 0.0, 0.0, 0, buf, sizeof(buf));
        CHECK(!strcmp(buf, "P_regional BP 0.70-10 Hz · n=2 (2)"),
              "HUD: P_regional BP 0.70-10 Hz · n=2 (2)");

        csnhypodbp_filtercfg_format_active(&c, -2, 3, 0.5, 8.0, 2, buf, sizeof(buf));
        CHECK(!strcmp(buf, "BP 0.50-8.0 Hz · n=2 (manual)"),
              "HUD: filtro manual (no dice Raw)");

        c.stalta_on = 1;
        csnhypodbp_filtercfg_format_active(&c, i, 0, 0.0, 0.0, 0, buf, sizeof(buf));
        CHECK(strstr(buf, "+ STA/LTA 0.5/10 s") != NULL,
              "HUD: sufijo STA/LTA");
    }

    /* 10. Texto de Commands… sale de la config vigente */
    {
        char buf[2048];
        csnhypodbp_filtercfg_defaults(&c);
        csnhypodbp_filtercfg_help_text(&c, buf, sizeof(buf));
        CHECK(strstr(buf, "P_local") != NULL, "help: lista P_local");
        CHECK(strstr(buf, "  r  ") != NULL, "help: lista la tecla r");
        CHECK(strstr(buf, "Filters") != NULL, "help: menciona la ventana Filters");
    }

    if (failures == 0) { printf("\nALL FILTERCFG TESTS PASSED\n"); return 0; }
    printf("\n%d FILTERCFG TEST(S) FAILED\n", failures);
    return 1;
}
