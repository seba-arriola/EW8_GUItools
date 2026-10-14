#include "csnwphase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Puente de respuestas instrumentales: los .pz del repo (SAC PZ en texto:
 * CONSTANT/ZEROS/POLES, tal como los escribe csnmags/tools/fetch_responses.py)
 * se reescriben como los ficheros que consume el motor.
 *
 * Contrato del NOMBRE (crítico, verificado en el código del motor):
 *   make_resp_lookup_table::pzfile2id exige EXACTAMENTE 7 guiones bajos y arma
 *   el id  NET_STA_LOC_CHAN  (con "--" si no hay location); es justo el id que
 *   consulta rec_dec_filt::makeid. El orden de campos debe ser
 *   <NET>_<STA>_<CHAN>_<LOC>_<X>_<Y>: así el 3.er campo termina en la letra de
 *   la componente (lo que también exige el glob de prepare_wp.csh) y el id sale
 *   en el orden que el motor busca. Con otro orden el canal se descarta EN
 *   SILENCIO.
 *
 * Contrato del CONTENIDO (make_resp_lookup_table::readpzfile):
 *   - primera línea ZEROS <n> + n pares "re im"
 *   - primera línea POLES <n> + n pares
 *   - primera línea CONSTANT <double>
 *   - al menos un cero con |z| <= 1e-5 (quita el cero del origen: displacement
 *     -> velocity); los .pz de instrumentos de velocidad lo cumplen.
 *   - las líneas se leen con fgets(...,32): se re-emiten con ancho 12 para no
 *     pasar de 31 caracteres.
 *
 * Unidades: rdseed escribe los SAC PZ en counts/m; los .pz del repo están en
 * nm (ResponseInNm 1). Se escala por 1e9 para dejarlos en counts/m. El factor
 * es configurable porque sólo una corrida real lo confirma del todo (AC6).
 */

typedef struct {
    double re, im;
} PzPair;

static int read_pz(const char *path, double *constant, PzPair *zeros, int *nz,
                   PzPair *poles, int *np)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int i, n;

    if (!f)
        return -1;

    *nz = 0;
    *np = 0;
    *constant = 1.0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "CONSTANT", 8) == 0) {
            sscanf(line + 8, "%lf", constant);
        } else if (strncmp(line, "ZEROS", 5) == 0) {
            if (sscanf(line + 5, "%d", &n) != 1 || n < 0 || n > CSNWPHASE_MAX_PZ)
                break;
            for (i = 0; i < n; i++) {
                if (fscanf(f, "%lf %lf", &zeros[i].re, &zeros[i].im) != 2)
                    break;
            }
            *nz = i;
        } else if (strncmp(line, "POLES", 5) == 0) {
            if (sscanf(line + 5, "%d", &n) != 1 || n < 0 || n > CSNWPHASE_MAX_PZ)
                break;
            for (i = 0; i < n; i++) {
                if (fscanf(f, "%lf %lf", &poles[i].re, &poles[i].im) != 2)
                    break;
            }
            *np = i;
        }
    }

    fclose(f);
    return (*nz > 0 && *np > 0) ? 0 : -1;
}

/* Patrón de nombre de los .pz del repo: %S %C %N %L. */
static void resp_name(char *out, size_t n, const char *pattern, const char *net,
                      const char *sta, const char *chan, const char *loc)
{
    const char *p = pattern;
    size_t len = 0;

    out[0] = '\0';
    while (*p && len + 1 < n) {
        const char *rep = NULL;
        char buf[64];

        if (*p == '%' && p[1]) {
            switch (p[1]) {
            case 'S': rep = sta; break;
            case 'C': rep = chan; break;
            case 'N': rep = net; break;
            case 'L': rep = (loc && loc[0]) ? loc : "--"; break;
            default: break;
            }
            if (rep) {
                snprintf(buf, sizeof(buf), "%s", rep);
                len += (size_t)snprintf(out + len, n - len, "%s", buf);
                p += 2;
                continue;
            }
        }
        out[len++] = *p++;
        out[len] = '\0';
    }
}

int csnwphase_pz_write(const CsnwphaseCfg *cfg, const char *dir, const char *net,
                       const char *sta, const char *chan, const char *loc,
                       char *out_path, size_t outsz)
{
    PzPair zeros[CSNWPHASE_MAX_PZ], poles[CSNWPHASE_MAX_PZ];
    char src[1024], dst[1200], name[128];
    double constant;
    int nz = 0, np = 0, i;
    FILE *f;

    if (!cfg || !dir || !net || !sta || !chan)
        return -1;

    snprintf(name, sizeof(name), "%s", cfg->resp_pattern);
    resp_name(src, sizeof(src), name, net, sta, chan, loc);

    {
        char full[1200];
        snprintf(full, sizeof(full), "%s/%s", cfg->resp_dir, src);
        if (read_pz(full, &constant, zeros, &nz, poles, &np) != 0)
            return -1;
    }

    if (cfg->resp_in_nm)
        constant *= 1.0e9;   /* nm -> counts/m (convención de rdseed) */

    snprintf(dst, sizeof(dst),
             "%s/SAC_PZs_%s_%s_%s_%s_2000.001_2100.001", dir, net, sta, chan,
             (loc && loc[0]) ? loc : "--");

    f = fopen(dst, "w");
    if (!f)
        return -1;

    fprintf(f, "* SAC PZ csnwphase\n");
    fprintf(f, "* id: %s_%s_%s_%s\n", net, sta, (loc && loc[0]) ? loc : "--", chan);
    fprintf(f, "CONSTANT %.9e\n", constant);
    fprintf(f, "ZEROS %d\n", nz);
    for (i = 0; i < nz; i++)
        fprintf(f, "%12.6e %12.6e\n", zeros[i].re, zeros[i].im);
    fprintf(f, "POLES %d\n", np);
    for (i = 0; i < np; i++)
        fprintf(f, "%12.6e %12.6e\n", poles[i].re, poles[i].im);

    if (fclose(f) != 0)
        return -1;

    if (out_path && outsz)
        snprintf(out_path, outsz, "%s", dst);

    return 0;
}
