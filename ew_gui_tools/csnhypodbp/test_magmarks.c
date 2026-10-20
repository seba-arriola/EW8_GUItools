/*
 * Test headless de las ventanas por (estacion, magnitud) (ver
 * csnhypodbp_magmarks.h): default vs ajustada, uso por magnitud, tope del
 * contenedor y geometria del agarre de la franja.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnhypodbp_magcfg.h"   /* CSNHYPODBP_MAG_ML/MWP/MB/MS */
#include "csnhypodbp_magmarks.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static void test_store(void)
{
    CsnhypodbpMagMarks m;
    double t0 = 0, t1 = 0;

    csnhypodbp_magmarks_init(&m);
    CHECK(m.n == 0 && m.v == NULL, "marks: arranca vacio");

    /* Sin marca: la ventana es la default del .d. */
    csnhypodbp_magmarks_window(&m, "GO01", CSNHYPODBP_MAG_ML, 10.0, 45.0, &t0, &t1);
    CHECK(fabs(t0 - 10.0) < 1e-9 && fabs(t1 - 45.0) < 1e-9,
          "marks: sin marca usa la default");
    CHECK(csnhypodbp_magmarks_touched(&m, "GO01", CSNHYPODBP_MAG_ML) == 0,
          "marks: sin marca no esta ajustada");
    CHECK(csnhypodbp_magmarks_used(&m, "GO01", CSNHYPODBP_MAG_ML, 1) == 1,
          "marks: el uso por defecto lo pone el llamador");

    /* Ajustada: solo esa (estacion, magnitud). */
    CHECK(csnhypodbp_magmarks_set(&m, "GO01", CSNHYPODBP_MAG_ML, 5.0, 30.0) == 0,
          "marks: fija la ventana");
    csnhypodbp_magmarks_window(&m, "GO01", CSNHYPODBP_MAG_ML, 10.0, 45.0, &t0, &t1);
    CHECK(fabs(t0 - 5.0) < 1e-9 && fabs(t1 - 30.0) < 1e-9,
          "marks: la ajustada gana sobre la default");
    csnhypodbp_magmarks_window(&m, "GO01", CSNHYPODBP_MAG_MS, 0.0, 400.0, &t0, &t1);
    CHECK(fabs(t0 - 0.0) < 1e-9 && fabs(t1 - 400.0) < 1e-9,
          "marks: las otras magnitudes siguen con la default");
    csnhypodbp_magmarks_window(&m, "GO02", CSNHYPODBP_MAG_ML, 10.0, 45.0, &t0, &t1);
    CHECK(fabs(t0 - 10.0) < 1e-9, "marks: las otras estaciones siguen con la default");
    CHECK(csnhypodbp_magmarks_touched(&m, "GO01", CSNHYPODBP_MAG_ML) == 1,
          "marks: queda marcada como ajustada");
    CHECK(csnhypodbp_magmarks_touched_count(&m) == 1, "marks: cuenta las ajustadas");

    /* Uso explicito por (estacion, magnitud). */
    CHECK(csnhypodbp_magmarks_set_used(&m, "GO01", CSNHYPODBP_MAG_ML, 0) == 0,
          "marks: fija el uso");
    CHECK(csnhypodbp_magmarks_used(&m, "GO01", CSNHYPODBP_MAG_ML, 1) == 0,
          "marks: el uso fijado gana sobre el default");
    CHECK(csnhypodbp_magmarks_used(&m, "GO01", CSNHYPODBP_MAG_MB, 1) == 1,
          "marks: el uso de otra magnitud sigue por defecto");

    /* Entradas invalidas. */
    CHECK(csnhypodbp_magmarks_set(&m, "GO01", CSNHYPODBP_MAG_ML, 30.0, 5.0) == -1,
          "marks: t1 <= t0 -> -1");
    CHECK(csnhypodbp_magmarks_set(&m, "GO01", CSNHYPODBP_MAG_ML, NAN, 5.0) == -1,
          "marks: NaN -> -1");
    CHECK(csnhypodbp_magmarks_set(&m, "GO01", 9, 0.0, 5.0) == -1,
          "marks: magnitud fuera de rango -> -1");
    CHECK(csnhypodbp_magmarks_set(&m, "", CSNHYPODBP_MAG_ML, 0.0, 5.0) == -1,
          "marks: estacion vacia -> -1");
    csnhypodbp_magmarks_window(NULL, "GO01", CSNHYPODBP_MAG_ML, 1.0, 2.0, &t0, &t1);
    CHECK(fabs(t0 - 1.0) < 1e-9 && fabs(t1 - 2.0) < 1e-9,
          "marks: contenedor NULL -> defaults");
    csnhypodbp_magmarks_window(&m, "GO01", CSNHYPODBP_MAG_ML, 1.0, 2.0, NULL, NULL);
    CHECK(1, "marks: ventana con salidas NULL no rompe");

    /* Limpiar (cambio de solucion) no libera el buffer. */
    csnhypodbp_magmarks_clear(&m);
    CHECK(m.n == 0 && m.v != NULL, "marks: clear vacia sin liberar");
    csnhypodbp_magmarks_window(&m, "GO01", CSNHYPODBP_MAG_ML, 10.0, 45.0, &t0, &t1);
    CHECK(fabs(t0 - 10.0) < 1e-9, "marks: tras clear vuelve la default");
    csnhypodbp_magmarks_free(&m);
    CHECK(m.v == NULL && m.n == 0, "marks: free deja limpio");
}

static void test_tope(void)
{
    CsnhypodbpMagMarks m;
    char sta[16];
    int i;

    csnhypodbp_magmarks_init(&m);
    for (i = 0; i < CSNHYPODBP_MAGMARKS_MAX; i++) {
        snprintf(sta, sizeof(sta), "S%04d", i);
        if (csnhypodbp_magmarks_set(&m, sta, CSNHYPODBP_MAG_ML, 1.0, 2.0) != 0) {
            CHECK(0, "marks: cabe hasta el tope");
            csnhypodbp_magmarks_free(&m);
            return;
        }
    }
    CHECK(m.n == CSNHYPODBP_MAGMARKS_MAX, "marks: llena hasta el tope");
    CHECK(csnhypodbp_magmarks_set(&m, "OTRA", CSNHYPODBP_MAG_ML, 1.0, 2.0) == -1,
          "marks: pasado el tope -> -1");
    /* La estacion que ya estaba se sigue actualizando aunque el tope este lleno. */
    CHECK(csnhypodbp_magmarks_set(&m, "S0000", CSNHYPODBP_MAG_MS, 0.0, 400.0) == 0,
          "marks: el tope no impide actualizar una existente");
    csnhypodbp_magmarks_free(&m);
}

static void test_grab(void)
{
    CHECK(csnhypodbp_magmarks_grab(100.0, 100.0, 200.0, 4.0) == CSNHYPODBP_MAGG_T0,
          "grab: borde izquierdo");
    CHECK(csnhypodbp_magmarks_grab(200.0, 100.0, 200.0, 4.0) == CSNHYPODBP_MAGG_T1,
          "grab: borde derecho");
    CHECK(csnhypodbp_magmarks_grab(150.0, 100.0, 200.0, 4.0) == CSNHYPODBP_MAGG_BOTH,
          "grab: dentro mueve la franja");
    CHECK(csnhypodbp_magmarks_grab(90.0, 100.0, 200.0, 4.0) == CSNHYPODBP_MAGG_NONE,
          "grab: fuera de la tolerancia");
    CHECK(csnhypodbp_magmarks_grab(103.0, 100.0, 200.0, 4.0) == CSNHYPODBP_MAGG_T0,
          "grab: dentro de la tolerancia del borde");
    CHECK(csnhypodbp_magmarks_grab(150.0, 148.0, 152.0, 4.0) == CSNHYPODBP_MAGG_BOTH,
          "grab: franja mas angosta que 2*tol -> franja completa");
    CHECK(csnhypodbp_magmarks_grab(100.0, 200.0, 100.0, 4.0) == CSNHYPODBP_MAGG_T0,
          "grab: bordes invertidos se normalizan");
    CHECK(csnhypodbp_magmarks_grab(NAN, 100.0, 200.0, 4.0) == CSNHYPODBP_MAGG_NONE,
          "grab: x NaN -> NONE");
    CHECK(csnhypodbp_magmarks_grab(150.0, NAN, 200.0, 4.0) == CSNHYPODBP_MAGG_NONE,
          "grab: franja sin ventana -> NONE");
    CHECK(csnhypodbp_magmarks_grab(150.0, 100.0, 200.0, 0.0) == CSNHYPODBP_MAGG_BOTH,
          "grab: tolerancia 0 y clic dentro");
    CHECK(csnhypodbp_magmarks_grab(99.0, 100.0, 200.0, 0.0) == CSNHYPODBP_MAGG_NONE,
          "grab: tolerancia 0 y clic fuera");
}

static void test_handle(void)
{
    /* El handle es la franja SUPERIOR: solo ahi se arrastra; el resto del canvas
       queda para el zoom por clics. */
    CHECK(csnhypodbp_magmarks_handle_hit(24.0, 24.0) == 1,
          "handle: borde superior");
    CHECK(csnhypodbp_magmarks_handle_hit(24.0 + CSNHYPODBP_MAG_HANDLE_H / 2.0, 24.0) == 1,
          "handle: en medio");
    CHECK(csnhypodbp_magmarks_handle_hit(24.0 + CSNHYPODBP_MAG_HANDLE_H, 24.0) == 1,
          "handle: borde inferior");
    CHECK(csnhypodbp_magmarks_handle_hit(24.0 + CSNHYPODBP_MAG_HANDLE_H + 1.0, 24.0) == 0,
          "handle: justo debajo no agarra");
    CHECK(csnhypodbp_magmarks_handle_hit(200.0, 24.0) == 0,
          "handle: sobre la traza no agarra");
    CHECK(csnhypodbp_magmarks_handle_hit(10.0, 24.0) == 0,
          "handle: arriba del handle no agarra");
    CHECK(csnhypodbp_magmarks_handle_hit(NAN, 24.0) == 0,
          "handle: y NaN -> 0");
}

static void test_ovr_write(void)
{
    const char *path = "/tmp/csnhypodbp_mag_ovr_test.txt";
    CsnhypodbpMagOvrRow r[4];
    char line[256];
    FILE *fp;
    int n, got = 0;

    memset(r, 0, sizeof(r));
    snprintf(r[0].sta, sizeof(r[0].sta), "GO01");
    r[0].kind = CSNHYPODBP_MAG_MS; r[0].t0 = 1000.5; r[0].t1 = 1400.5;
    snprintf(r[1].sta, sizeof(r[1].sta), "MT14");
    r[1].kind = CSNHYPODBP_MAG_ML; r[1].t0 = 10.0; r[1].t1 = 10.0;   /* t1 <= t0 */
    snprintf(r[2].sta, sizeof(r[2].sta), "GO02");
    r[2].kind = 99; r[2].t0 = 1.0; r[2].t1 = 2.0;                    /* kind fuera */
    /* r[3] sin estacion: se salta. */

    n = csnhypodbp_mag_ovr_write(path, r, 4);
    CHECK(n == 1, "ovr: solo se escribe la fila valida");

    fp = fopen(path, "r");
    CHECK(fp != NULL, "ovr: el archivo se crea");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (line[0] == '#') continue;
            got++;
            if (got == 1)
                CHECK(strncmp(line, "GO01 3 1000.500000 1400.500000", 30) == 0,
                      "ovr: formato `STA KIND T0 T1`");
        }
        fclose(fp);
    }
    CHECK(got == 1, "ovr: una sola linea de datos");
    remove(path);

    CHECK(csnhypodbp_mag_ovr_write("/no/existe/dir/x.txt", r, 4) == -1,
          "ovr: ruta invalida -> -1");
    CHECK(csnhypodbp_mag_ovr_write(NULL, r, 4) == -1, "ovr: path NULL -> -1");
}

static void test_anchor_ovr(void)
{
    CsnhypodbpMagMarks m;
    CsnhypodbpMagAnchor a[2];
    CsnhypodbpMagOvrRow r[16];
    const double def0[CSNHYPODBP_MAGMARKS_KIND] = { 10.0, 0.0, 0.0, 0.0 };
    const double def1[CSNHYPODBP_MAGMARKS_KIND] = { 45.0, 95.0, 30.0, 400.0 };
    int n, i, ml = -1, ms = -1, ml2 = -1, ms2 = -1;

    csnhypodbp_magmarks_init(&m);
    csnhypodbp_magmarks_set(&m, "GO01", CSNHYPODBP_MAG_ML, 12.0, 47.0);
    memset(a, 0, sizeof(a));
    snprintf(a[0].sta, sizeof(a[0].sta), "GO01"); a[0].anchor_t = 1000.0;
    snprintf(a[1].sta, sizeof(a[1].sta), "GO02"); a[1].anchor_t = 2000.0;

    n = csnhypodbp_mag_ovr_build_anchors(&m, a, 2, def0, def1, 0u, r, 16);
    CHECK(n == 8, "anchor_ovr: 2 estaciones x 4 magnitudes");
    for (i = 0; i < n; i++) {
        if (strcmp(r[i].sta, "GO01") == 0 && r[i].kind == CSNHYPODBP_MAG_ML) ml = i;
        if (strcmp(r[i].sta, "GO01") == 0 && r[i].kind == CSNHYPODBP_MAG_MS) ms = i;
        if (strcmp(r[i].sta, "GO02") == 0 && r[i].kind == CSNHYPODBP_MAG_ML) ml2 = i;
        if (strcmp(r[i].sta, "GO02") == 0 && r[i].kind == CSNHYPODBP_MAG_MS) ms2 = i;
    }
    CHECK(ml >= 0 && fabs(r[ml].t0 - 1012.0) < 1e-9 && fabs(r[ml].t1 - 1047.0) < 1e-9,
          "anchor_ovr: usa la ventana ajustada de GO01/ML");
    CHECK(ms >= 0 && fabs(r[ms].t0 - 1000.0) < 1e-9 && fabs(r[ms].t1 - 1400.0) < 1e-9,
          "anchor_ovr: GO01/Ms usa el default del .d");
    CHECK(ml2 >= 0 && fabs(r[ml2].t0 - 2010.0) < 1e-9 && fabs(r[ml2].t1 - 2045.0) < 1e-9,
          "anchor_ovr: GO02/ML ancla en su P");
    CHECK(ms2 >= 0 && fabs(r[ms2].t0 - 2000.0) < 1e-9 && fabs(r[ms2].t1 - 2400.0) < 1e-9,
          "anchor_ovr: GO02/Ms ancla en su P");

    n = csnhypodbp_mag_ovr_build_anchors(&m, a, 2, def0, def1,
                                         1u << CSNHYPODBP_MAG_MS, r, 16);
    CHECK(n == 2 && r[0].kind == CSNHYPODBP_MAG_MS && r[1].kind == CSNHYPODBP_MAG_MS,
          "anchor_ovr: la mascara filtra por magnitud");

    a[0].anchor_t = 0.0;
    n = csnhypodbp_mag_ovr_build_anchors(&m, a, 2, def0, def1, 0u, r, 16);
    CHECK(n == 4, "anchor_ovr: sin ancla no hay filas");

    csnhypodbp_magmarks_free(&m);
}

static void test_winsrc(void)
{
    CsnhypodbpMagMarks m;

    csnhypodbp_magmarks_init(&m);

    /* Sin marca: A si el ancla es observada, Teo si es teorica. */
    CHECK(csnhypodbp_magmarks_winsrc(&m, "GO01", CSNHYPODBP_MAG_ML, 'A') == 'A',
          "winsrc: sin marca y ancla automatica -> A");
    CHECK(csnhypodbp_magmarks_winsrc(&m, "GO01", CSNHYPODBP_MAG_ML, 'M') == 'A',
          "winsrc: sin marca el origen del pick no cambia la ventana");
    CHECK(csnhypodbp_magmarks_winsrc(&m, "GO01", CSNHYPODBP_MAG_ML, 'T') == 'T',
          "winsrc: sin marca y ancla teorica -> Teo");
    CHECK(csnhypodbp_magmarks_winsrc(&m, "GO01", CSNHYPODBP_MAG_ML, '\0') == 'A',
          "winsrc: sin ancla -> A");

    /* Con marca manda la marca (solo esa magnitud). */
    csnhypodbp_magmarks_set(&m, "GO01", CSNHYPODBP_MAG_MS, 0.0, 300.0);
    CHECK(csnhypodbp_magmarks_winsrc(&m, "GO01", CSNHYPODBP_MAG_MS, 'A') == 'M',
          "winsrc: ventana ajustada -> M");
    CHECK(csnhypodbp_magmarks_winsrc(&m, "GO01", CSNHYPODBP_MAG_MS, 'T') == 'M',
          "winsrc: la marca gana sobre el ancla teorica");
    CHECK(csnhypodbp_magmarks_winsrc(&m, "GO01", CSNHYPODBP_MAG_ML, 'A') == 'A',
          "winsrc: otra magnitud sigue automatica");

    /* Filtro: por defecto M y A si, Teo no. */
    CHECK(csnhypodbp_magmarks_allowed(&m, "GO01", CSNHYPODBP_MAG_ML, 'A', 1, 1, 0) == 1,
          "allowed: automatica con A encendido");
    CHECK(csnhypodbp_magmarks_allowed(&m, "GO01", CSNHYPODBP_MAG_ML, 'A', 0, 1, 0) == 0,
          "allowed: automatica con A apagado");
    CHECK(csnhypodbp_magmarks_allowed(&m, "GO01", CSNHYPODBP_MAG_MS, 'A', 1, 1, 0) == 1,
          "allowed: ajustada con M encendido");
    CHECK(csnhypodbp_magmarks_allowed(&m, "GO01", CSNHYPODBP_MAG_MS, 'A', 1, 0, 0) == 0,
          "allowed: ajustada con M apagado");
    CHECK(csnhypodbp_magmarks_allowed(&m, "GO02", CSNHYPODBP_MAG_ML, 'T', 1, 1, 0) == 0,
          "allowed: teorica apagada por defecto");
    CHECK(csnhypodbp_magmarks_allowed(&m, "GO02", CSNHYPODBP_MAG_ML, 'T', 1, 1, 1) == 1,
          "allowed: teorica con Teo encendido");

    csnhypodbp_magmarks_free(&m);
}

int main(void)
{
    test_store();
    test_tope();
    test_grab();
    test_handle();
    test_ovr_write();
    test_anchor_ovr();
    test_winsrc();
    if (fails) { printf("%d FALLOS\n", fails); return 1; }
    printf("OK test_magmarks\n");
    return 0;
}
