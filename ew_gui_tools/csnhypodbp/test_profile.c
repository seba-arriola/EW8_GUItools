/*
 * Test headless del nucleo puro de perfiles (ver csnhypodbp_profile.h):
 * recta declarada, distancia perpendicular, eleccion del mas cercano (con N
 * arbitrario), parser de declaraciones y lector de polilineas de slab/topo.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "csnhypodbp_profile.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok: %s\n", msg); \
} while (0)

/* Recta de los scripts del repo test_db: lat = m*lon + b, m = 1/3.6. */
#define M_PERFIL (1.0 / 3.6)
/* suma_c de generador_mapas.py:377 (intercepto base c = 1.0). */
static const double SUMA_C[13] = { -1, -3, -5, -6, -8, -10, -12, -14,
                                   -16, -18, -20, -22, -24 };
static const char *NOMBRES[13] = { "P01", "P02", "P03", "P04", "P05", "P06", "P07",
                                   "P08", "P09", "P010", "P011", "P012", "P013" };

static int write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fputs(content, f);
    fclose(f);
    return 1;
}

/* Los 13 perfiles declarados con dos puntos sobre SU recta. */
static int build_13(CsnhypodbpProfiles *ps)
{
    int i;
    memset(ps, 0, sizeof(*ps));
    for (i = 0; i < 13; i++) {
        double b = 1.0 + SUMA_C[i];
        if (!csnhypodbp_profile_add(ps, NOMBRES[i], -72.0, M_PERFIL * -72.0 + b,
                                    -66.0, M_PERFIL * -66.0 + b, NULL, NULL,
                                    NULL, 0))
            return 0;
    }
    return 1;
}

static void test_recta(void)
{
    CsnhypodbpProfiles ps;
    double d;

    CHECK(build_13(&ps), "13 perfiles agregados");
    CHECK(fabs(ps.p[0].a - M_PERFIL) < 1e-9, "pendiente a = 1/3.6");
    CHECK(fabs(ps.p[0].b) < 1e-9, "P01: b = 0 (lat = m*lon)");
    CHECK(fabs(ps.p[6].b + 11.0) < 1e-9, "P07: b = -11");
    CHECK(fabs(ps.p[12].b + 23.0) < 1e-9, "P013: b = -23");

    /* Sobre la recta -> 0; 1 grado al norte -> 1/sqrt(1+a^2). */
    d = csnhypodbp_profile_dist(&ps.p[0], M_PERFIL * -70.0, -70.0);
    CHECK(fabs(d) < 1e-9, "punto sobre la recta -> distancia 0");
    d = csnhypodbp_profile_dist(&ps.p[0], M_PERFIL * -66.0, -66.0);
    CHECK(fabs(d) < 1e-9, "distancia invariante a lo largo de la recta");
    d = csnhypodbp_profile_dist(&ps.p[0], M_PERFIL * -70.0 + 1.0, -70.0);
    CHECK(fabs(d - 0.963517) < 1e-5, "1 grado al norte -> 0.963517 grados");
    /* Al sur tambien es perpendicular. */
    d = csnhypodbp_profile_dist(&ps.p[0], M_PERFIL * -70.0 - 1.0, -70.0);
    CHECK(fabs(d - 0.963517) < 1e-5, "1 grado al sur -> mismo valor");

    CHECK(csnhypodbp_profile_dist(NULL, 0.0, 0.0) < 0.0, "dist(NULL) < 0");
}

static void test_cercano(void)
{
    CsnhypodbpProfiles ps;
    double b;

    CHECK(build_13(&ps), "13 perfiles para nearest");
    /* Sobre la recta de cada uno -> su indice. */
    CHECK(csnhypodbp_profile_nearest(&ps, M_PERFIL * -70.0 + 0.0, -70.0) == 0,
          "sobre P01 -> 0");
    CHECK(csnhypodbp_profile_nearest(&ps, M_PERFIL * -70.0 - 11.0, -70.0) == 6,
          "sobre P07 -> 6");
    CHECK(csnhypodbp_profile_nearest(&ps, M_PERFIL * -75.0 - 23.0, -75.0) == 12,
          "sobre P013 -> 12");
    /* Mas cerca de P01 que de P02. */
    CHECK(csnhypodbp_profile_nearest(&ps, -20.0, -70.0) == 0,
          "entre P01 y P02 pero mas cerca de P01");

    /* N arbitrario: solo 2 perfiles declarados. */
    {
        CsnhypodbpProfiles dos;
        memset(&dos, 0, sizeof(dos));
        b = 1.0 + SUMA_C[12];
        CHECK(csnhypodbp_profile_add(&dos, "A", -72.0, M_PERFIL * -72.0 + 0.0,
                                     -66.0, M_PERFIL * -66.0 + 0.0, NULL, NULL, NULL, 0),
              "2 perfiles: primero");
        CHECK(csnhypodbp_profile_add(&dos, "B", -72.0, M_PERFIL * -72.0 + b,
                                     -66.0, M_PERFIL * -66.0 + b, NULL, NULL, NULL, 0),
              "2 perfiles: segundo");
        CHECK(csnhypodbp_profile_nearest(&dos, M_PERFIL * -70.0 + b, -70.0) == 1,
              "con N=2 elige el segundo");
        CHECK(csnhypodbp_profile_nearest(&dos, M_PERFIL * -70.0, -70.0) == 0,
              "con N=2 elige el primero");
    }

    /* Sin perfiles. */
    {
        CsnhypodbpProfiles vacio;
        memset(&vacio, 0, sizeof(vacio));
        CHECK(csnhypodbp_profile_nearest(&vacio, -20.0, -70.0) == -1,
              "sin perfiles -> -1");
        CHECK(csnhypodbp_profile_nearest(NULL, -20.0, -70.0) == -1,
              "nearest(NULL) -> -1");
    }
}

static void test_vertical_y_degenerada(void)
{
    CsnhypodbpProfiles ps;
    double d;

    memset(&ps, 0, sizeof(ps));
    CHECK(csnhypodbp_profile_add(&ps, "V", -70.0, -20.0, -70.0, -30.0,
                                 NULL, NULL, NULL, 0), "recta vertical valida");
    d = csnhypodbp_profile_dist(&ps.p[0], -25.0, -70.0);
    CHECK(fabs(d) < 1e-12, "vertical: sobre la recta -> 0");
    d = csnhypodbp_profile_dist(&ps.p[0], -25.0, -69.5);
    CHECK(fabs(d - 0.5) < 1e-12, "vertical: 0.5 grados al este -> 0.5");
    CHECK(csnhypodbp_profile_nearest(&ps, -25.0, -69.9) == 0, "vertical: nearest");

    memset(&ps, 0, sizeof(ps));
    CHECK(!csnhypodbp_profile_add(&ps, "X", -70.0, -20.0, -70.0, -20.0,
                                  NULL, NULL, NULL, 0),
          "extremos iguales -> error");
    CHECK(ps.n == 0, "no se agrego nada");
    CHECK(!csnhypodbp_profile_add(&ps, "", -70.0, -20.0, -69.0, -20.0,
                                  NULL, NULL, NULL, 0),
          "sin nombre -> error");
}

static void test_parse(void)
{
    const char *path = "/tmp/csnhypodbp_test_profiles.d";
    const char *bad = "/tmp/csnhypodbp_test_profiles_bad.d";
    CsnhypodbpProfiles ps;
    char err[256] = "";
    int n, i;
    FILE *f;

    /* Archivo bueno: comentarios, vacias y los 13 perfiles. */
    f = fopen(path, "w");
    CHECK(f != NULL, "creo el archivo de declaraciones");
    if (f) {
        fputs("# perfiles de profundidad\n\n", f);
        for (i = 0; i < 13; i++) {
            double b = 1.0 + SUMA_C[i];
            fprintf(f, "Profile %s %.4f %.4f %.4f %.4f\n", NOMBRES[i],
                    -72.0, M_PERFIL * -72.0 + b, -66.0, M_PERFIL * -66.0 + b);
        }
        fclose(f);
    }
    n = csnhypodbp_profiles_parse(path, &ps, err, sizeof(err));
    CHECK(n == 13, "parse: 13 perfiles");
    CHECK(ps.n == 13, "parse: n = 13");
    CHECK(strcmp(ps.p[0].name, "P01") == 0 && strcmp(ps.p[12].name, "P013") == 0,
          "parse: nombres primero/ultimo");
    CHECK(fabs(ps.p[6].a - M_PERFIL) < 1e-4, "parse: pendiente de P07");

    /* Archivo con files explicitos. */
    CHECK(write_file(path,
                     "# un perfil\nProfile PX -70 -20 -69 -19 s.tmp t.tmp\n"),
          "escribo declaracion con files");
    n = csnhypodbp_profiles_parse(path, &ps, err, sizeof(err));
    CHECK(n == 1 && strcmp(ps.p[0].slab_file, "s.tmp") == 0 &&
          strcmp(ps.p[0].topo_file, "t.tmp") == 0,
          "parse: files explicitos");

    /* Malformados. */
    CHECK(write_file(bad, "Profile P01 -72 -20 -66\n"), "escribo falta lat2");
    CHECK(csnhypodbp_profiles_parse(bad, &ps, err, sizeof(err)) == -1 &&
          strstr(err, "lat2") != NULL, "parse: falta lat2 -> -1");
    CHECK(write_file(bad, "Profile P01 -72 x -66 -18\n"), "escribo lat1 no numerico");
    CHECK(csnhypodbp_profiles_parse(bad, &ps, err, sizeof(err)) == -1 &&
          strstr(err, "lat1") != NULL, "parse: lat1 no numerico -> -1");
    CHECK(write_file(bad, "Perfil P01 -72 -20 -66 -18\n"), "escribo clave mala");
    CHECK(csnhypodbp_profiles_parse(bad, &ps, err, sizeof(err)) == -1 &&
          strstr(err, "Profile") != NULL, "parse: clave mala -> -1");
    CHECK(write_file(bad, "Profile P01 -72 -20 -66 -18 a b c\n"), "escribo tokens de mas");
    CHECK(csnhypodbp_profiles_parse(bad, &ps, err, sizeof(err)) == -1,
          "parse: tokens de mas -> -1");
    CHECK(csnhypodbp_profiles_parse("/tmp/no_existe_perfiles.d", &ps, err, sizeof(err)) == -1,
          "parse: archivo ausente -> -1");
    CHECK(csnhypodbp_profiles_parse(NULL, &ps, err, sizeof(err)) == -1,
          "parse: path NULL -> -1");

    /* add_tokens: el mismo parseo que usa la clave Profile del .d (kom). */
    {
        CsnhypodbpProfiles t;
        char rest1[] = "-72 -20.000000 -66 -18.333333";
        char rest2[] = "-72 -22 -66 -20.333333 slab.tmp topo.tmp";
        char rest3[] = "-72 -20 -66";
        char rest4[] = "-72 -20 -66 -18 a b c";
        char rest5[] = "-72 x -66 -18";
        char err2[256] = "";
        memset(&t, 0, sizeof(t));
        CHECK(csnhypodbp_profile_add_tokens("A", rest1, &t, err2, sizeof(err2)) == 1 &&
              t.n == 1 && fabs(t.p[0].a - M_PERFIL) < 1e-5 && fabs(t.p[0].b) < 1e-4,
              "add_tokens: linea valida");
        CHECK(csnhypodbp_profile_add_tokens("B", rest2, &t, err2, sizeof(err2)) == 1 &&
              strcmp(t.p[1].slab_file, "slab.tmp") == 0 &&
              strcmp(t.p[1].topo_file, "topo.tmp") == 0, "add_tokens: con files");
        CHECK(csnhypodbp_profile_add_tokens("C", rest3, &t, err2, sizeof(err2)) == 0 &&
              strstr(err2, "lat2") != NULL, "add_tokens: falta lat2");
        CHECK(csnhypodbp_profile_add_tokens("C", rest4, &t, err2, sizeof(err2)) == 0 &&
              strstr(err2, "de mas") != NULL, "add_tokens: tokens de mas");
        CHECK(csnhypodbp_profile_add_tokens("C", rest5, &t, err2, sizeof(err2)) == 0 &&
              strstr(err2, "lat1") != NULL, "add_tokens: numero invalido");
        CHECK(csnhypodbp_profile_add_tokens("", rest1, &t, err2, sizeof(err2)) == 0,
              "add_tokens: sin nombre");
    }

    /* Convencion de nombres. */    CHECK(write_file(path, "Profile P01 -72 -20 -66 -18\nProfile P02 -72 -22 -66 -20\n"),
          "escribo 2 sin files");
    n = csnhypodbp_profiles_parse(path, &ps, err, sizeof(err));
    CHECK(n == 2, "parse: 2 perfiles");
    csnhypodbp_profiles_default_files(&ps, "dir");
    CHECK(strcmp(ps.p[0].slab_file, "dir/slabP01.tmp") == 0 &&
          strcmp(ps.p[0].topo_file, "dir/topoP01.tmp") == 0 &&
          strcmp(ps.p[1].slab_file, "dir/slabP02.tmp") == 0,
          "convencion slab<name>.tmp / topo<name>.tmp");

    unlink(path);
    unlink(bad);
}

static void test_polilinea(void)
{
    const char *path = "/tmp/csnhypodbp_test_line.tmp";
    const char *shortf = "/tmp/csnhypodbp_test_line_short.tmp";
    const char *big = "/tmp/csnhypodbp_test_line_big.tmp";
    CsnhypodbpProfileLine l;
    char err[256] = "";
    int i;

    CHECK(write_file(path,
        "-0\t2.47563360181e-12\t-72\t-20\t-72\t-20\tNaN\n"
        "5.03365065503\t-1.39904531547\t-71.9535871588\t-19.9878780807\t-71.95\t-20\tNaN\n"
        "59.8638781496\t0.584157400921\t-71.4484874814\t-19.8550571573\t-71.45\t-19.85\t-11.0055093765\n"
        "508.784248464\t1.78440586051\t-67.3450274692\t-18.7153408215\t-67.35\t-18.7\t-208.497665405\n"
        "\n# comentario\n"),
        "escribo polilinea de 7 columnas");

    /* Las columnas son 1-based: lon=3, valor=7 (el usecols=[2,6] de los
       scripts es 0-based). */
    CHECK(csnhypodbp_profile_line_read(path, 3, 7, &l, err, sizeof(err)) == 0,
          "leo la polilinea");
    CHECK(l.n == 4, "4 filas leidas");
    CHECK(l.truncated == 0, "sin truncar");
    CHECK(isnan(l.val[0]) && isnan(l.val[1]), "las dos primeras son huecos (NaN)");
    CHECK(fabs(l.val[2] + 11.0055093765) < 1e-9, "col7 fila 3 = profundidad del slab");
    CHECK(fabs(l.lon[3] + 67.3450274692) < 1e-9, "col3 fila 4 = longitud de la traza");
    CHECK(fabs(l.lon_min + 71.4484874814) < 1e-9 &&
          fabs(l.lon_max + 67.3450274692) < 1e-9,
          "bbox de lon solo con filas finitas");
    CHECK(fabs(l.val_min + 208.497665405) < 1e-9 &&
          fabs(l.val_max + 11.0055093765) < 1e-9, "bbox de valores");
    csnhypodbp_profile_line_free(&l);
    CHECK(l.n == 0 && l.lon == NULL, "free deja la estructura limpia");

    /* Filas cortas: todas -> error. */
    CHECK(write_file(shortf, "-0 2.47\n5.03 -1.39\n"), "escribo filas cortas");
    CHECK(csnhypodbp_profile_line_read(shortf, 3, 7, &l, err, sizeof(err)) == -1,
          "sin filas validas -> -1");
    CHECK(csnhypodbp_profile_line_read("/tmp/no_existe_line.tmp", 3, 7, &l, err, sizeof(err)) == -1,
          "archivo ausente -> -1");
    CHECK(csnhypodbp_profile_line_read(path, 0, 7, &l, err, sizeof(err)) == -1,
          "columna 0 -> -1");

    /* Truncacion: mas filas que el tope. */
    {
        FILE *f = fopen(big, "w");
        CHECK(f != NULL, "creo archivo grande");
        if (f) {
            for (i = 0; i < CSNHYPODBP_PROFILE_MAX_PTS + 100; i++)
                fprintf(f, "0\t0\t-70.0\t0\t0\t0\t-10.0\n");
            fclose(f);
        }
        CHECK(csnhypodbp_profile_line_read(big, 3, 7, &l, err, sizeof(err)) == 0,
              "leo archivo grande");
        CHECK(l.n == CSNHYPODBP_PROFILE_MAX_PTS && l.truncated == 1,
              "truncado en el tope y marcado");
        csnhypodbp_profile_line_free(&l);
    }

    unlink(path);
    unlink(shortf);
    unlink(big);
}

int main(void)
{
    test_recta();
    test_cercano();
    test_vertical_y_degenerada();
    test_parse();
    test_polilinea();

    if (fails > 0) { printf("\n%d FALLOS\n", fails); return 1; }
    printf("\nOK test_profile\n");
    return 0;
}
