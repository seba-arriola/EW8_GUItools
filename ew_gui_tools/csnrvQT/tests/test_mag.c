/* C4: regla de magnitud preferida (csnrv.c:346-360). */
#include <stdio.h>
#include <string.h>

#include "crv_capi.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);             \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

static void expect(const CrvMagState *st, const char *type, double mag)
{
    double m = -1.0;
    int n = -1;
    const char *t = NULL;

    crv_mag_preferred(st, &m, &n, &t);
    if (strcmp(t, type) != 0 || (mag > 0.0 && m != mag)) {
        printf("FAIL: preferida esperada %s %.1f, obtenida %s %.1f\n",
               type, mag, t, m);
        g_fail = 1;
    }
}

int main(void)
{
    CrvMagState st;

    /* Mwp >= 5.5 y n >= 3 gana a todo. */
    memset(&st, 0, sizeof st);
    crv_mag_reset(&st, 42);
    st.ml = 4.7; st.n_ml = 5;
    st.mwp = 6.0; st.n_mwp = 4;
    expect(&st, "Mwp", 6.0);

    /* Mwp < 5.5 -> Ml. */
    st.mwp = 5.0;
    expect(&st, "Ml", 4.7);

    /* Sin Ml -> Mwp de respaldo. */
    st.ml = 0.0; st.n_ml = 0;
    expect(&st, "Mwp", 5.0);

    /* Nada -> "--". */
    st.mwp = 0.0; st.n_mwp = 0;
    expect(&st, "--", 0.0);

    /* Ms con n>=3 gana a Ml. */
    st.ml = 4.5; st.n_ml = 6;
    st.ms = 5.6; st.n_ms = 5;
    expect(&st, "Ms", 5.6);

    /* Sin Ms -> Mb con n>=3. */
    st.ms = 0.0; st.n_ms = 0;
    st.mb = 5.2; st.n_mb = 6;
    expect(&st, "Mb", 5.2);

    /* Mb con menos de 3 estaciones NO se prefiere: cae a Ml. */
    st.n_mb = 2;
    expect(&st, "Ml", 4.5);

    /* Ms con menos de 3 tampoco. */
    st.ms = 5.6; st.n_ms = 2;
    st.mb = 0.0; st.n_mb = 0;
    expect(&st, "Ml", 4.5);

    /* Reset del qid limpia todo. */
    crv_mag_reset(&st, 99);
    expect(&st, "--", 0.0);

    if (g_fail == 0)
        printf("OK: magnitud preferida (Mwp/Ms/Mb/Ml y umbral de estaciones)\n");
    return g_fail;
}
