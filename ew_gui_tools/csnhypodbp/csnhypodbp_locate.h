#ifndef CSNHYPODBP_LOCATE_H
#define CSNHYPODBP_LOCATE_H

#include <stddef.h>
#include <stdint.h>

#include "csnhypodbp_group.h"
#include "csnhypodbp_locator.h"
#include "csnhypodbp_manual.h"

/*
 * csnhypodbp_locate.c - Pipeline OFFLINE del boton `locate` (headless).
 *
 * Cadena: picks -> csnloc offline (DumpHypo 1; el ARC se extrae del log) ->
 * refinador elegido (hyp2000_ring/nlloc_ring, ARC->ARC) -> magnitud
 * (hypos.jsonl + tank -> csnmags offline). Todo por subproceso (popen).
 */

#define CSNHYPODBP_MAG_MAX 32

typedef struct {
    char          sta[16], net[16], chan[16], loc[16];
    double        rate, oldest;
    const int32_t *samples;   /* muestras crudas (INT_MAX = gap) */
    long          nsamp;
} CsnhypodbpTankTrace;

typedef struct {
    char ml[CSNHYPODBP_MAG_MAX];
    char mwp[CSNHYPODBP_MAG_MAX];
    char mb[CSNHYPODBP_MAG_MAX];
    char ms[CSNHYPODBP_MAG_MAX];
} CsnhypodbpMagOut;

/* Archivo de picadas TYPE_PICK_SCNL (solucion base + overlay manual). */
int csnhypodbp_picks_write(const char *path, int type, int mod, int inst,
                           const CsnhypodbpSolution *base,
                           const CsnhypodbpManualPick *mp, int nmp);

/* hypos.jsonl (schema de csnloc offline) de una solucion; net/loc se toman
   del tank por (sta,chan). */
int csnhypodbp_hypos_write(const char *path, const CsnhypodbpSolution *sol,
                           const CsnhypodbpTankTrace *tank, int ntank);

/* Tank EarthWorm (TRACE2_HEADER + int32) desde las trazas en memoria. */
int csnhypodbp_tank_write(const char *path, const CsnhypodbpTankTrace *tr, int n);

/* Primer ARC Y2K entre los marcadores de un log de csnloc. 0 si hay. */
int csnhypodbp_arc_extract(const char *logtext, char *out, size_t outn);

/* Construye un ARC Y2K (para reinyectar una solucion manual por el camino
   normal de procesar_mensaje_sismo). Devuelve 0 si OK. */
int csnhypodbp_arc_build(const CsnhypodbpSolution *sol, int manual, char *out, size_t outn);

/* Parsea una linea JSON de csnmags a "%.1f-%d" por tipo ("" si null). */
int csnhypodbp_mag_parse(const char *json, CsnhypodbpMagOut *mag);

/* --- Persistencia de soluciones manuales (ManualStateFile) --- */
int csnhypodbp_manual_state_save(const char *path, const CsnhypodbpSolution *sol,
                                 const CsnhypodbpMagOut *mag);
int csnhypodbp_manual_state_load(const char *path, CsnhypodbpSolution *out,
                                 CsnhypodbpMagOut *magout, int max);

/* Orquesta la cadena. Devuelve 0 si localiza; `out` = solucion, `mag` =
   magnitudes. `err` recibe el motivo si falla. */
int csnhypodbp_locate_run(const CsnhypodbpLocator *list, int nlist, const char *selected,
                          const char *ew_params, const char *ew_home, const char *ew_version,
                          const char *tmp_root,
                          const CsnhypodbpSolution *base,
                          const CsnhypodbpManualPick *mp, int nmp,
                          const CsnhypodbpTankTrace *tank, int ntank,
                          CsnhypodbpSolution *out, CsnhypodbpMagOut *mag,
                          char *err, size_t errlen);

#endif /* CSNHYPODBP_LOCATE_H */
