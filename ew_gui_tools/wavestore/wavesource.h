#ifndef WAVESOURCE_H
#define WAVESOURCE_H

/*
 * Fuente de onda unificada:
 *   WSRC_WS    : wave_serverV por TCP.
 *   WSRC_TANK  : tank directo (indice en memoria; patron pickS).
 *   WSRC_STORE : store indexado (wstore).
 *   WSRC_AUTO  : intenta STORE y, si no hay datos, cae a WS.
 */

typedef enum { WSRC_WS, WSRC_TANK, WSRC_STORE, WSRC_AUTO } WaveSourceMode;

typedef struct {
    WaveSourceMode mode;
    const char    *store_root;   /* WSRC_STORE / WSRC_AUTO */
    const char    *tank_or_null; /* WSRC_TANK              */
    char           ws_ip[64];
    char           ws_port[16];
    int            ws_timeout;
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
