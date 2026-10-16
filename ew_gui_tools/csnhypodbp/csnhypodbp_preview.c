#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csnhypodbp_preview.h"

const char *const csnhypodbp_preview_headers[CSNHYPODBP_PREVIEW_NVALS] = {
    "Lat", "Lon", "Depth[km]", "Nph", "RMS[s]", "ML", "Mwp", "Mb", "Ms", "Mww"
};

int csnhypodbp_mag_split(const char *txt, double *val, int *nst)
{
    char *end = NULL;
    double v;

    if (val) *val = 0.0;
    if (nst) *nst = 0;
    if (!txt || !txt[0] || txt[0] == '-') return 0;
    v = strtod(txt, &end);
    if (end == txt || !isfinite(v) || v <= 0.0) return 0;
    if (val) *val = v;
    if (end && *end == '-') {
        int n = atoi(end + 1);
        if (nst) *nst = (n > 0) ? n : 0;
    }
    return 1;
}

int csnhypodbp_mag_pref_default(const char *ml, const char *mwp,
                                const char *mb, const char *ms)
{
    double v;
    int n;

    if (csnhypodbp_mag_split(mwp, &v, &n) && n >= 3 && v >= 5.5) return 1;
    if (csnhypodbp_mag_split(ms, &v, &n) && n >= 3) return 3;
    if (csnhypodbp_mag_split(mb, &v, &n) && n >= 3) return 2;
    if (csnhypodbp_mag_split(ml, &v, &n)) return 0;
    return -1;
}

int csnhypodbp_mag_pref_ok(const int has[CSNHYPODBP_PREVIEW_MAGS], int kind)
{
    if (!has || kind < 0 || kind >= CSNHYPODBP_PREVIEW_MAGS) return 0;
    return has[kind] ? 1 : 0;
}

/* Texto tal cual, o "-" si viene vacio/NULL. */
static const char *or_dash(const char *s)
{
    return (s && s[0]) ? s : "-";
}

static void set_values(CsnhypodbpPreviewVals *v, double lat, double lon, double depth,
                       const char *nph, const char *rms, const char *ml, const char *mwp,
                       const char *mb, const char *ms, const char *mww)
{
    snprintf(v->val[0], sizeof(v->val[0]), "%.3f", lat);
    snprintf(v->val[1], sizeof(v->val[1]), "%.3f", lon);
    snprintf(v->val[2], sizeof(v->val[2]), "%.0f", depth);
    snprintf(v->val[3], sizeof(v->val[3]), "%s", or_dash(nph));
    snprintf(v->val[4], sizeof(v->val[4]), "%s", or_dash(rms));
    snprintf(v->val[5], sizeof(v->val[5]), "%s", or_dash(ml));
    snprintf(v->val[6], sizeof(v->val[6]), "%s", or_dash(mwp));
    snprintf(v->val[7], sizeof(v->val[7]), "%s", or_dash(mb));
    snprintf(v->val[8], sizeof(v->val[8]), "%s", or_dash(ms));
    snprintf(v->val[9], sizeof(v->val[9]), "%s", or_dash(mww));
}

void csnhypodbp_preview_clear(CsnhypodbpPreviewVals *v)
{
    int i;
    if (!v) return;
    memset(v, 0, sizeof(*v));
    snprintf(v->title, sizeof(v->title), "No solution selected");
    for (i = 0; i < CSNHYPODBP_PREVIEW_NVALS; i++)
        snprintf(v->val[i], sizeof(v->val[i]), "-");
}

void csnhypodbp_preview_from_candidate(CsnhypodbpPreviewVals *v, const char *locator,
                                       double lat, double lon, double depth, int nph,
                                       double rms, const char *ml, const char *mwp,
                                       const char *mb, const char *ms, const char *mww)
{
    char nph_s[16], rms_s[16];
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->candidate = 1;
    snprintf(v->title, sizeof(v->title), "New solution · %s", or_dash(locator));
    snprintf(nph_s, sizeof(nph_s), "%d", nph);
    snprintf(rms_s, sizeof(rms_s), "%.2f", rms);
    set_values(v, lat, lon, depth, nph_s, rms_s, ml, mwp, mb, ms, mww);
}

void csnhypodbp_preview_from_selected(CsnhypodbpPreviewVals *v, double lat, double lon,
                                      double depth, const char *rms, const char *nph,
                                      const char *ml, const char *mwp, const char *mb,
                                      const char *ms, const char *mww)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    snprintf(v->title, sizeof(v->title), "Selected solution");
    set_values(v, lat, lon, depth, nph, rms, ml, mwp, mb, ms, mww);
}
