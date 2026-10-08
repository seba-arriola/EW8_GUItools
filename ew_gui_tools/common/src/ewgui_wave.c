#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ewgui/wave.h"

#define EWGUI_CIRC_IDX(abs_val, size) \
    (int)(((abs_val) % (size) + (size)) % (size))

/* --- EwGuiTrace: buffer crudo + vista filtrada --- */
struct EwGuiTrace {
    long     cap;
    long     len;
    double   rate;
    double   oldest;
    int32_t *raw;
    int32_t *filt;
};

EwGuiTrace *ewgui_trace_new(long capacity)
{
    EwGuiTrace *t;
    if (capacity <= 0)
        return NULL;
    t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->raw = malloc((size_t)capacity * sizeof(int32_t));
    t->filt = malloc((size_t)capacity * sizeof(int32_t));
    if (!t->raw || !t->filt) {
        free(t->raw);
        free(t->filt);
        free(t);
        return NULL;
    }
    t->cap = capacity;
    ewgui_trace_clear(t);
    return t;
}

void ewgui_trace_free(EwGuiTrace *t)
{
    if (!t)
        return;
    free(t->raw);
    free(t->filt);
    free(t);
}

void ewgui_trace_clear(EwGuiTrace *t)
{
    if (!t)
        return;
    for (long k = 0; k < t->cap; k++) {
        t->raw[k] = INT_MAX;
        t->filt[k] = INT_MAX;
    }
    t->len = 0;
    t->rate = 0.0;
    t->oldest = 0.0;
}

long ewgui_trace_capacity(const EwGuiTrace *t) { return t ? t->cap : 0; }
long ewgui_trace_length(const EwGuiTrace *t)   { return t ? t->len : 0; }
void ewgui_trace_set_length(EwGuiTrace *t, long n)
{
    if (!t) return;
    if (n < 0) n = 0;
    if (n > t->cap) n = t->cap;
    t->len = n;
}
double ewgui_trace_rate(const EwGuiTrace *t)   { return t ? t->rate : 0.0; }
void ewgui_trace_set_rate(EwGuiTrace *t, double rate) { if (t) t->rate = rate; }
double ewgui_trace_oldest(const EwGuiTrace *t) { return t ? t->oldest : 0.0; }
void ewgui_trace_set_oldest(EwGuiTrace *t, double t0) { if (t) t->oldest = t0; }

int32_t *ewgui_trace_raw(EwGuiTrace *t)      { return t ? t->raw : NULL; }
int32_t *ewgui_trace_filtered(EwGuiTrace *t) { return t ? t->filt : NULL; }

void ewgui_trace_finish(EwGuiTrace *t)
{
    if (!t || t->len <= 0 || t->rate <= 0.0)
        return;
    ewgui_interpolate_short_gaps(t->raw, (size_t)t->len, t->rate);
    t->len = (long)ewgui_find_data_end(t->raw, (size_t)t->len);
}

void ewgui_trace_filter(EwGuiTrace *t, const EwFilterParams *p)
{
    if (!t || t->len <= 0 || t->rate <= 0.0)
        return;

    ewgui_demean(t->raw, (size_t)t->len, t->filt);

    int type = p ? p->filter_type : 0;
    if (type == 0)
        return;

    double nyquist = t->rate / 2.0;
    double safe_f1 = p->f1, safe_f2 = p->f2;
    if (safe_f1 >= nyquist) safe_f1 = nyquist * 0.95;
    if (type == 3) {
        if (safe_f2 >= nyquist) safe_f2 = nyquist * 0.95;
        if (safe_f1 >= safe_f2) safe_f1 = safe_f2 * 0.5;
    }

    if (type == 1)
        ewgui_filter_iir(t->filt, (size_t)t->len, t->rate, EW_FILTER_HP, safe_f1, p->order);
    else if (type == 2)
        ewgui_filter_iir(t->filt, (size_t)t->len, t->rate, EW_FILTER_LP, safe_f1, p->order);
    else if (type == 3)
        ewgui_filter_bandpass(t->filt, (size_t)t->len, t->rate, safe_f1, safe_f2, p->order);
}

void ewgui_trace_cache_init(EwTraceCache *c)
{
    if (c)
        memset(c, 0, sizeof(*c));
}

void ewgui_trace_cache_free(EwTraceCache *c)
{
    if (!c)
        return;
    free(c->proc_buf);
    free(c->env_min);
    free(c->env_max);
    free(c->env_has);
    memset(c, 0, sizeof(*c));
}

void ewgui_trace_cache_invalidate(EwTraceCache *c)
{
    if (c)
        c->proc_valid = 0;
}

/* Portado de csntvp.c compute_station_envelope (A2/A3). */
int ewgui_trace_envelope(const int32_t *circ, long circ_size,
                         int64_t last_abs_idx, double rate,
                         double t_end, double window_secs, double pad_secs,
                         int width, const EwFilterParams *p, EwTraceCache *c)
{
    if (!c || circ_size <= 0)
        return -1;
    if (rate <= 0.0)
        rate = 20.0;

    double t_left = t_end - window_secs;
    double extract_start = t_left - pad_secs;
    int64_t abs_start = (int64_t)(extract_start * rate);
    int64_t abs_end = (int64_t)(t_end * rate);
    long num_samps = abs_end - abs_start;

    if (num_samps <= 0 || num_samps > circ_size)
        return -1;

    EwFilterParams fp;
    if (p)
        fp = *p;
    else
        memset(&fp, 0, sizeof(fp));

    int cache_hit =
        c->proc_valid &&
        c->proc_abs_start == abs_start &&
        c->proc_abs_end == abs_end &&
        c->proc_params.filter_type == fp.filter_type &&
        c->proc_params.order == fp.order &&
        c->proc_params.f1 == fp.f1 &&
        c->proc_params.f2 == fp.f2;

    int32_t *trace_buf;
    int found_first;

    if (!cache_hit) {
        if (c->proc_cap < num_samps) {
            int32_t *nb = realloc(c->proc_buf, (size_t)num_samps * sizeof(int32_t));
            if (!nb)
                return -1;
            c->proc_buf = nb;
            c->proc_cap = num_samps;
        }
        trace_buf = c->proc_buf;

        /* Extracción al buffer lineal */
        for (long k = 0; k < num_samps; k++) {
            int64_t abs_k = abs_start + k;
            if (abs_k > last_abs_idx || abs_k < 0)
                trace_buf[k] = INT_MAX;
            else
                trace_buf[k] = circ[EWGUI_CIRC_IDX(abs_k, circ_size)];
        }

        /* Interpolación de gaps */
        long last_valid = 0;
        long gap_start = -1;
        found_first = 0;
        for (long k = 0; k < num_samps; k++) {
            if (trace_buf[k] != INT_MAX) {
                last_valid = trace_buf[k];
                found_first = 1;
                break;
            }
        }

        if (found_first) {
            for (long k = 0; k < num_samps; k++) {
                if (trace_buf[k] != INT_MAX) break;
                trace_buf[k] = (int32_t)last_valid;
            }
            for (long k = 0; k < num_samps; k++) {
                if (trace_buf[k] == INT_MAX) {
                    if (gap_start == -1) gap_start = k;
                } else {
                    if (gap_start != -1) {
                        long gap_len = k - gap_start;
                        long next_valid = trace_buf[k];
                        for (long g = gap_start; g < k; g++) {
                            double frac = (double)(g - gap_start + 1) / (double)(gap_len + 1);
                            trace_buf[g] = (int32_t)(last_valid + (long)(frac * (next_valid - last_valid)));
                        }
                        gap_start = -1;
                    }
                    last_valid = trace_buf[k];
                }
            }
            if (gap_start != -1)
                for (long g = gap_start; g < num_samps; g++) trace_buf[g] = (int32_t)last_valid;
        } else {
            for (long k = 0; k < num_samps; k++) trace_buf[k] = 0;
        }

        /* Quitar DC */
        double sum = 0;
        for (long k = 0; k < num_samps; k++) sum += (double)trace_buf[k];
        long mean = (long)(sum / num_samps);
        for (long k = 0; k < num_samps; k++) trace_buf[k] -= mean;

        /* Filtros */
        if (fp.filter_type != 0 && found_first) {
            double nyquist = rate / 2.0;
            double safe_f1 = fp.f1, safe_f2 = fp.f2;
            if (safe_f1 >= nyquist) safe_f1 = nyquist * 0.95;
            if (fp.filter_type == 3) {
                if (safe_f2 >= nyquist) safe_f2 = nyquist * 0.95;
                if (safe_f1 >= safe_f2) safe_f1 = safe_f2 * 0.5;
            }
            if (fp.filter_type == 1)
                ewgui_filter_iir(trace_buf, (size_t)num_samps, rate, EW_FILTER_HP, safe_f1, fp.order);
            else if (fp.filter_type == 2)
                ewgui_filter_iir(trace_buf, (size_t)num_samps, rate, EW_FILTER_LP, safe_f1, fp.order);
            else if (fp.filter_type == 3)
                ewgui_filter_bandpass(trace_buf, (size_t)num_samps, rate, safe_f1, safe_f2, fp.order);
        }

        c->proc_valid = 1;
        c->proc_found_first = found_first;
        c->proc_abs_start = abs_start;
        c->proc_abs_end = abs_end;
        c->proc_params = fp;
    } else {
        trace_buf = c->proc_buf;
        found_first = c->proc_found_first;
    }

    /* max_abs sobre el rango dibujado (para auto_scale) */
    long draw_start_idx = (long)(pad_secs * rate);
    if (draw_start_idx > num_samps) draw_start_idx = 0;

    long real_samps_end = (long)(last_abs_idx - abs_start + 1);
    if (real_samps_end > num_samps) real_samps_end = num_samps;
    if (real_samps_end < 0) real_samps_end = 0;

    double max_abs = 0.0;
    int has_data = 0;
    for (long k = draw_start_idx; k < real_samps_end; k++) {
        double abs_val = fabs((double)trace_buf[k]);
        if (abs_val > max_abs) max_abs = abs_val;
        has_data = 1;
    }
    if (max_abs < 1.0 || !has_data || !found_first) max_abs = 1.0;
    c->env_max_abs = max_abs;
    c->env_found_first = found_first;

    /* Envolvente (min/max) por columna */
    if (c->env_cap < width) {
        int new_cap = width + 256;
        double *nmin = realloc(c->env_min, (size_t)new_cap * sizeof(double));
        double *nmax = realloc(c->env_max, (size_t)new_cap * sizeof(double));
        int *nhas = realloc(c->env_has, (size_t)new_cap * sizeof(int));
        if (!nmin || !nmax || !nhas) {
            if (nmin) free(nmin);
            if (nmax) free(nmax);
            if (nhas) free(nhas);
            c->env_valid = 0;
            return -1;
        }
        c->env_min = nmin;
        c->env_max = nmax;
        c->env_has = nhas;
        c->env_cap = new_cap;
    }
    c->env_width = width;

    if (found_first) {
        for (int px = 0; px < width; px++) {
            double px_t_start = t_left + ((double)px / width) * window_secs;
            double px_t_end = t_left + ((double)(px + 1) / width) * window_secs;

            long p_local_start = (long)((px_t_start - extract_start) * rate);
            long p_local_end = (long)((px_t_end - extract_start) * rate);
            if (p_local_end == p_local_start) p_local_end++;
            if (p_local_start < 0) p_local_start = 0;
            if (p_local_end > num_samps) p_local_end = num_samps;

            double p_min = 1e12, p_max = -1e12;
            int px_has = 0;
            for (long local_k = p_local_start; local_k < p_local_end; local_k++) {
                if (local_k >= real_samps_end) continue;
                double val = (double)trace_buf[local_k];
                if (val < p_min) p_min = val;
                if (val > p_max) p_max = val;
                px_has = 1;
            }
            if (px_has) {
                c->env_min[px] = p_min;
                c->env_max[px] = p_max;
                c->env_has[px] = 1;
            } else {
                c->env_has[px] = 0;
            }
        }
    }

    c->env_valid = 1;
    return cache_hit ? 0 : 1;
}
