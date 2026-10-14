/*
 * test_mag_lines - dimensionado del array de canales de un TYPE_MAGNITUDE.
 *
 * POR QUE: `rd_chan_mag` de EarthWorm NO acota sus escrituras contra el tamano
 * que se le pasa (el 4.o argumento solo alimenta el `memset` inicial:
 * earthworm_8.0/src/libsrc/util/rw_mag.c:114). Si el array se dimensiona con un
 * conteo MENOR que el numero de entradas que el motor escribe, escribe FUERA del
 * heap; glibc lo detecta en el siguiente malloc/free y el modulo muere con
 * SIGABRT (signal 6) minutos despues, sin dejar rastro. Eso es lo que le pasaba a
 * hypodb_ingest (medido 2026-10-14: `calloc(1)` con el motor escribiendo 3).
 *
 * Y contar '\n' NO vale de proxy: el formato no parte solo por '\n' (2 '\n' para
 * 3 estaciones con wr_mag_scnl). El dimensionado tiene que ser lo que el motor
 * escribe de verdad => hypodb_mag_station_count().
 *
 * OJO: `rd_chan_mag` parsea IN-PLACE (`tmpMsg = msg`, `line[len] = '\0'`), o sea
 * que DESTRUYE el buffer. Por eso cada parseo de este test usa su propia copia, y
 * por eso `hypodb_mag_station_count()` tambien copia.
 *
 * Determinista, sin BD ni anillos. El desbordamiento real (Invalid write dentro
 * del motor, que es codigo sin instrumentar) se ve con:
 *   make check-valgrind
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include <rw_mag.h>

#include "hypodb_ingest.h"
#include "hypodb_config.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
                              else printf("ok  : %s\n", msg); } while (0)

/* Referencia independiente: cuantas entradas escribe el motor, sobre SU PROPIA
   COPIA (el motor destruye el buffer) y con capacidad provablemente suficiente
   (nunca escribe mas de `len-1` entradas). */
static int engine_writes(const char *msg, int len)
{
    MAG_CHAN_INFO *ref;
    char *copy;
    int i, n = 0;

    if (len <= 0)
        return 0;
    ref = calloc((size_t)len, sizeof(*ref));
    /* Copia con holgura y a cero: el motor lee 1 byte mas alla del mensaje
       (tokenlength -> strchr/strlen, rw_mag.c:384-385). */
    copy = calloc((size_t)len + 64, 1);
    if (!ref || !copy) {
        free(ref);
        free(copy);
        return -1;
    }
    memcpy(copy, msg, (size_t)len);
    (void)rd_chan_mag(copy, len, ref, (int)((size_t)len * sizeof(*ref)));
    free(copy);
    for (i = 0; i < len; i++)
        if (ref[i].sta[0])
            n++;
    free(ref);
    return n;
}

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
    char buf[2048];
    char saved[2048];
    HypodbConfig cfg;
    HypodbState *st;
    int len, n, w;

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

    if (build_mag(buf, sizeof buf, 3) != 0) {
        printf("FAIL: wr_mag_scnl no serializa\n");
        return 1;
    }

    /* 1. Mensaje tal cual lo emite el motor. */
    len = (int)strlen(buf);
    w = engine_writes(buf, len);
    n = hypodb_mag_station_count(buf, len);
    printf("      (motor escribe %d, dimensionado %d)\n", w, n);
    CHECK(w == 3, "el motor escribe 3 estaciones");
    CHECK(n == 3, "el dimensionado es 3");
    CHECK(n == w, "el dimensionado coincide con lo que escribe el motor");

    /* 2. Nuestro dimensionado NO debe destruir el mensaje del llamante (el motor
          si lo hace; por eso hypodb_mag_station_count trabaja sobre una copia). */
    memcpy(saved, buf, (size_t)len + 1);
    hypodb_mag_station_count(buf, (size_t)len);
    CHECK(memcmp(saved, buf, (size_t)len) == 0,
          "hypodb_mag_station_count no toca el mensaje del llamante");

    /* 3. Sin '\n' final. */
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }
    w = engine_writes(buf, len);
    n = hypodb_mag_station_count(buf, len);
    printf("      (motor escribe %d, dimensionado %d)\n", w, n);
    CHECK(w == 3, "sin '\\n' final: el motor sigue escribiendo 3");
    CHECK(n == w, "sin '\\n' final: el dimensionado coincide");

    /* 4. Truncado a 511 bytes: es EXACTAMENTE lo que guarda store_pending_mag
          (pm->msg[512] con msg_len = min(len, 511)), asi que la ultima entrada
          queda partida y el conteo por '\n' se quedaba corto. */
    {
        char full[2048], trunc[512];
        int flen, tlen;

        if (build_mag(full, sizeof full, 12) != 0) {
            printf("FAIL: wr_mag_scnl no serializa (12 estaciones)\n");
            return 1;
        }
        flen = (int)strlen(full);
        memset(trunc, 0, sizeof trunc);
        tlen = (flen > 511) ? 511 : flen;
        memcpy(trunc, full, (size_t)tlen);
        trunc[tlen] = '\0';
        w = engine_writes(trunc, (int)strlen(trunc));
        n = hypodb_mag_station_count(trunc, (int)strlen(trunc));
        printf("      (mensaje de %d bytes -> %d; motor escribe %d, dimensionado %d)\n",
               flen, tlen, w, n);
        CHECK(flen > 511, "el mensaje de 12 estaciones supera 511 bytes");
        CHECK(w >= 1, "el motor sigue escribiendo entradas en el truncado");
        CHECK(n == w, "truncado a 511: el dimensionado coincide");

        /* 5. El camino real. Sin BD no toca la BD; con el dimensionado corto,
              valgrind ve el Invalid write del motor. */
        hypodb_write_station_magnitudes(st, "MAG-TEST/163/ML", "ML", trunc,
                                        (size_t)strlen(trunc));
        CHECK(1, "write_station_magnitudes con mensaje truncado a 511");
    }

    hypodb_state_free(st);
    if (fails) {
        printf("\n%d FALLOS\n", fails);
        return 1;
    }
    printf("\nOK test_mag_lines\n");
    return 0;
}
