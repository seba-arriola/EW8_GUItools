/* gen_tank.c — genera un tank sintético 3C con un arribo S conocido.        *
 *                                                                            *
 * Uso: gen_tank <out.tank> <STA> <NET> <LOC> <ChanE> <ChanN> <ChanZ>         */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <trace_buf.h>

#define PI 3.14159265358979323846

int main(int argc, char **argv)
{
    const char *out;
    const char *sta, *net, *loc;
    const char *chans[3];
    double fs = 100.0, t0 = 1700000000.0;
    int    N = 3000, i, c;
    FILE  *f;
    int32_t *d;

    if (argc != 8) {
        fprintf(stderr, "Uso: %s <out.tank> <STA> <NET> <LOC> <ChanE> <ChanN> <ChanZ>\n", argv[0]);
        return 1;
    }
    out = argv[1];
    sta = argv[2]; net = argv[3]; loc = argv[4];
    chans[0] = argv[5]; chans[1] = argv[6]; chans[2] = argv[7];

    d = (int32_t *)malloc((size_t)N * sizeof(int32_t));
    if (!d) return 1;

    f = fopen(out, "wb");
    if (!f) { perror(out); free(d); return 1; }

    for (c = 0; c < 3; c++) {
        TRACE2_HEADER h;
        memset(&h, 0, sizeof(h));
        h.pinno = 1;
        h.nsamp = N;
        h.starttime = t0;
        h.endtime = t0 + (double)(N - 1) / fs;
        h.samprate = fs;
        strncpy(h.sta, sta, TRACE2_STA_LEN - 1);
        strncpy(h.net, net, TRACE2_NET_LEN - 1);
        strncpy(h.chan, chans[c], TRACE2_CHAN_LEN - 1);
        strncpy(h.loc, loc, TRACE2_LOC_LEN - 1);
        h.version[0] = '2'; h.version[1] = '0';
        strcpy(h.datatype, "i4");
        h.quality[0] = ' '; h.quality[1] = ' ';

        for (i = 0; i < N; i++) {
            double t = (double)i / fs;
            double base = 0.3 * sin(2.0 * PI * 7.0 * t + 0.3);
            double s = 0.0, v;
            if (t >= 10.0) {
                double dt = t - 10.0;
                s = 30.0 * exp(-(dt * dt) / (0.25 * 0.25)) * cos(2.0 * PI * 4.0 * dt);
            }
            if (c == 0)      v = base + s;
            else if (c == 1) v = 0.7 * base + 0.8 * s;
            else             v = 0.15 * base + 0.05 * s;
            d[i] = (int32_t)lround(v);
        }

        if (fwrite(&h, sizeof(h), 1, f) != 1) { fclose(f); free(d); return 1; }
        if (fwrite(d, sizeof(int32_t), (size_t)N, f) != (size_t)N) { fclose(f); free(d); return 1; }
    }

    fclose(f);
    free(d);
    return 0;
}
