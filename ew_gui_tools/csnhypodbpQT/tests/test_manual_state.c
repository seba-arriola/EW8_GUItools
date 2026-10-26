/*
 * test_manual_state.c - Persistencia del overlay manual POR EVENTO.
 *
 * Trampa medida (2026-10-25): el overlay se guardaba con la clave de la
 * SOLUCION (`qid:mod:version`). Al cambiar la preferida, `bpq_manual_load`
 * limpiaba la lista y rechazaba el archivo: el `locate` con solo M enviaba 0
 * picadas y nlloc respondia `MinPhases: 3 fases en el header < 4`.
 *
 * Este test fija el contrato:
 *   - la clave del evento es `qid:otime`; el formato viejo por solucion se
 *     acepta si coincide el qid (migracion);
 *   - cargar un archivo de OTRO evento devuelve -1 y NO toca la lista;
 *   - guardar con clave vacia devuelve -2 y no trunca nada;
 *   - la ruta por evento no colisiona entre eventos ni lleva ':'.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bpq_capi.h"

static int g_checks = 0;
static int g_fail = 0;

#define CK(cond) do {                                                          \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
        g_fail++;                                                              \
        fprintf(stderr, "FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                          \
} while (0)

static int count_phase(char ph)
{
    CsnhypodbpManualPick all[CSNHYPODBP_MANUAL_MAX];
    int n = bpq_manual_list(all, CSNHYPODBP_MANUAL_MAX);
    int i, k = 0;

    for (i = 0; i < n; i++)
        if (all[i].phase == ph) k++;
    return k;
}

static long file_size(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static int write_picks(const char *path, const char *key, int npicks)
{
    FILE *f = fopen(path, "w");
    int i;

    if (!f) return -1;
    fprintf(f, "# csnhypodbpQT manual picks v2 %s\n", key ? key : "");
    for (i = 0; i < npicks; i++)
        fprintf(f, "PB%02d HHZ %c %.6f\n", i + 1, (i % 2) ? 'S' : 'P',
                100.0 + i);
    fclose(f);
    return 0;
}

int main(void)
{
    char dir[] = "/tmp/bpq_manual_state_XXXXXX";
    char legacy[512], path[512], other[512];
    const char *ev = "2022100320:1792877847.000000";
    const char *ev2 = "2022100321:1792877900.000000";
    long sz;

    if (!mkdtemp(dir)) {
        fprintf(stderr, "FALLA mkdtemp\n");
        return 1;
    }
    snprintf(legacy, sizeof(legacy), "%s/manual.events.picks", dir);
    snprintf(other, sizeof(other), "%s/manual.events.picks.%s", dir,
             "2022100321_1792877900.000000");

    /* --- 1. clave: formato viejo aceptado por qid, otro qid no ------------ */
    CK(bpq_manual_key_matches("2022100320:162:2", ev) == 1);
    CK(bpq_manual_key_matches(ev, ev) == 1);
    CK(bpq_manual_key_matches("2022100321:162:2", ev) == 0);
    CK(bpq_manual_key_matches("", ev) == 1);        /* v1 sin clave */
    CK(bpq_manual_key_matches("no-es-un-qid", ev) == 0);

    /* --- 2. archivo legado de la solucion, mismo qid ---------------------- */
    CK(write_picks(legacy, "2022100320:162:2", 2) == 0);
    bpq_manual_clear();
    CK(bpq_manual_load(legacy, ev) == 1);
    CK(bpq_manual_count() == 2);
    CK(count_phase('P') == 1 && count_phase('S') == 1);

    /* --- 3. archivo de otro evento: -1 y la lista queda intacta ----------- */
    CK(bpq_manual_load(legacy, ev2) == -1);
    CK(bpq_manual_count() == 2);

    /* --- 4. ruta por evento: sin ':' y distinta por evento ---------------- */
    CK(bpq_manual_path_for_event(legacy, ev, path, sizeof(path)) == 0);
    CK(strstr(path, "2022100320_1792877847.000000") != NULL);
    CK(strstr(path, ".picks.2022100320_") != NULL);
    {
        const size_t pl = strlen(path);

        /* El base ya termina en .picks: no se duplica el sufijo. */
        CK(pl > 6 && strcmp(path + pl - 6, ".picks") != 0);
    }
    CK(strchr(path, ':') == NULL);
    {
        char path2[512];

        CK(bpq_manual_path_for_event(legacy, ev2, path2, sizeof(path2)) == 0);
        CK(strcmp(path, path2) != 0);
    }

    /* --- 5. save/load en la ruta del evento ------------------------------- */
    CK(bpq_manual_save(path, ev) == 0);
    bpq_manual_clear();
    CK(bpq_manual_count() == 0);
    CK(bpq_manual_load(path, ev) == 1);
    CK(bpq_manual_count() == 2);

    /* --- 6. clave vacia: -2 y el archivo no se trunca --------------------- */
    sz = file_size(path);
    CK(sz > 0);
    CK(bpq_manual_save(path, "") == -2);
    CK(file_size(path) == sz);
    CK(bpq_manual_save(path, NULL) == -2);
    CK(file_size(path) == sz);

    /* --- 7. archivo v1 sin clave: se acepta ------------------------------- */
    {
        FILE *f = fopen(path, "w");

        CK(f != NULL);
        if (f) {
            fprintf(f, "PB01 HHZ P 100.000000\n");
            fclose(f);
        }
        bpq_manual_clear();
        CK(bpq_manual_load(path, ev) == 1);
        CK(bpq_manual_count() == 1);
    }

    /* --- 8. archivo de otro qid no reemplaza la lista --------------------- */
    CK(write_picks(other, "2022100321:162:2", 3) == 0);
    bpq_manual_clear();
    bpq_manual_set("PB09", "HHZ", 'P', 200.0);
    CK(bpq_manual_count() == 1);
    CK(bpq_manual_load(other, ev) == -1);
    CK(bpq_manual_count() == 1);

    /* --- 9. archivo del evento vacio: limpia, no cae al legado ------------ */
    {
        FILE *f = fopen(path, "w");

        CK(f != NULL);
        if (f) fclose(f);
        bpq_manual_set("PB09", "HHZ", 'P', 300.0);
        CK(bpq_manual_count() == 1);
        CK(bpq_manual_load(path, ev) == 1);
        CK(bpq_manual_count() == 0);
    }

    printf("== manual_state checks=%d fallas=%d ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
