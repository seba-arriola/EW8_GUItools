/******************************************************************************
 * pick_scln.c                                                                *
 *                                                                            *
 * Parseo de mensajes TYPE_PICK_SCNL provenientes de PICK_RING.               *
 *                                                                            *
 * Formato ASCII (una linea):                                                 *
 *   type mod inst seq S.C.N.L fmwt time amp1 amp2 amp3                       *
 * Ejemplo:                                                                   *
 *   8 151 255 4974 BO03.HHZ.C1.-- U1 20260309163736.490 22 0 0              *
 *                                                                            *
 * fmwt[0] = first motion ('U'/'D'/'?'),  fmwt[1] = weight ('0'..'4').        *
 ******************************************************************************/
#include "csnloc.h"

/* ------------------------------------------------------------------------- */
/* Fecha UTC (YYYYMMDDHHMMSS.sss) -> epoch segundos.                          */
/* Algoritmo days-from-civil (Howard Hinnant), sin dependencias externas.     */
/* ------------------------------------------------------------------------- */
long long CSLoc_YmdHmsToEpoch(int y, int m, int d, int hh, int mm, double ss)
{
    long long yy = y;
    unsigned long long era, yoe, doy, doe, days, secs;

    if (m <= 2) yy -= 1;
    era = (yy >= 0 ? (unsigned long long)yy : (unsigned long long)(yy - 399)) / 400;
    yoe = (unsigned long long)(yy - (long long)(era * 400));
    doy = (153ULL * (unsigned long long)(m + (m > 2 ? -3 : 9)) + 2ULL) / 5ULL
          + (unsigned long long)(d - 1);
    doe = yoe * 365ULL + yoe / 4ULL - yoe / 100ULL + doy;
    days = (long long)(era * 146097ULL + doe) - 719468LL;

    secs = (unsigned long long)((long long)(hh * 3600 + mm * 60) + (long long)ss);
    return (long long)days * 86400LL + (long long)secs;
}

int Phase_FromName(const char *name)
{
    if (!name || !name[0]) return -1;
    if (name[0] == 'P' || name[0] == 'p') return CSLOC_PHASE_P;
    if (name[0] == 'S' || name[0] == 's') return CSLOC_PHASE_S;
    return -1;
}

const char *Phase_Name(int phase)
{
    return (phase == CSLOC_PHASE_S) ? "S" : "P";
}

/* ------------------------------------------------------------------------- */
/* Parseo. Devuelve 0 si el mensaje es un pick valido, -1 si no.              */
/* ------------------------------------------------------------------------- */
int PickSCNL_Parse(const char *msg, int len, Pick *out)
{
    int   type, mod, inst, seq;
    char  scnl[64], fmwt[16], tstr[64];
    int   amp1, amp2, amp3;
    char  extra[8];
    char *p, *dot;
    int   n;

    if (!msg || !out || len <= 0) return -1;

    memset(out, 0, sizeof(*out));

    /* El quinto campo (SCNL) no puede ser mas largo que 63 chars. */
    n = sscanf(msg, "%d %d %d %d %63s %15s %63s %d %d %d",
               &type, &mod, &inst, &seq, scnl, fmwt, tstr,
               &amp1, &amp2, &amp3);
    if (n < 7) return -1;

    /* S.C.N.L  (Station.Channel.Network.Location) */
    {
        char work[64];
        char *fields[4];
        int   nf = 0;
        strncpy(work, scnl, sizeof(work) - 1);
        work[sizeof(work) - 1] = '\0';
        p = work;
        while (p && nf < 4) {
            fields[nf++] = p;
            dot = strchr(p, '.');
            if (!dot) break;
            *dot = '\0';
            p = dot + 1;
        }
        if (nf < 1 || fields[0][0] == '\0') return -1;
        strncpy(out->sta, fields[0], sizeof(out->sta) - 1);
        if (nf >= 2) strncpy(out->chan, fields[1], sizeof(out->chan) - 1);
        if (nf >= 3) strncpy(out->net,  fields[2], sizeof(out->net) - 1);
        if (nf >= 4) strncpy(out->loc,  fields[3], sizeof(out->loc) - 1);
    }

    /* first motion + weight */
    if (fmwt[0] == '\0') return -1;
    if (fmwt[1] >= '0' && fmwt[1] <= '4') out->weight = fmwt[1] - '0';
    else if (fmwt[0] >= '0' && fmwt[0] <= '4') out->weight = fmwt[0] - '0';
    else out->weight = 4;   /* desconocido -> peor peso */

    /* time YYYYMMDDHHMMSS.sss */
    {
        int y, mo, d, hh, mi;
        double ss;
        if (sscanf(tstr, "%4d%2d%2d%2d%2d%lf", &y, &mo, &d, &hh, &mi, &ss) != 6)
            return -1;
        if (y < 1900 || mo < 1 || mo > 12 || d < 1 || d > 31) return -1;
        out->t_epoch = (double)CSLoc_YmdHmsToEpoch(y, mo, d, hh, mi, ss);
    }

    /* Fase: TYPE_PICK_SCNL no siempre la trae. Campo extra opcional. */
    if (sscanf(msg, "%*d %*d %*d %*d %*63s %*15s %*63s %*d %*d %*d %7s", extra) == 1) {
        int ph = Phase_FromName(extra);
        if (ph >= 0) {
            out->phase = ph;
        } else {
            out->phase = CSLOC_PHASE_P;
        }
    } else {
        out->phase = CSLOC_PHASE_P;   /* default: P (como ew2glass) */
    }
    strncpy(out->phase_name, Phase_Name(out->phase), sizeof(out->phase_name) - 1);

    out->used = 0;
    return 0;
}
