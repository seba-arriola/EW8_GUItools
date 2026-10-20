/* Test headless de csnhypodbp_trace: parseo TYPE_PICK_SCNL, captura por
 * estacion y referencia P (picada vs teorica). Sin GTK/EW link. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "csnhypodbp_trace.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void)
{
    CsnhypodbpTracePick p;

    /* 10 tokens (pick_FP): fase ausente => P, origen ausente => A. */
    CHECK(csnhypodbp_pick_parse(
        "8 1 1 7 ABC.HHZ.XX.00 ?0 20240101120000.000 0 0 0", &p),
        "parse 10 tokens");
    CHECK(!strcmp(p.sta, "ABC"), "sta");
    CHECK(!strcmp(p.chan, "HHZ"), "chan");
    CHECK(p.phase == 'P', "fase default P");
    CHECK(p.origin == 'A', "origen default A");
    CHECK(p.t > 1.0e9, "tiempo valido");

    /* P M */
    CHECK(csnhypodbp_pick_parse(
        "8 1 1 8 ABC.HHZ.XX.00 ?0 20240101120000.000 0 0 0 P M", &p),
        "parse P M");
    CHECK(p.phase == 'P' && p.origin == 'M', "P M");

    /* S M (canal horizontal) */
    CHECK(csnhypodbp_pick_parse(
        "8 1 1 9 ABC.HHN.XX.00 ?0 20240101120005.000 0 0 0 S M", &p),
        "parse S M");
    CHECK(p.phase == 'S' && p.origin == 'M', "S M");
    CHECK(!strcmp(p.chan, "HHN"), "chan S");

    /* S A explicito */
    CHECK(csnhypodbp_pick_parse(
        "8 1 1 10 ABC.HHE.XX.00 ?0 20240101120005.000 0 0 0 S A", &p),
        "parse S A");
    CHECK(p.phase == 'S' && p.origin == 'A', "S A");

    /* Mensaje invalido */
    CHECK(!csnhypodbp_pick_parse("basura", &p), "parse invalido");

    /* Captura por estacion (ignora canal). */
    CsnhypodbpPickCap cap;
    csnhypodbp_cap_init(&cap);
    CsnhypodbpTracePick pp = { "ABC", "HHZ", 'P', 'A', 1000.0 };
    CsnhypodbpTracePick ps = { "ABC", "HHN", 'S', 'M', 1010.0 };
    CsnhypodbpTracePick p2 = { "XYZ", "HHZ", 'P', 'A', 1005.0 };
    csnhypodbp_cap_add(&cap, &pp);
    csnhypodbp_cap_add(&cap, &ps);
    csnhypodbp_cap_add(&cap, &p2);

    CsnhypodbpTracePick out;
    CHECK(csnhypodbp_cap_find(&cap, "ABC", 'P', 1001.0, 600.0, &out), "find P ABC");
    CHECK(out.t == 1000.0, "P ABC t");
    CHECK(csnhypodbp_cap_find(&cap, "ABC", 'S', 1001.0, 600.0, &out), "find S ABC");
    CHECK(out.chan && !strcmp(out.chan, "HHN"), "S ABC canal");
    CHECK(!csnhypodbp_cap_find(&cap, "ZZZ", 'P', 1001.0, 600.0, &out), "no P ZZZ");
    CHECK(!csnhypodbp_cap_find(&cap, "ABC", 'P', 5000.0, 600.0, &out), "P fuera de ventana");

    CsnhypodbpTracePick all[8];
    int na = csnhypodbp_cap_find_all(&cap, "ABC", 'P', 1001.0, 600.0, all, 8);
    CHECK(na == 1, "find_all P ABC");

    /* Referencia P: pick si existe, teorica si no. */
    double rt = 0.0;
    CHECK(csnhypodbp_ref_pick(&pp, 1, 900.0, &rt) == 1 && rt == 1000.0, "ref pick");
    CHECK(csnhypodbp_ref_pick(&ps, 1, 900.0, &rt) == 0 && rt == 900.0, "ref theo (solo S)");

    /* Color de traza: SIEMPRE negra (el origen va en la marca del pick). */
    double rgb[3] = { 9.0, 9.0, 9.0 };
    csnhypodbp_trace_rgb(rgb);
    CHECK(rgb[0] == 0.0 && rgb[1] == 0.0 && rgb[2] == 0.0, "traza siempre negra");

    /* Aclarado hacia blanco: c + (1-c)*frac, con acotado. */
    CHECK(fabs(csnhypodbp_color_tint_channel(0.0, 0.40) - 0.40) < 1e-9, "aclarado 40");
    CHECK(fabs(csnhypodbp_color_tint_channel(0.0, 0.70) - 0.70) < 1e-9, "aclarado 70");
    CHECK(fabs(csnhypodbp_color_tint_channel(1.0, 0.70) - 1.0) < 1e-9, "blanco sigue blanco");
    CHECK(fabs(csnhypodbp_color_tint_channel(-1.0, 0.40) - 0.40) < 1e-9, "canal acotado");
    CHECK(fabs(csnhypodbp_color_tint_channel(0.0, -1.0)) < 1e-9, "frac acotado abajo");
    CHECK(fabs(csnhypodbp_color_tint_channel(0.0, 2.0) - 1.0) < 1e-9, "frac acotado arriba");

    /* Herencia viva: auto/teo se derivan del manual al resolver. */
    CsnhypodbpColorSet cs;
    const double red[3] = { 1.0, 0.0, 0.0 };
    const double green[3] = { 0.0, 1.0, 0.0 };
    const double blue[3] = { 0.0, 0.0, 1.0 };
    csnhypodbp_color_set_init(&cs, red);
    csnhypodbp_color_resolve(&cs, CSNHYPODBP_PICK_MANUAL, rgb);
    CHECK(rgb[0] == 1.0 && rgb[1] == 0.0 && rgb[2] == 0.0, "manual = base");
    csnhypodbp_color_resolve(&cs, CSNHYPODBP_PICK_AUTO, rgb);
    CHECK(fabs(rgb[1] - 0.40) < 1e-9 && fabs(rgb[2] - 0.40) < 1e-9, "auto hereda 40%");
    csnhypodbp_color_resolve(&cs, CSNHYPODBP_PICK_THEO, rgb);
    CHECK(fabs(rgb[1] - 0.70) < 1e-9 && fabs(rgb[2] - 0.70) < 1e-9, "teo hereda 70%");
    csnhypodbp_color_set_override(&cs, CSNHYPODBP_PICK_MANUAL, green);
    csnhypodbp_color_resolve(&cs, CSNHYPODBP_PICK_AUTO, rgb);
    CHECK(fabs(rgb[0] - 0.40) < 1e-9 && fabs(rgb[1] - 1.0) < 1e-9 &&
          fabs(rgb[2] - 0.40) < 1e-9,
          "herencia viva sigue al manual (auto)");
    csnhypodbp_color_resolve(&cs, CSNHYPODBP_PICK_THEO, rgb);
    CHECK(fabs(rgb[0] - 0.70) < 1e-9 && rgb[1] == 1.0, "herencia viva sigue al manual (teo)");
    csnhypodbp_color_set_override(&cs, CSNHYPODBP_PICK_AUTO, blue);
    csnhypodbp_color_set_override(&cs, CSNHYPODBP_PICK_THEO, blue);
    csnhypodbp_color_set_override(&cs, CSNHYPODBP_PICK_MANUAL, red);
    csnhypodbp_color_resolve(&cs, CSNHYPODBP_PICK_AUTO, rgb);
    CHECK(rgb[0] == 0.0 && rgb[1] == 0.0 && rgb[2] == 1.0, "auto explicita gana al manual");
    csnhypodbp_color_resolve(&cs, CSNHYPODBP_PICK_THEO, rgb);
    CHECK(rgb[0] == 0.0 && rgb[1] == 0.0 && rgb[2] == 1.0, "teo explicita gana al manual");

    /* Origen: M manual, T teorica, el resto (A, 0, espacio) automatica. */
    CHECK(csnhypodbp_pick_origin('M') == CSNHYPODBP_PICK_MANUAL, "M => manual");
    CHECK(csnhypodbp_pick_origin('T') == CSNHYPODBP_PICK_THEO, "T => teorica");
    CHECK(csnhypodbp_pick_origin('A') == CSNHYPODBP_PICK_AUTO, "A => automatica");
    CHECK(csnhypodbp_pick_origin('\0') == CSNHYPODBP_PICK_AUTO, "sin origen => automatica");
    CHECK(csnhypodbp_pick_origin(' ') == CSNHYPODBP_PICK_AUTO, "origen raro => automatica");

    /* Hex para el log y --print-config (redondeo half-up). */
    char hex[7];
    const double h1[3] = { 1.0, 0.4, 0.4 };
    const double h2[3] = { 1.0, 0.7, 0.7 };
    const double h3[3] = { 0.0, 0.6, 0.0 };
    const double h4[3] = { 0.4, 0.76, 0.4 };
    const double h5[3] = { 0.7, 0.88, 0.7 };
    csnhypodbp_color_to_hex(h1, hex);
    CHECK(!strcmp(hex, "FF6666"), "hex auto P");
    csnhypodbp_color_to_hex(h2, hex);
    CHECK(!strcmp(hex, "FFB3B3"), "hex teo P");
    csnhypodbp_color_to_hex(h3, hex);
    CHECK(!strcmp(hex, "009900"), "hex S base");
    csnhypodbp_color_to_hex(h4, hex);
    CHECK(!strcmp(hex, "66C266"), "hex auto S");
    csnhypodbp_color_to_hex(h5, hex);
    CHECK(!strcmp(hex, "B3E0B3"), "hex teo S");

    /* Fase por tecla. */
    CHECK(csnhypodbp_phase_from_char('s') == 'S', "s => S");
    CHECK(csnhypodbp_phase_from_char('S') == 'S', "S => S");
    CHECK(csnhypodbp_phase_from_char('p') == 'P', "p => P");
    CHECK(csnhypodbp_phase_from_char('x') == 'P', "x => P");

    if (fails == 0) printf("test_trace: OK\n");
    return fails ? 1 : 0;
}
