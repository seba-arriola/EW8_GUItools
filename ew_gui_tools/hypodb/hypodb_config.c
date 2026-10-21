#include "hypodb_config.h"

#include <string.h>

#include "ewgui/config.h"

int hypodb_config_load(HypodbConfig *cfg, const char *path)
{
    if (!cfg || !path)
        return -1;

    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->in_pick_ring, "PICK_RING");
    cfg->group_pick_tol_sec = 0.1;
    cfg->group_min_match_picks = 3;
    cfg->event_max_dt_sec = 300.0;
    cfg->event_load_hours = 24;
    cfg->pick_match_tol_sec = 0.05;
    cfg->heartbeat_int = 30;
    cfg->log_file = 1;
    cfg->debug = 0;

    EwKeySpec spec[] = {
        { "MyModuleId", EW_KEY_STR, cfg->my_module_id, sizeof(cfg->my_module_id), 1 },
        { "InRing", EW_KEY_STR, cfg->in_ring, sizeof(cfg->in_ring), 1 },
        { "InRingRef", EW_KEY_STR, cfg->in_ring_ref, sizeof(cfg->in_ring_ref), 1 },
        { "InRingConf", EW_KEY_STR, cfg->in_ring_conf, sizeof(cfg->in_ring_conf), 0 },
        { "InPickRing", EW_KEY_STR, cfg->in_pick_ring, sizeof(cfg->in_pick_ring), 0 },
        { "DbEnvFile", EW_KEY_STR, cfg->db_env_file, sizeof(cfg->db_env_file), 1 },
        { "GroupPickTolSec", EW_KEY_DBL, &cfg->group_pick_tol_sec, 0, 0 },
        { "GroupMinMatchPicks", EW_KEY_INT, &cfg->group_min_match_picks, 0, 0 },
        { "EventMaxDtSec", EW_KEY_DBL, &cfg->event_max_dt_sec, 0, 0 },
        { "EventLoadHours", EW_KEY_INT, &cfg->event_load_hours, 0, 0 },
        { "PickMatchTolSec", EW_KEY_DBL, &cfg->pick_match_tol_sec, 0, 0 },
        { "HeartBeatInt", EW_KEY_INT, &cfg->heartbeat_int, 0, 0 },
        { "LogFile", EW_KEY_INT, &cfg->log_file, 0, 0 },
        { "Debug", EW_KEY_INT, &cfg->debug, 0, 0 },
    };

    return ewgui_config_load(path, spec, sizeof(spec) / sizeof(spec[0]));
}
