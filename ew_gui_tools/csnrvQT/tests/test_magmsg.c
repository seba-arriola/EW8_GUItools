/* C3: TYPE_MAGNITUDE (rd_mag) y la copia defensiva del buffer.
 *
 * Fija las dos cosas que se pagan caras:
 *   - `rd_mag` destruye el buffer del llamante (parsea in-place);
 *   - el buffer del anillo NO viene terminado en NUL.
 */
#include <stdio.h>
#include <string.h>

#include <rw_mag.h>

#include "crv_capi.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);             \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

static size_t build_mag(char *buf, size_t n, const char *qid, int imagtype,
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
        return 0;
    return strlen(buf);
}

int main(void)
{
    char buf[1024];
    char orig[1024];
    CrvMagMsg msg;
    CrvMagState st;
    size_t len;

    len = build_mag(buf, sizeof buf, "42", 1, "ML", 4.7, 5);
    CHECK(len > 0, "wr_mag ML");
    memcpy(orig, buf, len);

    memset(&msg, 0, sizeof msg);
    CHECK(crv_magmsg_parse(buf, len, &msg) == 0, "parse ML");
    CHECK(msg.qid == 42, "qid");
    CHECK(msg.mag > 4.699 && msg.mag < 4.701, "mag");
    CHECK(msg.nstations == 5, "nstations");
    CHECK(strcmp(msg.type, "ML") == 0, "type");
    CHECK(memcmp(orig, buf, len) == 0,
          "el buffer del llamante no debe quedar destruido");

    /* rd_mag con el mensaje crudo (sin copia) si lo destruye: se comprueba
     * que el parser no lo hace, sin afirmar como funciona rd_mag. */
    memset(&st, 0, sizeof st);
    crv_mag_reset(&st, 42);
    CHECK(crv_mag_apply(&st, &msg) == 1, "aplica ML");
    CHECK(st.ml == 4.7 && st.n_ml == 5, "ML en el estado");

    /* qid ajeno: se ignora. */
    len = build_mag(buf, sizeof buf, "7", 1, "Mwp", 6.0, 4);
    CHECK(crv_magmsg_parse(buf, len, &msg) == 0, "parse Mwp");
    CHECK(crv_mag_apply(&st, &msg) == 0, "qid ajeno ignorado");
    CHECK(st.mwp == 0.0, "Mwp no se contamino");

    /* Tipos con las dos grafias. */
    len = build_mag(buf, sizeof buf, "42", 3, "Mb", 5.2, 6);
    CHECK(crv_magmsg_parse(buf, len, &msg) == 0, "parse Mb");
    CHECK(crv_mag_apply(&st, &msg) == 1 && st.mb == 5.2 && st.n_mb == 6, "Mb");

    len = build_mag(buf, sizeof buf, "42", 4, "Ms", 5.6, 5);
    CHECK(crv_magmsg_parse(buf, len, &msg) == 0, "parse Ms");
    CHECK(crv_mag_apply(&st, &msg) == 1 && st.ms == 5.6 && st.n_ms == 5, "Ms");

    len = build_mag(buf, sizeof buf, "42", 3, "MB", 4.9, 4);
    CHECK(crv_magmsg_parse(buf, len, &msg) == 0, "parse MB");
    CHECK(crv_mag_apply(&st, &msg) == 1 && st.mb == 4.9 && st.n_mb == 4, "MB");

    len = build_mag(buf, sizeof buf, "42", 4, "MS", 6.1, 3);
    CHECK(crv_magmsg_parse(buf, len, &msg) == 0, "parse MS");
    CHECK(crv_mag_apply(&st, &msg) == 1 && st.ms == 6.1 && st.n_ms == 3, "MS");

    /* Un tipo desconocido no toca el estado. */
    len = build_mag(buf, sizeof buf, "42", 9, "XX", 9.9, 9);
    if (len > 0) {
        CHECK(crv_magmsg_parse(buf, len, &msg) == 0, "parse XX");
        CHECK(crv_mag_apply(&st, &msg) == 0, "tipo desconocido ignorado");
    }

    if (g_fail == 0)
        printf("OK: TYPE_MAGNITUDE (copia defensiva, 5 tipos y qid ajeno)\n");
    return g_fail;
}
