#include "csnwphase.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Escritura de las ondas en SAC para el motor: reemplaza a
 * `extract.csh`/`rdseed` del flujo del CSN.
 *
 * Cabecera SAC: 632 bytes = 158 palabras de 32 bits en este orden
 *   [0..69]   float: delta, depmin, depmax, scale, odelta, b, e, o, a, fmt,
 *             t0..t9, f, resp0..resp9, stla, stlo, stel, stdp, evla, evlo,
 *             evel, evdp, mag, user0..user9, dist, az, baz, gcarc, sb, sdelta,
 *             depmen, cmpaz, cmpinc, xminimum, xmaximum, yminimum, ymaximum,
 *             unused6..unused12
 *   [70..84]  int:   nzyear, nzjday, nzhour, nzmin, nzsec, nzmsec, nvhdr,
 *                    norid, nevid, npts, nsnpts, nwfid, nxsize, nysize, unused15
 *   [85..109] int:   iftype, idep, iztype, unused16, iinst, istreg, ievreg,
 *                    ievtyp, iqual, isynth, imagtyp, imagsrc, unused19..26,
 *                    leven, lpspol, lovrok, lcalda, unused27
 *   [110..133] char: kstnm, kevnm(16), khole, ko, ka, kt0..kt9, kf, kuser0..2,
 *                    kcmpnm, knetwk, kdatrd, kinst
 *
 * Se escribe en el orden NATIVO: el lector del motor (`rhdrsac`) hace fread
 * directo, sin detección de byte order.
 */

#define SAC_WORDS 158
#define SAC_HDR_BYTES 632

/* floats */
#define F_DELTA 0
#define F_B 5
#define F_E 6
#define F_O 7
#define F_STLA 31
#define F_STLO 32
#define F_STEL 33
#define F_STDP 34
#define F_EVLA 35
#define F_EVLO 36
#define F_EVEL 37
#define F_EVDP 38
#define F_MAG 39
#define F_DIST 50
#define F_AZ 51
#define F_BAZ 52
#define F_GCARC 53
/* ints */
#define I_NZYEAR 70
#define I_NZJDAY 71
#define I_NZHOUR 72
#define I_NZMIN 73
#define I_NZSEC 74
#define I_NZMSEC 75
#define I_NVHDR 76
#define I_NPTS 79
#define I_IFTYPE 85
#define I_IDEP 86
#define I_IZTYPE 87
#define I_IINST 89
#define I_IEVTYP 92
#define I_IQUAL 93
#define I_ISYNTH 94
#define I_LEVEN 105
#define I_LPSPOL 106
#define I_LOVROK 107
#define I_LCALDA 108
/* chars (índice de palabra; cada palabra = 8 bytes) */
/* Índices de PALABRA (4 bytes) del bloque de texto del SACHEAD (sac.h:189-211).
   Ojo: 158 palabras = 632 bytes; kstnm son 2 palabras, kevnm 4 y el resto 2.
   Sacar esto mal desplaza TODOS los campos de texto siguientes. */
#define C_KSTNM 110   /* 2 palabras (440) */
#define C_KEVNM 112   /* 4 palabras (448) */
#define C_KHOLE 116   /* (464) */
#define C_KO 118      /* (472) */
#define C_KCMPNM 150  /* (600) */
#define C_KNETWK 152   /* (608) */
#define C_KDATRD 154  /* (616) */
#define C_KINST 156   /* (624) */

static void put_f(int32_t *h, int idx, float v)
{
    memcpy(&h[idx], &v, sizeof(float));
}

static void put_i(int32_t *h, int idx, int v)
{
    h[idx] = (int32_t)v;
}

/*
 * Campo de texto: se rellena con NUL (no con "-12345"). nbchar() del motor
 * cuenta hasta el último blanco, así que un NUL deja la longitud correcta y
 * una location vacía da 0 -> makeid() usa "--" (lo que espera el nombre de los
 * SAC_PZs).
 */
static void put_s(int32_t *h, int idx, int bytes, const char *s)
{
    char *p = (char *)&h[idx];
    size_t n = s ? strlen(s) : 0;

    memset(p, 0, (size_t)bytes);
    if (n >= (size_t)bytes)
        n = (size_t)bytes - 1;
    if (n)
        memcpy(p, s, n);
}

/* Día juliano del año (1..366). */
static int day_of_year(int year, int mon, int day)
{
    static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273,
                                 304, 334 };
    int doy = cum[mon - 1] + day;

    if (mon > 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        doy++;
    return doy;
}

void csnwphase_distaz(double evla, double evlo, double stla, double stlo,
                      double *dist_km, double *az, double *baz,
                      double *gcarc_deg)
{
    const double d2r = M_PI / 180.0, r2d = 180.0 / M_PI;
    double p1 = evla * d2r, p2 = stla * d2r;
    double dl = (stlo - evlo) * d2r;
    double cosd, gcarc;

    cosd = sin(p1) * sin(p2) + cos(p1) * cos(p2) * cos(dl);
    if (cosd > 1.0)
        cosd = 1.0;
    if (cosd < -1.0)
        cosd = -1.0;

    gcarc = acos(cosd) * r2d;
    if (gcarc_deg)
        *gcarc_deg = gcarc;
    if (dist_km)
        *dist_km = gcarc * 111.195;

    if (az)
        *az = atan2(sin(dl) * cos(p2),
                    cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) * r2d;
    if (baz)
        *baz = atan2(sin(-dl) * cos(p1),
                     cos(p2) * sin(p1) - sin(p2) * cos(p1) * cos(-dl)) * r2d;
}

int csnwphase_sac_write(const char *path, const CsnwphaseSAC *s)
{
    int32_t h[SAC_WORDS];
    float *data = NULL;
    FILE *f;
    struct tm tm;
    time_t t;
    double dist, az, baz, gcarc;
    long i;

    if (!path || !s || !s->data || s->npts <= 0 || s->rate <= 0.0)
        return -1;

    memset(h, 0, sizeof(h));

    csnwphase_distaz(s->evla, s->evlo, s->stla, s->stlo, &dist, &az, &baz,
                     &gcarc);

    /* Tiempo de referencia = primer sample; b = 0 y o relativo a él. */
    put_f(h, F_DELTA, (float)(1.0 / s->rate));
    put_f(h, F_B, 0.0f);
    put_f(h, F_E, (float)((s->npts - 1) / s->rate));
    put_f(h, F_O, (float)(s->origin_epoch - s->start_epoch));

    put_f(h, F_STLA, (float)s->stla);
    put_f(h, F_STLO, (float)s->stlo);
    put_f(h, F_STEL, (float)s->stel_m);
    put_f(h, F_STDP, (float)s->stdp_m);
    put_f(h, F_EVLA, (float)s->evla);
    put_f(h, F_EVLO, (float)s->evlo);
    put_f(h, F_EVEL, 0.0f);
    put_f(h, F_EVDP, (float)s->evdp_km);
    put_f(h, F_MAG, (float)s->mag);
    put_f(h, F_DIST, (float)dist);
    put_f(h, F_AZ, (float)az);
    put_f(h, F_BAZ, (float)baz);
    put_f(h, F_GCARC, (float)gcarc);

    t = (time_t)s->start_epoch;
    gmtime_r(&t, &tm);
    put_i(h, I_NZYEAR, tm.tm_year + 1900);
    put_i(h, I_NZJDAY, day_of_year(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday));
    put_i(h, I_NZHOUR, tm.tm_hour);
    put_i(h, I_NZMIN, tm.tm_min);
    put_i(h, I_NZSEC, tm.tm_sec);
    put_i(h, I_NZMSEC, 0);
    put_i(h, I_NVHDR, 6);
    put_i(h, I_NPTS, (int)s->npts);

    put_i(h, I_IFTYPE, 1);    /* ITIME */
    put_i(h, I_IDEP, 7);      /* IVEL  (BH/HH son velocidad) */
    put_i(h, I_IZTYPE, 11);   /* IO    (tiempo de referencia = origen) */
    put_i(h, I_IINST, 32);    /* ILLLBB */
    put_i(h, I_IEVTYP, 40);   /* IQUAKE */
    put_i(h, I_IQUAL, 45);    /* IGOOD */
    put_i(h, I_ISYNTH, 49);   /* IRLDTA (dato real) */
    put_i(h, I_LEVEN, 1);
    put_i(h, I_LPSPOL, 0);
    put_i(h, I_LOVROK, 1);
    put_i(h, I_LCALDA, 1);

    put_s(h, C_KSTNM, 8, s->sta);
    /* kevnm va VACÍO, como lo deja rdseed cuando el miniSEED no trae bloque de
       evento: el motor arma el id de estación como kstnm+kevnm, así que poner
       aquí la estación produce "AP01AP01" y confunde el árbol de estaciones. */
    put_s(h, C_KEVNM, 16, "");
    put_s(h, C_KHOLE, 8, s->loc);
    put_s(h, C_KO, 8, "P");
    put_s(h, C_KCMPNM, 8, s->chan);
    put_s(h, C_KNETWK, 8, s->net);
    put_s(h, C_KDATRD, 8, "");
    put_s(h, C_KINST, 8, "");

    f = fopen(path, "wb");
    if (!f)
        return -1;

    if (fwrite(h, 1, SAC_HDR_BYTES, f) != SAC_HDR_BYTES) {
        fclose(f);
        return -1;
    }

    data = malloc((size_t)s->npts * sizeof(float));
    if (!data) {
        fclose(f);
        return -1;
    }
    for (i = 0; i < s->npts; i++)
        data[i] = s->data[i];

    if (fwrite(data, sizeof(float), (size_t)s->npts, f) != (size_t)s->npts) {
        free(data);
        fclose(f);
        return -1;
    }

    free(data);
    return fclose(f) == 0 ? 0 : -1;
}
