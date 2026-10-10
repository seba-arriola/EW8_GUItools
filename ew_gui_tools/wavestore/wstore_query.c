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
    const char *root = NULL;
    WStore     *s;
    char        scnls[1024][WSTORE_SCNL_LEN];
    int         n, i;

    for (int k = 1; k < argc; k++)
        if (!strcmp(argv[k], "--root") && k + 1 < argc) root = argv[++k];
    if (!root) { fprintf(stderr, "Uso: wstore_query --root <store>\n"); return 1; }

    if (wstore_open(&s, root, NULL) != 0) return 1;
    n = wstore_scnls(s, scnls, 1024);
    printf("store %s: %d SCNL\n", root, n);
    for (i = 0; i < n && i < 1024; i++) {
        char          dir[1600];
        DIR          *dp;
        struct dirent *de;
        long          nrec = 0;
        double        first = 0, last = 0;
        int           have = 0;

        snprintf(dir, sizeof(dir), "%s/%s", root, scnls[i]);
        dp = opendir(dir);
        if (!dp) continue;
        while ((de = readdir(dp)) != NULL) {
            char         idxp[1700], tankp[1700], *dot;
            int          ifd;
            WStoreIdxHdr h;
            if (strstr(de->d_name, ".tank") == NULL) continue;
            snprintf(idxp, sizeof(idxp), "%s/%s", dir, de->d_name);
            snprintf(tankp, sizeof(tankp), "%s/%s", dir, de->d_name);
            dot = strrchr(idxp, '.');
            if (!dot) continue;
            strcpy(dot, ".idx");
            ifd = open(idxp, O_RDONLY);
            if (ifd < 0) continue;
            if (wstore_index_hdr_read(ifd, &h) == 0 && h.count > 0) {
                WStoreIdxEnt e0, e1;
                nrec += (long)h.count;
                if (wstore_index_ent_read(ifd, 0, &e0) == 0 &&
                    wstore_index_ent_read(ifd, h.count - 1, &e1) == 0) {
                    if (!have || e0.start < first) first = e0.start;
                    if (!have || e1.end > last) last = e1.end;
                    have = 1;
                }
            }
            close(ifd);
            (void)tankp;
        }
        closedir(dp);
        printf("  %-24s records=%-8ld span=[%.3f, %.3f]\n", scnls[i], nrec, first, last);
    }
    wstore_close(s);
    return 0;
}
