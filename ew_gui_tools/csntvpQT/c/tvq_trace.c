/* Trazas en vivo: TYPE_TRACEBUF2 -> buffer circular int32 por (estacion, canal).
 *
 * Copia la maquinaria de csntvp_data.c:241-329 (indice absoluto temporal,
 * relleno de huecos con INT_MAX, buffer dimensionado por la tasa real con tope
 * de 120 min) y le anade las dos guardas que el GTK no tiene:
 *   - `nsamp` se acota a las muestras que el mensaje REALMENTE trae (un TRACE2
 *     truncado no puede leer fuera del buffer del anillo);
 *   - una estacion no listada se ignora (no crea pistas fantasma).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tvq_capi.h"
#include "tvq_internal.h"

double tvq_now_epoch(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int read_sample_at(const char *datatype, const char *pdata, int idx,
                          int32_t *out)
{
    if (strcmp(datatype, "i2") == 0 || strcmp(datatype, "s2") == 0) {
        int16_t v;
        memcpy(&v, pdata + (size_t)idx * 2, 2);
        *out = v;
        return 0;
    }
    if (strcmp(datatype, "i4") == 0 || strcmp(datatype, "s4") == 0) {
        int32_t v;
        memcpy(&v, pdata + (size_t)idx * 4, 4);
        *out = v;
        return 0;
    }
    if (strcmp(datatype, "f4") == 0 || strcmp(datatype, "t4") == 0) {
        float v;
        memcpy(&v, pdata + (size_t)idx * 4, 4);
        *out = (int32_t)(v + (v >= 0.0f ? 0.5f : -0.5f));
        return 0;
    }
    *out = 0;
    return -1;
}

static int bytes_per_sample(const char *datatype)
{
    if (strcmp(datatype, "i2") == 0 || strcmp(datatype, "s2") == 0)
        return 2;
    return 4;
}

static int ensure_buffer(TvqStation *s, double rate)
{
    int rate_int = (int)(rate + 0.5);
    long want;

    if (rate_int < 1)
        rate_int = 1;
    if (rate_int > TVQ_MAX_SAMP_RATE)
        rate_int = TVQ_MAX_SAMP_RATE;
    want = (long)TVQ_MAX_MINUTES * 60L * (long)rate_int;

    if (s->circ && s->circ_size == want) {
        s->rate = rate;
        return 0;
    }

    {
        int32_t *nb = (int32_t *)realloc(s->circ, (size_t)want * sizeof(int32_t));

        if (!nb)
            return -1;
        s->circ = nb;
        s->circ_size = want;
        s->rate = rate;
        for (long k = 0; k < want; k++)
            s->circ[k] = TVQ_GAP;
        s->last_abs = 0;
        s->cache.proc_valid = 0;
        s->cache.env_valid = 0;
    }
    return 0;
}

int tvq_core_ingest_trace(TvqCore *c, const char *msg, int len)
{
    TRACE2_HEADER h;
    TvqStation *s = NULL;
    double rate, t_start, t_end;
    int64_t abs_start, abs_end;
    const char *pdata;
    char datatype[3];
    int bps, avail, nsamp;

    if (!c || !msg || len < (int)sizeof(TRACE2_HEADER))
        return 0;

    memcpy(&h, msg, sizeof h);   /* el buffer del anillo puede no estar alineado */
    if (h.nsamp <= 0)
        return 0;

    for (int i = 0; i < c->n_sta; i++) {
        if (strcmp(c->sta[i].sta, h.sta) == 0 &&
            strcmp(c->sta[i].chan, h.chan) == 0) {
            s = &c->sta[i];
            break;
        }
    }
    if (!s)
        return 0;   /* estacion no listada: se ignora */

    rate = h.samprate;
    if (rate <= 0.0)
        rate = 20.0;
    if (rate > (double)TVQ_MAX_SAMP_RATE)
        rate = (double)TVQ_MAX_SAMP_RATE;

    if (ensure_buffer(s, rate) != 0) {
        tvq_logf("csntvpQT: sin memoria para %s (%d muestras/s)\n", s->sta,
                 (int)(rate + 0.5));
        return 0;
    }

    snprintf(datatype, sizeof datatype, "%.2s", h.datatype);
    datatype[2] = '\0';
    bps = bytes_per_sample(datatype);
    pdata = msg + sizeof(TRACE2_HEADER);
    avail = (len - (int)sizeof(TRACE2_HEADER)) / bps;
    nsamp = (h.nsamp < avail) ? h.nsamp : avail;
    if (nsamp <= 0)
        return 0;

    t_start = h.starttime;
    t_end = h.endtime;
    abs_start = (int64_t)(t_start * rate);
    abs_end = (int64_t)(t_end * rate);

    /* Hueco entre el ultimo dato y este paquete: relleno con INT_MAX. */
    if (s->last_abs > 0 && abs_start > s->last_abs) {
        int64_t gap = abs_start - s->last_abs;

        if (gap > s->circ_size)
            gap = s->circ_size;
        for (int64_t g = 0; g < gap; g++) {
            int idx = TVQ_CIRC_IDX(s->last_abs + g, s->circ_size);

            s->circ[idx] = TVQ_GAP;
        }
        if (s->cache.proc_valid && (abs_start - 1) >= s->cache.proc_abs_start)
            s->cache.proc_valid = 0;
    }

    for (int k = 0; k < nsamp; k++) {
        int32_t x;
        double t_samp;
        int64_t abs_idx;
        int idx;

        if (read_sample_at(datatype, pdata, k, &x) != 0) {
            if (!c->warned_datatype) {
                c->warned_datatype = 1;
                tvq_logf("csntvpQT: datatype <%s> no soportado, leo int32\n",
                         datatype);
            }
            memcpy(&x, pdata + (size_t)k * sizeof(int32_t), sizeof(int32_t));
        }
        t_samp = t_start + ((double)k / rate);
        abs_idx = (int64_t)(t_samp * rate);
        idx = TVQ_CIRC_IDX(abs_idx, s->circ_size);
        s->circ[idx] = x;
    }

    if (abs_end > s->last_abs)
        s->last_abs = abs_end;
    if (s->cache.proc_valid && abs_start <= s->cache.proc_abs_end)
        s->cache.proc_valid = 0;

    s->has_data = 1;
    s->last_packet_wall = tvq_now_epoch();
    c->last_data_wall = s->last_packet_wall;

    /* Una sola linea: confirma que el anillo trae datos (y a que tasa). */
    if (!c->logged_first_trace) {
        c->logged_first_trace = 1;
        tvq_logf("csntvpQT: primera traza %s.%s.%s.%s a %.1f Hz\n", s->sta,
                 s->chan, s->net, s->loc, rate);
    }
    return 1;
}
