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

/*
 * startstop pide la parada de DOS maneras: pone `TERMINATE` en el anillo, o pone
 * el PID del modulo para pararlo solo a el. Hay que mirar las dos (es lo que
 * hacen csnloc, csnmags, hyp2000_ring y nlloc_ring), y hay que mirarlas TAMBIEN
 * dentro del drenado: el cuerpo de este bucle puede procesar hasta 3x1000
 * mensajes con escrituras a la BD, y startstop solo espera 5 s antes de matar el
 * proceso a la fuerza ("did not self-terminate in 5 sec; terminating it!").
 * Medido 2026-10-14: con solo `!= TERMINATE` y el chequeo al tope, un reinicio
 * del modulo acababa en SIGKILL.
 */
static int terminate_requested(SHM_INFO *r, long mypid)
{
    int flag = tport_getflag(r);

    return flag == TERMINATE || flag == (int)mypid;
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
    long           bufsz = 65536 + 1, size;   /* +1: los parsers exigen NUL final */
    char          *buf;
    time_t         last_retry;
    long           mypid = (long)getpid();

    if (argc != 2) {
        fprintf(stderr, "Uso: hypodb_ingest <hypodb.d>\n");
        return 1;
    }
    if (hypodb_config_load(&cfg, argv[1]) != 0) {
        fprintf(stderr, "hypodb_ingest: error de config %s\n", argv[1]);
        return 1;
    }
    /* Log propio en EW_LOG (`hypodb_ingest_<fecha>.log`), con el modo que pide
       `LogFile` en el .d. Mismo patron que csnhypodbp/csnrv/csntvp
       (`logit_init(argv[1], 0, 1024, LogFile)`). Sin esto el modulo era MUDO: sus
       mensajes iban a stderr, que startstop comparte y `ew_monitor.sh` trunca en
       cada arranque, asi que un fallo (p. ej. el SIGABRT por desbordar el array
       de canales) no dejaba ni una linea que mirar. */
    logit_init(argv[1], 0, 1024, cfg.log_file);
    if (GetType("TYPE_HYP2000ARC", &t_arc) != 0 ||
        GetType("TYPE_MAGNITUDE", &t_mag) != 0 ||
        GetType("TYPE_PICK_SCNL", &t_pick) != 0) {
        logit("e", "hypodb_ingest: tipos EW desconocidos\n");
        return 1;
    }

    ring_names[0] = cfg.in_ring;
    ring_names[1] = cfg.in_ring_ref;
    for (int i = 0; i < 2; i++) {
        long key = GetKey((char *)ring_names[i]);
        if (key == -1) {
            logit("e", "hypodb_ingest: anillo invalido <%s>\n",
                  ring_names[i]);
            return 1;
        }
        tport_attach(&r[i], key);
    }
    {
        long key = GetKey(cfg.in_pick_ring);
        if (key == -1) {
            logit("e", "hypodb_ingest: anillo invalido <%s>\n",
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
        while (tport_getmsg(&r[i], logos, 2, &logo, &size, buf, bufsz - 1)
                   == GET_OK)
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
            logit("e", "hypodb_ingest: no conecta a la BD: %s\n",
                  ewguidb_last_error(db));
            ewguidb_close(db);
            db = NULL;
        }
    } else {
        logit("e", "hypodb_ingest: no se pudo leer %s\n", cfg.db_env_file);
    }

    logit("et", "hypodb_ingest: %s + %s + %s -> BD\n",
          cfg.in_ring, cfg.in_ring_ref, cfg.in_pick_ring);

    last_retry = time(NULL);
    while (!terminate_requested(&r[0], mypid)) {
        int any = 0;

        for (int i = 0; i < 2; i++) {
            int n = 0;
            while (n < MAX_PER_RING &&
                   !terminate_requested(&r[0], mypid) &&
                   tport_getmsg(&r[i], logos, 2, &logo, &size, buf, bufsz - 1)
                       == GET_OK) {
                /* El buffer del anillo NO viene terminado en NUL, y los parsers
                   lo exigen (rd_mag, sscanf, csnhypodbp_parse_solution). */
                buf[size] = '\0';
                dispatch(st, &logo, t_arc, t_mag, i == 0 ? 162 : 163, buf,
                         size);
                any = 1;
                n++;
            }
        }
        {
            int n = 0;
            while (n < MAX_PER_RING &&
                   !terminate_requested(&r[0], mypid) &&
                   tport_getmsg(&r_pick, &plogo, 1, &logo, &size, buf, bufsz - 1)
                       == GET_OK) {
                buf[size] = '\0';
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
                logit("et", "hypodb_ingest: BD conectada\n");
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
