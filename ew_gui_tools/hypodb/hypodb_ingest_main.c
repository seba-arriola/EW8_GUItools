/*
 * hypodb_ingest — proceso EarthWorm: consume los anillos hipocentrales y los
 * escribe en PostgreSQL. Ver hypodb_ingest.h y hypodb.d.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "earthworm.h"
#include "transport.h"

#include "ewgui/db.h"
#include "hypodb_config.h"
#include "hypodb_ingest.h"

#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif

#define MAX_PER_RING 1000

static void dispatch(HypodbState *st, const MSG_LOGO *logo, unsigned char t_arc,
                     unsigned char t_mag, int default_mod, const char *buf,
                     long size)
{
    if (logo->type == t_arc) {
        int mod = logo->mod ? (int)logo->mod : default_mod;
        hypodb_feed_arc(st, buf, (size_t)size, mod);
    } else if (logo->type == t_mag) {
        hypodb_feed_mag(st, buf, (size_t)size);
    }
}

int main(int argc, char **argv)
{
    HypodbConfig   cfg;
    HypodbState   *st;
    EwGuiDbConfig  dbcfg;
    EwGuiDb       *db = NULL;
    unsigned char  t_arc, t_mag, t_pick;
    SHM_INFO       r[2], r_pick;
    MSG_LOGO       logos[2], plogo, logo;
    const char    *ring_names[2];
    long           bufsz = 65536, size;
    char          *buf;
    time_t         last_retry;

    if (argc != 2) {
        fprintf(stderr, "Uso: hypodb_ingest <hypodb.d>\n");
        return 1;
    }
    if (hypodb_config_load(&cfg, argv[1]) != 0) {
        fprintf(stderr, "hypodb_ingest: error de config %s\n", argv[1]);
        return 1;
    }
    if (GetType("TYPE_HYP2000ARC", &t_arc) != 0 ||
        GetType("TYPE_MAGNITUDE", &t_mag) != 0 ||
        GetType("TYPE_PICK_SCNL", &t_pick) != 0) {
        fprintf(stderr, "hypodb_ingest: tipos EW desconocidos\n");
        return 1;
    }

    ring_names[0] = cfg.in_ring;
    ring_names[1] = cfg.in_ring_ref;
    for (int i = 0; i < 2; i++) {
        long key = GetKey((char *)ring_names[i]);
        if (key == -1) {
            fprintf(stderr, "hypodb_ingest: anillo invalido <%s>\n",
                    ring_names[i]);
            return 1;
        }
        tport_attach(&r[i], key);
    }
    {
        long key = GetKey(cfg.in_pick_ring);
        if (key == -1) {
            fprintf(stderr, "hypodb_ingest: anillo invalido <%s>\n",
                    cfg.in_pick_ring);
            return 1;
        }
        tport_attach(&r_pick, key);
    }

    logos[0].instid = INST_WILDCARD;
    logos[0].mod = MOD_WILDCARD;
    logos[0].type = t_arc;
    logos[1].instid = INST_WILDCARD;
    logos[1].mod = MOD_WILDCARD;
    logos[1].type = t_mag;
    plogo.instid = INST_WILDCARD;
    plogo.mod = MOD_WILDCARD;
    plogo.type = t_pick;

    buf = malloc((size_t)bufsz);
    if (!buf)
        return 1;

    /* Purga inicial de los anillos hypo: arrancamos en el presente. */
    for (int i = 0; i < 2; i++)
        while (tport_getmsg(&r[i], logos, 2, &logo, &size, buf, bufsz) == GET_OK)
            ;

    st = hypodb_state_new(&cfg);
    if (!st) {
        free(buf);
        return 1;
    }

    if (ewguidb_config_load(cfg.db_env_file, &dbcfg) == 0) {
        if (ewguidb_open(&dbcfg, &db) == 0) {
            hypodb_state_set_db(st, db);
            hypodb_load_from_db(st);
        } else {
            fprintf(stderr, "hypodb_ingest: no conecta a la BD: %s\n",
                    ewguidb_last_error(db));
            ewguidb_close(db);
            db = NULL;
        }
    } else {
        fprintf(stderr, "hypodb_ingest: no se pudo leer %s\n", cfg.db_env_file);
    }

    fprintf(stderr, "hypodb_ingest: %s + %s + %s -> BD\n",
            cfg.in_ring, cfg.in_ring_ref, cfg.in_pick_ring);

    last_retry = time(NULL);
    while (tport_getflag(&r[0]) != TERMINATE) {
        int any = 0;

        for (int i = 0; i < 2; i++) {
            int n = 0;
            while (n < MAX_PER_RING &&
                   tport_getmsg(&r[i], logos, 2, &logo, &size, buf, bufsz)
                       == GET_OK) {
                dispatch(st, &logo, t_arc, t_mag, i == 0 ? 162 : 163, buf,
                         size);
                any = 1;
                n++;
            }
        }
        {
            int n = 0;
            while (n < MAX_PER_RING &&
                   tport_getmsg(&r_pick, &plogo, 1, &logo, &size, buf, bufsz)
                       == GET_OK) {
                hypodb_feed_pick(st, buf, (size_t)size);
                any = 1;
                n++;
            }
        }

        if (!db && time(NULL) - last_retry >= 10) {
            last_retry = time(NULL);
            if (ewguidb_config_load(cfg.db_env_file, &dbcfg) == 0 &&
                ewguidb_open(&dbcfg, &db) == 0) {
                hypodb_state_set_db(st, db);
                hypodb_load_from_db(st);
                fprintf(stderr, "hypodb_ingest: BD conectada\n");
            } else {
                ewguidb_close(db);
                db = NULL;
            }
        }
        if (!any)
            usleep(20000);
    }

    for (int i = 0; i < 2; i++)
        tport_detach(&r[i]);
    tport_detach(&r_pick);
    hypodb_state_free(st);
    if (db)
        ewguidb_close(db);
    free(buf);
    return 0;
}
