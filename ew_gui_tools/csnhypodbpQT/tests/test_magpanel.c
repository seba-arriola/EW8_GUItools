/*
 * test_magpanel.c - Sub-pestana `magnitudes` (fase 6.1): estado de ventanas,
 * filas de la tabla, overrides para csnmags y lienzo (traza procesada + franja).
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>

#include "bpq_capi.h"
#include "bpq_internal.h"     /* bpq_mag_draw (cairo) */

static int g_checks, g_fail;

#define CK(c) do {                                                    \
        g_checks++;                                                    \
        if (!(c)) {                                                    \
            g_fail++;                                                  \
            fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                              \
    } while (0)

static void sta_set(CsnhypodbpStaRow *r, const char *sta, double t, double ddeg,
                    double dkm)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->sta, sizeof(r->sta), "%s", sta);
    r->p.present = 1;
    r->p.origin = 'M';
    r->p.t = t;
    r->dist_deg = ddeg;
    r->dist_km = dkm;
    r->has_data = 1;
}

int main(void)
{
    char err[256] = "";
    BpqMagPanel *p;
    CsnhypodbpStaRow stas[2];
    CsnhypodbpMagRow rows[16];
    CsnhypodbpMagOvrRow ovr[8];
    double t0 = 0.0, t1 = 0.0;
    int n;

    /* --- 1. estado basico: magnitud activa y ventana por defecto ---------- */
    p = bpq_magpanel_new(NULL);           /* defaults de csnmags (sin .d) */
    CK(p != NULL);
    CK(bpq_magpanel_kind(p) == 0);
    bpq_magpanel_set_kind(p, 2);
    CK(bpq_magpanel_kind(p) == 2);
    bpq_magpanel_set_kind(p, 9);              /* fuera de rango: se ignora */
    CK(bpq_magpanel_kind(p) == 2);
    bpq_magpanel_set_kind(p, 1);

    bpq_magpanel_defaults_window(p, 1, &t0, &t1);
    CK(isfinite(t0) && isfinite(t1) && t1 > t0);
    bpq_magpanel_window(p, "PB01", 1, &t0, &t1);   /* sin ajuste = la del .d */
    CK(isfinite(t0) && isfinite(t1) && t1 > t0);

    /* --- 2. ventana ajustada a mano: se recuerda y se marca --------------- */
    CK(bpq_magpanel_set_window(p, "PB01", 1, 5.0, 25.0) == 1);
    CK(bpq_magpanel_touched(p, "PB01", 1) == 1);
    CK(bpq_magpanel_touched(p, "PB01", 0) == 0);   /* por magnitud */
    CK(bpq_magpanel_touched_count(p) == 1);
    bpq_magpanel_window(p, "PB01", 1, &t0, &t1);
    CK(fabs(t0 - 5.0) < 1e-9 && fabs(t1 - 25.0) < 1e-9);
    CK(bpq_magpanel_set_window(NULL, "PB01", 1, 1.0, 2.0) == 0);

    /* --- 3. filas: ancla, ventana, puerta y elegibilidad ------------------ */
    sta_set(&stas[0], "PB01", 1700000010.0, 3.0, 300.0);
    sta_set(&stas[1], "PB02", 1700000020.0, 200.0, 15000.0);

    /* Sin override manda la puerta del .d; con override, la del usuario. */
    CK(bpq_magpanel_gate_set(p, "0.0", "10.0") == 1);
    n = bpq_magpanel_rows(p, stas, 2, NULL, 0, NAN, rows, 16);
    CK(n == 2);
    CK(strcmp(rows[0].sta, "PB01") == 0);
    CK(rows[0].anchor == 'M');
    CK(rows[0].in_range == 1);
    CK(rows[0].eligible == 1);
    CK(rows[0].used == 1);
    CK(rows[0].win_src == 'M');                       /* ajustada a mano */
    CK(fabs(rows[0].t0 - 5.0) < 1e-9 && fabs(rows[0].t1 - 25.0) < 1e-9);
    CK(fabs(rows[0].anchor_t - 1700000010.0) < 1e-6);
    CK(rows[1].in_range == 0);                        /* 200 grados */
    CK(rows[1].eligible == 0);
    CK(rows[1].used == 0);
    CK(rows[1].win_src == 'A');                       /* la del .d con P */

    /* Textos de celda: la magnitud no se inventa si no hay valor. */
    {
        char txt[32];

        csnhypodbp_magrow_mag_text(&rows[0], txt, sizeof(txt));
        CK(txt[0] == '-' || isdigit((unsigned char)txt[0]));
        csnhypodbp_magrow_t0_text(&rows[0], txt, sizeof(txt));
        CK(strcmp(txt, "+5") == 0);
    }

    /* Caja invalida: no se aplica (ni cambia el estado) y no rompe. */
    CK(bpq_magpanel_gate_set(p, "abc", "10.0") == 0);
    CK(bpq_magpanel_gate_set(p, "0.0", "zz") == 0);
    CK(bpq_magpanel_gate_set(p, "10.0", "1.0") == 0);   /* rango invertido */
    bpq_magpanel_gate_reset(p);
    CK(bpq_magpanel_gate_set(p, "0.0", "10.0") == 1);

    /* --- 4. overrides para csnmags (t0/t1 ABSOLUTOS por ancla) ------------ */
    n = bpq_magpanel_ovr(p, stas, 2, 1u << 1, ovr, 8);   /* solo Mwp */
    CK(n == 2);                          /* las DOS estaciones tienen P */
    CK(strcmp(ovr[0].sta, "PB01") == 0);
    CK(ovr[0].kind == 1);
    CK(fabs(ovr[0].t0 - (1700000010.0 + 5.0)) < 1e-6);
    CK(fabs(ovr[0].t1 - (1700000010.0 + 25.0)) < 1e-6);
    /* La mascara 0 = las cuatro: una fila por magnitud y ancla. */
    CK(bpq_magpanel_ovr(p, stas, 2, 0u, ovr, 8) == 8);

    /* --- 5. lienzo: traza procesada + franja y hit-test del handle -------- */
    {
        int32_t *s = (int32_t *)malloc(400 * sizeof(int32_t));
        BpqTrace *tr;
        CsnhypodbpMagCfg def;
        BpqMagCtx mc;
        unsigned char *buf = (unsigned char *)calloc(400 * 220, 4);
        cairo_surface_t *sf;
        cairo_t *cr;
        int hits = 0, x, negras = 0;
        int grab = 0;
        double t_at = 0.0;

        CK(s != NULL && buf != NULL);
        for (long k = 0; s && k < 400; k++)
            s[k] = (int32_t)(8000.0 * sin(2.0 * M_PI * 5.0 * (double)k / 100.0));
        tr = s ? bpq_trace_new(1700000000.0, 100.0, s, 400) : NULL;
        CK(tr != NULL);
        CK(bpq_trace_ew(tr) != NULL);

        csnhypodbp_magcfg_defaults(&def);
        memset(&mc, 0, sizeof(mc));
        mc.tr = tr;
        mc.magcfg = &def;
        mc.kind = 0;
        mc.anchor = 1700000000.0;
        mc.t0 = 1.0;
        mc.t1 = 1.5;
        mc.dpr = 1.0;
        mc.sta = "PB01";
        mc.y_gain = 1.0;

        sf = cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_ARGB32, 400,
                                                 220, 400 * 4);
        cr = cairo_create(sf);
        bpq_mag_draw(cr, 400, 220, &mc);
        cairo_destroy(cr);
        cairo_surface_destroy(sf);

        for (x = 0; x < 400 * 220; x++) {
            const unsigned char *q = buf + x * 4;

            if (q[0] < 245 || q[1] < 245 || q[2] < 245) negras++;
        }
        CK(negras > 200);                 /* dibujo real, no un lienzo vacio */

        /* El handle de la franja se agarra arriba; abajo el canvas es del zoom. */
        for (x = 0; x < 400; x++) {
            int g2 = 0;

            if (bpq_mag_hit(&mc, 400, 220, (double)x, 8.0, &g2, &t_at)) {
                hits++;
                grab = g2;
            }
        }
        CK(hits > 0);
        CK(grab == 3 || grab == 1 || grab == 2);
        CK(bpq_mag_hit(&mc, 400, 220, 200.0, 110.0, &grab, &t_at) == 0);
        CK(bpq_mag_hit(NULL, 400, 220, 200.0, 8.0, &grab, &t_at) == 0);
        /* Con ventana nula no hay franja: el hit-test no inventa geometria. */
        mc.t0 = mc.t1 = NAN;
        CK(bpq_mag_hit(&mc, 400, 220, 200.0, 8.0, &grab, &t_at) == 0);

        bpq_trace_free(tr);
        free(s);
        free(buf);
    }

    /* --- 6. linea de estado ----------------------------------------------- */
    {
        char st[160] = "";

        bpq_magpanel_status(rows, 2, st, sizeof(st));
        CK(strstr(st, "2 estaciones") != NULL);
        CK(strstr(st, "en uso") != NULL);
    }

    bpq_magpanel_free(p);
    (void)err;

    printf("== magpanel checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
