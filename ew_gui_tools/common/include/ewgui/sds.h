#ifndef EWGUI_SDS_H
#define EWGUI_SDS_H

#include <stddef.h>

/*
 * Ruta de un archivo en el layout SDS de SeisComP (sin GTK):
 *
 *   <root>/<YYYY>/<NET>/<STA>/<CHAN>.D/<NET>.<STA>.<LOC>.<CHAN>.D.<YYYY>.<DDD>
 *
 * (igual que `earthworm_8.0/src/archiving/mseedarchplayer/mseedfilename.c`).
 * `t` es un epoch UTC en segundos. Devuelve 0 si OK; -1 si error.
 *
 * `loc` se normaliza a la convencion del SDS: el location "nulo" de EarthWorm
 * (`"--"`, LOC_NULL_STRING) y el vacio/solo-espacios se escriben vacios
 * (`NET.STA..CHAN`); los locations reales (`00`, `10`, `04`, ...) pasan tal cual.
 */
int ewgui_sds_path(char *out, size_t outsz, const char *root,
                   const char *net, const char *sta, const char *loc,
                   const char *chan, double t);

#endif /* EWGUI_SDS_H */
