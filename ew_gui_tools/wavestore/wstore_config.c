#include "wstore_config.h"

#include <kom.h>
#include <stdio.h>
#include <string.h>

static void copy_str(char *dst, size_t dstsz, const char *src)
{
    if (!src) return;
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

int wstore_config_load(WStoreConfig *out, const char *path)
{
    char *com, *str;

    if (!out || !path) return -1;
    memset(out, 0, sizeof(*out));
    out->retention_days = 7;
    out->reorder_ms = 0;
    out->heartbeat_int = 30;
    out->log_file = 1;
    out->ingest_shards = 4;
    copy_str(out->ring_name, sizeof(out->ring_name), "SLINK_RING");
    copy_str(out->module_id, sizeof(out->module_id), "MOD_WSTORE");

    if (!k_open(path)) {
        fprintf(stderr, "wstore: no se pudo abrir %s\n", path);
        return -1;
    }
    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if      (k_its("StoreRoot"))     { str = k_str(); copy_str(out->root, sizeof(out->root), str); }
        else if (k_its("RingName"))      { str = k_str(); copy_str(out->ring_name, sizeof(out->ring_name), str); }
        else if (k_its("MyModuleId"))    { str = k_str(); copy_str(out->module_id, sizeof(out->module_id), str); }
        else if (k_its("ChannelFilterFile")) { str = k_str(); copy_str(out->channel_filter_file, sizeof(out->channel_filter_file), str); }
        else if (k_its("RetentionDays")) out->retention_days = k_long();
        else if (k_its("ReorderMs"))     out->reorder_ms = k_int();
        else if (k_its("HeartbeatInt"))  out->heartbeat_int = k_int();
        else if (k_its("HeartBeatInt"))  out->heartbeat_int = k_int();
        else if (k_its("LogFile"))       out->log_file = k_int();
        else if (k_its("IngestShards"))  out->ingest_shards = k_int();
        else continue;

        if (k_err()) {
            fprintf(stderr, "wstore: error de config en '%s'\n", com);
            k_close();
            return -1;
        }
    }
    k_close();

    if (out->root[0] == '\0') {
        fprintf(stderr, "wstore: falta StoreRoot en %s\n", path);
        return -1;
    }
    if (out->ingest_shards < 1)  out->ingest_shards = 1;
    if (out->ingest_shards > 64) out->ingest_shards = 64;
    return 0;
}
