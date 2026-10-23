/* Log propio del modulo: `$EW_LOG/csnrvQT_<YYYYMMDD>.log`.
 *
 * Consume la clave `LogFile` del .d. Tres cosas no se dejan al azar (las
 * mismas que ew_controllerQT pago):
 *
 *   - el nombre es el LITERAL del proceso, no `argv[1]`: `logit_init` deriva el
 *     fichero del primer argumento truncado en el primer punto, asi que con el
 *     .d saldria `csnrvqt_<fecha>.log` y `diag.sh log csnrvQT` no lo hallaria;
 *   - sin `EW_LOG` escribible `logit_init` TERMINA el proceso (`exit(-1)` sin
 *     la variable, `exit(0)` si no puede abrir); un visor lanzado a mano fuera
 *     del entorno de EarthWorm moriria antes de abrir la ventana;
 *   - un segundo `logit_init` con logflag 0 hace `fclose(fp)` sin poner
 *     `fp = NULL` y el siguiente `fclose` aborta con `free(): double free`.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "earthworm.h"
#include "earthworm_simple_funcs.h"

#include "crv_capi.h"

#define CRV_LOG_NAME "csnrvQT"

static int g_inited = 0;
static int g_flag = 0;

int crv_log_init(int log_file)
{
    int flag = log_file ? 1 : 0;
    const char *dir;

    if (g_inited)
        return g_flag;

    if (flag) {
        dir = getenv("EW_LOG");
        if (!dir || !*dir || access(dir, W_OK) != 0)
            flag = 0;   /* logit_init mataria el proceso */
    }

    logit_init(CRV_LOG_NAME, 0, 1024, flag);
    g_flag = flag;
    g_inited = 1;
    return flag;
}

void crv_logf(const char *fmt, ...)
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
