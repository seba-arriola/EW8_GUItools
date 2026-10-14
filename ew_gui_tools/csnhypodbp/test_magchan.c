/*
 * test_magchan - contrato de csnhypodbp_magchan_parse().
 *
 * Fija las tres cosas que rompian:
 *   1. el numero de entradas debe ser EXACTAMENTE el que `rd_chan_mag` escribe
 *      (no un conteo de '\n': el formato no parte solo por '\n');
 *   2. el array devuelto nunca queda corto (con el corto, el motor escribe fuera
 *      del heap y glibc lo detecta minutos despues con SIGABRT);
 *   3. el mensaje del llamante NO se modifica (el motor parsea in-place).
 *
 * Determinista, sin GTK ni BD. El desbordamiento/lectura de mas se ve con:
 *   make check-valgrind
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include <rw_mag.h>

#include "csnhypodbp_magchan.h"

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
    char buf[2048], saved[2048];
    MAG_CHAN_INFO *ch;
    int len, n, w;

    setlocale(LC_NUMERIC, "C");

    if (build_mag(buf, sizeof buf, 3) != 0) {
        printf("FAIL: wr_mag_scnl no serializa\n");
        return 1;
    }

    /* 1. Mensaje tal cual lo emite el motor. */
    len = (int)strlen(buf);
    w = engine_writes(buf, len);
    n = csnhypodbp_magchan_parse(buf, (size_t)len, &ch);
    printf("      (motor escribe %d, helper devuelve %d)\n", w, n);
    CHECK(w == 3, "el motor escribe 3 estaciones");
    CHECK(n == 3, "el helper devuelve 3");
    CHECK(n == w, "el helper coincide con lo que escribe el motor");
    CHECK(ch != NULL && ch[0].sta[0] != '\0', "devuelve entradas utiles");
    free(ch);

    /* 2. No debe destruir el mensaje del llamante. */
    memcpy(saved, buf, (size_t)len + 1);
    n = csnhypodbp_magchan_parse(buf, (size_t)len, &ch);
    CHECK(memcmp(saved, buf, (size_t)len) == 0,
          "no toca el mensaje del llamante");
    free(ch);

    /* 3. Sin '\n' final (el motor procesa igual la ultima entrada). */
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }
    w = engine_writes(buf, len);
    n = csnhypodbp_magchan_parse(buf, (size_t)len, &ch);
    printf("      (motor escribe %d, helper devuelve %d)\n", w, n);
    CHECK(w == 3, "sin '\\n' final: el motor sigue escribiendo 3");
    CHECK(n == w, "sin '\\n' final: el helper coincide");
    free(ch);

    /* 4. Truncado a 511 bytes (lo que hacia `store_pending_mag`): la ultima
          entrada queda partida y el conteo por '\n' se quedaba corto. */
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
        n = csnhypodbp_magchan_parse(trunc, strlen(trunc), &ch);
        printf("      (mensaje de %d bytes -> %d; motor escribe %d, helper devuelve %d)\n",
               flen, tlen, w, n);
        CHECK(flen > 511, "el mensaje de 12 estaciones supera 511 bytes");
        CHECK(w >= 1, "el motor sigue escribiendo entradas en el truncado");
        CHECK(n == w, "truncado a 511: el helper coincide");
        free(ch);
    }

    /* 5. Casos limite. */
    n = csnhypodbp_magchan_parse(NULL, 10, &ch);
    CHECK(n == -1 && ch == NULL, "msg NULL: -1 y out NULL");
    n = csnhypodbp_magchan_parse("x", 0, &ch);
    CHECK(n == -1 && ch == NULL, "len 0: -1 y out NULL");
    n = csnhypodbp_magchan_parse("solo resumen sin canales\n", 25, &ch);
    CHECK(n == 0 && ch == NULL, "sin canales: 0 y out NULL");

    if (fails) {
        printf("\n%d FALLOS\n", fails);
        return 1;
    }
    printf("\nOK test_magchan\n");
    return 0;
}
