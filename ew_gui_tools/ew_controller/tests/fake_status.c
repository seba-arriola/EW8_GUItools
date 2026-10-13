/* fake_status - publica un TYPE_STATUS sintetico en un anillo de EarthWorm.
 *
 *   fake_status <anillo> <archivo-de-estado>
 *
 * Sirve para probar ew_controller sin startstop (o sin esperar su respuesta):
 * el archivo es el cuerpo del mensaje, tal cual lo leeria el parser.
 *
 * Solo ESCRIBE en el anillo; no lo crea ni lo destruye. Necesita EW_PARAMS con
 * earthworm.d (GetKey/GetType/GetModId) y que el anillo ya exista.
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

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <anillo> <archivo-de-estado>\n", argv[0]);
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
