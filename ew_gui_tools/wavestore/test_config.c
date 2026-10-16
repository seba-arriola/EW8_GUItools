#define _GNU_SOURCE
#include "wstore_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char g_dir[64];

static void write_file(char *path, size_t n, const char *name, const char *content)
{
    FILE *f;

    snprintf(path, n, "%s/%s", g_dir, name);
    f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(1); }
    fputs(content, f);
    fclose(f);
}

int main(void)
{
    char          path[512];
    WStoreConfig  c;
    const char   *names[] = { "full.d", "nofilter.d", "viejo.d", "sinroot.d" };
    size_t        i;

    strcpy(g_dir, "/tmp/wstore_config_XXXXXX");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 1; }

    /* --- config completa: ChannelFilterFile se parsea -------------------- */
    write_file(path, sizeof(path), "full.d",
               "MyModuleId    MOD_WSTORE\n"
               "RingName      SLINK_RING\n"
               "StoreRoot     ../wstore\n"
               "ChannelFilterFile wstore.sta\n"
               "RetentionDays 7\n"
               "IngestShards  4\n"
               "HeartbeatInt  30\n"
               "LogFile       1\n");
    CHECK(wstore_config_load(&c, path) == 0);
    CHECK(strcmp(c.root, "../wstore") == 0);
    CHECK(strcmp(c.channel_filter_file, "wstore.sta") == 0);
    CHECK(strcmp(c.ring_name, "SLINK_RING") == 0);
    CHECK(c.retention_days == 7);
    CHECK(c.ingest_shards == 4);

    /* --- sin la clave: sin filtro (""), defaults intactos ---------------- */
    write_file(path, sizeof(path), "nofilter.d", "StoreRoot ../wstore\nIngestShards 8\n");
    CHECK(wstore_config_load(&c, path) == 0);
    CHECK(c.channel_filter_file[0] == '\0');
    CHECK(c.ingest_shards == 8);
    CHECK(strcmp(c.module_id, "MOD_WSTORE") == 0);
    CHECK(strcmp(c.ring_name, "SLINK_RING") == 0);
    CHECK(c.retention_days == 7);
    CHECK(c.reorder_ms == 0);
    CHECK(c.heartbeat_int == 30);
    CHECK(c.log_file == 1);

    /* --- la clave muerta `ChannelFilter` ya no se parsea (ni rompe) ------ */
    write_file(path, sizeof(path), "viejo.d",
               "StoreRoot ../wstore\nChannelFilter *.*.*.HHZ\n");
    CHECK(wstore_config_load(&c, path) == 0);
    CHECK(c.channel_filter_file[0] == '\0');

    /* --- falta StoreRoot -> error --------------------------------------- */
    write_file(path, sizeof(path), "sinroot.d", "IngestShards 2\n");
    CHECK(wstore_config_load(&c, path) < 0);

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", g_dir, names[i]);
        remove(path);
    }
    rmdir(g_dir);

    if (failures) { printf("%d FALLOS\n", failures); return 1; }
    printf("test_config OK\n");
    return 0;
}
