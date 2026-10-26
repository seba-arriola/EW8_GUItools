#define _GNU_SOURCE
#include "wstore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void usage(void)
{
    fprintf(stderr,
            "Uso: wstore_prune --root <store> --days <N> [--dry-run] [--quiet]\n"
            "\n"
            "Borra los segmentos <SCNL>/<YYYYMMDD>.ws (y su .idx) anteriores a la\n"
            "medianoche UTC de hoy menos N dias (lo mismo que hace wstore_ingest\n"
            "cada hora con `RetentionDays`). Con --dry-run solo cuenta lo que\n"
            "borraria. Sin --root o con --days <= 0 sale 2 sin tocar nada.\n");
}

int main(int argc, char **argv)
{
    const char       *root = NULL;
    long              days = -1;
    int               dry = 0, quiet = 0, k;
    WStore           *s = NULL;
    WStorePruneStats  st;

    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--root") && k + 1 < argc)       root = argv[++k];
        else if (!strcmp(argv[k], "--days") && k + 1 < argc) {
            char *end = NULL;
            days = strtol(argv[++k], &end, 10);
            if (end == NULL || *end != '\0') { usage(); return 2; }   /* "7abc" no vale */
        }
        else if (!strcmp(argv[k], "--dry-run"))               dry = 1;
        else if (!strcmp(argv[k], "--quiet"))                 quiet = 1;
        else {
            fprintf(stderr, "wstore_prune: argumento invalido <%s>\n", argv[k]);
            usage();
            return 2;
        }
    }
    if (!root || root[0] == '\0' || days <= 0 || days > 36500) { usage(); return 2; }

    /* `wstore_open` no falla con una raiz que no existe (el ingester la crea al
     * primer registro), pero desde un CLI eso no debe reportarse como "0
     * segmentos": casi siempre es una ruta mal escrita. */
    {
        struct stat sb;
        if (stat(root, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
            fprintf(stderr, "wstore_prune: no existe el store %s\n", root);
            return 1;
        }
    }

    if (wstore_open(&s, root, NULL) != 0) {
        fprintf(stderr, "wstore_prune: no se pudo abrir el store %s\n", root);
        return 1;
    }
    if (wstore_prune_ex(s, days, dry ? WSTORE_PRUNE_DRY_RUN : 0u, &st) != 0) {
        fprintf(stderr, "wstore_prune: fallo la purga de %s\n", root);
        wstore_close(s);
        return 1;
    }
    wstore_close(s);

    if (!quiet)
        printf("%s: %s %ld segmentos (+%ld idx), %ld MB\n",
               root, dry ? "borraria" : "borro",
               st.segments_removed, st.idx_removed,
               (st.bytes_freed + 1048575) / 1048576);
    return 0;
}
