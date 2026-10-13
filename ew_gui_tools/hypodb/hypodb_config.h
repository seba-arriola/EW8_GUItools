#ifndef HYPODB_CONFIG_H
#define HYPODB_CONFIG_H

/*
 * Configuración del ingester hypodb_ingest (archivo .d tipo EarthWorm).
 * Claves: MyModuleId, InRing, InRingRef, InRingRef2, InPickRing, DbEnvFile,
 * GroupPickTolSec, GroupMinMatchPicks, EventMaxDtSec, EventLoadHours,
 * PickMatchTolSec, HeartBeatInt, LogFile, Debug.
 */
typedef struct {
    char   my_module_id[32];
    char   in_ring[32];        /* HYPO_RING      (csnloc, mod 162)          */
    char   in_ring_ref[32];    /* HYPO_RING_REF  (hyp2000 163 / nlloc 164)  */
    char   in_pick_ring[32];   /* PICK_RING */
    char   db_env_file[128];   /* db.env */
    double group_pick_tol_sec;
    int    group_min_match_picks;
    double event_max_dt_sec;   /* ventana de la guarda anti-fusión por qid (s) */
    int    event_load_hours;
    double pick_match_tol_sec;
    int    heartbeat_int;
    int    log_file;
    int    debug;
} HypodbConfig;

/* Devuelve 0 si OK, -1 si falta una clave obligatoria o hay error de parseo. */
int hypodb_config_load(HypodbConfig *cfg, const char *path);

#endif /* HYPODB_CONFIG_H */
