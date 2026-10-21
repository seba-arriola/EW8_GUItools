/******************************************************************************
 * pickml_buffer.c — buffer 3C por estación con máscara de validez.           *
 *                                                                            *
 * Por qué una máscara y no "huecos = 0": `wstore_get` rellena los huecos con  *
 * ceros y una red los lee como señal (envenena la normalización). Aquí cada  *
 * muestra sabe si existe; la ventana declara el hueco o lo interpola si es    *
 * corto (MaxGapSec).                                                         *
 *                                                                            *
 * El buffer es deslizante: al entrar un paquete que no cabe se desplaza a la  *
 * izquierda (memmove) y `t0` avanza. Índice absoluto de una muestra:          *
 *     idx = lround((t - t0) * rate)                                           *
 ******************************************************************************/

#include "pickml.h"

#define PM_RATE_TOL 0.01

static void reset_all(PmBuf *b, double t0)
{
    int c;
    b->t0 = t0;
    for (c = 0; c < PM_MAX_COMP; c++) {
        b->lo[c] = b->hi[c] = 0;
        b->first_t[c] = -1.0;
        memset(b->x[c], 0, (size_t)b->cap * sizeof(int32_t));
        memset(b->valid[c], 0, (size_t)b->cap);
    }
    b->started = 0;
}

int pm_buf_init(PmBuf *b, double rate, long cap)
{
    int c;

    if (!b || rate <= 0.0 || cap < 8) return PM_ERR;
    memset(b, 0, sizeof(*b));
    b->rate = rate;
    b->cap  = cap;

    for (c = 0; c < PM_MAX_COMP; c++) {
        b->x[c]     = (int32_t *)calloc((size_t)cap, sizeof(int32_t));
        b->valid[c] = (uint8_t *)calloc((size_t)cap, 1);
        if (!b->x[c] || !b->valid[c]) {
            pm_buf_free(b);
            return PM_ERR;
        }
    }
    reset_all(b, 0.0);
    return PM_OK;
}

void pm_buf_free(PmBuf *b)
{
    int c;
    if (!b) return;
    for (c = 0; c < PM_MAX_COMP; c++) {
        free(b->x[c]);
        free(b->valid[c]);
        b->x[c] = NULL;
        b->valid[c] = NULL;
    }
}

static void buf_shift(PmBuf *b, long shift)
{
    int c;

    if (shift <= 0) return;

    if (shift >= b->cap) {
        reset_all(b, b->t0 + (double)shift / b->rate);
        b->started = 1;          /* el buffer sigue vivo: solo se descartó la historia */
        b->shift += shift;
        return;
    }

    for (c = 0; c < PM_MAX_COMP; c++) {
        memmove(b->x[c], b->x[c] + shift,
                (size_t)(b->cap - shift) * sizeof(int32_t));
        memset(b->x[c] + (b->cap - shift), 0,
               (size_t)shift * sizeof(int32_t));

        memmove(b->valid[c], b->valid[c] + shift, (size_t)(b->cap - shift));
        memset(b->valid[c] + (b->cap - shift), 0, (size_t)shift);

        b->lo[c] = (b->lo[c] > shift) ? b->lo[c] - shift : 0;
        b->hi[c] = (b->hi[c] > shift) ? b->hi[c] - shift : 0;
    }
    b->t0 += (double)shift / b->rate;
    b->shift += shift;
}

int pm_buf_add(PmBuf *b, int comp, double t0, double rate,
               const int32_t *x, long n)
{
    long idx0, i, off;

    if (!b || !x || n <= 0 || comp < 0 || comp >= PM_MAX_COMP) return PM_ERR;
    if (fabs(rate - b->rate) > PM_RATE_TOL) return PM_ERR;
    if (n > b->cap) {
        b->overrun++;
        return PM_ERR;
    }

    off = 0;

    if (!b->started) {
        reset_all(b, t0);
        b->started = 1;
    }

    idx0 = lround((t0 - b->t0) * b->rate);

    /* Paquete anterior al inicio del buffer: se conserva la parte que solape. */
    if (idx0 + n <= 0) {
        b->late++;
        return PM_NODATA;
    }

    if (idx0 + n > b->cap) {
        buf_shift(b, idx0 + n - b->cap);
        idx0 = lround((t0 - b->t0) * b->rate);
    }
    if (idx0 < 0) {
        /* Recorte del prefijo que ya no cabe. */
        off = -idx0;
        if (off >= n) {
            b->late++;
            return PM_NODATA;
        }
        b->dup++;
        x  += off;
        n  -= off;
        idx0 = 0;
    }
    if (idx0 + n > b->cap) {
        b->overrun++;
        return PM_ERR;
    }

    /* Solape con datos ya presentes: solo se escriben las muestras nuevas.
     * Así un reenvío del anillo es idempotente. */
    for (i = 0; i < n; i++) {
        long k = idx0 + i;
        if (b->valid[comp][k] && b->x[comp][k] == x[i]) {
            b->dup++;
            continue;
        }
        b->x[comp][k]     = x[i];
        b->valid[comp][k] = 1;
    }
    if (idx0 < b->lo[comp] || b->hi[comp] <= b->lo[comp])
        b->lo[comp] = idx0;
    if (idx0 + n > b->hi[comp])
        b->hi[comp] = idx0 + n;

    if (b->first_t[comp] < 0.0)
        b->first_t[comp] = t0 + (double)off / b->rate;

    return PM_OK;
}

double pm_buf_ready(const PmBuf *b)
{
    double tmin = -1.0;
    int    c, have = 1;

    if (!b || !b->started) return -1.0;

    for (c = 0; c < PM_MAX_COMP; c++) {
        double t;
        if (b->hi[c] <= 0) { have = 0; break; }
        t = b->t0 + (double)(b->hi[c] - 1) / b->rate;
        if (tmin < 0.0 || t < tmin) tmin = t;
    }
    return have ? tmin : -1.0;
}

/* Rellena una muestra inválida interpolando linealmente entre sus vecinas
 * válidas. Si el hueco toca un borde sin vecino válido, no se puede. */
static int fill_run(const PmBuf *b, int comp, long base, long a, long len,
                    float *out)
{
    long left  = a - 1;
    long right = a + len;
    int  have_l = (left  >= 0 && b->valid[comp][base + left]);
    int  have_r = (right + base < b->cap && b->valid[comp][base + right]);
    long k;

    if (!have_l && !have_r) return PM_GAP;

    for (k = 0; k < len; k++) {
        float v;
        if (have_l && have_r) {
            double fl = (double)b->x[comp][base + left];
            double fr = (double)b->x[comp][base + right];
            double w  = (double)(k + 1) / (double)(len + 1);
            v = (float)(fl + (fr - fl) * w);
        } else if (have_l) {
            v = (float)b->x[comp][base + left];
        } else {
            v = (float)b->x[comp][base + right];
        }
        out[k] = v;
    }
    return PM_OK;
}

int pm_buf_window(const PmBuf *b, double t0, long nsamp, double maxgap_sec,
                  float *out3c, long *interp)
{
    long idx, i, run_start, run_len;
    long maxrun = (long)floor(maxgap_sec * b->rate + 1e-9);
    int  c;

    if (interp) *interp = 0;
    if (!b || !out3c || nsamp <= 0 || !b->started) return PM_NODATA;

    idx = lround((t0 - b->t0) * b->rate);
    if (idx < 0 || idx + nsamp > b->cap) return PM_NODATA;

    for (c = 0; c < PM_MAX_COMP; c++) {
        float *out = out3c + (size_t)c * (size_t)nsamp;

        for (i = 0; i < nsamp; i++)
            out[i] = b->valid[c][idx + i] ? (float)b->x[c][idx + i] : NAN;

        run_start = -1;
        for (i = 0; i <= nsamp; i++) {
            int bad = (i < nsamp) && isnan(out[i]);
            if (bad && run_start < 0) {
                run_start = i;
            } else if (!bad && run_start >= 0) {
                run_len = i - run_start;
                if (run_len > maxrun) return PM_GAP;
                if (fill_run(b, c, idx, run_start, run_len, out + run_start) != PM_OK)
                    return PM_GAP;
                if (interp) *interp += run_len;
                run_start = -1;
            }
        }
    }
    return PM_OK;
}
