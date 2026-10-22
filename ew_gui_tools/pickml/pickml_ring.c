/******************************************************************************
 * pickml_ring.c — fuente de ondas en vivo desde un anillo EarthWorm.          *
 *                                                                            *
 * Lee TYPE_TRACEBUF2 de `InRing` con el cursor propio del proceso (tport),    *
 * purga el historial al arrancar (arrancamos en vivo, no re-procesando el     *
 * anillo) y entrega una PmTrace por mensaje. Este fichero NO publica nada:    *
 * el envío de picks (PublishRing) vive en pickml_main.c, que necesita además  *
 * GetModId y el tipo TYPE_PICK_SCNL.                                         *
 *                                                                            *
 * No usa logit: pickml puede correr fuera de startstop, así que aquí sólo     *
 * necesita el anillo en earthworm.d y la shm de EarthWorm.                    *
 ******************************************************************************/

#include "pickml.h"

#include <earthworm.h>
#include <transport.h>

#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif

struct PmRing {
    SHM_INFO      region;
    MSG_LOGO      logo;
    unsigned char type;
    long          key;
    int           wait_ms;
    char         *buf;
    long          bufsz;
    TRACE2_HEADER hdr;
    int32_t      *samples;
    long          cap;
    long          skipped;
};

int pm_ring_open(PmRing **out, const char *ringname, int wait_ms,
                 char *err, size_t errn)
{
    PmRing *r;
    MSG_LOGO logo;
    long     size;

    if (!out || !ringname) return PM_ERR;
    *out = NULL;

    r = (PmRing *)calloc(1, sizeof(*r));
    if (!r) {
        if (err) snprintf(err, errn, "sin memoria");
        return PM_ERR;
    }

    r->key = GetKey((char *)ringname);
    if (r->key < 0) {
        if (err) snprintf(err, errn,
            "el anillo '%s' no esta declarado en earthworm.d", ringname);
        free(r);
        return PM_ERR;
    }
    if (GetType("TYPE_TRACEBUF2", &r->type) != 0) {
        if (err) snprintf(err, errn, "TYPE_TRACEBUF2 no esta en earthworm.d");
        free(r);
        return PM_ERR;
    }

    r->bufsz = MAX_TRACEBUF_SIZ + sizeof(TRACE2_HEADER) + 64;
    r->buf   = (char *)malloc((size_t)r->bufsz);
    if (!r->buf) {
        if (err) snprintf(err, errn, "sin memoria para el buffer del anillo");
        free(r);
        return PM_ERR;
    }
    r->wait_ms = wait_ms > 0 ? wait_ms : 500;

    r->logo.instid = INST_WILDCARD;
    r->logo.mod    = MOD_WILDCARD;
    r->logo.type   = r->type;

    tport_attach(&r->region, r->key);

    /* Purga: el cursor del lector es propio, así que descartar aquí no afecta a
     * nadie. Sin esto, el primer ciclo procesaría minutos de historial (y
     * emitiría picks viejos con un lag enorme). Ojo: además de GET_MISS y
     * GET_NOTRACK, GET_MISS_LAPPED y GET_MISS_SEQGAP también traen mensaje
     * (transport.h); cortar en cualquiera de ellos dejaba historial dentro y
     * el arranque no quedaba limpio. */
    {
        int rc;

        while ((rc = tport_getmsg(&r->region, &r->logo, 1, &logo, &size,
                                  r->buf, r->bufsz)) == GET_OK ||
               rc == GET_MISS || rc == GET_NOTRACK ||
               rc == GET_MISS_LAPPED || rc == GET_MISS_SEQGAP)
            r->skipped++;
    }

    *out = r;
    return PM_OK;
}

int pm_ring_next(PmRing *r, PmTrace *tr)
{
    MSG_LOGO       logo;
    long           size = 0;
    int            rc;
    TRACE2_HEADER *h;
    long           i;

    if (!r || !tr) return PM_ERR;

    rc = tport_getmsg(&r->region, &r->logo, 1, &logo, &size, r->buf, r->bufsz);
    if (rc == GET_NONE) return 0;
    if (rc == GET_TOOBIG) { r->skipped++; return 0; }
    /* GET_MISS, GET_NOTRACK, GET_MISS_LAPPED y GET_MISS_SEQGAP SÍ traen
     * mensaje (transport.c); devolver 0 aquí lo descartaría en silencio y
     * abriría un hueco en la ventana. */
    if (rc != GET_OK && rc != GET_MISS && rc != GET_NOTRACK &&
        rc != GET_MISS_LAPPED && rc != GET_MISS_SEQGAP)
        return 0;

    if (logo.type != r->type) return 0;
    if (size < (long)sizeof(TRACE2_HEADER)) { r->skipped++; return 0; }

    h = (TRACE2_HEADER *)r->buf;
    if (h->nsamp <= 0 || h->nsamp > MAX_TRACEBUF_SIZ / 4) { r->skipped++; return 0; }
    if (!(h->samprate > 0.0)) { r->skipped++; return 0; }
    if (size < (long)(sizeof(TRACE2_HEADER) + (size_t)h->nsamp * 2)) {
        r->skipped++;
        return 0;
    }

    if (h->nsamp > r->cap) {
        int32_t *p = (int32_t *)realloc(r->samples,
                                        (size_t)h->nsamp * sizeof(int32_t));
        if (!p) return PM_ERR;
        r->samples = p;
        r->cap     = h->nsamp;
    }

    if ((h->datatype[0] == 'i' || h->datatype[0] == 's') && h->datatype[1] == '2') {
        const int16_t *d = (const int16_t *)(r->buf + sizeof(TRACE2_HEADER));
        for (i = 0; i < h->nsamp; i++) r->samples[i] = (int32_t)d[i];
    } else {
        memcpy(r->samples, r->buf + sizeof(TRACE2_HEADER),
               (size_t)h->nsamp * sizeof(int32_t));
    }

    memcpy(&r->hdr, h, sizeof(TRACE2_HEADER));

    memset(tr, 0, sizeof(*tr));
    snprintf(tr->sta,  sizeof(tr->sta),  "%s", r->hdr.sta);
    snprintf(tr->net,  sizeof(tr->net),  "%s", r->hdr.net);
    snprintf(tr->loc,  sizeof(tr->loc),  "%s", r->hdr.loc);
    snprintf(tr->chan, sizeof(tr->chan), "%s", r->hdr.chan);
    tr->t0      = r->hdr.starttime;
    tr->rate    = r->hdr.samprate;
    tr->n       = r->hdr.nsamp;
    tr->samples = r->samples;
    return 1;
}

long pm_ring_skipped(const PmRing *r)
{
    return r ? r->skipped : 0;
}

void pm_ring_close(PmRing *r)
{
    if (!r) return;
    tport_detach(&r->region);
    free(r->samples);
    free(r->buf);
    free(r);
}
