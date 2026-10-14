#ifndef CSNHYPODBP_MWWERR_H
#define CSNHYPODBP_MWWERR_H

#include <stddef.h>

/*
 * Extrae el PRIMER error real de una corrida del panel `Mww` para poder mostrarlo
 * en la linea de estado del panel.
 *
 * Por que existe: cuando `run.sh` aborta (p. ej. `coeffs_rec_lut` vacio porque
 * `make_resp_lookup_table` dio `fc=-nan`), el hilo solo puede reportar un
 * `rc=256` generico y el panel decia "sin resultado" sin motivo. El porque esta
 * escrito en el log del motor, asi que se busca ahi.
 *
 * Mira `<rundir>/run.log` y, si no hay nada, `<rundir>/full_output.txt`.
 * Devuelve 0 y deja la linea (sin salto) en `buf`; -1 si no encontro nada.
 *
 * Es logica pura (solo stdio) para poder testearla headless.
 */
int csnhypodbp_mww_first_error(const char *rundir, char *buf, size_t n);

#endif /* CSNHYPODBP_MWWERR_H */
