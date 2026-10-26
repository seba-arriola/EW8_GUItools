/*
 * bpq_manual.c - Overlay de picadas manuales del picking.
 *
 * El nucleo puro (csnhypodbp_manual.c) ya guarda las picadas en una lista
 * global; aqui se agrega lo que la GUI necesita y el nucleo no tiene:
 *
 *   - `set` semantico: colocar el pick de (estacion, fase) REEMPLAZA el
 *     anterior de esa fase y deja intacta la de la otra (asi mover un pick es
 *     volver a hacer clic, sin acumular duplicados);
 *   - persistencia en texto, para que un re-pick no se pierda al reiniciar.
 *
 * Igual que en la GTK, nada de esto toca el anillo: las picadas se publican a
 * PICK_RING recien al Confirmar (ver confirmCandidate en la ventana).
 */
#include "bpq_capi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define BPQ_MANUAL_MAX CSNHYPODBP_MANUAL_MAX

/* Crea los directorios padre de `path` (mkdir -p). El runtime de la GUI puede no
 * existir todavia: `startstop` no se lo crea a los binarios que no son modulos,
 * y sin esto el overlay se perdia en silencio. */
static void mkdir_parents(const char *path)
{
    char tmp[512];
    char *p;

    if (!path || strlen(path) >= sizeof(tmp)) return;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        (void)mkdir(tmp, 0755);
        *p = '/';
    }
}

/* Version del formato del archivo (primera linea). */
static const char *kHeader = "# csnhypodbpQT manual picks v2";

int bpq_manual_set(const char *sta, const char *chan, char phase, double t)
{
    CsnhypodbpManualPick all[BPQ_MANUAL_MAX];
    CsnhypodbpManualPick keep[BPQ_MANUAL_MAX];
    CsnhypodbpManualPick p;
    int n, i, k = 0;

    if (!sta || !sta[0] || (phase != 'P' && phase != 'S')) return -1;
    n = csnhypodbp_manual_list(all, BPQ_MANUAL_MAX);

    for (i = 0; i < n && i < BPQ_MANUAL_MAX; i++) {
        if (strcmp(all[i].sta, sta) == 0 && all[i].phase == phase) continue;
        keep[k++] = all[i];
    }

    memset(&p, 0, sizeof(p));
    snprintf(p.sta, sizeof(p.sta), "%s", sta);
    if (chan) snprintf(p.chan, sizeof(p.chan), "%s", chan);
    p.phase = phase;
    p.t = t;

    csnhypodbp_manual_clear();
    for (i = 0; i < k; i++) csnhypodbp_manual_add(&keep[i]);
    csnhypodbp_manual_add(&p);
    return 0;
}

int bpq_manual_remove(const char *sta, char phase)
{
    CsnhypodbpManualPick all[BPQ_MANUAL_MAX];
    CsnhypodbpManualPick keep[BPQ_MANUAL_MAX];
    int n, i, k = 0, had = 0;

    if (!sta) return 0;
    n = csnhypodbp_manual_list(all, BPQ_MANUAL_MAX);
    for (i = 0; i < n && i < BPQ_MANUAL_MAX; i++) {
        if (strcmp(all[i].sta, sta) == 0 && all[i].phase == phase) {
            had = 1;
            continue;
        }
        keep[k++] = all[i];
    }
    if (!had) return 0;
    csnhypodbp_manual_clear();
    for (i = 0; i < k; i++) csnhypodbp_manual_add(&keep[i]);
    return 1;
}

void bpq_manual_clear(void)
{
    csnhypodbp_manual_clear();
}

int bpq_manual_count(void)
{
    return csnhypodbp_manual_count();
}

int bpq_manual_list(CsnhypodbpManualPick *out, int max)
{
    return csnhypodbp_manual_list(out, max);
}

/* qid de una clave; -1 si no empieza por un entero seguido de ':' o fin. */
static long long key_qid(const char *key)
{
    char *end = NULL;
    long long q;

    if (!key || !key[0]) return -1;
    q = strtoll(key, &end, 10);
    if (end == key || (*end != ':' && *end != '\0')) return -1;
    return (q > 0) ? q : -1;
}

int bpq_manual_key_matches(const char *file_key, const char *event_key)
{
    /* v1 sin clave: se acepta siempre (migracion). */
    if (!file_key || !file_key[0]) return 1;
    if (!event_key || !event_key[0]) return 0;
    if (strcmp(file_key, event_key) == 0) return 1;
    /* Formato viejo `<qid>:<mod>:<version>`: mismo evento si coincide el qid. */
    {
        const long long a = key_qid(file_key), b = key_qid(event_key);

        if (a > 0 && a == b) return 1;
    }
    return 0;
}

int bpq_manual_path_for_event(const char *base, const char *event_key,
                              char *out, size_t n)
{
    char safe[160];
    size_t i, k, bl;

    if (!base || !base[0] || !event_key || !event_key[0] || !out || n == 0)
        return -1;
    for (i = 0, k = 0; event_key[i] && k + 1 < sizeof(safe); i++) {
        const char c = event_key[i];

        if (c == ':' || c == '/' || c == ' ' || c == '\\') safe[k++] = '_';
        else safe[k++] = c;
    }
    safe[k] = '\0';
    bl = strlen(base);
    if (bl > 6 && strcmp(base + bl - 6, ".picks") == 0)
        snprintf(out, n, "%s.%s", base, safe);
    else
        snprintf(out, n, "%s.%s.picks", base, safe);
    return 0;
}

int bpq_manual_save(const char *path, const char *event_key)
{
    CsnhypodbpManualPick all[BPQ_MANUAL_MAX];
    FILE *f;
    int n, i;

    /* Sin evento no se guarda: escribir la lista actual bajo una clave vacia
     * (o la de otro evento) borraria las picadas de ese evento. */
    if (!event_key || !event_key[0]) return -2;
    if (!path || !path[0]) return -1;
    mkdir_parents(path);
    f = fopen(path, "w");
    if (!f) return -1;
    n = csnhypodbp_manual_list(all, BPQ_MANUAL_MAX);
    fprintf(f, "%s %s\n", kHeader, event_key);
    for (i = 0; i < n && i < BPQ_MANUAL_MAX; i++) {
        /* `t` con 6 decimales: microsegundo, sobra para una picada. */
        fprintf(f, "%s %s %c %.6f\n", all[i].sta, all[i].chan, all[i].phase,
                all[i].t);
    }
    fclose(f);
    return 0;
}

int bpq_manual_load(const char *path, const char *event_key)
{
    CsnhypodbpManualPick tmp[BPQ_MANUAL_MAX];
    char line[256];
    int match = 0;                 /* 1 = la clave del archivo es la pedida */
    int ntmp = 0;
    FILE *f;

    if (!path || !path[0] || !event_key || !event_key[0]) return -2;
    f = fopen(path, "r");
    if (!f) return 0;              /* sin archivo = overlay vacio, no es error */

    /* Primera linea: "# ... v2 <clave>"; un encabezado v1 no lleva clave y se
     * acepta siempre. Un archivo del evento VACIO (0 bytes) es un overlay
     * limpiado a proposito: limpia la lista y NO cae al archivo legado. */
    if (!fgets(line, sizeof(line), f)) {
        match = 1;
    } else if (line[0] == '#') {
        const char *v2 = strstr(line, " v2 ");

        if (v2) {
            char key[128] = "";
            size_t k;

            snprintf(key, sizeof(key), "%s", v2 + 4);
            k = strlen(key);
            while (k > 0 && (key[k - 1] == '\n' || key[k - 1] == '\r'))
                key[--k] = '\0';
            match = bpq_manual_key_matches(key, event_key);
        } else {
            match = 1;             /* v1: sin clave */
        }
    } else {
        /* Formato v1 sin encabezado: la primera linea ya es una picada. */
        match = 1;
        rewind(f);
    }
    if (!match) {
        fclose(f);
        return -1;                 /* de otro evento: la lista queda intacta */
    }

    while (fgets(line, sizeof(line), f)) {
        CsnhypodbpManualPick p;
        char sta[32] = "", chan[32] = "", phase = 0;
        double t = 0.0;

        if (line[0] == '#' || line[0] == '\n') continue;
        if (ntmp >= BPQ_MANUAL_MAX) continue;
        if (sscanf(line, "%31s %31s %c %lf", sta, chan, &phase, &t) != 4)
            continue;
        if (phase != 'P' && phase != 'S') continue;
        memset(&p, 0, sizeof(p));
        snprintf(p.sta, sizeof(p.sta), "%s", sta);
        snprintf(p.chan, sizeof(p.chan), "%s", chan);
        p.phase = phase;
        p.t = t;
        tmp[ntmp++] = p;
    }
    fclose(f);

    /* Solo ahora se reemplaza la lista: un archivo de otro evento no deja el
     * overlay a medias. */
    csnhypodbp_manual_clear();
    for (int i = 0; i < ntmp; i++) csnhypodbp_manual_add(&tmp[i]);
    return 1;
}
