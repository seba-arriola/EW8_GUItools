/*
 * test_traces - rejilla de trazas por pantalla (`tracesPerScreen`), headless.
 * Cubre csnhypodbp_traces_layout / csnhypodbp_traces_per_screen_norm:
 *   - una banda por hueco de la rejilla, sin importar cuantos canales haya;
 *   - el scroll aparece exactamente cuando hay mas canales que huecos;
 *   - el suelo de banda (CSNHYPODBP_BAND_MIN) y el viewport sin asignar.
 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "csnhypodbp_traces.h"

#define BAND_MIN CSNHYPODBP_TRACES_BAND_MIN   /* mismo suelo que el modulo */

static CsnhypodbpTracesLayout lay(int n_rows, int per_screen, int viewport)
{
    return csnhypodbp_traces_layout(n_rows, per_screen, viewport, BAND_MIN);
}

int main(void)
{
    CsnhypodbpTracesLayout l;

    /* --- tracesPerScreen: solo 1..3 ------------------------------------- */
    assert(csnhypodbp_traces_per_screen_norm(1) == 1);
    assert(csnhypodbp_traces_per_screen_norm(2) == 2);
    assert(csnhypodbp_traces_per_screen_norm(3) == 3);
    assert(csnhypodbp_traces_per_screen_norm(0) == 0);
    assert(csnhypodbp_traces_per_screen_norm(4) == 0);
    assert(csnhypodbp_traces_per_screen_norm(-1) == 0);
    assert(csnhypodbp_traces_per_screen_norm(99) == 0);
    assert(csnhypodbp_traces_per_screen_norm(INT_MIN) == 0);

    /* --- 6 canales, viewport 860: 1, 2 y 3 por pantalla ------------------ */
    /* 1 por pantalla: cada banda ocupa el viewport entero (860-60). */
    l = lay(6, 1, 860);
    assert(l.per_screen == 1);
    assert(l.slot_count == 6);
    assert(l.band_h == 800);
    assert(l.canvas_h == 6 * 800 + 60);        /* 4860 > 860: scroll */

    /* 2 por pantalla (la mitad): el contenido se reduce a la mitad. */
    l = lay(6, 2, 860);
    assert(l.slot_count == 6);
    assert(l.band_h == 400);
    assert(l.canvas_h == 6 * 400 + 60);        /* 2460 */

    /* 3 por pantalla (un tercio, truncado): 800/3 = 266. */
    l = lay(6, 3, 860);
    assert(l.slot_count == 6);
    assert(l.band_h == 266);
    assert(l.canvas_h == 6 * 266 + 60);        /* 1656 */

    /* --- menos canales que huecos: no se estira la traza ---------------- */
    l = lay(2, 3, 860);
    assert(l.slot_count == 3);                 /* 3 huecos reservados */
    assert(l.band_h == 266);                   /* la banda NO crece */
    assert(l.canvas_h == 3 * 266 + 60);        /* 858 <= 860: sin scroll */

    /* --- sin estacion / sin canales ------------------------------------- */
    l = lay(0, 2, 700);
    assert(l.slot_count == 2);
    assert(l.band_h == 320);
    assert(l.canvas_h == 700);

    /* --- viewport sin asignar (arranque): se usa el suelo --------------- */
    l = lay(2, 3, 0);
    assert(l.slot_count == 3);
    assert(l.band_h == BAND_MIN);
    assert(l.canvas_h == 3 * BAND_MIN + 60);
    l = lay(0, 1, 0);
    assert(l.slot_count == 1);
    assert(l.band_h == BAND_MIN);

    /* --- suelo de banda: ventana muy baja (scroll aunque n_rows == per) -- */
    l = lay(3, 3, 200);
    assert(l.band_h == BAND_MIN);              /* 140/3 = 46 -> suelo */
    assert(l.canvas_h == 3 * BAND_MIN + 60);   /* 390 > 200 */

    /* --- valor invalido del .d: el nucleo normaliza a 1 ----------------- */
    l = lay(2, 9, 700);
    assert(l.per_screen == 1);
    assert(l.slot_count == 2);
    assert(l.band_h == 640);
    assert(l.canvas_h == 2 * 640 + 60);

    /* --- invariantes sobre todo el rango -------------------------------- */
    {
        static const int views[] = { 0, 200, 480, 700, 860, 1200 };
        int vi, n, tps;

        for (vi = 0; vi < (int)(sizeof(views) / sizeof(views[0])); vi++) {
            int vp = views[vi];
            for (tps = 1; tps <= 3; tps++) {
                int band_ref = -1;
                for (n = 0; n <= 8; n++) {
                    l = lay(n, tps, vp);
                    /* huecos = max(n, per_screen); nunca 0 */
                    assert(l.slot_count == (n > tps ? n : tps));
                    assert(l.slot_count >= 1);
                    assert(l.per_screen == tps);
                    /* alto de banda >= suelo y AJENO a n_rows */
                    assert(l.band_h >= BAND_MIN);
                    if (band_ref < 0) band_ref = l.band_h;
                    assert(l.band_h == band_ref);
                    /* el alto pedido siempre cubre la rejilla */
                    assert(l.canvas_h == l.slot_count * l.band_h +
                                          CSNHYPODBP_TRACES_MARGIN);
                    /* mas canales que huecos => hay scroll */
                    if (n > tps)
                        assert(l.canvas_h > vp);
                    /* con viewport suficiente y huecos de sobra, no hay scroll */
                    if (n <= tps && vp >= tps * BAND_MIN +
                                         CSNHYPODBP_TRACES_MARGIN)
                        assert(l.canvas_h <= vp);
                }
            }
        }
    }

    printf("test_traces OK\n");
    return 0;
}
