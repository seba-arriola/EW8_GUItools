#define _GNU_SOURCE
#include "wavesource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    const char *mode = "store", *store = NULL, *traces = NULL, *scnl = NULL;
    double      t0 = -1, t1 = -1;
    int         repeat = 10;
    char        net[16], sta[16], loc[8], chan[8];
    WaveSrcCfg  cfg;
    WaveSource *w = NULL;
    double      best = 1e18, worst = 0, sum = 0;
    long        nsamp = 0;
    int         ok = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
        else if (!strcmp(argv[i], "--store") && i + 1 < argc) store = argv[++i];
        else if (!strcmp(argv[i], "--traces") && i + 1 < argc) traces = argv[++i];
        else if (!strcmp(argv[i], "--scnl") && i + 1 < argc) scnl = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) t0 = atof(argv[++i]);
        else if (!strcmp(argv[i], "--end") && i + 1 < argc) t1 = atof(argv[++i]);
        else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = atoi(argv[++i]);
    }
    if (!scnl || t0 < 0 || t1 < 0 || repeat < 1) {
        fprintf(stderr, "Uso: wsbench --mode store|traces [--store R | --traces F] "
                        "--scnl NET.STA.LOC.CHAN --start <e> --end <e> [--repeat N]\n");
        return 1;
    }
    if (sscanf(scnl, "%15[^.].%15[^.].%7[^.].%7s", net, sta, loc, chan) != 4) {
        fprintf(stderr, "wsbench: SCNL invalido <%s>\n", scnl);
        return 1;
    }

    memset(&cfg, 0, sizeof(cfg));
    if (!strcmp(mode, "store"))      cfg.mode = WSRC_STORE;
    else if (!strcmp(mode, "traces"))  cfg.mode = WSRC_TRACES;
    else { fprintf(stderr, "wsbench: --mode invalido <%s> (store|traces)\n", mode); return 1; }
    cfg.store_root = store;
    cfg.traces_or_null = traces;

    if (wave_open(&w, &cfg) != 0) {
        fprintf(stderr, "wsbench: no abro la fuente (%s)\n", mode);
        return 1;
    }

    for (int i = 0; i < repeat; i++) {
        WaveTrace tr;
        double    a, b;
        int       r;
        memset(&tr, 0, sizeof(tr));
        a = now_ms();
        r = wave_get(w, sta, net, chan, loc, t0, t1, &tr);
        b = now_ms();
        if (r == 0) {
            double d = b - a;
            ok++;
            nsamp = tr.n;
            sum += d;
            if (d < best) best = d;
            if (d > worst) worst = d;
            free(tr.x);
        }
    }
    wave_close(w);

    if (ok == 0) {
        fprintf(stderr, "wsbench: sin datos (%s %s)\n", mode, scnl);
        return 1;
    }
    printf("%-5s %s: n=%d/%d nsamp=%ld min=%.2fms avg=%.2fms max=%.2fms\n",
           mode, scnl, ok, repeat, nsamp, best, sum / ok, worst);
    return 0;
}
