#define _GNU_SOURCE
#include "wstore_writer.h"
#include "wstore_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char          sta[8], net[10], loc[4], chan[8];
    WStoreWriter *w;
} CEntry;

int main(int argc, char **argv)
{
    const char   *traces = NULL, *root = NULL;
    FILE         *f;
    CEntry       *ents = NULL;
    int           ne = 0, cap = 0;
    TRACE2_HEADER h;
    int32_t      *data = NULL;
    int           maxsamp = 0;
    long          nrec = 0, nskip = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!traces) traces = argv[i];
    }
    if (!traces || !root) {
        fprintf(stderr, "Uso: wstore_import <traces> --root <store>\n");
        return 1;
    }

    f = fopen(traces, "rb");
    if (!f) { perror(traces); return 1; }

    while (fread(&h, sizeof(h), 1, f) == 1) {
        size_t   esz;
        int32_t *s;
        int32_t *conv = NULL;
        CEntry  *e = NULL;

        if (h.nsamp <= 0 || h.nsamp > 200000) break;
        if (h.nsamp > maxsamp) {
            maxsamp = h.nsamp;
            data = (int32_t *)realloc(data, (size_t)maxsamp * sizeof(int32_t));
            if (!data) { fclose(f); return 1; }
        }
        esz = (memcmp(h.datatype, "i2", 2) == 0 || memcmp(h.datatype, "s2", 2) == 0) ? 2 : 4;
        if (fread(data, esz, (size_t)h.nsamp, f) != (size_t)h.nsamp) break;

        s = data;
        if (memcmp(h.datatype, "i4", 2) != 0 && memcmp(h.datatype, "s4", 2) != 0) {
            if (wstore_normalize_samples(&h, data, &conv) != 0) { nskip++; continue; }
            s = conv;
        }

        for (int i = 0; i < ne; i++) {
            if (!strncmp(ents[i].sta, h.sta, 7) && !strncmp(ents[i].net, h.net, 9) &&
                !strncmp(ents[i].loc, h.loc, 3) && !strncmp(ents[i].chan, h.chan, 4)) {
                e = &ents[i];
                break;
            }
        }
        if (!e) {
            if (ne == cap) {
                cap = cap ? cap * 2 : 8;
                ents = (CEntry *)realloc(ents, (size_t)cap * sizeof(*ents));
                if (!ents) { free(conv); fclose(f); return 1; }
            }
            e = &ents[ne++];
            snprintf(e->sta, sizeof(e->sta), "%s", h.sta);
            snprintf(e->net, sizeof(e->net), "%s", h.net);
            snprintf(e->loc, sizeof(e->loc), "%s", h.loc);
            snprintf(e->chan, sizeof(e->chan), "%s", h.chan);
            if (wstore_writer_open(&e->w, root, h.sta, h.net, h.loc, h.chan) != 0) {
                fprintf(stderr, "wstore_import: no se pudo abrir writer\n");
                free(conv);
                fclose(f);
                return 1;
            }
        }
        if (wstore_writer_append(e->w, &h, s) != 0) nskip++;
        else nrec++;
        free(conv);
    }

    for (int i = 0; i < ne; i++) wstore_writer_close(ents[i].w);
    free(ents);
    free(data);
    fclose(f);
    fprintf(stderr, "wstore_import: %ld registros, %ld omitidos\n", nrec, nskip);
    return 0;
}
