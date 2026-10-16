#define _GNU_SOURCE
#include "wstore_filter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char g_dir[64];

/* Escribe `content` en <g_dir>/<name> y deja la ruta en `path`. */
static void write_file(char *path, size_t n, const char *name, const char *content)
{
    FILE *f;

    snprintf(path, n, "%s/%s", g_dir, name);
    f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fputs(content, f);
    fclose(f);
}

int main(void)
{
    char          path[512];
    WStoreFilter *f = NULL;

    strcpy(g_dir, "/tmp/wstore_filter_XXXXXX");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 1; }

    /* --- sin filtro: se acepta todo ------------------------------------- */
    CHECK(wstore_filter_load(NULL, &f) == 0);
    CHECK(f == NULL);
    CHECK(wstore_filter_accept(NULL, "AA", "BB", "CC", "--") == 1);
    CHECK(wstore_filter_accept(NULL, "AA", "BB", "CC", NULL) == 1);
    CHECK(wstore_filter_count(NULL) == 0);
    CHECK(wstore_filter_load("", &f) == 0);
    CHECK(f == NULL);

    /* --- `* * * *` = todo ------------------------------------------------- */
    write_file(path, sizeof(path), "all.sta",
               "# comentario\n"
               "   \n"
               "* * * *\n");
    CHECK(wstore_filter_load(path, &f) == 0);
    CHECK(wstore_filter_count(f) == 1);
    CHECK(wstore_filter_accept(f, "XX", "YY", "ZZZ", "--") == 1);
    CHECK(wstore_filter_accept(f, "XX", "YY", "ZZZ", "10") == 1);
    CHECK(wstore_filter_accept(f, "XX", "YY", "ZZZ", NULL) == 1);
    CHECK(wstore_filter_accept(f, "XX", "YY", "ZZZ", "") == 1);
    wstore_filter_free(f);

    /* --- patrones concretos: comodines, loc, case, comentarios ------------ */
    write_file(path, sizeof(path), "ok.sta",
               "FAR1 C1_AC01 HH? --\n"
               "ST02   CX   HHZ  10   # comentario al final\n");
    CHECK(wstore_filter_load(path, &f) == 0);
    CHECK(wstore_filter_count(f) == 2);

    /* `FAR1 C1_AC01 HH? --`: `?` = exactamente 1 caracter, loc nula         */
    CHECK(wstore_filter_accept(f, "FAR1", "C1_AC01", "HHZ", "--") == 1);
    CHECK(wstore_filter_accept(f, "FAR1", "C1_AC01", "HHN", NULL) == 1);
    CHECK(wstore_filter_accept(f, "FAR1", "C1_AC01", "HHE", "") == 1);
    CHECK(wstore_filter_accept(f, "FAR1", "C1_AC01", "HHZ", "10") == 0);
    CHECK(wstore_filter_accept(f, "FAR2", "C1_AC01", "HHZ", "--") == 0);
    CHECK(wstore_filter_accept(f, "FAR1", "C2_XX", "HHZ", "--") == 0);
    CHECK(wstore_filter_accept(f, "FAR1", "C1_AC01", "HHEE", "--") == 0);
    CHECK(wstore_filter_accept(f, "FAR1", "C1_AC01", "HH", "--") == 0);
    CHECK(wstore_filter_accept(f, "far1", "C1_AC01", "HHZ", "--") == 0);

    /* loc real y recorte de espacios en los campos de la traza             */
    CHECK(wstore_filter_accept(f, "ST02", "CX", "HHZ", "10") == 1);
    CHECK(wstore_filter_accept(f, "ST02", "CX", "HHZ", "10 ") == 1);
    CHECK(wstore_filter_accept(f, "ST02", "CX", "HHZ", "--") == 0);
    CHECK(wstore_filter_accept(f, " FAR1 ", "C1_AC01", "HHZ", " -- ") == 1);

    wstore_filter_free(f);

    /* --- archivo solo con comentarios: 0 patrones (todo) ----------------- */
    write_file(path, sizeof(path), "vacio.sta", "# nada\n\n");
    f = NULL;
    CHECK(wstore_filter_load(path, &f) == 0);
    CHECK(wstore_filter_count(f) == 0);
    CHECK(wstore_filter_accept(f, "AA", "BB", "CC", "--") == 1);
    wstore_filter_free(f);

    /* --- errores de formato: <0 y sin filtro a medias -------------------- */
    write_file(path, sizeof(path), "tres.sta", "FAR1 CX HHZ\n");
    f = NULL;
    CHECK(wstore_filter_load(path, &f) < 0);
    CHECK(f == NULL);

    write_file(path, sizeof(path), "cinco.sta", "FAR1 CX HHZ -- extra\n");
    f = NULL;
    CHECK(wstore_filter_load(path, &f) < 0);
    CHECK(f == NULL);

    write_file(path, sizeof(path), "largo.sta",
               "FAR1 CX HHZ AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
    f = NULL;
    CHECK(wstore_filter_load(path, &f) < 0);
    CHECK(f == NULL);

    snprintf(path, sizeof(path), "%s/no_existe.sta", g_dir);
    f = NULL;
    CHECK(wstore_filter_load(path, &f) < 0);
    CHECK(f == NULL);

    /* limpieza */
    {
        static const char *names[] = { "all.sta", "ok.sta", "vacio.sta", "tres.sta", "cinco.sta", "largo.sta" };
        size_t i;
        char   p[512];
        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            snprintf(p, sizeof(p), "%s/%s", g_dir, names[i]);
            remove(p);
        }
        rmdir(g_dir);
    }

    if (failures) { printf("%d FALLOS\n", failures); return 1; }
    printf("test_filter OK\n");
    return 0;
}
