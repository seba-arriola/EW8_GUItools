#define _GNU_SOURCE
#include "wstore.h"
#include "wstore_internal.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char   *root = NULL, *scnl = NULL, *out = NULL;
    double        t0 = -1, t1 = -1;
    char          net[16], sta[16], loc[8], chan[8], dir[1500];
    FILE         *o;
    DIR          *dp;
    struct dirent *de;
    long          n = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--scnl") && i + 1 < argc) scnl = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) t0 = atof(argv[++i]);
        else if (!strcmp(argv[i], "--end") && i + 1 < argc) t1 = atof(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    }
    if (!root || !scnl || !out || t0 < 0 || t1 < 0) {
        fprintf(stderr, "Uso: store2tank --root <store> --scnl NET.STA.LOC.CHAN "
                        "--start <epoch> --end <epoch> --out <file.tank>\n");
        return 1;
    }
    if (sscanf(scnl, "%15[^.].%15[^.].%7[^.].%7s", net, sta, loc, chan) != 4) {
        fprintf(stderr, "store2tank: SCNL invalido <%s>\n", scnl);
        return 1;
    }
    if (wstore_scnl_dir(dir, sizeof(dir), root, sta, net, loc, chan) != 0) return 1;

    o = fopen(out, "wb");
    if (!o) { perror(out); return 1; }
    dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "store2tank: sin datos en %s\n", dir);
        fclose(o);
        return 1;
    }

    while ((de = readdir(dp)) != NULL) {
        char          tankp[1600], idxp[1600], *dot;
        int           tfd, ifd;
        WStoreIdxHdr  h;
        int64_t       f = -1, l = -1, i;

        if (!wstore_seg_overlaps(de->d_name, t0, t1)) continue;
        snprintf(tankp, sizeof(tankp), "%s/%s", dir, de->d_name);
        snprintf(idxp, sizeof(idxp), "%s/%s", dir, de->d_name);
        dot = strrchr(idxp, '.');
        if (!dot) continue;
        strcpy(dot, ".idx");

        tfd = open(tankp, O_RDONLY);
        if (tfd < 0) continue;
        ifd = open(idxp, O_RDONLY);
        if (ifd < 0) { close(tfd); continue; }
        if (wstore_index_hdr_read(ifd, &h) == 0 &&
            wstore_index_lookup(&h, ifd, t0, t1, &f, &l) > 0) {
            for (i = f; i <= l; i++) {
                WStoreIdxEnt  e;
                TRACE2_HEADER hh;
                int32_t      *s = NULL;
                if (wstore_index_ent_read(ifd, i, &e) != 0) continue;
                if (e.end < t0 || e.start > t1) continue;
                if (wstore_read_record(tfd, e.offset, &hh, &s) != 0) continue;
                fwrite(&hh, sizeof(hh), 1, o);
                fwrite(s, sizeof(int32_t), (size_t)hh.nsamp, o);
                free(s);
                n++;
            }
        }
        close(tfd);
        close(ifd);
    }
    closedir(dp);
    fclose(o);
    fprintf(stderr, "store2tank: %ld registros -> %s\n", n, out);
    return n > 0 ? 0 : 1;
}
