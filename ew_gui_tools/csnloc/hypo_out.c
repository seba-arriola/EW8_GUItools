/******************************************************************************
 * hypo_out.c                                                                 *
 *                                                                            *
 * Construye el mensaje TYPE_HYP2000ARC con el MISMO layout que glass2ew.c     *
 * (linea 1 de 162 chars + "$1" + lineas de fase de 114 chars + linea vacia),  *
 * para que csnhypodbp, csnrv y csnmags_toy lo consuman sin cambios.           *
 *                                                                            *
 * Referencia de layout: ew_gui_tools/glass2ew/glass2ew.c:242-370.             *
 ******************************************************************************/
#include "csnloc.h"

#ifndef RAD
#define RAD 0.017453292519943
#endif

/* Linea 1 canonica de EarthWorm: qid en 136, version en 161, eventVersion en
   178 (read_arc.h:202/207/215). Se extiende a 197 para cubrir eventVersion[19]
   (178+19). No se acorta el layout estandar. */
#define ARC_LINE1   197
#define ARC_PHASE   114

static void pad_string(char *dest, const char *src, int length)
{
    int i;
    int srclen = src ? (int)strlen(src) : 0;
    for (i = 0; i < length; i++)
        dest[i] = (i < srclen) ? src[i] : ' ';
}

/* Convierte latitud geocentrica (grados) a geografica (grados). */
static double geo_to_geographic(double geoc_deg)
{
    double g = geoc_deg * RAD;
    return atan(tan(g) / 0.993277) / RAD;
}

/* Normaliza longitud a (-180, 180]. */
static double norm_lon(double lon)
{
    while (lon <= -180.0) lon += 360.0;
    while (lon > 180.0)   lon -= 360.0;
    return lon;
}

int FormatHYP2000ARC(const HypoCandidate *h, const StationList *st,
                     const Pick *picks, const CSLocParams *cfg,
                     unsigned long event_id, unsigned int version,
                     char *buf, int buflen)
{
    char    line[256];
    char    temp[64];
    char    temp_id[16];
    char    temp_ver[24];
    double  lat_geo;
    time_t  t0;
    struct tm tmv;
    char    lat_dir, lon_dir;
    int     lat_deg, lon_deg, lat_min_100, lon_min_100;
    int     z_100, s_100, nps, gap, dmin_i, rms_100;
    int     used = 0, i;
    unsigned long final_id;

    if (!h || !st || !picks || !cfg || !buf || buflen <= 0) return -1;

    buf[0] = '\0';

    /* t0 es epoch (UTC). */
    t0 = (time_t)h->t0;
    {
        struct tm *pg = gmtime(&t0);
        if (!pg) return -1;
        tmv = *pg;
    }

    lat_geo = geo_to_geographic(h->lat);
    lat_dir = (lat_geo >= 0.0) ? 'N' : 'S';
    lat_deg = (int)fabs(lat_geo);
    lat_min_100 = (int)((fabs(lat_geo) - lat_deg) * 60.0 * 100.0 + 0.5);
    /* Evita 60.00' por redondeo. */
    if (lat_min_100 >= 6000) { lat_deg += 1; lat_min_100 -= 6000; }

    {
        double lon_geo = norm_lon(h->lon);
        lon_dir = (lon_geo >= 0.0) ? 'E' : 'W';
        lon_deg = (int)fabs(lon_geo);
        lon_min_100 = (int)((fabs(lon_geo) - lon_deg) * 60.0 * 100.0 + 0.5);
        if (lon_min_100 >= 6000) { lon_deg += 1; lon_min_100 -= 6000; }
    }

    z_100 = (int)(h->depth_km * 100.0 + 0.5);
    s_100 = (int)(tmv.tm_sec * 100.0 + 0.5);
    if (s_100 >= 6000) { s_100 -= 6000; /* el minuto ya lo maneja gmtime */ }

    nps = h->nphases;
    gap = (int)(h->gap_deg + 0.5);
    dmin_i = (int)(h->dmin_km + 0.5);
    rms_100 = (int)(h->rms_sec * 100.0 + 0.5);

    final_id = event_id % 2147000000UL;
    snprintf(temp_id, sizeof(temp_id), "%010lu", final_id);
    /* version[1] en offset 161 (digito menos significativo) y eventVersion[19]
       en offset 178 (version completa). */
    snprintf(temp_ver, sizeof(temp_ver), "%04u", version);

    /* ---- linea 1: hipocentro (ARC_LINE1 chars) ---- */
    memset(line, ' ', ARC_LINE1);
    line[ARC_LINE1] = '\n';
    line[ARC_LINE1 + 1] = '\0';

    snprintf(temp, sizeof(temp), "%04d%02d%02d%02d%02d%04d%02d%c%04d%03d%c%04d%05d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, s_100,
             lat_deg, lat_dir, lat_min_100,
             lon_deg, lon_dir, lon_min_100, z_100);
    memcpy(line, temp, strlen(temp));

    snprintf(temp, sizeof(temp), "%3d%3d%3d%4d", nps, gap, dmin_i, rms_100);
    memcpy(line + 39, temp, 13);

    memcpy(line + 136, temp_id, 10);

    /* version[1] en offset 161: digito menos significativo. */
    line[161] = (char)('0' + (version % 10));
    /* eventVersion[19] en offset 178: version completa. */
    memcpy(line + 178, temp_ver, 4);

    if ((int)strlen(buf) + ARC_LINE1 + 2 >= buflen) return -1;
    strcat(buf, line);

    /* ---- linea $1 ---- */
    memset(line, ' ', ARC_LINE1);
    line[0] = '$'; line[1] = '1';
    line[ARC_LINE1] = '\n';
    line[ARC_LINE1 + 1] = '\0';
    strcat(buf, line);

    /* ---- lineas de fase ---- */
    for (i = 0; i < h->nphases; i++) {
        int    pi = h->phase_idx[i];
        const  Pick *p;
        time_t pt;
        struct tm ptm;
        int    py, pm, pd, ph, pmn;
        double psec;
        const  char *loc_str;
        int    sidx;
        double sta_lat, sta_lon;
        char   phase_char;

        if (pi < 0 || pi >= CSLOC_MAX_PICKS) continue;
        p = &picks[pi];

        pt = (time_t)p->t_epoch;
        {
            struct tm *pg = gmtime(&pt);
            if (!pg) continue;
            ptm = *pg;
        }
        py = ptm.tm_year + 1900; pm = ptm.tm_mon + 1; pd = ptm.tm_mday;
        ph = ptm.tm_hour; pmn = ptm.tm_min; psec = (double)ptm.tm_sec;
        /* Fraccion de segundo desde el epoch original. */
        psec += (p->t_epoch - floor(p->t_epoch));

        loc_str = (p->loc[0] != '\0' && strcmp(p->loc, "--") != 0) ? p->loc : "--";
        phase_char = (p->phase == CSLOC_PHASE_S) ? 'S' : 'P';

        /* Validar que la estacion exista en la metadata. */
        sidx = Stations_Find(st, p->sta);
        sta_lat = (sidx >= 0) ? st->st[sidx].lat : 0.0;
        sta_lon = (sidx >= 0) ? st->st[sidx].lon : 0.0;
        (void)sta_lat; (void)sta_lon;

        if ((int)strlen(buf) + 3 * ARC_PHASE + 8 >= buflen) break;

        memset(line, ' ', ARC_PHASE);
        line[ARC_PHASE] = '\n';
        line[ARC_PHASE + 1] = '\0';

        pad_string(&line[0], p->sta, 5);
        pad_string(&line[5], p->net, 2);
        line[8] = ' ';
        pad_string(&line[9], p->chan, 3);
        line[13] = ' ';
        line[14] = phase_char;
        line[15] = ' ';
        line[16] = '0';

        snprintf(temp, sizeof(temp), "%04d%02d%02d%02d%02d%05.2f",
                 py, pm, pd, ph, pmn, psec);
        memcpy(&line[17], temp, 17);

        pad_string(&line[111], loc_str, 2);
        strcat(buf, line);

        /* linea de continuacion (misma estacion, $1 en 104) */
        memset(line, ' ', ARC_PHASE);
        line[ARC_PHASE] = '\n';
        line[ARC_PHASE + 1] = '\0';
        pad_string(&line[0], p->sta, 5);
        pad_string(&line[5], p->net, 2);
        line[8] = ' ';
        pad_string(&line[9], p->chan, 3);
        line[104] = '$';
        line[105] = '1';
        pad_string(&line[111], loc_str, 2);
        strcat(buf, line);

        used++;
    }
    (void)used;

    /* ---- linea final vacia ---- */
    memset(line, ' ', ARC_PHASE);
    line[ARC_PHASE] = '\n';
    line[ARC_PHASE + 1] = '\0';
    if ((int)strlen(buf) + ARC_PHASE + 2 < buflen)
        strcat(buf, line);

    return 0;
}