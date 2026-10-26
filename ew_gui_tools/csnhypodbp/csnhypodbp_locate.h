#ifndef CSNHYPODBP_LOCATE_H
#define CSNHYPODBP_LOCATE_H

#include <stddef.h>
#include <stdint.h>

#include "csnhypodbp_group.h"
#include "csnhypodbp_locator.h"
#include "csnhypodbp_manual.h"
#include "csnhypodbp_magmarks.h"

/*
 * csnhypodbp_locate.c - Pipeline OFFLINE del boton `locate` (headless).
 *
 * Cadena: picks -> csnloc offline (DumpHypo 1; el ARC se extrae del log) ->
 * refinador elegido (hyp2000_ring/nlloc_ring, ARC->ARC) -> magnitud
 * (hypos.jsonl + trace -> csnmags offline). Todo por subproceso (popen).
 */

#define CSNHYPODBP_MAG_MAX 32

typedef struct {
    char          sta[16], net[16], chan[16], loc[16];
    double        rate, oldest;
    const int32_t *samples;   /* muestras crudas (INT_MAX = gap) */
    long          nsamp;
} CsnhypodbpTrace;

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
   del trace por (sta,chan). */
int csnhypodbp_hypos_write(const char *path, const CsnhypodbpSolution *sol,
                           const CsnhypodbpTrace *trace, int ntr);

/* hypos.jsonl de magnitudes: origen refinado (`refined`) + SOLO las P de la
 * solucion elegida (`picked`, ya filtrada por g_pick_on/g_filter_*). Una entrada
 * por estacion: csnmags ancla sus 4 ventanas en la fase y no distingue P/S. */
int csnhypodbp_hypos_write_mag(const char *path,
                               const CsnhypodbpSolution *refined,
                               const CsnhypodbpSolution *picked,
                               const CsnhypodbpTrace *trace, int ntr);

/* Trace EarthWorm (TRACE2_HEADER + int32) desde las trazas en memoria. */
int csnhypodbp_trace_write(const char *path, const CsnhypodbpTrace *tr, int n);

/* Primer ARC Y2K entre los marcadores de un log de csnloc. 0 si hay. */
int csnhypodbp_arc_extract(const char *logtext, char *out, size_t outn);

/* Construye un ARC Y2K (para reinyectar una solucion manual por el camino
   normal de procesar_mensaje_sismo). Devuelve 0 si OK. */
int csnhypodbp_arc_build(const CsnhypodbpSolution *sol, int manual, char *out, size_t outn);

/* ARC del snapshot CONFIRMADO a mano: el que se publica en el anillo de
   confirmaciones para que `hypodb` guarde una fila `origin` propia. Exige una
   version 1..9999 (es el sufijo de `origin_id`; 0 no identificaria la fila).
   Devuelve 0 si OK y -1 si la solucion no es identificable. */
int csnhypodbp_confirmation_arc(const CsnhypodbpSolution *sol, char *out,
                                size_t outn);

/* Rellena sol->picks[].residual desde el JSONL que csnloc offline emite a
   stdout: el ARC del log no trae residual, pero el JSONL si ("residual" por
   fase). Solo toca picks con residual NAN, para no pisar el residual que ya
   venga en el ARC de un refinador. Devuelve cuantas picadas actualizo. */
int csnhypodbp_sol_apply_jsonl(CsnhypodbpSolution *sol, const char *jsonl);

/* Parsea una linea JSON de csnmags a "%.1f-%d" por tipo ("" si null). */
int csnhypodbp_mag_parse(const char *json, CsnhypodbpMagOut *mag);

/* Corrida de SOLO magnitudes: recalcula ML/Mwp/Mb/Ms con las ventanas ajustadas a
 * mano por estacion (`WindowOverridesFile`) sin tocar la localizacion. `kind_mask`
 * elige que magnitudes llevan ventana ajustada (bit 0=ML, 1=Mwp, 2=Mb, 3=Ms). */
int csnhypodbp_mag_run(const char *ew_params, const char *ew_home, const char *ew_version,
                       const char *tmp_root,
                       const CsnhypodbpSolution *base,
                       const CsnhypodbpTrace *trace, int ntr,
                       const CsnhypodbpMagOvrRow *ovr, int novr, unsigned kind_mask,
                       CsnhypodbpMagOut *mag, char *err, size_t errlen);

/* 1 si alguna de las magnitudes de la mascara (bit 0=ML..3=Ms; 0 = las cuatro)
 * vino con valor. Una corrida que no calculo nada no debe entrar como candidata
 * (dejaria la barra vacia), asi que se trata como fallo. */
int csnhypodbp_mag_result_ok(const CsnhypodbpMagOut *mag, unsigned kind_mask);

/* --- Persistencia de soluciones manuales (ManualStateFile) --- */
int csnhypodbp_manual_state_save(const char *path, const CsnhypodbpSolution *sol,
                                 const CsnhypodbpMagOut *mag);
int csnhypodbp_manual_state_load(const char *path, CsnhypodbpSolution *out,
                                 CsnhypodbpMagOut *magout, int max);

/* 1 = NO borrar el directorio temporal al terminar y anexar su ruta al motivo
   de error. Para diagnosticar el boton `locate` (clave LocateKeepTmp del .d). */
void csnhypodbp_locate_keep_tmp(int on);

/* Salta el preambulo que algunos refinadores escriben en stdout antes del ARC
   (HYPOINVERSE imprime "NNN STATIONS READ IN."). Devuelve el puntero a la linea
   que parece cabecera de ARC (>=190 chars y 14 digitos iniciales) o NULL. */
char *csnhypodbp_arc_skip_preamble(char *buf);

/* Override de una clave de un .d (cfg temporal del locate). */
typedef struct {
    const char *key;
    const char *value;
} CsnhypodbpCfgOverride;

/* Copia `src` a `dst` linea a linea reemplazando por "<key> <value>" la primera
 * linea cuyo primer token sea `key`; si no existe, la anade al final. Las
 * repeticiones de la misma clave se descartan. 0 = OK, -1 = error de I/O. */
int csnhypodbp_cfg_copy(const char *src, const char *dst,
                        const CsnhypodbpCfgOverride *ovr, int novr);

/* Atajo para la profundidad fija: km < 0 copia literal; km >= 0 anade/reemplaza
 * `FixedDepthKm %.3f`. 0 = OK, -1 = error de I/O. */
int csnhypodbp_cfg_with_fixdepth(const char *src, const char *dst, double fix_depth_km);

/* Copia el `.d` del refinador con `WorkDir <workdir>` (si no es NULL/vacio) y
 * `FixedDepthKm %.3f` (si km >= 0). El `WorkDir` aislado evita que la corrida
 * offline comparta `obs.nll`/`nll_ring.in`/`arcIn` con el demonio. */
int csnhypodbp_cfg_with_refiner_opts(const char *src, const char *dst,
                                     const char *workdir, double fix_depth_km);

/* Etapa intermedia: se invoca tras `csnloc` y antes del refinador, con la
 * solucion base ya parseada. Corre en el hilo worker; el callback NO toca GTK. */
typedef void (*CsnhypodbpLocateStageFn)(const CsnhypodbpSolution *base, void *ud);

/* Fija la identidad offline de una solucion: `mod` del localizador elegido y,
 * si el ARC no los trae, `qid`/`version` de la base. No toca lat/lon/otime ni
 * las picadas. */
void csnhypodbp_locate_identity(const CsnhypodbpSolution *base,
                                const CsnhypodbpLocator *sel,
                                CsnhypodbpSolution *out);

/* Orquesta la cadena. Devuelve 0 si localiza; `out` = solucion, `mag` =
   magnitudes. `err` recibe el motivo si falla. `fix_depth_km` < 0 = profundidad
   libre; >= 0 = `FixedDepthKm` en la cfg temporal de csnloc y del refinador.
   `timeout_sec` <= 0 = sin limite. `cancel` (opcional) aborta al ponerse a 1. */int csnhypodbp_locate_run(const CsnhypodbpLocator *list, int nlist, const char *selected,
                          const char *ew_params, const char *ew_home, const char *ew_version,
                          const char *tmp_root,
                          const CsnhypodbpSolution *base,
                          const CsnhypodbpManualPick *mp, int nmp,
                          const CsnhypodbpTrace *trace, int ntr,
                          const CsnhypodbpMagOvrRow *ovr, int novr, unsigned kind_mask,
                          double fix_depth_km, int timeout_sec,
                          CsnhypodbpLocateStageFn on_stage, void *stage_ud,
                          volatile int *cancel,
                          CsnhypodbpSolution *out, CsnhypodbpMagOut *mag,
                          char *err, size_t errlen);

#endif /* CSNHYPODBP_LOCATE_H */
