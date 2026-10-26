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
    sta_set(&stas[1], "PB02", 1700000020.0, 20.0, 500.0);

    /* --- 3b. ventana de traza por magnitud (la que pide BpqWave) ---------- */
    {
        double wt0 = 0.0, wsec = 0.0;

        CK(bpq_magpanel_req_window(p, &stas[0], 0, NULL, 0, &wt0, &wsec) == 1);
        CK(fabs(wt0 - (1700000010.0 + 3.0)) < 1e-6);
        CK(fabs(wsec - 47.0) < 1e-6);
        CK(bpq_magpanel_req_window(p, &stas[0], 1, NULL, 0, &wt0, &wsec) == 1);
        CK(fabs(wt0 - (1700000010.0 - 7.0)) < 1e-6);
        CK(fabs(wsec - 107.0) < 1e-6);
        CK(bpq_magpanel_req_window(p, &stas[0], 2, NULL, 0, &wt0, &wsec) == 1);
        CK(fabs(wsec - 42.0) < 1e-6);
        CK(bpq_magpanel_req_window(p, &stas[0], 3, NULL, 0, &wt0, &wsec) == 1);
        CK(fabs(wsec - 412.0) < 1e-6);
        CK(bpq_magpanel_req_window(p, &stas[0], 4, NULL, 0, &wt0, &wsec) == 0);
        {
            CsnhypodbpStaRow empty = stas[0];

            empty.p.present = 0;
            CK(bpq_magpanel_req_window(p, &empty, 0, NULL, 0, &wt0, &wsec) == 0);
        }
        {
            CsnhypodbpMagOvrRow o;

            memset(&o, 0, sizeof(o));
            snprintf(o.sta, sizeof(o.sta), "PB01");
            o.kind = 1;
            o.t0 = 100.0;
            o.t1 = 200.0;
            CK(bpq_magpanel_req_window(p, &stas[0], 1, &o, 1, &wt0, &wsec) == 1);
            CK(fabs(wt0 - 95.0) < 1e-9);
            CK(fabs(wsec - 110.0) < 1e-9);
        }
    }

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

    /* La tabla solo lista lo que pasa la puerta; el resto queda para picking. */
    {
        CsnhypodbpMagRow filt[4];

        CK(bpq_magrows_filter_in_range(rows, 2, filt, 4) == 1);
        CK(strcmp(filt[0].sta, "PB01") == 0);
        CK(bpq_magrows_filter_in_range(rows, 2, filt, 1) == 1);  /* tope */
        CK(bpq_magrows_filter_in_range(NULL, 2, filt, 4) == 0);
        CK(bpq_magrows_filter_in_range(rows, 2, NULL, 4) == 0);
        CK(bpq_magrows_filter_in_range(rows, 0, filt, 4) == 0);

        /* Si el usuario amplia la puerta, la fila oculta reaparece. */
        CK(bpq_magpanel_gate_set(p, "0.0", "30.0") == 1);
        n = bpq_magpanel_rows(p, stas, 2, NULL, 0, NAN, rows, 16);
        CK(n == 2);
        CK(bpq_magrows_filter_in_range(rows, n, filt, 4) == 2);
        CK(bpq_magpanel_gate_set(p, "0.0", "10.0") == 1);
        n = bpq_magpanel_rows(p, stas, 2, NULL, 0, NAN, rows, 16);
        CK(n == 2);
    }

    /* Ancla TEORICA: la ventana del .d existe y la fila entra si Teo esta on. */
    {
        CsnhypodbpStaRow th[1];
        CsnhypodbpMagRow tr;
        CsnhypodbpMagOvrRow tovr[8];

        sta_set(&th[0], "PB03", 1700000030.0, 4.0, 400.0);
        th[0].p.origin = 'T';
        n = bpq_magpanel_rows(p, th, 1, NULL, 0, NAN, &tr, 1);
        CK(n == 1);
        CK(tr.anchor == 'T');
        CK(tr.win_src == 'T');
        CK(isfinite(tr.t0) && isfinite(tr.t1) && tr.t1 > tr.t0);
        CK(tr.used == 1 && tr.eligible == 1);
        CK(bpq_magpanel_ovr(p, th, 1, 0u, tovr, 8) >= 1);
        CK(strcmp(tovr[0].sta, "PB03") == 0);
        /* Teo apagado: la fila se sigue viendo, pero no entra al calculo. */
        bpq_magpanel_set_masks(p, 1, 1, 0);
        n = bpq_magpanel_rows(p, th, 1, NULL, 0, NAN, &tr, 1);
        CK(n == 1);
        CK(tr.used == 0 && tr.eligible == 0);
        bpq_magpanel_set_masks(p, 1, 1, 1);
    }

    /* Categoria = origen de la PICADA: con solo M entran las manuales aunque
     * su ventana sea la del .d, y tocar una ventana no reduce el conjunto a
     * esa estacion (la ventana tocada de un pick A sigue siendo A). */
    {
        CsnhypodbpStaRow man[3];
        int rb[8], nf;

        sta_set(&man[0], "PM01", 1700000010.0, 3.0, 300.0);
        sta_set(&man[1], "PM02", 1700000012.0, 4.0, 400.0);
        sta_set(&man[2], "PM03", 1700000014.0, 5.0, 500.0);
        CK(bpq_magpanel_gate_set(p, "0.0", "10.0") == 1);
        bpq_magpanel_set_masks(p, 0, 1, 0);           /* solo M */
        nf = bpq_magpanel_fetch_rows(p, man, 3, 0u, 10.0, 0, 1, 0, rb, 8);
        CK(nf == 3);                                  /* ninguna ventana tocada */
        CK(bpq_magpanel_set_window(p, "PM01", 0, 5.0, 25.0) == 1);
        nf = bpq_magpanel_fetch_rows(p, man, 3, 0u, 10.0, 0, 1, 0, rb, 8);
        CK(nf == 3);                                  /* tocar una no la vuelve la unica */
        man[0].p.origin = 'A';                        /* la ventana sigue tocada */
        nf = bpq_magpanel_fetch_rows(p, man, 3, 0u, 10.0, 1, 0, 0, rb, 8);
        CK(nf == 1 && rb[0] == 0);                    /* solo A: PM01 */
        nf = bpq_magpanel_fetch_rows(p, man, 3, 0u, 10.0, 0, 1, 0, rb, 8);
        CK(nf == 2 && rb[0] == 1 && rb[1] == 2);      /* solo M: PM02 y PM03 */
        bpq_magpanel_set_masks(p, 1, 1, 1);
    }

    /* La ventana es un offset del ancla: mover la picada la desplaza y un
     * ajuste a mano conserva sus offsets (no se reescribe ni se pierde). */
    {
        CsnhypodbpStaRow mv = stas[0], md = stas[1];
        CsnhypodbpMagOvrRow ov[8];
        double wa0 = 0.0, wsec0 = 0.0, wa1 = 0.0, wsec1 = 0.0;
        double d0 = 0.0, d1 = 0.0, e0 = 0.0, e1 = 0.0;
        int no;

        /* PB01/Mwp tiene ventana tocada (5..25). */
        bpq_magpanel_window(p, "PB01", 1, &d0, &d1);
        CK(fabs(d0 - 5.0) < 1e-9 && fabs(d1 - 25.0) < 1e-9);
        CK(bpq_magpanel_req_window(p, &mv, 1, NULL, 0, &wa0, &wsec0) == 1);
        no = bpq_magpanel_ovr(p, &mv, 1, 1u << 1, ov, 8);
        CK(no == 1 && fabs(ov[0].t0 - (mv.p.t + 5.0)) < 1e-9);
        mv.p.t += 10.0;
        bpq_magpanel_window(p, "PB01", 1, &e0, &e1);
        CK(fabs(e0 - d0) < 1e-9 && fabs(e1 - d1) < 1e-9);
        CK(bpq_magpanel_req_window(p, &mv, 1, NULL, 0, &wa1, &wsec1) == 1);
        CK(fabs(wa1 - (wa0 + 10.0)) < 1e-9 && fabs(wsec1 - wsec0) < 1e-9);
        no = bpq_magpanel_ovr(p, &mv, 1, 1u << 1, ov, 8);
        CK(no == 1 && fabs(ov[0].t0 - (mv.p.t + 5.0)) < 1e-9);

        /* PB02/Mwp no tiene ajuste: la ventana del .d sigue al ancla. */
        bpq_magpanel_window(p, "PB02", 1, &d0, &d1);
        CK(bpq_magpanel_req_window(p, &md, 1, NULL, 0, &wa0, &wsec0) == 1);
        md.p.t += 10.0;
        bpq_magpanel_window(p, "PB02", 1, &e0, &e1);
        CK(fabs(e0 - d0) < 1e-9 && fabs(e1 - d1) < 1e-9);
        CK(bpq_magpanel_req_window(p, &md, 1, NULL, 0, &wa1, &wsec1) == 1);
        CK(fabs(wa1 - (wa0 + 10.0)) < 1e-9 && fabs(wsec1 - wsec0) < 1e-9);
    }

    /* `calculate`: copia la candidata y reemplaza sus picks por las P
     * efectivas de la tabla (M/A/Teo). Sin esto csnmags no las ve. */
    {
        CsnhypodbpSolution base, out;
        CsnhypodbpStaRow cr[3];

        memset(&base, 0, sizeof(base));
        base.otime = 1700000000.0;
        base.lat = -30.0;
        base.lon = -70.0;
        base.npicks = 1;
        snprintf(base.picks[0].sta, sizeof(base.picks[0].sta), "PB01");
        base.picks[0].phase = 'P';
        base.picks[0].t_pick = 1700000010.0;
        base.picks[0].manual = 1;

        sta_set(&cr[0], "PB01", 1700000010.0, 3.0, 300.0);   /* M */
        sta_set(&cr[1], "PB02", 1700000020.0, 4.0, 400.0);
        cr[1].p.origin = 'A';
        sta_set(&cr[2], "PB03", 1700000030.0, 5.0, 500.0);
        cr[2].p.origin = 'T';

        CK(bpq_magpanel_calc_solution(cr, 3, 1, 1, 1, &base, &out) == 3);
        CK(fabs(out.otime - base.otime) < 1e-9);
        CK(out.npicks == 3 && out.nph == 3);
        CK(out.picks[0].phase == 'P' && out.picks[1].phase == 'P' &&
           out.picks[2].phase == 'P');
        CK(bpq_magpanel_calc_solution(cr, 3, 1, 0, 0, &base, &out) == 1);
        CK(out.npicks == 1 && strcmp(out.picks[0].sta, "PB02") == 0);
        CK(bpq_magpanel_calc_solution(cr, 3, 0, 0, 1, &base, &out) == 1);
        CK(out.npicks == 1 && strcmp(out.picks[0].sta, "PB03") == 0);
        CK(bpq_magpanel_calc_solution(cr, 3, 0, 0, 0, &base, &out) == 0);
        CK(bpq_magpanel_calc_solution(NULL, 3, 1, 1, 1, &base, &out) == -1);
        CK(bpq_magpanel_calc_solution(cr, 0, 1, 1, 1, &base, &out) == -1);
        CK(bpq_magpanel_calc_solution(cr, 3, 1, 1, 1, NULL, &out) == -1);
    }

    /* --- 3b. seleccion de estaciones que se piden al tanque --------------- */
    {
        BpqMagPanel *fp = bpq_magpanel_new(NULL);
        CsnhypodbpStaRow fr[30];
        int sel[64];
        int i, ns;

        CK(fp != NULL);
        for (i = 0; i < 30; i++) {
            char name[16];

            snprintf(name, sizeof(name), "S%02d", i);
            if (i < 24)
                sta_set(&fr[i], name, 1700000000.0 + i, 20.0, 2200.0);
            else
                sta_set(&fr[i], name, 1700000000.0 + i, 2.0, 220.0);
        }
        /* ML (0-8 deg): solo las 6 de 2 grados, aunque las 24 primeras no
         * entren. Esto es lo que evita pedir 24 estaciones inservibles. */
        ns = bpq_magpanel_fetch_rows(fp, fr, 30, 1u << 0, 30.0, 1, 1, 1,
                                     sel, 64);
        CK(ns == 6);
        for (i = 0; i < ns; i++) CK(sel[i] >= 24);
        /* Mwp (3-90 deg): las 24 de 20 grados. */
        ns = bpq_magpanel_fetch_rows(fp, fr, 30, 1u << 1, 30.0, 1, 1, 1,
                                     sel, 64);
        CK(ns == 24);
        /* Las cuatro: union = 30. */
        ns = bpq_magpanel_fetch_rows(fp, fr, 30, 0u, 30.0, 1, 1, 1, sel, 64);
        CK(ns == 30);
        /* Con el tope justo, primero las P reales: 24 Teo cercanas no pueden
         * desplazar a las 6 M/A que el operador acaba de picar. */
        for (i = 0; i < 24; i++) fr[i].p.origin = 'T';
        for (i = 24; i < 30; i++) fr[i].p.origin = 'A';
        ns = bpq_magpanel_fetch_rows(fp, fr, 30, 0u, 30.0, 1, 1, 1, sel, 6);
        CK(ns == 6);
        for (i = 0; i < 6; i++) CK(sel[i] >= 24);
        /* Sin Teo permitidas, solo las 6 reales. */
        ns = bpq_magpanel_fetch_rows(fp, fr, 30, 0u, 30.0, 1, 1, 0, sel, 64);
        CK(ns == 6);
        for (i = 0; i < 6; i++) CK(sel[i] >= 24);
        /* Ningun origen permitido: no se pide nada. */
        ns = bpq_magpanel_fetch_rows(fp, fr, 30, 1u << 0, 30.0, 0, 0, 0,
                                     sel, 64);
        CK(ns == 0);
        bpq_magpanel_free(fp);
    }

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

    /* --- 5. lienzo: una banda por canal + franja y hit-test ---------------- */
    {
        int32_t *s = (int32_t *)malloc(400 * sizeof(int32_t));
        BpqTrace *tr;
        CsnhypodbpMagCfg def;
        BpqMagCtx mc;
        BpqMagTrace mts[3];
        unsigned char *buf = (unsigned char *)calloc(400 * 220, 4);
        unsigned char *buf2 = (unsigned char *)calloc(400 * 220, 4);
        cairo_surface_t *sf;
        cairo_t *cr;
        int hits = 0, x, y, negras = 0;
        int grab = 0;
        double t_at = 0.0;

        CK(s != NULL && buf != NULL && buf2 != NULL);
        for (long k = 0; s && k < 400; k++)
            s[k] = (int32_t)(8000.0 * sin(2.0 * M_PI * 5.0 * (double)k / 100.0));
        tr = s ? bpq_trace_new(1700000000.0, 100.0, s, 400) : NULL;
        CK(tr != NULL);
        CK(bpq_trace_ew(tr) != NULL);

        csnhypodbp_magcfg_defaults(&def);
        memset(&mc, 0, sizeof(mc));
        memset(mts, 0, sizeof(mts));
        mts[0].tr = tr; mts[0].chan = "HHZ"; mts[0].dist_km = 32.0;
        mts[1].tr = tr; mts[1].chan = "HHN"; mts[1].dist_km = 32.0;
        mts[2].tr = tr; mts[2].chan = "HHE"; mts[2].dist_km = 32.0;
        {
            char lbl[16];

            CK(bpq_mag_band_label(&mc, 0, lbl, sizeof(lbl)) == 0);
            mc.trs = mts;
            mc.ntr = 3;
            CK(bpq_mag_band_label(&mc, 0, lbl, sizeof(lbl)) == 1);
            CK(strcmp(lbl, "HHZ") == 0);
            CK(bpq_mag_band_label(&mc, 2, lbl, sizeof(lbl)) == 1);
            CK(strcmp(lbl, "HHE") == 0);
            CK(bpq_mag_band_label(&mc, 3, lbl, sizeof(lbl)) == 0);
            CK(bpq_mag_band_label(NULL, 0, lbl, sizeof(lbl)) == 0);
            CK(fabs(bpq_mag_left_margin(1.0) - 72.0) < 1e-9);
        }
        mc.trs = mts;
        mc.ntr = 3;
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

        /* Tinta OSCURA (la traza) en cada banda: las tres se pintan. */
        {
            const int ya[3] = { 36, 95, 154 };
            const int yb[3] = { 94, 152, 211 };
            int dark[3] = { 0, 0, 0 }, b;

            for (b = 0; b < 3; b++)
                for (y = ya[b]; y <= yb[b]; y++)
                    for (x = 72; x < 400; x++) {
                        const unsigned char *q = buf + ((size_t)y * 400 + x) * 4;

                        if (q[0] < 70 && q[1] < 70 && q[2] < 70) dark[b]++;
                    }
            CK(dark[0] > 20 && dark[1] > 20 && dark[2] > 20);
        }

        /* Sin ancla no hay traza: solo el mensaje (gris, no tinta oscura). */
        {
            BpqMagCtx ma = mc;
            int dark = 0;

            ma.anchor = 0.0;
            sf = cairo_image_surface_create_for_data(buf2, CAIRO_FORMAT_ARGB32,
                                                     400, 220, 400 * 4);
            cr = cairo_create(sf);
            bpq_mag_draw(cr, 400, 220, &ma);
            cairo_destroy(cr);
            cairo_surface_destroy(sf);
            for (x = 0; x < 400 * 220; x++) {
                const unsigned char *q = buf2 + x * 4;

                if (q[0] < 70 && q[1] < 70 && q[2] < 70) dark++;
            }
            CK(dark == 0);
            CK(bpq_mag_hit(&ma, 400, 220, 200.0, 8.0, &grab, &t_at) == 0);
        }

        /* Un canal sin traza deja su banda sin tinta, sin afectar a las otras. */
        {
            const int ya[3] = { 36, 95, 154 };
            const int yb[3] = { 94, 152, 211 };
            int dark[3] = { 0, 0, 0 }, b;

            mts[1].tr = NULL;
            memset(buf, 0, 400 * 220 * 4);
            sf = cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_ARGB32,
                                                     400, 220, 400 * 4);
            cr = cairo_create(sf);
            bpq_mag_draw(cr, 400, 220, &mc);
            cairo_destroy(cr);
            cairo_surface_destroy(sf);
            for (b = 0; b < 3; b++)
                for (y = ya[b]; y <= yb[b]; y++)
                    for (x = 72; x < 400; x++) {
                        const unsigned char *q = buf + ((size_t)y * 400 + x) * 4;

                        if (q[0] < 70 && q[1] < 70 && q[2] < 70) dark[b]++;
                    }
            CK(dark[0] > 20 && dark[1] == 0 && dark[2] > 20);
            mts[1].tr = tr;
        }

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

        /* Cursor: borde vertical -> EDGE, banda superior -> HANDLE y el resto
         * del canvas sin cursor especial. */
        {
            int x0 = -1, x1 = -1;

            mc.t0 = 1.0;                 /* el caso anterior dejo la franja NAN */
            mc.t1 = 1.5;
            for (x = 0; x < 400; x++) {
                if (bpq_mag_hover(&mc, 400, 220, (double)x, 110.0) ==
                    BPQ_MAG_HOVER_EDGE) {
                    if (x0 < 0) x0 = x;
                    x1 = x;
                }
            }
            CK(x0 >= 0 && x1 > x0);
            CK(bpq_mag_hover(&mc, 400, 220, (double)x0, 110.0) ==
               BPQ_MAG_HOVER_EDGE);
            CK(bpq_mag_hover(&mc, 400, 220, (double)x1, 110.0) ==
               BPQ_MAG_HOVER_EDGE);
            CK(bpq_mag_hover(&mc, 400, 220, (double)x0, 8.0) ==
               BPQ_MAG_HOVER_HANDLE);
            CK(bpq_mag_hover(&mc, 400, 220, (double)((x0 + x1) / 2), 110.0) ==
               BPQ_MAG_HOVER_NONE);
            CK(bpq_mag_hover(&mc, 400, 220, 2.0, 110.0) ==
               BPQ_MAG_HOVER_NONE);
            CK(bpq_mag_hover(NULL, 400, 220, 200.0, 8.0) ==
               BPQ_MAG_HOVER_NONE);
            {
                BpqMagCtx mz = mc;

                mz.anchor = 0.0;
                CK(bpq_mag_hover(&mz, 400, 220, 200.0, 8.0) ==
                   BPQ_MAG_HOVER_NONE);
            }
        }

        bpq_trace_free(tr);
        free(s);
        free(buf);
        free(buf2);
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
