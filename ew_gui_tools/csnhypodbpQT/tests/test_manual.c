/*
 * test_manual.c - Overlay de picadas manuales y ventana de las trazas (fase 4.5).
 *
 * Verifica, sin GUI:
 *   - que `set` REEMPLAZA la picada de esa fase (mover el pick no acumula) y
 *     deja intacta la de la otra fase;
 *   - `remove` por fase, `clear` y el tope de `list`;
 *   - la persistencia: round-trip por archivo, archivo ausente y lineas basura;
 *   - el zoom/paneo de las trazas: el instante bajo el cursor se conserva, la
 *     ventana se acota y el arrastre mueve el contenido con el cursor.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bpq_capi.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

static const CsnhypodbpManualPick *find(const CsnhypodbpManualPick *v, int n,
                                        const char *sta, char ph)
{
    int i;

    for (i = 0; i < n; i++)
        if (strcmp(v[i].sta, sta) == 0 && v[i].phase == ph) return &v[i];
    return NULL;
}

int main(void)
{
    const char *path = "/tmp/bpq_test_manual_picks.txt";
    CsnhypodbpManualPick v[CSNHYPODBP_MANUAL_MAX];

    printf("== test_manual ==\n");

    /* --- 1. set / reemplazo por fase --------------------------------------- */
    bpq_manual_clear();
    CK(bpq_manual_count() == 0);
    CK(bpq_manual_set("PB16", "HHZ", 'P', 1000.5) == 0);
    CK(bpq_manual_count() == 1);
    CK(bpq_manual_set("PB16", "HHZ", 'S', 1003.25) == 0);
    CK(bpq_manual_count() == 2);

    /* Mover la P (mismo sta+fase) recorta la anterior, no suma. */
    CK(bpq_manual_set("PB16", "HHZ", 'P', 1000.75) == 0);
    CK(bpq_manual_count() == 2);
    {
        int n = bpq_manual_list(v, CSNHYPODBP_MANUAL_MAX);
        const CsnhypodbpManualPick *p = find(v, n, "PB16", 'P');
        const CsnhypodbpManualPick *s = find(v, n, "PB16", 'S');

        CK(n == 2);
        CK(p != NULL && s != NULL);
        CK(p && fabs(p->t - 1000.75) < 1e-6);
        CK(s && fabs(s->t - 1003.25) < 1e-6);
        CK(s && strcmp(s->chan, "HHZ") == 0);
    }

    /* Otra estacion convive. */
    CK(bpq_manual_set("PB12", "HHZ", 'P', 1010.0) == 0);
    CK(bpq_manual_count() == 3);

    /* --- 2. remove por fase y bordes --------------------------------------- */
    CK(bpq_manual_remove("PB16", 'P') == 1);
    CK(bpq_manual_count() == 2);
    CK(bpq_manual_remove("PB16", 'P') == 0);      /* ya no estaba */
    CK(bpq_manual_remove("NOEXISTE", 'S') == 0);
    {
        int n = bpq_manual_list(v, CSNHYPODBP_MANUAL_MAX);

        CK(find(v, n, "PB16", 'P') == NULL);
        CK(find(v, n, "PB16", 'S') != NULL);
        CK(find(v, n, "PB12", 'P') != NULL);
    }
    /* Argumentos invalidos. */
    CK(bpq_manual_set(NULL, "HHZ", 'P', 1.0) == -1);
    CK(bpq_manual_set("X", "HHZ", 'X', 1.0) == -1);
    CK(bpq_manual_set("", "HHZ", 'P', 1.0) == -1);
    CK(bpq_manual_count() == 2);

    /* --- 3. persistencia --------------------------------------------------- */
    CK(bpq_manual_save(path, "933600024:0:1") == 0);
    bpq_manual_clear();
    CK(bpq_manual_count() == 0);
    CK(bpq_manual_load(path, "933600024:0:1") == 1);
    CK(bpq_manual_count() == 2);
    {
        int n = bpq_manual_list(v, CSNHYPODBP_MANUAL_MAX);
        const CsnhypodbpManualPick *s = find(v, n, "PB16", 'S');

        CK(s != NULL);
        CK(s && fabs(s->t - 1003.25) < 1e-5);   /* 6 decimales en el archivo */
        CK(s && strcmp(s->chan, "HHZ") == 0);
    }

    /* Otro evento: el load devuelve -1 y NO toca la lista; el llamador limpia
     * al cambiar de evento (asi una recarga por cambio de solucion no borra). */
    CK(bpq_manual_load(path, "111:0:1") == -1);
    CK(bpq_manual_count() == 2);
    /* Sin clave: argumentos invalidos (-2) y la lista queda intacta. */
    CK(bpq_manual_load(path, NULL) == -2);
    CK(bpq_manual_count() == 2);
    CK(bpq_manual_load(path, "") == -2);
    CK(bpq_manual_count() == 2);

    /* Archivo ausente: overlay vacio y 0 (no es error). */
    bpq_manual_clear();
    CK(bpq_manual_load("/tmp/bpq_no_existe_12345.txt", "k") == 0);
    CK(bpq_manual_count() == 0);

    /* Lineas basura y fase invalida se ignoran. */
    {
        FILE *f = fopen(path, "w");

        CK(f != NULL);
        if (f) {
            fprintf(f, "# csnhypodbpQT manual picks v2 K1\n");
            fprintf(f, "basura\n");
            fprintf(f, "PB01 HHZ X 5.0\n");
            fprintf(f, "PB01 HHZ P 7.5\n");
            fclose(f);
        }
        CK(bpq_manual_load(path, "K1") == 1);
        CK(bpq_manual_count() == 1);
        {
            int n = bpq_manual_list(v, CSNHYPODBP_MANUAL_MAX);

            CK(find(v, n, "PB01", 'P') != NULL);
        }
    }
    /* Formato v1 (sin clave en el encabezado): se acepta. */
    {
        FILE *f = fopen(path, "w");

        CK(f != NULL);
        if (f) {
            fprintf(f, "# csnhypodbpQT manual picks v1\n");
            fprintf(f, "PB02 HHZ S 9.5\n");
            fclose(f);
        }
        CK(bpq_manual_load(path, "cualquiera") == 1);
        CK(bpq_manual_count() == 1);
    }
    bpq_manual_clear();

    /* --- 4. zoom/paneo de las trazas --------------------------------------- */
    {
        BpqTracesCtx c;
        const int w = 1000;
        double t_before, t_after, win0;

        memset(&c, 0, sizeof(c));
        c.t0 = 1700000000.0;
        c.window = 120.0;
        c.dpr = 1.0;

        /* El margen izquierdo de las etiquetas (58 px con dpr=1) desplaza la
         * pista: el instante bajo el cursor se toma de la MISMA geometria que
         * el clic. */
        win0 = c.window;
        bpq_traces_zoom_at(&c, w, 500.0, 2.0);
        CK(c.window < win0);
        CK(fabs(c.window - win0 / 2.0) < 1e-6);

        /* El instante del cursor no se mueve con el zoom. */
        t_before = 1700000000.0 + ((500.0 - 72.0) / (w - 72.0 - 6.0)) * 120.0;
        t_after = c.t0 + ((500.0 - 72.0) / (w - 72.0 - 6.0)) * c.window;
        CK(fabs(t_after - t_before) < 0.6);   /* < 1 pixel de ventana */

        /* Paneo: arrastrar 100 px a la derecha va al pasado. */
        {
            const double t0 = c.t0;

            bpq_traces_pan(&c, w, 100.0);
            CK(c.t0 < t0);
            CK(fabs((t0 - c.t0) - (100.0 / (w - 72.0 - 6.0)) * c.window) < 1e-6);
        }

        /* Limites: la ventana no baja de 1 s ni pasa de 1 h. */
        c.t0 = 1700000000.0;
        c.window = 120.0;
        bpq_traces_zoom_at(&c, w, 500.0, 1e6);
        CK(fabs(c.window - 1.0) < 1e-9);
        c.window = 120.0;
        bpq_traces_zoom_at(&c, w, 500.0, 1e-6);
        CK(fabs(c.window - 3600.0) < 1e-9);

        /* Sin geometria valida no toca nada. */
        c.window = 120.0;
        c.t0 = 1700000000.0;
        bpq_traces_zoom_at(&c, 0, 10.0, 2.0);
        CK(fabs(c.window - 120.0) < 1e-9);
        bpq_traces_pan(&c, 0, 10.0);
        CK(fabs(c.t0 - 1700000000.0) < 1e-9);
        bpq_traces_zoom_at(NULL, w, 10.0, 2.0);
        bpq_traces_pan(NULL, w, 10.0);
    }

    /* --- 5. zoom por dos clics (rango) ------------------------------------- */
    {
        BpqTracesCtx c;
        const int w = 1000;
        const double x0 = 72.0, pw = w - 72.0 - 6.0;

        memset(&c, 0, sizeof(c));
        c.t0 = 1700000000.0;
        c.window = 100.0;
        c.dpr = 1.0;

        /* El borde izquierdo de la pista es exactamente t0. */
        CK(fabs(bpq_traces_time_at(&c, w, 72.0) - 1700000000.0) < 1e-6);
        CK(fabs(bpq_traces_time_at(&c, w, 72.0 + pw) - 1700000100.0) < 1e-6);
        CK(bpq_traces_time_at(&c, 0, 10.0) == 0.0);
        CK(bpq_traces_time_at(NULL, w, 10.0) == 0.0);

        {
            const double t0_0 = c.t0, w_0 = c.window;

            CK(bpq_traces_zoom_range(&c, w, 220.0, 640.0) == 1);
            CK(c.window < w_0);
            CK(c.t0 > t0_0);
            /* Normaliza el orden: el inverso da el MISMO intervalo. */
            {
                const double win1 = c.window, t1 = c.t0;

                c.t0 = t0_0;
                c.window = w_0;
                CK(bpq_traces_zoom_range(&c, w, 640.0, 220.0) == 1);
                CK(fabs(c.window - win1) < 1e-9);
                CK(fabs(c.t0 - t1) < 1e-9);
            }
            /* Rango casi nulo: no toca la ventana. */
            c.t0 = t0_0;
            c.window = w_0;
            CK(bpq_traces_zoom_range(&c, w, 300.0, 300.2) == 0);
            CK(fabs(c.t0 - t0_0) < 1e-9);
            CK(fabs(c.window - w_0) < 1e-9);
        }
        CK(bpq_traces_zoom_range(NULL, w, 100.0, 200.0) == 0);
        CK(bpq_traces_zoom_range(&c, 0, 100.0, 200.0) == 0);
    }

    /* --- 6. canales de la estacion actual (channelsOrder x componentsOrder) - */
    {
        const char *cat = "/tmp/bpq_test_chan_sta.txt";
        FILE *f = fopen(cat, "w");
        BpqConfig cfg;
        BpqStations *c;
        CsnhypodbpStaRow base, out[8];
        char chans[8][8];
        char err[128] = "";
        int n;

        CK(f != NULL);
        if (f) {
            fprintf(f, "XX C HHN -- -30.00000 -70.00000 1000.0 1.0e+09\n");
            fprintf(f, "XX C HHE -- -30.00000 -70.00000 1000.0 1.0e+09\n");
            fprintf(f, "XX C HHZ -- -30.00000 -70.00000 1000.0 1.0e+09\n");
            fprintf(f, "YY C HHZ -- -31.00000 -70.00000 1000.0 1.0e+09\n");
            fclose(f);
        }
        memset(&cfg, 0, sizeof(cfg));
        snprintf(cfg.sta_file, sizeof(cfg.sta_file), "%s", cat);
        c = bpq_stations_open(&cfg, err, sizeof(err));
        CK(c != NULL);

        memset(&base, 0, sizeof(base));
        snprintf(base.sta, sizeof(base.sta), "XX");
        base.idx = 0;
        base.p.present = 1;
        base.p.origin = 'A';
        base.p.t = 1000.0;
        base.dist_km = 42.0;

        n = bpq_stations_chan_rows(c, "XX", "HH BH HN LH", "Z N E", &base, out,
                                   chans, 8);
        CK(n == 3);
        CK(strcmp(chans[0], "HHZ") == 0);
        CK(strcmp(chans[1], "HHN") == 0);
        CK(strcmp(chans[2], "HHE") == 0);
        /* Todas las componentes heredan las picadas de la estacion. */
        CK(out[0].p.present == 1 && out[2].p.present == 1);
        CK(fabs(out[0].p.t - 1000.0) < 1e-9);
        /* Disponibilidad por BANDA: YY solo lista HHZ y aun asi da 3 canales. */
        {
            CsnhypodbpStaRow base2 = base;

            snprintf(base2.sta, sizeof(base2.sta), "YY");
            n = bpq_stations_chan_rows(c, "YY", "HH", "Z N E", &base2, out,
                                       chans, 8);
            CK(n == 3);
            CK(strcmp(chans[0], "HHZ") == 0);
            CK(strcmp(chans[2], "HHE") == 0);
        }
        /* Otro componentsOrder cambia el orden, no el conjunto. */
        n = bpq_stations_chan_rows(c, "XX", "HH", "N Z E", &base, out, chans, 8);
        CK(n == 3);
        CK(strcmp(chans[0], "HHN") == 0);
        CK(strcmp(chans[1], "HHZ") == 0);
        /* Bandas que la estacion no tiene: no aparecen (BH no esta en XX). */
        n = bpq_stations_chan_rows(c, "XX", "BH", "Z N E", &base, out, chans, 8);
        CK(n == 1);                       /* fallback: la fila base */
        /* Estacion fuera del catalogo: al menos la fila base. */
        n = bpq_stations_chan_rows(c, "ZZ", "HH", "Z N E", &base, out, chans, 8);
        CK(n == 1 && out[0].idx == base.idx);
        CK(bpq_stations_chan_rows(NULL, "XX", "HH", "Z", &base, out, chans, 8) == 0);
        bpq_stations_close(c);
    }

    /* --- 7. Shift+flechas: span centrado y visibilidad por tracesPerScreen - */
    {
        double t0 = 1000.0, win = 100.0;

        bpq_traces_zoom_span(&t0, &win, +1);
        CK(fabs(win - 150.0) < 1e-9);
        CK(fabs(t0 - (1050.0 - 75.0)) < 1e-9);   /* centro conservado */
        bpq_traces_zoom_span(&t0, &win, -1);
        CK(fabs(win - 100.0) < 1e-9);
        win = 1e9;
        bpq_traces_zoom_span(&t0, &win, +1);
        CK(win <= 86400.0);
        win = 0.01;
        bpq_traces_zoom_span(&t0, &win, -1);
        CK(fabs(win - 0.1) < 1e-9);
        bpq_traces_zoom_span(NULL, &win, +1);
    }
    {
        BpqTracesCtx c;
        int vis = 0, first;

        memset(&c, 0, sizeof(c));
        c.n = 6;
        c.dpr = 1.0;
        c.per_screen = 1;
        first = bpq_traces_visible(&c, 540, &vis);
        CK(first == 0 && vis == 1);
        c.scroll_row = 99;
        first = bpq_traces_visible(&c, 540, &vis);
        CK(first == 5 && vis == 1);            /* se acota a la ultima pagina */
        c.per_screen = 3;
        c.scroll_row = 0;
        first = bpq_traces_visible(&c, 540, &vis);
        CK(first == 0 && vis == 3);
        c.scroll_row = 4;
        first = bpq_traces_visible(&c, 540, &vis);
        CK(first == 3 && vis == 3);            /* 6 - 3 */
        c.n = 0;
        first = bpq_traces_visible(&c, 540, &vis);
        CK(first == 0 && vis == 0);
    }

    printf("== manual checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
