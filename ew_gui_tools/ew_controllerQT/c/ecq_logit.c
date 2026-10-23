/* Log propio del modulo: `$EW_LOG/ew_controllerQT_<YYYYMMDD>.log`.
 *
 * Consume la clave `LogFile` del .d (0 = sin disco, 1 = disco + consola), que
 * hasta ahora se parseaba y no la usaba nadie. Dos cosas no se dejan al azar:
 *
 *   - el nombre es el LITERAL del proceso, no `argv[1]`: `logit_init` deriva el
 *     fichero del primer argumento truncado en el primer punto, asi que con el
 *     .d saldria `ew_controllerqt_<fecha>.log` y `diag.sh log ew_controllerQT`
 *     no lo encontraria (misma razon que `wstore_ingest` pasa su literal);
 *   - con `EW_LOG` ausente o no escribible `logit_init` TERMINA el proceso
 *     (`exit(-1)` sin la variable, `exit(0)` si no puede abrir el fichero), y
 *     un panel lanzado a mano fuera del entorno de EarthWorm moriria antes de
 *     abrir la ventana: en ese caso se queda en consola;
 *   - llamarlo dos veces no es inocuo: `logit_init` con logflag 0 hace
 *     `fclose(fp)` y NO pone `fp = NULL`, asi que el segundo `fclose` aborta
 *     con `free(): double free`; por eso `ecq_log_init` es idempotente.
 *
 * `logit("t", ...)` escribe SOLO al fichero (nunca a stdout/stderr), igual que
 * el unico logit del GTK (ew_controller_main.c:75): no contamina los modos
 * instrumentados (`ECQ_MODE`), que son el oraculo del arnes.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "earthworm.h"
#include "earthworm_simple_funcs.h"

#include "ecq_capi.h"

/* Nombre del log = nombre del PROCESO (el de startstop_unix.d y diag.sh). */
#define ECQ_LOG_NAME "ew_controllerQT"

/* 1 desde el unico `logit_init` valido. Evita `logit()` sin inicializar (que
 * escribiria un WARNING a stderr) en los tests C, que ejercitan el nucleo sin
 * arrancar el modulo. */
static int g_inited = 0;
static int g_flag = 0;

int ecq_log_init(int log_file)
{
    int flag = log_file ? 1 : 0;
    const char *dir;

    /* IDEMPOTENTE a proposito: un segundo `logit_init` con logflag 0 hace
     * `fclose(fp)` sin poner `fp = NULL` (logit_common.c) y el siguiente
     * `fclose` aborta con `free(): double free`. No hay nada que ganar: el
     * fichero no se reabre y los flags solo pueden apagarse. */
    if (g_inited)
        return g_flag;

    if (flag)
    {
        dir = getenv("EW_LOG");
        if (!dir || !*dir || access(dir, W_OK) != 0)
            flag = 0;   /* logit_init mataria el proceso */
    }

    /* 0 = solo consola; 1 = disco + consola (logit_common.c). */
    logit_init(ECQ_LOG_NAME, 0, 1024, flag);
    g_flag = flag;
    g_inited = 1;
    return flag;
}

void ecq_logf(const char *fmt, ...)
{
    char line[512];
    va_list ap;

    if (!g_inited || !fmt)
        return;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    logit("t", "%s", line);
}
