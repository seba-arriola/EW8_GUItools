/*
 * test_mag_lines - camino de escritura de magnitudes por estacion de hypodb.
 *
 * El DIMENSIONADO del array de canales ya no vive aqui: lo hace el helper
 * compartido `csnhypodbp_magchan_parse()` (ver `test_magchan` en csnhypodbp, que
 * fija el conteo y el desbordamiento). Este test cubre lo propio de hypodb:
 *
 *   1. el camino de escritura con el mensaje TRUNCADO a 511 bytes (lo que hacia
 *      `store_pending_mag`) no se sale de nada y devuelve 0 (bajo valgrind, sin
 *      `Invalid write/read`: es el bug que mataba al modulo con SIGABRT);
 *   2. el mensaje del llamante queda INTACTO: `rd_chan_mag` parsea in-place y lo
 *      destruiria si no se le diera una copia (antes lo destruia, y la segunda
 *      lectura de los mismos bytes veia otra cosa).
 *
 * Determinista, sin BD (`st->db == NULL` no toca la BD) y sin anillos.
 * El desbordamiento/lectura de mas se ve con: make check-valgrind
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include <rw_mag.h>

#include "csnhypodbp_magchan.h"
#include "hypodb_config.h"
#include "hypodb_ingest.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

/* TYPE_MAGNITUDE con `nsta` estaciones, serializado con el motor. */
static int build_mag(char *buf, size_t n, int nsta)
{
    MAG_CHAN_INFO ch[16];
    MAG_INFO m;
    int i;

    if (nsta > (int)(sizeof ch / sizeof ch[0]))
        return -1;
    memset(ch, 0, sizeof ch);
    for (i = 0; i < nsta; i++) {
        snprintf(ch[i].sta, sizeof ch[i].sta, "GO%02d", i + 1);
        snprintf(ch[i].comp, sizeof ch[i].comp, "HHZ");
        snprintf(ch[i].net, sizeof ch[i].net, "C1");
        ch[i].mag = 5.0 + 0.1 * i;
        ch[i].Amp1 = 1000.0f + i;
    }
    memset(&m, 0, sizeof m);
    snprintf(m.qid, sizeof m.qid, "%s", "8210600000");
    snprintf(m.szmagtype, sizeof m.szmagtype, "%s", "ML");
    snprintf(m.algorithm, sizeof m.algorithm, "%s", "CSNNet");
    m.imagtype = 1;
    m.mag = 5.1;
    m.nstations = nsta;
    m.origin_version = 163;
    m.nchannels = nsta;
    m.pMagAux = (char *)ch;
    m.size_aux = (size_t)nsta * sizeof(MAG_CHAN_INFO);
    return wr_mag_scnl(&m, buf, (int)n) == 0 ? 0 : -1;
}

int main(void)
{
    char full[2048], trunc[512], saved[2048];
    HypodbConfig cfg;
    HypodbState *st;
    MAG_CHAN_INFO *ch = NULL;
    int flen, tlen, rc, n;

    setlocale(LC_NUMERIC, "C");
    memset(&cfg, 0, sizeof cfg);
    cfg.group_pick_tol_sec = 0.1;
    cfg.group_min_match_picks = 3;
    cfg.pick_match_tol_sec = 0.05;
    cfg.event_max_dt_sec = 300;
    st = hypodb_state_new(&cfg);
    if (!st) {
        printf("FAIL: hypodb_state_new\n");
        return 1;
    }

    if (build_mag(full, sizeof full, 12) != 0) {
        printf("FAIL: wr_mag_scnl no serializa\n");
        return 1;
    }
    flen = (int)strlen(full);

    /* 1. El helper compartido ve las 12 estaciones del mensaje completo. */
    n = csnhypodbp_magchan_parse(full, (size_t)flen, &ch);
    printf("      (mensaje de %d bytes, el helper ve %d estaciones)\n", flen, n);
    CHECK(n == 12, "el helper ve las 12 estaciones");
    free(ch);
    ch = NULL;

    /* 2. Truncado a 511 bytes: es EXACTAMENTE lo que guardaba store_pending_mag.
          El mensaje del llamante debe quedar intacto (el motor lo destruiria). */
    tlen = (flen > 511) ? 511 : flen;
    memset(trunc, 0, sizeof trunc);
    memcpy(trunc, full, (size_t)tlen);
    trunc[tlen] = '\0';
    memcpy(saved, trunc, (size_t)tlen + 1);
    rc = hypodb_write_station_magnitudes(st, "MAG-TEST/163/ML", "ML", trunc,
                                         (size_t)tlen);
    CHECK(rc == 0, "escritura con el mensaje truncado a 511: rc 0");
    CHECK(memcmp(saved, trunc, (size_t)tlen) == 0,
          "el mensaje truncado queda intacto (el motor lo destruiria)");

    /* 3. El camino completo, sin truncar. */
    memcpy(saved, full, (size_t)flen + 1);
    rc = hypodb_write_station_magnitudes(st, "MAG-TEST/163/ML", "ML", full,
                                         (size_t)flen);
    CHECK(rc == 0, "escritura con el mensaje completo: rc 0");
    CHECK(memcmp(saved, full, (size_t)flen) == 0,
          "el mensaje completo tambien queda intacto");

    hypodb_state_free(st);
    if (fails) {
        printf("\n%d FALLOS\n", fails);
        return 1;
    }
    printf("\nOK test_mag_lines\n");
    return 0;
}
