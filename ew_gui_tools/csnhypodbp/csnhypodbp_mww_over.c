#include "csnhypodbp_mww_over.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Overrides del panel `Mww` (lógica pura, sin GTK): ver csnhypodbp_mww_over.h.
 *
 * Se guarda un bit por campo tocado para que `mww_over_apply()` no pise lo que
 * viene del `.d` o de la solución elegida con valores viejos: si el usuario no
 * tocó la ventana de ondas, se aplica la del `.d` aunque el `.d` se haya
 * releído después.
 *
 * Las celdas de la tabla son cajas de texto: el rango y el parseo de cada campo
 * viven aquí (`mww_field_limits`/`mww_field_parse`) para poder testearlos sin
 * GTK, y un texto inválido usa `mww_over_unset()` (ese campo vuelve al origen).
 */

#define OVER_BIT(field) (1u << (unsigned)(field))

void mww_over_init(const CsnwphaseInput *in, const CsnwphaseCfg *cfg,
                   MwwOverrides *ov)
{
    if (!in || !cfg || !ov)
        return;

    memset(ov, 0, sizeof *ov);
    ov->year      = in->year;
    ov->month     = in->month;
    ov->day       = in->day;
    ov->hour      = in->hour;
    ov->minute    = in->minute;
    ov->second    = in->second;
    ov->lat       = in->latitude;
    ov->lon       = in->longitude;
    ov->depth_km  = in->depth_km;
    ov->magnitude = in->magnitude;
    ov->win_pre_sec  = cfg->win_pre_sec;
    ov->win_post_sec = cfg->win_post_sec;
    ov->screen_th    = cfg->screen_th;
    ov->screen_nr    = cfg->screen_nr;
    ov->dmin_deg     = cfg->dmin_deg;
    ov->dmax_deg     = cfg->dmax_deg;
}

int mww_over_set(MwwOverrides *ov, int field, double value)
{
    if (!ov)
        return -1;

    switch (field) {
    case CSNWPHASE_PF_LAT:        ov->lat = value;        break;
    case CSNWPHASE_PF_LON:        ov->lon = value;        break;
    case CSNWPHASE_PF_DEPTH:      ov->depth_km = value;   break;
    case CSNWPHASE_PF_MAGNITUDE:  ov->magnitude = value;  break;
    case CSNWPHASE_PF_WIN_PRE:    ov->win_pre_sec = (int)(value + 0.5);  break;
    case CSNWPHASE_PF_WIN_POST:   ov->win_post_sec = (int)(value + 0.5); break;
    case CSNWPHASE_PF_SCREEN_TH:  ov->screen_th = value;  break;
    case CSNWPHASE_PF_SCREEN_NR:  ov->screen_nr = value;  break;
    case CSNWPHASE_PF_DMIN:       ov->dmin_deg = value;   break;
    case CSNWPHASE_PF_DMAX:       ov->dmax_deg = value;   break;
    default:
        /* NONE (solo lectura) y ORIGIN_TIME (necesita texto: set_time). */
        return -1;
    }
    ov->touched |= OVER_BIT(field);
    return 0;
}

int mww_over_set_time(MwwOverrides *ov, const char *txt)
{
    CsnwphaseInput tmp;

    if (!ov)
        return -1;

    memset(&tmp, 0, sizeof tmp);
    if (csnwphase_time_parse(txt, &tmp) != 0)
        return -1;

    ov->year   = tmp.year;
    ov->month  = tmp.month;
    ov->day    = tmp.day;
    ov->hour   = tmp.hour;
    ov->minute = tmp.minute;
    ov->second = tmp.second;
    ov->touched |= OVER_BIT(CSNWPHASE_PF_ORIGIN_TIME);
    return 0;
}

int mww_over_unset(MwwOverrides *ov, int field)
{
    if (!ov || mww_field_limits(field, NULL, NULL) != 0)
        return -1;

    ov->touched &= ~OVER_BIT(field);
    return 0;
}

int mww_field_limits(int field, double *lo, double *hi)
{
    /* Rango de cada campo numerico editable de la tabla (los mismos valores que
     * tenia el GtkSpinButton que la celda usaba antes, para que el cambio de
     * widget no mueva ni un limite). Antes vivia en la GUI, junto al spin; ahora
     * que la celda es una caja de texto, el rango y el parseo son logica pura y
     * se testean sin GTK. */
    static const struct { int field; double lo, hi; } lim[] = {
        { CSNWPHASE_PF_LAT,       -90.0,  90.0 },
        { CSNWPHASE_PF_LON,       -180.0, 360.0 },
        { CSNWPHASE_PF_DEPTH,     0.0,    700.0 },
        { CSNWPHASE_PF_MAGNITUDE, 0.0,    9.9 },
        { CSNWPHASE_PF_WIN_PRE,   0.0,    3600.0 },
        { CSNWPHASE_PF_WIN_POST,  0.0,    7200.0 },
        { CSNWPHASE_PF_SCREEN_TH, 0.0,    50.0 },
        { CSNWPHASE_PF_SCREEN_NR, 0.0,    50.0 },
        { CSNWPHASE_PF_DMIN,      0.0,    180.0 },
        { CSNWPHASE_PF_DMAX,      0.0,    180.0 },
    };
    size_t k;

    for (k = 0; k < sizeof(lim) / sizeof(lim[0]); k++) {
        if (lim[k].field != field)
            continue;
        if (lo)
            *lo = lim[k].lo;
        if (hi)
            *hi = lim[k].hi;
        return 0;
    }
    return -1;
}

int mww_field_parse(int field, const char *txt, double *out)
{
    double lo = 0.0, hi = 0.0, v;
    char buf[64], *stop, *p;
    const char *end;
    size_t n;

    if (!txt || mww_field_limits(field, &lo, &hi) != 0)
        return -1;

    /* Se acepta ',' como separador decimal (strtod solo entiende '.'), igual que
     * la barra de magnitudes. */
    n = strlen(txt);
    if (n >= sizeof buf)
        return -1;
    memcpy(buf, txt, n + 1);
    for (p = buf; *p; p++) {
        if (*p == ',')
            *p = '.';
    }

    p = buf;
    while (*p == ' ' || *p == '\t')
        p++;
    if (!*p)
        return -1;

    errno = 0;
    v = strtod(p, &stop);
    if (stop == p || errno == ERANGE || !isfinite(v))
        return -1;

    /* Nada despues del numero salvo espacios: "12abc" no es valido. */
    for (end = stop; *end == ' ' || *end == '\t'; end++)
        ;
    if (*end)
        return -1;

    if (v < lo || v > hi)
        return -1;

    if (out)
        *out = v;
    return 0;
}

void mww_over_apply(const MwwOverrides *ov, CsnwphaseInput *in, CsnwphaseCfg *cfg)
{
    if (!ov || !in || !cfg)
        return;

    if (ov->touched & OVER_BIT(CSNWPHASE_PF_ORIGIN_TIME)) {
        in->year   = ov->year;
        in->month  = ov->month;
        in->day    = ov->day;
        in->hour   = ov->hour;
        in->minute = ov->minute;
        in->second = ov->second;
        /* La hora editada manda también para el epoch (lo usan la ventana de
         * ondas y el i_master). */
        in->origin_epoch = csnwphase_epoch_utc(ov->year, ov->month, ov->day,
                                               ov->hour, ov->minute, ov->second);
    }
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_LAT))
        in->latitude = ov->lat;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_LON))
        in->longitude = ov->lon;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_DEPTH))
        in->depth_km = ov->depth_km;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_MAGNITUDE))
        in->magnitude = ov->magnitude;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_WIN_PRE))
        cfg->win_pre_sec = ov->win_pre_sec;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_WIN_POST))
        cfg->win_post_sec = ov->win_post_sec;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_SCREEN_TH))
        cfg->screen_th = ov->screen_th;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_SCREEN_NR))
        cfg->screen_nr = ov->screen_nr;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_DMIN))
        cfg->dmin_deg = ov->dmin_deg;
    if (ov->touched & OVER_BIT(CSNWPHASE_PF_DMAX))
        cfg->dmax_deg = ov->dmax_deg;
}
