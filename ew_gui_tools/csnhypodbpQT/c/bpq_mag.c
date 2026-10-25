/*
 * bpq_mag.c - Magnitudes (fase 6): csnmags con el tank de trazas en memoria.
 *
 * No reimplementa nada: `csnhypodbp_mag_run` (nucleo puro del modulo GTK)
 * escribe hypos.jsonl + tank, corre `csnmags --out` con las ventanas y puertas
 * del MagConfigFile y devuelve las cuatro magnitudes. Aqui solo se resuelve el
 * entorno EW_* y se traduce el rc.
 */
#include "bpq_capi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bpq_mag_run(const BpqConfig *cfg, const CsnhypodbpSolution *base,
                const CsnhypodbpTankTrace *tank, int ntank,
                const CsnhypodbpMagOvrRow *ovr, int novr, unsigned kind_mask,
                CsnhypodbpMagOut *out, char *err, size_t errn)
{
    const char *ewp, *ewh, *ewv;

    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (err && errn) err[0] = '\0';

    if (!cfg || !base) {
        if (err && errn) snprintf(err, errn, "magnitudes: falta la solucion");
        return 0;
    }
    if (!tank || ntank <= 0) {
        if (err && errn) snprintf(err, errn, "magnitudes: no hay trazas para csnmags");
        return 0;
    }

    ewp = getenv("EW_PARAMS");
    ewh = getenv("EW_HOME");
    ewv = getenv("EW_VERSION");
    if (!ewp || !ewh || !ewv) {
        if (err && errn)
            snprintf(err, errn, "magnitudes: faltan EW_PARAMS/EW_HOME/EW_VERSION");
        return 0;
    }

    return csnhypodbp_mag_run(ewp, ewh, ewv, NULL, base, tank, ntank,
                              ovr, novr, kind_mask,
                              out, err, errn) == 0 ? 1 : 0;
}

int bpq_mag_result_ok(const CsnhypodbpMagOut *mag)
{
    return bpq_mag_result_ok_mask(mag, 0u);
}

int bpq_mag_result_ok_mask(const CsnhypodbpMagOut *mag, unsigned kind_mask)
{
    return csnhypodbp_mag_result_ok(mag, kind_mask) ? 1 : 0;
}
