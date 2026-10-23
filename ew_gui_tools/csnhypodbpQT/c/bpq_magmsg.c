/*
 * bpq_magmsg.c - Lectura y escritura de TYPE_MAGNITUDE sin GTK.
 *
 * TRAMPA (AGENTS §7, pagada en hypodb_ingest): `rd_mag` parsea IN-PLACE (sustituye
 * '\n' por '\0', destruyendo el buffer del llamante) y puede leer 1 byte mas alla
 * del mensaje. Aqui siempre se trabaja sobre una copia NUL-terminada con holgura.
 * Para las entradas por estacion se delega en csnhypodbp_magchan.c, que ya hace
 * ese trabajo (y es el unico sitio que llama a rd_chan_mag).
 */
#include "bpq_capi.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include <rw_mag.h>

/* Imagtype de EarthWorm por nombre de magnitud (ws.c:1754-1761). */
static int mag_imagtype(const char *type)
{
    if (!type) return 0;
    if (!strcmp(type, "ML") || !strcmp(type, "Ml"))   return 1;
    if (!strcmp(type, "Mwp") || !strcmp(type, "MWP")) return 5;
    if (!strcmp(type, "Mb") || !strcmp(type, "MB"))   return 3;
    if (!strcmp(type, "Ms") || !strcmp(type, "MS"))   return 4;
    return 0;
}

int bpq_magmsg_parse(const char *raw, size_t len, BpqMagMsg *out)
{
    MAG_INFO mag;
    char    *copy;

    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!raw || len == 0) return -1;

    /* rd_mag usa sscanf: con locale no-C "3.4" no parsea. */
    setlocale(LC_NUMERIC, "C");

    copy = (char *)calloc(len + 64, 1);   /* holgura a cero: corta la lectura extra */
    if (!copy) return -1;
    memcpy(copy, raw, len);

    memset(&mag, 0, sizeof(mag));
    if (rd_mag(copy, (int)len, &mag) < 0) {
        free(copy);
        return -1;
    }
    free(copy);

    out->qid     = atoi(mag.qid);
    out->mod     = (int)mag.origin_version;
    out->version = mag.qdds_version;
    out->value   = mag.mag;
    out->nsta    = mag.nstations;
    snprintf(out->type, sizeof(out->type), "%s", mag.szmagtype);
    snprintf(out->algorithm, sizeof(out->algorithm), "%s", mag.algorithm);
    return 0;
}

int bpq_mag_msg_format(char *out, size_t n, const BpqMagMsg *m, const char *text)
{
    MAG_INFO mi;
    double   val = 0.0;
    int      nst = 0;
    int      imagtype;

    if (!out || n == 0 || !m) return -1;
    out[0] = '\0';
    if (!text || !text[0]) return -1;
    if (!csnhypodbp_mag_split(text, &val, &nst)) return -1;

    imagtype = mag_imagtype(m->type);
    if (imagtype == 0) return -1;

    memset(&mi, 0, sizeof(mi));
    snprintf(mi.qid, sizeof(mi.qid), "%010d", m->qid);
    snprintf(mi.szmagtype, sizeof(mi.szmagtype), "%s", m->type);
    snprintf(mi.algorithm, sizeof(mi.algorithm), "%s", "CSNMan");
    mi.imagtype       = imagtype;
    mi.mag            = val;
    mi.quality        = 1.0;
    mi.nstations      = (nst > 0) ? nst : 1;
    mi.nchannels      = 0;
    mi.origin_version = (unsigned)m->mod;

    if (wr_mag_scnl(&mi, out, (int)n) < 0) return -1;
    return 0;
}
