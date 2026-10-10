/* Test headless de csnhypodbp_trace: parseo TYPE_PICK_SCNL, captura por
 * estacion y referencia P (picada vs teorica). Sin GTK/EW link. */
#include <stdio.h>
#include <string.h>

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

    /* Color de traza. */
    double rgb[3];
    csnhypodbp_trace_rgb(1, rgb);
    CHECK(rgb[0] == 0.0 && rgb[1] == 0.0 && rgb[2] == 0.0, "traza negra");
    csnhypodbp_trace_rgb(0, rgb);
    CHECK(rgb[0] > 0.5 && rgb[1] > 0.5 && rgb[2] > 0.5, "traza gris");

    /* Fase por tecla. */
    CHECK(csnhypodbp_phase_from_char('s') == 'S', "s => S");
    CHECK(csnhypodbp_phase_from_char('S') == 'S', "S => S");
    CHECK(csnhypodbp_phase_from_char('p') == 'P', "p => P");
    CHECK(csnhypodbp_phase_from_char('x') == 'P', "x => P");

    if (fails == 0) printf("test_trace: OK\n");
    return fails ? 1 : 0;
}
