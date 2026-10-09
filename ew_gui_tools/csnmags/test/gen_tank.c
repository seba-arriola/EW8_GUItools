#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <trace_buf.h>

/*
 * gen_tank.c - tank sintético determinista para el test offline.
 * Tres componentes (HHZ/HHN/HHE) por estación, 60 s a 100 Hz, seno de 1 Hz
 * (1000 cuentas) entre t0+2 y t0+55.
 */

#define NBUF 6000           /* 60 s * 100 Hz */
#define T0   1700000000.0

static void emit(FILE *f, const char *sta, const char *chan)
{
    TRACE2_HEADER h;
    int32_t data[NBUF];
    int i;

    memset(&h, 0, sizeof(h));
    h.pinno = 0;
    h.nsamp = NBUF;
    h.starttime = T0;
    h.endtime = T0 + (NBUF - 1) / 100.0;
    h.samprate = 100.0;
    snprintf(h.sta, sizeof(h.sta), "%s", sta);
    snprintf(h.net, sizeof(h.net), "%s", "C1");
    snprintf(h.chan, sizeof(h.chan), "%s", chan);
    snprintf(h.loc, sizeof(h.loc), "%s", "--");
    h.version[0] = '2'; h.version[1] = '0';
    snprintf(h.datatype, sizeof(h.datatype), "%s", "i4");

    for (i = 0; i < NBUF; i++) {
        double t = i / 100.0;
        double v = 0.0;
        if (t >= 2.0 && t <= 55.0) v = 1000.0 * sin(2.0 * M_PI * 1.0 * t);
        data[i] = (int32_t)(v + (i % 3 - 1));   /* +-1 cuenta de "ruido" determinista */
    }
    fwrite(&h, sizeof(h), 1, f);
    fwrite(data, sizeof(int32_t), NBUF, f);
}

int main(int argc, char **argv)
{
    FILE *f;
    const char *path = (argc > 1) ? argv[1] : "test/tmp/test.tank";
    f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    emit(f, "TST1", "HHZ");
    emit(f, "TST1", "HHN");
    emit(f, "TST1", "HHE");
    emit(f, "TST2", "HHZ");
    emit(f, "TST2", "HHN");
    emit(f, "TST2", "HHE");
    fclose(f);
    return 0;
}
