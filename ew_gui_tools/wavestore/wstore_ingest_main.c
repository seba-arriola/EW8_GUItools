#include "wstore_config.h"
#include "wstore_ingest.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    WStoreConfig  cfg;
    WStoreIngest *ing = NULL;
    int           rc;

    if (argc != 2) {
        fprintf(stderr, "Uso: wstore_ingest <wavestore.d>\n");
        return 1;
    }
    if (wstore_config_load(&cfg, argv[1]) != 0) return 1;
    if (wstore_ingest_open(&ing, &cfg) != 0) {
        fprintf(stderr, "wstore_ingest: no se pudo abrir el store\n");
        return 1;
    }
    rc = wstore_ingest_run_ring(ing, &cfg);
    wstore_ingest_close(ing);
    return rc == 0 ? 0 : 1;
}
