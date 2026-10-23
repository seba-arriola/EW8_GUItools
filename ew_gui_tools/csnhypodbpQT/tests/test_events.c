/*
 * test_events.c - Store de eventos: agrupacion, deduplicacion, preferida,
 * magnitudes (antes y despues del ARC), celdas y claves de orden.
 *
 * Construye las soluciones a mano (sin pasar por un ARC) para fijar los valores
 * esperados; el parseo del ARC ya lo cubre test_parse_solution del modulo GTK y
 * la agrupacion la implementa el MISMO nucleo puro (csnhypodbp_group_*).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                       \
    g_checks++;                                                             \
    if (!(cond)) {                                                          \
        g_fail++;                                                           \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                       \
} while (0)

static void ck_str(const char *got, const char *want, int line)
{
    g_checks++;
    if (!got || strcmp(got, want) != 0) {
        g_fail++;
        fprintf(stderr, "FALLA %s:%d  = <%s>, esperaba <%s>\n",
                __FILE__, line, got ? got : "(null)", want);
    }
}

static void ck_cell(const BpqEvent *ev, int col, const char *want, int line)
{
    char buf[64];
    bpq_event_cell(ev, col, buf, sizeof(buf));
    ck_str(buf, want, line);
}

/* Solucion sintetica con `n` picadas en las estaciones de `base`. */
static void make_sol(CsnhypodbpSolution *s, int qid, int mod, unsigned ver,
                     int nph, double rms, int manual, char sta_base)
{
    int i;
    memset(s, 0, sizeof(*s));
    s->qid = qid;
    s->mod = mod;
    s->version = ver;
    s->otime = 1700000000.0 + (double)(qid % 1000);
    s->lat = -20.123;
    s->lon = -70.456;
    s->depth = 33.4;
    s->rms = rms;
    s->nph = nph;
    s->gap = 180;
    s->manual = manual;
    s->confirmed = 0;
    s->npicks = nph;
    for (i = 0; i < nph && i < CSNHYPODBP_MAX_PICKS; i++) {
        snprintf(s->picks[i].sta, sizeof(s->picks[i].sta), "%c%02d", sta_base, i);
        snprintf(s->picks[i].chan, sizeof(s->picks[i].chan), "HHZ");
        s->picks[i].phase = 'P';
        s->picks[i].t_pick = 1700000000.0 + 10.0 * i;
        s->picks[i].weight = 0.5;
        s->picks[i].residual = NAN;
    }
}

static BpqConfig make_cfg(void)
{
    BpqConfig c;
    memset(&c, 0, sizeof(c));
    c.group_pick_tol_s = 0.1;
    c.group_min_match_picks = 3;
    c.group_max_solutions = 100;
    snprintf(c.modlabels[0].label, sizeof(c.modlabels[0].label), "%s", "csnloc");
    c.modlabels[0].id = 162;
    snprintf(c.modlabels[1].label, sizeof(c.modlabels[1].label), "%s", "hyp2000");
    c.modlabels[1].id = 163;
    c.n_modlabels = 2;
    return c;
}

int main(void)
{
    BpqConfig cfg = make_cfg();
    BpqEvents *e = bpq_events_new(&cfg);
    CsnhypodbpSolution s;
    BpqMagMsg mag;

    printf("== test_events ==\n");
    CK(e != NULL);
    if (!e) return 1;

    CK(bpq_events_count(e) == 0);
    CK(bpq_event_ncells() == 15);
    CK(bpq_origin_ncells() == 15);
    ck_str(bpq_event_header(0), "Date", __LINE__);
    ck_str(bpq_event_header(7), "ID", __LINE__);
    ck_str(bpq_event_header(14), "Est.", __LINE__);
    ck_str(bpq_origin_header(3), "Created", __LINE__);
    ck_str(bpq_origin_header(10), "ID", __LINE__);

    /* --- 1. dos soluciones del MISMO qid (162 crudo y 163 refinado) --- */
    make_sol(&s, 100, 162, 1, 4, 0.5, 0, 'A');
    CK(bpq_events_feed_arc(e, &s) == 0);
    make_sol(&s, 100, 163, 1, 12, 0.2, 0, 'A');
    CK(bpq_events_feed_arc(e, &s) == 0);

    CK(bpq_events_count(e) == 1);
    {
        const BpqEvent *ev = bpq_events_at(e, 0);
        CK(ev != NULL);
        CK(bpq_event_nsols(ev) == 2);
        /* Preferida: mas fases y menor RMS -> la de 163 (indice 1). */
        CK(bpq_event_preferred(ev) == 1);

        ck_cell(ev, 7, "0000000100", __LINE__);
        ck_cell(ev, 12, "hyp2000", __LINE__);
        ck_cell(ev, 13, "1", __LINE__);
        ck_cell(ev, 14, "A", __LINE__);
        ck_cell(ev, 6, "12", __LINE__);
        ck_cell(ev, 3, "33", __LINE__);          /* %.0f */
        ck_cell(ev, 4, "0.2", __LINE__);         /* %.1f */
        ck_cell(ev, 1, "20.12S", __LINE__);
        ck_cell(ev, 2, "70.46W", __LINE__);
        {
            char buf[64];
            bpq_event_cell(ev, 0, buf, sizeof(buf));
            CK(strlen(buf) == 19);               /* YYYY-MM-DD HH:MM:SS */
            CK(buf[4] == '-' && buf[10] == ' ' && buf[13] == ':');
        }
        /* Sin magnitudes todavia: "-". */
        ck_cell(ev, 8, "-", __LINE__);
        ck_cell(ev, 11, "-", __LINE__);

        /* Origenes: la 162 se llama csnloc y la 163 hyp2000. */
        {
            char buf[64];
            bpq_origin_cell(ev, 0, 0, buf, sizeof(buf));
            ck_str(buf, "csnloc", __LINE__);
            bpq_origin_cell(ev, 1, 0, buf, sizeof(buf));
            ck_str(buf, "hyp2000", __LINE__);
            bpq_origin_cell(ev, 1, 2, buf, sizeof(buf));
            ck_str(buf, "A", __LINE__);
            bpq_origin_cell(ev, 0, 3, buf, sizeof(buf));
            CK(strlen(buf) == 19);               /* Created */
            bpq_origin_cell(ev, 0, 10, buf, sizeof(buf));
            ck_str(buf, "0000000100", __LINE__);
        }

        /* Claves de orden. */
        CK(bpq_event_sort_kind(0) == 1);         /* Date */
        CK(bpq_event_sort_kind(12) == 0);        /* Mod: texto */
        CK(bpq_event_sort_num(ev, 7) == 100.0);
        CK(bpq_event_sort_num(ev, 1) < -20.0);   /* Lat: valor crudo */
        CK(fabs(bpq_event_sort_num(ev, 3) - 33.4) < 1e-9);
    }

    /* --- 2. deduplicacion por (qid, mod, version) --- */
    make_sol(&s, 100, 163, 1, 13, 0.15, 0, 'A');
    CK(bpq_events_feed_arc(e, &s) == 0);
    CK(bpq_events_count(e) == 1);
    {
        const BpqEvent *ev = bpq_events_at(e, 0);
        CK(bpq_event_nsols(ev) == 2);            /* no se duplico */
        ck_cell(ev, 6, "13", __LINE__);          /* se actualizo */
        ck_cell(ev, 4, "0.1", __LINE__);         /* %.1f de 0.15 -> 0.1 */
    }

    /* --- 3. evento distinto -> grupo nuevo --- */
    make_sol(&s, 200, 162, 1, 5, 0.4, 0, 'X');
    CK(bpq_events_feed_arc(e, &s) == 0);
    CK(bpq_events_count(e) == 2);

    /* --- 4. qid distinto pero firma compatible -> MISMO grupo --- */
    make_sol(&s, 300, 163, 1, 5, 0.3, 0, 'X');
    CK(bpq_events_feed_arc(e, &s) == 0);
    CK(bpq_events_count(e) == 2);                /* agrupo por firma, no creo otro */
    {
        const BpqEvent *ev = bpq_events_at(e, 1);
        CK(bpq_event_nsols(ev) == 2);
    }

    /* --- 5. magnitud DESPUES del ARC --- */
    memset(&mag, 0, sizeof(mag));
    mag.qid = 100; mag.mod = 163; mag.value = 3.4; mag.nsta = 5;
    snprintf(mag.type, sizeof(mag.type), "%s", "ML");
    CK(bpq_events_feed_mag(e, &mag) == 0);
    {
        const BpqEvent *ev = bpq_events_at(e, 0);
        char buf[64];
        ck_cell(ev, 8, "3.4-5", __LINE__);       /* Ml */
        ck_cell(ev, 9, "-", __LINE__);           /* Mwp sin dato */
        CK(fabs(bpq_event_sort_num(ev, 8) - 3.4) < 1e-9);
        /* La magnitud llega a la solucion 163, no a la 162. */
        bpq_event_sol_cell(ev, 0, 8, buf, sizeof(buf));
        ck_str(buf, "-", __LINE__);
        bpq_event_sol_cell(ev, 1, 8, buf, sizeof(buf));
        ck_str(buf, "3.4-5", __LINE__);
    }

    /* --- 6. magnitud ANTES del ARC (queda pendiente y se aplica) --- */
    memset(&mag, 0, sizeof(mag));
    mag.qid = 400; mag.mod = 162; mag.value = 4.1; mag.nsta = 7;
    snprintf(mag.type, sizeof(mag.type), "%s", "Mb");
    CK(bpq_events_feed_mag(e, &mag) == 0);
    CK(bpq_events_count(e) == 2);                /* solo pendiente: no crea grupo */
    make_sol(&s, 400, 162, 1, 6, 0.3, 0, 'Q');
    CK(bpq_events_feed_arc(e, &s) == 0);
    CK(bpq_events_count(e) == 3);
    {
        const BpqEvent *ev = bpq_events_at(e, 2);
        ck_cell(ev, 10, "4.1-7", __LINE__);      /* Mb */
    }

    /* --- 7. la manual gana a la automatica aunque tenga menos fases --- */
    make_sol(&s, 500, 162, 1, 20, 0.1, 0, 'M');
    CK(bpq_events_feed_arc(e, &s) == 0);
    make_sol(&s, 500, 162, 2, 3, 0.9, 1, 'M');   /* manual, peor en todo */
    CK(bpq_events_feed_arc(e, &s) == 0);
    {
        const BpqEvent *ev = bpq_events_at(e, 3);
        CK(bpq_event_nsols(ev) == 2);
        CK(bpq_event_preferred(ev) == 1);        /* la manual */
        ck_cell(ev, 14, "M", __LINE__);
        ck_cell(ev, 13, "2", __LINE__);
    }

    /* --- 8. argumentos invalidos --- */
    CK(bpq_events_feed_arc(NULL, &s) == -1);
    CK(bpq_events_feed_arc(e, NULL) == -1);
    CK(bpq_events_feed_mag(NULL, &mag) == -1);
    CK(bpq_events_feed_mag(e, NULL) == -1);
    CK(bpq_events_at(e, -1) == NULL);
    CK(bpq_events_at(e, 99) == NULL);
    CK(bpq_event_nsols(NULL) == 0);
    CK(bpq_event_preferred(NULL) == -1);
    CK(isfinite(bpq_event_sort_num(NULL, 0)) == 0);

    bpq_events_free(e);
    printf("== events checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
