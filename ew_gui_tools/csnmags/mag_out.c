#include "csnmags.h"

#include <string.h>

/*
 * mag_out.c - Publicación TYPE_MAGNITUDE (modo anillo) y emisión JSON (offline).
 */

int mag_out_publish(SHM_INFO *region, MSG_LOGO *logo, const char *event_id,
                    int imagtype, const char *szmagtype, double mag,
                    double err, double qual, double mindist, int gap, int nsta)
{
    char     buf[1024];
    MAG_INFO m;

    if (!region || !logo || !event_id || !szmagtype) return -1;
    if (nsta <= 0 || mag <= 0.0) return -1;

    memset(&m, 0, sizeof(m));
    snprintf(m.qid, sizeof(m.qid), "%s", event_id);
    snprintf(m.szmagtype, sizeof(m.szmagtype), "%s", szmagtype);
    snprintf(m.algorithm, sizeof(m.algorithm), "%s", "CSNNet");
    m.imagtype  = imagtype;
    m.mag       = mag;
    m.error     = err;
    m.quality   = qual;
    m.mindist   = mindist;
    m.azimuth   = gap;
    m.nstations = nsta;

    if (wr_mag(&m, buf, sizeof(buf)) != 0) return -1;
    return tport_putmsg(region, logo, (long)strlen(buf), buf);
}

static void json_field(FILE *out, const char *name, int has, double val, int nsta)
{
    if (has && val > 0.0) fprintf(out, "\"%s\":%.2f,\"n%s\":%d,", name, val, name, nsta);
    else                  fprintf(out, "\"%s\":null,\"n%s\":0,", name, name);
}

void mag_json_emit(FILE *out, const char *event_id, double t0, double lat,
                   double lon, double depth_km,
                   int has_ml, double ml, int nml,
                   int has_mwp, double mwp, int nmwp,
                   int has_mb, double mb, int nmb,
                   int has_ms, double ms, int nms)
{
    if (!out) return;
    fprintf(out, "{\"event\":\"%s\",\"t0\":%.3f,\"lat\":%.4f,\"lon\":%.4f,\"depth_km\":%.1f,",
            event_id, t0, lat, lon, depth_km);
    json_field(out, "ML", has_ml, ml, nml);
    json_field(out, "Mwp", has_mwp, mwp, nmwp);
    json_field(out, "Mb", has_mb, mb, nmb);
    json_field(out, "Ms", has_ms, ms, nms);
    fprintf(out, "\"done\":true}\n");
    fflush(out);
}
