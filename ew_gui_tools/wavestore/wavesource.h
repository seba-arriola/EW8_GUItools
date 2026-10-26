#ifndef WAVESOURCE_H
#define WAVESOURCE_H

/*
 * Fuente de onda unificada:
 *   WSRC_TRACES  : traces directo (indice en memoria; patron pickS).
 *   WSRC_STORE : store indexado (wstore).
 */

typedef enum { WSRC_TRACES, WSRC_STORE } WaveSourceMode;

typedef struct {
    WaveSourceMode mode;
    const char    *store_root;   /* WSRC_STORE */
    const char    *traces_or_null; /* WSRC_TRACES  */
} WaveSrcCfg;

typedef struct {
    char    sta[8], net[10], chan[8], loc[4];
    double  t0, dt, samprate;
    long    n;
    double *x;   /* malloc; liberar con free() */
} WaveTrace;

typedef struct WaveSource WaveSource;

int  wave_open (WaveSource **out, const WaveSrcCfg *cfg);
int  wave_get  (WaveSource *w, const char *sta, const char *net, const char *chan,
                const char *loc, double t0, double t1, WaveTrace *out);  /* 0 OK, -1 sin datos */
void wave_close(WaveSource *w);

#endif /* WAVESOURCE_H */
