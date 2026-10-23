/* TYPE_MAGNITUDE -> CrvMagMsg.
 *
 * `rd_mag` (rw_mag.c) parsea IN-PLACE (`tmpMsg = msg`, `line[len] = '\0'`) y
 * ademas lee 1 byte mas alla del mensaje; el buffer del anillo NO viene
 * terminado en NUL. Por eso aqui se le da una COPIA con holgura a ceros y
 * nunca el buffer original (la misma trampa que se pago en hypodb_ingest).
 */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rw_mag.h>

#include "crv_capi.h"

int crv_magmsg_parse(const char *msg, size_t len, CrvMagMsg *out)
{
    MAG_INFO mag;
    char *copy;
    int qid;
    size_t n;

    if (!msg || !out || len == 0 || len > 4096)
        return -1;

    /* rd_mag usa sscanf: el punto decimal, no la coma del locale. */
    setlocale(LC_NUMERIC, "C");

    n = (size_t)len;
    copy = calloc(1, n + 8);
    if (!copy)
        return -1;
    memcpy(copy, msg, n);

    memset(&mag, 0, sizeof(mag));
    if (rd_mag(copy, (int)n, &mag) != 0) {
        free(copy);
        return -1;
    }
    free(copy);

    qid = atoi(mag.qid);
    if (qid <= 0)
        return -1;

    out->qid = qid;
    out->mag = mag.mag;
    out->nstations = mag.nstations;
    snprintf(out->type, sizeof(out->type), "%s", mag.szmagtype);
    return 0;
}
