#include "wstore_config.h"
#include "wstore_ingest.h"

#include <earthworm.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    WStoreConfig   cfg;
    WStoreIngest  *ing = NULL;
    unsigned char  modid = 0;
    int            rc;

    if (argc != 2) {
        fprintf(stderr, "Uso: wstore_ingest <wavestore.d>\n");
        return 1;
    }
    if (wstore_config_load(&cfg, argv[1]) != 0) return 1;

    /* Log propio en EW_LOG (`wstore_ingest_<fecha>.log`), con el modo que pide
     * `LogFile` en el .d (0=consola, 1=disco+consola, 2=disco). Sin esto el
     * modulo era mudo en el disco: sus mensajes iban a stderr, que startstop
     * comparte con todos los hijos. Mismo patron que csnloc/hypodb_ingest.
     *
     * OJO: el primer argumento es un literal, NO `argv[1]` como en csnloc
     * (`logit_init(argv[1], ...)`): este .d se llama `wavestore.d`, asi que el
     * log saldria como `wavestore_<fecha>.log` y `diag.sh log wstore_ingest`
     * (que busca `<modulo>_*.log`) no lo encontraria. */
    if (GetModId(cfg.module_id, &modid) != 0) {
        fprintf(stderr, "wstore_ingest: GetModId %s (falta en earthworm.d)\n",
                cfg.module_id);
        return 1;
    }
    logit_init("wstore_ingest", (short)modid, 1024, cfg.log_file);

    if (wstore_ingest_open(&ing, &cfg) != 0) {
        logit("e", "wstore_ingest: no se pudo abrir el store (%s); revisa la ruta y "
                   "ChannelFilterFile '%s' (el detalle archivo:linea va a stderr)\n",
              cfg.root, cfg.channel_filter_file);
        return 1;
    }
    rc = wstore_ingest_run_ring(ing, &cfg);
    wstore_ingest_close(ing);
    return rc == 0 ? 0 : 1;
}
