#ifndef WSTORE_CONFIG_H
#define WSTORE_CONFIG_H

/* Config del ingester (wavestore.d), leida con kom (k_*). */
typedef struct {
    char root[512];           /* StoreRoot (obligatorio)        */
    char ring_name[32];       /* RingName   (SLINK_RING)        */
    char module_id[32];       /* MyModuleId (MOD_WSTORE)        */
    char channel_filter_file[512]; /* ChannelFilterFile (opcional); "" = sin filtro */
    long retention_days;      /* RetentionDays (7)              */
    int  reorder_ms;          /* ReorderMs (0)                  */
    int  heartbeat_int;       /* HeartbeatInt (30)              */
    int  log_file;            /* LogFile (1)                    */
    int  ingest_shards;       /* IngestShards (4)               */
} WStoreConfig;

/* 0 OK, <0 error (falta StoreRoot o error de parseo). */
int wstore_config_load(WStoreConfig *out, const char *path);

#endif /* WSTORE_CONFIG_H */
