#include "csnhypodbp_mww_over.h"

#include <string.h>

/*
 * Overrides del panel `Mww` (lógica pura, sin GTK): ver csnhypodbp_mww_over.h.
 *
 * Se guarda un bit por campo tocado para que `mww_over_apply()` no pise lo que
 * viene del `.d` o de la solución elegida con valores viejos: si el usuario no
 * tocó la ventana de ondas, se aplica la del `.d` aunque el `.d` se haya
 * releído después.
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
}
