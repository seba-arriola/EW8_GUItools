/* test_buffer.c — buffer 3C: cobertura, huecos, solape, tasa y deslizamiento. */

#include "../pickml.h"

static int fails;

#define CHECK(c) do { \
    if (!(c)) { fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
} while (0)

int main(void)
{
    PmBuf   b;
    int32_t x[1000];
    float   w[3 * 600];
    int     i;

    for (i = 0; i < 1000; i++) x[i] = i + 1;

    /* --- cobertura de las tres componentes --- */
    CHECK(pm_buf_init(&b, 100.0, 1000) == PM_OK);
    CHECK(pm_buf_add(&b, PM_COMP_Z, 0.0, 100.0, x, 1000) == PM_OK);
    CHECK(pm_buf_ready(&b) < 0.0);                 /* faltan N y E */
    CHECK(pm_buf_add(&b, PM_COMP_N, 0.0, 100.0, x, 1000) == PM_OK);
    CHECK(pm_buf_ready(&b) < 0.0);
    CHECK(pm_buf_add(&b, PM_COMP_E, 0.0, 100.0, x, 1000) == PM_OK);
    CHECK(fabs(pm_buf_ready(&b) - 9.99) < 1e-6);

    CHECK(pm_buf_window(&b, 0.0, 600, 0.1, w, NULL) == PM_OK);
    CHECK(w[0] == 1.0f);
    CHECK(w[599] == 600.0f);
    CHECK(w[600] == 1.0f);                          /* componente N */
    CHECK(w[1200] == 1.0f);                         /* componente E */

    /* ventana fuera del buffer */
    CHECK(pm_buf_window(&b, 20.0, 600, 0.1, w, NULL) == PM_NODATA);

    /* --- solape idempotente: reenviar el mismo bloque no corrompe --- */
    {
        long dup0 = b.dup;
        CHECK(pm_buf_add(&b, PM_COMP_Z, 0.0, 100.0, x, 1000) == PM_OK);
        CHECK(b.dup > dup0);
        CHECK(pm_buf_window(&b, 0.0, 600, 0.1, w, NULL) == PM_OK);
        CHECK(w[0] == 1.0f);
    }

    /* --- tasa distinta: rechazo duro --- */
    CHECK(pm_buf_add(&b, PM_COMP_Z, 0.0, 50.0, x, 100) == PM_ERR);
    pm_buf_free(&b);

    /* --- hueco corto: se interpola; hueco largo: PM_GAP --- */
    {
        int32_t y[694];
        for (i = 0; i < 694; i++) y[i] = 307 + i;

        CHECK(pm_buf_init(&b, 100.0, 1000) == PM_OK);
        for (i = 0; i < 3; i++) {
            CHECK(pm_buf_add(&b, i, 0.0, 100.0, x, 300) == PM_OK);      /* 0..2.99 */
            CHECK(pm_buf_add(&b, i, 3.06, 100.0, y, 694) == PM_OK);     /* 3.06.. */
        }
        {
            long interp = -1;
            CHECK(pm_buf_window(&b, 0.0, 600, 0.10, w, &interp) == PM_OK);
            CHECK(interp == 18);                 /* 6 muestras x 3 componentes */
            /* la interpolación cae estrictamente entre vecinos (300 y 307) */
            CHECK(w[300] > 300.0f && w[300] < 307.0f);
            CHECK(pm_buf_window(&b, 0.0, 600, 0.01, w, &interp) == PM_GAP);
        }
        pm_buf_free(&b);
    }

    /* --- deslizamiento: un paquete muy posterior descarta la historia --- */
    CHECK(pm_buf_init(&b, 100.0, 200) == PM_OK);
    for (i = 0; i < 3; i++)
        CHECK(pm_buf_add(&b, i, 0.0, 100.0, x, 200) == PM_OK);
    for (i = 0; i < 3; i++)
        CHECK(pm_buf_add(&b, i, 10.0, 100.0, x, 200) == PM_OK);
    CHECK(b.t0 > 0.0);
    CHECK(pm_buf_window(&b, 0.0, 100, 0.1, w, NULL) == PM_NODATA);
    CHECK(pm_buf_window(&b, 10.0, 100, 0.1, w, NULL) == PM_OK);

    /* --- paquete más grande que el buffer: error controlado --- */
    CHECK(pm_buf_add(&b, PM_COMP_Z, 20.0, 100.0, x, 1000) == PM_ERR);
    pm_buf_free(&b);

    if (fails) { fprintf(stderr, "test_buffer: %d fallos\n", fails); return 1; }
    printf("test_buffer: OK\n");
    return 0;
}
