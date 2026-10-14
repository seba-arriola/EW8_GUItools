/* fake_status - publica un TYPE_STATUS sintetico en un anillo de EarthWorm.
 *
 *   fake_status <anillo> <archivo-de-estado>
 *   fake_status --create <anillo> [KB]     (crea el anillo si no existe: idempotente)
 *
 * Sirve para probar ew_controller sin startstop (o sin esperar su respuesta):
 * el archivo es el cuerpo del mensaje, tal cual lo leeria el parser.
 *
 * El arnes de tests usa un anillo PROPIO (TEST_CTRL_RING) porque el CONTROL_RING
 * de produccion lo comparten startstop y la instancia real de ew_controller y su
 * puntero de lectura es UNO SOLO: el que llega primero se lleva el mensaje, lo que
 * hacia intermitentes los tests (y metia modulos falsos en la GUI del usuario).
 *
 * Necesita EW_PARAMS con earthworm.d (GetKey/GetType/GetModId).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <earthworm.h>
#include <transport.h>

#define MAXBODY 8192

int main(int argc, char *argv[])
{
    SHM_INFO region;
    MSG_LOGO logo;
    long key;
    char body[MAXBODY];
    size_t n;
    FILE *fp;

    if (argc >= 3 && !strcmp(argv[1], "--create")) {
        const char *ring = argv[2];
        int kb = (argc >= 4) ? atoi(argv[3]) : 512;
        key = GetKey(ring);
        if (key == -1) {
            fprintf(stderr, "fake_status: el anillo <%s> no esta en earthworm.d\n", ring);
            return 1;
        }
        if (shmget(key, 0, 0) >= 0) {
            fprintf(stderr, "fake_status: <%s> ya existe (key %ld)\n", ring, key);
            return 0;
        }
        tport_create(&region, 1024L * kb, key);
        if (shmget(key, 0, 0) < 0) {
            fprintf(stderr, "fake_status: no pude crear <%s> (key %ld)\n", ring, key);
            return 1;
        }
        fprintf(stderr, "fake_status: creado <%s> %d KB (key %ld)\n", ring, kb, key);
        return 0;
    }

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <anillo> <archivo-de-estado>\n", argv[0]);
        fprintf(stderr, "     %s --create <anillo> [KB]\n", argv[0]);
        return 2;
    }

    if (GetLocalInst(&logo.instid) != 0) logo.instid = 0;
    if (GetType("TYPE_STATUS", &logo.type) != 0) {
        fprintf(stderr, "fake_status: TYPE_STATUS no esta en la tabla de tipos (EW_PARAMS?)\n");
        return 1;
    }
    /* instid = instalacion local; mod = WILD (0, transport.h:83). El match del
       lector se hace tambien por el instid/mod del MENSAJE, asi que el comodin
       vale para cualquier lector. No se consulta la tabla de modulos (GetModId
       aborta con nombres no registrados). */
    logo.mod = 0;

    key = GetKey(argv[1]);
    if (key == -1) {
        fprintf(stderr, "fake_status: el anillo <%s> no esta en earthworm.d\n", argv[1]);
        return 1;
    }
    if (shmget(key, 0, 0) < 0) {
        fprintf(stderr, "fake_status: el anillo <%s> (key %ld) no existe (esta startstop arriba?)\n",
                argv[1], key);
        return 1;
    }

    fp = fopen(argv[2], "rb");
    if (!fp) { perror(argv[2]); return 1; }
    n = fread(body, 1, sizeof(body), fp);
    fclose(fp);
    if (n == 0) {
        fprintf(stderr, "fake_status: <%s> esta vacio\n", argv[2]);
        return 1;
    }

    tport_attach(&region, key);
    if (tport_putmsg(&region, &logo, (long) n, body) != PUT_OK) {
        fprintf(stderr, "fake_status: tport_putmsg fallo\n");
        tport_detach(&region);
        return 1;
    }
    tport_detach(&region);
    fprintf(stderr, "fake_status: %ld bytes como TYPE_STATUS en <%s> (key %ld)\n",
            (long) n, argv[1], key);
    return 0;
}
