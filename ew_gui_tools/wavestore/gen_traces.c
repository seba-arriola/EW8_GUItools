#include "trace_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *out;
    int         nrec, per = 100;
    double      rate = 100.0, t0 = 1700000000.0;
    FILE       *f;
    int32_t     buf[100];

    if (argc < 2) { fprintf(stderr, "Uso: gen_traces <out.traces> [nrec]\n"); return 1; }
    out = argv[1];
    nrec = argc > 2 ? atoi(argv[2]) : 5;

    f = fopen(out, "wb");
    if (!f) { perror(out); return 1; }
    for (int k = 0; k < nrec; k++) {
        TRACE2_HEADER h;
        for (int i = 0; i < per; i++) buf[i] = k * per + i + 1;
        memset(&h, 0, sizeof(h));
        snprintf(h.sta, sizeof(h.sta), "ABC");
        snprintf(h.net, sizeof(h.net), "CX");
        snprintf(h.chan, sizeof(h.chan), "HHZ");
        snprintf(h.loc, sizeof(h.loc), "--");
        h.starttime = t0 + (double)k * per / rate;
        h.samprate = rate;
        h.nsamp = per;
        h.endtime = h.starttime + (per - 1) / rate;
        memcpy(h.datatype, "i4", 3);
        h.version[0] = '2';
        h.version[1] = '0';
        fwrite(&h, sizeof(h), 1, f);
        fwrite(buf, sizeof(int32_t), (size_t)per, f);
    }
    fclose(f);
    return 0;
}
