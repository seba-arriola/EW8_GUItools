/* Estado de las cuatro magnitudes del evento vigente y regla de preferencia.
 * Replica `procesar_mensaje_mag` (csnrv.c:371-402) y `mag_preferida`
 * (csnrv.c:346-360), incluida la comparacion literal de tipos ("Mb"/"MB",
 * "Ms"/"MS") y el filtro por qid.
 */
#include <stdio.h>
#include <string.h>

#include <rw_mag.h>

#include "crv_capi.h"

void crv_mag_reset(CrvMagState *st, int qid)
{
    if (!st)
        return;
    memset(st, 0, sizeof(*st));
    st->qid = qid;
}

int crv_mag_apply(CrvMagState *st, const CrvMagMsg *m)
{
    if (!st || !m)
        return 0;
    if (m->qid == 0 || m->qid != st->qid)
        return 0;

    if (strcmp(m->type, "ML") == 0) {
        st->ml = m->mag;
        st->n_ml = m->nstations;
    } else if (strcmp(m->type, "Mb") == 0 || strcmp(m->type, "MB") == 0) {
        st->mb = m->mag;
        st->n_mb = m->nstations;
    } else if (strcmp(m->type, "Ms") == 0 || strcmp(m->type, "MS") == 0) {
        st->ms = m->mag;
        st->n_ms = m->nstations;
    } else if (strcmp(m->type, "Mwp") == 0) {
        st->mwp = m->mag;
        st->n_mwp = m->nstations;
    } else {
        return 0;
    }
    return 1;
}

void crv_mag_preferred(const CrvMagState *st, double *mag, int *nst, const char **type)
{
    if (!st) {
        if (mag) *mag = 0.0;
        if (nst) *nst = 0;
        if (type) *type = "--";
        return;
    }
    if (st->mwp >= 5.5 && st->n_mwp >= 3) {
        if (mag) *mag = st->mwp;
        if (nst) *nst = st->n_mwp;
        if (type) *type = "Mwp";
    } else if (st->ms > 0.0 && st->n_ms >= 3) {
        if (mag) *mag = st->ms;
        if (nst) *nst = st->n_ms;
        if (type) *type = "Ms";
    } else if (st->mb > 0.0 && st->n_mb >= 3) {
        if (mag) *mag = st->mb;
        if (nst) *nst = st->n_mb;
        if (type) *type = "Mb";
    } else if (st->ml > 0.0) {
        if (mag) *mag = st->ml;
        if (nst) *nst = st->n_ml;
        if (type) *type = "Ml";
    } else if (st->mwp > 0.0) {
        if (mag) *mag = st->mwp;
        if (nst) *nst = st->n_mwp;
        if (type) *type = "Mwp";
    } else {
        if (mag) *mag = 0.0;
        if (nst) *nst = 0;
        if (type) *type = "--";
    }
}

/* --- Autotest C3..C9 (oraculo de `--test-mag`) -------------------------- */

static int build_msg(char *buf, size_t n, const char *qid, int imagtype,
                     const char *type, double mag, int nstations)
{
    MAG_INFO m;

    memset(&m, 0, sizeof m);
    snprintf(m.qid, sizeof m.qid, "%s", qid);
    m.imagtype = (short)imagtype;
    snprintf(m.szmagtype, sizeof m.szmagtype, "%s", type);
    m.mag = mag;
    snprintf(m.algorithm, sizeof m.algorithm, "%s", "CSNNet");
    m.nstations = nstations;
    m.nchannels = nstations;
    m.error = 0.1;
    m.quality = 1.0;
    snprintf(m.qauthor, sizeof m.qauthor, "%s", "CL");
    if (wr_mag(&m, buf, (int)n) != 0)
        return -1;
    return 0;
}

static int feed(CrvMagState *st, const char *qid, int imagtype,
                const char *type, double mag, int nstations)
{
    char buf[1024];
    CrvMagMsg msg;

    if (build_msg(buf, sizeof buf, qid, imagtype, type, mag, nstations) != 0)
        return 0;
    if (crv_magmsg_parse(buf, strlen(buf), &msg) != 0)
        return 0;
    return crv_mag_apply(st, &msg);
}

int crv_selftest_mag(FILE *out)
{
    CrvMagState st;
    double pm;
    int ps, fails = 0;
    const char *pt;

    if (!out)
        out = stdout;

    /* C3: parseo de una linea wr_mag real (ML). */
    crv_mag_reset(&st, 42);
    if (!feed(&st, "42", 1, "ML", 4.7, 5) ||
        st.ml != 4.7 || st.n_ml != 5) {
        fprintf(out, "FAIL: C3 ML parse\n");
        fails++;
    } else {
        fprintf(out, "ok  : C3 ML parse (%.1f, %d stn)\n", st.ml, st.n_ml);
    }

    /* C4: magnitud de qid ajeno se ignora. */
    if (feed(&st, "7", 1, "Mwp", 6.0, 4) != 0 || st.mwp != 0.0) {
        fprintf(out, "FAIL: C4 qid ajeno\n");
        fails++;
    } else {
        fprintf(out, "ok  : C4 qid ajeno ignorado\n");
    }

    /* C5: regla de preferencia. */
    st.ml = 4.7; st.n_ml = 5;
    st.mwp = 6.0; st.n_mwp = 4;
    crv_mag_preferred(&st, &pm, &ps, &pt);
    if (strcmp(pt, "Mwp") != 0 || pm != 6.0) {
        fprintf(out, "FAIL: C5 Mwp>=5.5\n");
        fails++;
    } else {
        fprintf(out, "ok  : C5 Mwp preferida (%.1f)\n", pm);
    }

    st.mwp = 5.0; st.n_mwp = 4;
    crv_mag_preferred(&st, &pm, &ps, &pt);
    if (strcmp(pt, "Ml") != 0 || pm != 4.7) {
        fprintf(out, "FAIL: C5 Ml\n");
        fails++;
    } else {
        fprintf(out, "ok  : C5 Ml preferida (%.1f)\n", pm);
    }

    st.ml = 0.0; st.n_ml = 0;
    crv_mag_preferred(&st, &pm, &ps, &pt);
    if (strcmp(pt, "Mwp") != 0 || pm != 5.0) {
        fprintf(out, "FAIL: C5 Mwp fallback\n");
        fails++;
    } else {
        fprintf(out, "ok  : C5 Mwp fallback (%.1f)\n", pm);
    }

    st.mwp = 0.0; st.n_mwp = 0;
    crv_mag_preferred(&st, &pm, &ps, &pt);
    if (strcmp(pt, "--") != 0) {
        fprintf(out, "FAIL: C5 sin magnitud\n");
        fails++;
    } else {
        fprintf(out, "ok  : C5 sin magnitud -> --\n");
    }

    /* C6: cambio de qid resetea magnitudes. */
    crv_mag_reset(&st, 99);
    crv_mag_preferred(&st, &pm, &ps, &pt);
    if (strcmp(pt, "--") != 0) {
        fprintf(out, "FAIL: C6 reset\n");
        fails++;
    } else {
        fprintf(out, "ok  : C6 reset a --\n");
    }

    /* C7: parseo de Mb y Ms. */
    crv_mag_reset(&st, 42);
    if (!feed(&st, "42", 3, "Mb", 5.2, 6))
        fails++;
    if (!feed(&st, "42", 4, "Ms", 5.6, 5))
        fails++;
    if (st.mb != 5.2 || st.n_mb != 6 || st.ms != 5.6 || st.n_ms != 5) {
        fprintf(out, "FAIL: C7 Mb/Ms parse\n");
        fails++;
    } else {
        fprintf(out, "ok  : C7 Mb/Ms parse (%.1f/%.1f)\n", st.mb, st.ms);
    }

    /* C8: Ms preferida sobre Ml cuando n>=3. */
    st.ml = 4.5; st.n_ml = 6;
    crv_mag_preferred(&st, &pm, &ps, &pt);
    if (strcmp(pt, "Ms") != 0 || pm != 5.6) {
        fprintf(out, "FAIL: C8 Ms prefiere\n");
        fails++;
    } else {
        fprintf(out, "ok  : C8 Ms preferida (%.1f)\n", pm);
    }

    /* C9: Mb preferida sobre Ml cuando no hay Ms. */
    st.ms = 0.0; st.n_ms = 0;
    crv_mag_preferred(&st, &pm, &ps, &pt);
    if (strcmp(pt, "Mb") != 0 || pm != 5.2) {
        fprintf(out, "FAIL: C9 Mb prefiere\n");
        fails++;
    } else {
        fprintf(out, "ok  : C9 Mb preferida (%.1f)\n", pm);
    }

    if (fails)
        fprintf(out, "\n%d FALLOS\n", fails);
    else
        fprintf(out, "\nOK test_mag\n");
    return fails;
}
