/*
 * test_wave.c - Capa de ondas (fase 3).
 *
 * Verifica sin GUI:
 *   - bpq_trace2_decode: paquetes TRACE2 i4 e i2, grilla desplazada, recorte por
 *     capacidad, basura/truncado y paquete invalido;
 *   - contrato de bpq_wave_fetch: sin store ni SDS devuelve NULL con motivo, no
 *     peta con argumentos invalidos, y un store vacio tampoco rompe;
 *   - accesores y bpq_wave_probe (formato de la linea de diagnostico).
 *
 * No necesita EarthWorm ni red: el unico camino con datos es el decode.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpq_capi.h"
#include "ewgui/stalta.h"
#include "trace_buf.h"

static int g_fail = 0;
static int g_checks = 0;

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

/* Escribe un paquete TRACE2 con `n` muestras (i4 o i2) y devuelve los bytes. */
static long pack(char *dst, const char *sta, const char *chan, double start,
                 double rate, int n, const double *vals, int i2)
{
    TRACE2_HEADER *h = (TRACE2_HEADER *)dst;
    const long off = (long)sizeof(*h);
    int i;

    memset(dst, 0, (size_t)(off + (long)n * 4));
    h->pinno = 1;
    h->nsamp = n;
    h->starttime = start;
    h->endtime = (n > 0) ? start + (n - 1) / rate : start;
    h->samprate = rate;
    snprintf(h->sta, sizeof(h->sta), "%s", sta);
    snprintf(h->chan, sizeof(h->chan), "%s", chan);
    h->version[0] = '2';
    h->version[1] = '0';
    snprintf(h->datatype, sizeof(h->datatype), "%s", i2 ? "i2" : "i4");

    for (i = 0; i < n; i++) {
        if (i2)
            ((int16_t *)(dst + off))[i] = (int16_t)vals[i];
        else
            ((int32_t *)(dst + off))[i] = (int32_t)vals[i];
    }
    return off + (long)n * (i2 ? 2 : 4);
}

int main(void)
{
    char buf[512];
    int32_t out[64];
    double rate, lo = 0.0, hi = 0.0;
    double v1[4] = { 10, 20, 30, 40 };
    double v2[4] = { -1, -2, -3, -4 };
    long len = 0, n;

    printf("== test_wave ==\n");

    /* --- 1. dos paquetes contiguos, uno i4 y otro i2 --- */
    len += pack(buf + len, "ANTO", "HHZ", 1000.0, 10.0, 4, v1, 0);
    len += pack(buf + len, "ANTO", "HHZ", 1000.4, 10.0, 4, v2, 1);

    memset(out, 0x7f, sizeof(out));
    rate = 0.0;
    n = bpq_trace2_decode(buf, len, 1000.0, &rate, out, 64);
    CK(n == 8);
    CK(fabs(rate - 10.0) < 1e-12);
    CK(out[0] == 10 && out[3] == 40);
    CK(out[4] == -1 && out[7] == -4);

    /* --- 2. grilla desplazada: las muestras fuera de la ventana se descartan --- */
    rate = 0.0;
    n = bpq_trace2_decode(buf, len, 1000.2, &rate, out, 64);
    CK(n == 6);
    CK(out[0] == 30);       /* 1000.2 */
    CK(out[1] == 40);       /* 1000.3 */
    CK(out[2] == -1);       /* 1000.4, segundo paquete */
    CK(out[5] == -4);

    /* --- 3. capacidad menor que los datos: recorta sin salirse --- */
    rate = 0.0;
    n = bpq_trace2_decode(buf, len, 1000.0, &rate, out, 5);
    CK(n == 5);
    CK(out[4] == -1);

    /* --- 4. basura, truncado y paquete invalido --- */
    rate = 0.0;
    CK(bpq_trace2_decode(buf, 10, 1000.0, &rate, out, 64) == 0);
    CK(bpq_trace2_decode(NULL, len, 1000.0, &rate, out, 64) == 0);
    CK(bpq_trace2_decode(buf, len, 1000.0, &rate, NULL, 64) == 0);
    CK(bpq_trace2_decode(buf, len, 1000.0, &rate, out, 0) == 0);
    {
        TRACE2_HEADER *h = (TRACE2_HEADER *)buf;
        const int ns = h->nsamp;
        h->nsamp = -1;
        rate = 0.0;
        CK(bpq_trace2_decode(buf, len, 1000.0, &rate, out, 64) == 0);
        h->nsamp = ns;
        h->samprate = 0.0;
        rate = 0.0;
        CK(bpq_trace2_decode(buf, len, 1000.0, &rate, out, 64) == 0);
        h->samprate = 10.0;
    }

    /* --- 5. contrato de fetch sin store ni SDS --- */
    {
        BpqConfig cfg;
        BpqWave *w;
        BpqTrace *t;
        char err[256] = "";

        memset(&cfg, 0, sizeof(cfg));
        w = bpq_wave_open(&cfg, err, sizeof(err));
        CK(w != NULL);
        CK(err[0] == '\0');

        t = bpq_wave_fetch(w, &cfg, "ANTO", "CX", "HHZ", "--", 1000.0, 10.0,
                           err, sizeof(err));
        CK(t == NULL);
        CK(strstr(err, "sin datos") != NULL);

        CK(bpq_wave_fetch(NULL, &cfg, "", "CX", "HHZ", "--", 1000.0, 10.0,
                          err, sizeof(err)) == NULL);
        CK(bpq_wave_fetch(w, NULL, "ANTO", "CX", "HHZ", "--", 1000.0, 10.0,
                          err, sizeof(err)) == NULL);
        CK(bpq_wave_fetch(w, &cfg, "ANTO", "CX", "HHZ", "--", 1000.0, 0.0,
                          err, sizeof(err)) == NULL);
        bpq_wave_close(w);

        CK(bpq_trace_n(NULL) == 0);
        CK(bpq_trace_rate(NULL) == 0.0);
        CK(bpq_trace_t0(NULL) == 0.0);
        CK(bpq_trace_range(NULL, &lo, &hi) == 0);
        bpq_trace_free(NULL);
        bpq_wave_close(NULL);

        /* Store abierto sobre un directorio que no existe: sin datos y sin peta. */
        memset(&cfg, 0, sizeof(cfg));
        snprintf(cfg.store_root, sizeof(cfg.store_root), "%s%s",
                 "/tmp/bpq_test_store_", "vacio");
        w = bpq_wave_open(&cfg, err, sizeof(err));
        CK(w != NULL);
        t = bpq_wave_fetch(w, &cfg, "ANTO", "CX", "HHZ", "--", 1000.0, 10.0,
                           err, sizeof(err));
        CK(t == NULL);
        CK(strstr(err, "sin datos") != NULL);
        bpq_wave_close(w);
    }

    /* --- 6. probe: linea de diagnostico y validacion del spec --- */
    {
        BpqConfig cfg;
        BpqWave *w;
        char err[256] = "";
        char out[256] = "";

        memset(&cfg, 0, sizeof(cfg));
        w = bpq_wave_open(&cfg, err, sizeof(err));
        CK(bpq_wave_probe(w, &cfg, "ANTO,CX,HHZ,--,1000,10", out, sizeof(out)) == -1);
        CK(strstr(out, "wave ANTO.HHZ.CX.--") != NULL);
        CK(bpq_wave_probe(w, &cfg, "faltan,campos", out, sizeof(out)) == -1);
        CK(strstr(out, "6 campos") != NULL);
        CK(bpq_wave_probe(w, &cfg, NULL, out, sizeof(out)) == -1);
        bpq_wave_close(w);
    }

    /* --- 7. filtro de vista: demean y paso alto --- */
    {
        int32_t *s = (int32_t *)malloc(3000 * sizeof(int32_t));
        BpqTrace *tf;
        const int32_t *f = NULL;
        long nf = 0, k;
        double mean = 0.0, amp_raw = 0.0, amp_f = 0.0;

        CK(s != NULL);
        if (s) {
            /* DC 100000 + seno de 1 Hz (fs = 100 Hz) */
            for (k = 0; k < 3000; k++)
                s[k] = 100000 + (int32_t)(50.0 * sin(2.0 * M_PI * 1.0 * k / 100.0));
            tf = bpq_trace_new(1000.0, 100.0, s, 3000);
            CK(tf != NULL);

            CK(bpq_trace_filter(tf, 0, 0.0, 0.0, 0, 1) == 0);   /* solo demean */
            f = bpq_trace_filtered(tf, &nf);
            CK(f != NULL && nf == 3000);
            if (f) {
                for (k = 0; k < nf; k++) mean += (double)f[k];
                mean /= (double)nf;
                CK(fabs(mean) < 1.0);            /* el DC se fue */
            }

            /* HP en 4 Hz, muy por encima del seno de 1 Hz: debe atenuarlo. */
            amp_raw = 50.0;
            CK(bpq_trace_filter(tf, 1, 4.0, 0.0, 2, 1) == 0);
            f = bpq_trace_filtered(tf, &nf);
            if (f) {
                for (k = 0; k < nf; k++)
                    if (fabs((double)f[k]) > amp_f) amp_f = fabs((double)f[k]);
            }
            CK(amp_f < 0.4 * amp_raw);

            CK(bpq_trace_filter(NULL, 0, 0.0, 0.0, 0, 1) == -1);
            CK(bpq_trace_filtered(NULL, &nf) == NULL);
            bpq_trace_free(tf);
            free(s);
        }
    }

    /* --- 8. STA/LTA: curva CF y disparo estimado (no crea picks) --- */
    {
        int32_t *s = (int32_t *)malloc(2000 * sizeof(int32_t));
        BpqTrace *ts;
        BpqStaLta *sl;
        long k, m = 0;

        CK(s != NULL);
        if (s) {
            for (k = 0; k < 2000; k++) s[k] = (k < 1000) ? 0 : 5000;  /* escalon */
            ts = bpq_trace_new(1000.0, 100.0, s, 2000);
            CK(ts != NULL);

            sl = bpq_stalta_new(ts, 0.5, 5.0, 3.0, 1.5, -0.25);
            CK(sl != NULL);
            CK(bpq_stalta_cf(sl, &m) != NULL);
            CK(m == 2000);
            CK(bpq_stalta_peak(sl) > 3.0);
            {
                const double tt = bpq_stalta_trigger_t(sl);
                /* onset en 1010 s; el cruce llega despues y se corrige -0.25 */
                CK(tt > 1005.0 && tt < 1030.0);
            }
            bpq_stalta_free(sl);

            /* parametros invalidos y NULL */
            CK(bpq_stalta_new(ts, 0.0, 5.0, 3.0, 1.5, 0.0) == NULL);
            CK(bpq_stalta_new(ts, 5.0, 1.0, 3.0, 1.5, 0.0) == NULL);
            bpq_stalta_free(NULL);
            CK(bpq_stalta_cf(NULL, &m) == NULL);
            CK(bpq_stalta_trigger_t(NULL) == 0.0);
            CK(bpq_stalta_peak(NULL) == 0.0);
            bpq_trace_free(ts);
            free(s);
        }
    }

    /* --- 9. tank de magnitudes (fase 6): muestras crudas + SCNL ---------- */
    {
        int32_t *s = (int32_t *)malloc(200 * sizeof(int32_t));
        BpqTrace *t;
        CsnhypodbpTankTrace tk;

        CK(s != NULL);
        if (s) {
            for (long k = 0; k < 200; k++) s[k] = (int32_t)(k * 3 - 100);
            t = bpq_trace_new(1700000000.0, 100.0, s, 200);
            CK(t != NULL);
            CK(bpq_trace_tank(t, "PB01", "C", "HHZ", "--", &tk) == 1);
            CK(tk.nsamp == 200);
            CK(fabs(tk.rate - 100.0) < 1e-9);
            CK(fabs(tk.oldest - 1700000000.0) < 1e-9);
            CK(strcmp(tk.sta, "PB01") == 0 && strcmp(tk.chan, "HHZ") == 0);
            CK(strcmp(tk.net, "C") == 0 && strcmp(tk.loc, "--") == 0);
            /* Apunta al buffer CRUDO (lo que consume csnmags). */
            CK(tk.samples != NULL && tk.samples[5] == s[5]);
            CK(bpq_trace_tank(NULL, "PB01", "C", "HHZ", "--", &tk) == 0);
            CK(bpq_trace_tank(t, "PB01", "C", "HHZ", "--", NULL) == 0);
            bpq_trace_free(t);
            free(s);
        }
    }

    /* --- 10. fase cero: el filtro de vista no desplaza el impulso -------- */
    {
        const long N = 8000;
        int32_t *s = (int32_t *)calloc((size_t)N, sizeof(int32_t));
        BpqTrace *tf;
        const int32_t *f = NULL;
        long nf = 0, k, ip;
        double pre = 0.0;

        CK(s != NULL);
        if (s) {
            s[4000] = 10000;
            tf = bpq_trace_new(1000.0, 100.0, s, N);
            CK(tf != NULL);

            /* zero: fase neta cero (forward+backward). El pico y la simetria
             * no se mueven; es la condicion que traia el modulo GTK. */
            CK(bpq_trace_filter(tf, 1, 1.0, 0.0, 2, 1) == 0);
            f = bpq_trace_filtered(tf, &nf);
            CK(f != NULL && nf == N);
            if (f) {
                ip = 0;
                for (k = 0; k < nf; k++)
                    if (labs((long)f[k]) > labs((long)f[ip])) ip = k;
                CK(labs(ip - 4000) <= 1);       /* el pico no se mueve */
                for (k = 3980; k < 4000; k++) pre += fabs((double)f[k]);
                CK(pre > 0.0);                  /* pre-ringing simetrico */
            }

            /* causal: salida nula antes del impulso; confirma que el criterio
             * de arriba distingue de verdad una fase cero. */
            CK(bpq_trace_filter(tf, 1, 1.0, 0.0, 2, 0) == 0);
            f = bpq_trace_filtered(tf, &nf);
            if (f) {
                pre = 0.0;
                for (k = 3980; k < 4000; k++) pre += fabs((double)f[k]);
                CK(pre < 1e-9);
            }
            bpq_trace_free(tf);
            free(s);
        }
    }

    /* --- 11. STA/LTA sobre la traza DIBUJADA (filtrada), como el GTK ---- */
    {
        const long N = 3000;
        int32_t *s = (int32_t *)calloc((size_t)N, sizeof(int32_t));
        BpqTrace *tf, *tu;
        BpqStaLta *sf, *su;
        const double *cf_f = NULL, *cf_u = NULL;
        long nf = 0, nu = 0, k;
        double dmax = 0.0;

        CK(s != NULL);
        if (s) {
            /* drift de 1 Hz (amplitud 1000) + burst de 20 Hz en 1015..1016 s */
            for (k = 0; k < N; k++) {
                double v = 1000.0 * sin(2.0 * M_PI * 1.0 * (k / 100.0));

                if (k >= 1500 && k < 1600)
                    v += 300.0 * sin(2.0 * M_PI * 20.0 * (k / 100.0));
                s[k] = (int32_t)v;
            }
            tf = bpq_trace_new(1000.0, 100.0, s, N);
            tu = bpq_trace_new(1000.0, 100.0, s, N);    /* sin filtrar */
            CK(tf != NULL && tu != NULL);

            /* HP 5 Hz, fase cero: el drift de 1 Hz desaparece y manda el burst */
            CK(bpq_trace_filter(tf, 1, 5.0, 0.0, 2, 1) == 0);
            sf = bpq_stalta_new(tf, 0.5, 5.0, 3.0, 1.5, 0.0);
            su = bpq_stalta_new(tu, 0.5, 5.0, 3.0, 1.5, 0.0);
            CK(sf != NULL && su != NULL);
            cf_f = bpq_stalta_cf(sf, &nf);
            cf_u = bpq_stalta_cf(su, &nu);
            CK(cf_f != NULL && cf_u != NULL && nf == N && nu == N);
            if (cf_f && cf_u && nf == nu) {
                for (k = 0; k < nf; k++) {
                    const double d = fabs(cf_f[k] - cf_u[k]);

                    if (d > dmax) dmax = d;
                }
                CK(dmax > 0.01);    /* la CF no es la del crudo */
            }
            bpq_stalta_free(sf);
            bpq_stalta_free(su);
            bpq_trace_free(tf);
            bpq_trace_free(tu);
            free(s);
        }
    }

    printf("== wave checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
