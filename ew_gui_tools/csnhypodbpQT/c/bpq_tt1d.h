/*
 * bpq_tt1d.h - Tiempos de viaje P en un modelo 1D de capas.
 *
 * Port del camino que usa HYPOINVERSE para las bandas Potin de Chile
 * (`CRH` -> `HYCRH` -> `HYCRE2` -> `TRVCON`): capas planas horizontales de
 * velocidad constante, fuente a profundidad, estacion en el techo del modelo.
 * Es el MISMO modelo con el que `hyp2000_ring` localiza, asi que la P teorica
 * de la GUI deja de contradecir a la solucion que se esta mostrando.
 *
 * Dos piezas:
 *   1. `BpqTt1dModel`: un `.crh` (linea 1 = nombre; luego "Vp prof" en 2F5.2,
 *      la ultima capa es semiespacio) y `bpq_tt1d_time()` = directo + ondas
 *      refractadas, minimo de ramas. Sin tabla: ~1 us por consulta.
 *   2. `BpqTt1dSet`: un `.hyp` (`CRH n file` + `NOD` + `MUL`) y la seleccion de
 *      banda por epicentro con la regla de `hytra.for` (circulo RAD1 y anillo
 *      de transicion DRAD con taper coseno; fuera de todo, el modelo por
 *      defecto).
 *
 * C puro: sin Qt, sin GTK, sin EarthWorm.
 */
#ifndef BPQ_TT1D_H
#define BPQ_TT1D_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BPQ_TT1D_MAX_LAYERS  24    /* HYPOINVERSE NLYR=20 + holgura */
#define BPQ_TT1D_MAX_REGIONS 124   /* HYPOINVERSE NODMAX */

typedef struct BpqTt1dModel BpqTt1dModel;
typedef struct BpqTt1dSet   BpqTt1dSet;

/* Modelos que entran al calculo de UNA solucion (<= 3, pesos normalizados). */
typedef struct {
    int    n;
    int    mod[3];
    double wt[3];
} BpqTt1dSel;

/* --- un modelo de capas (.crh) -------------------------------------------- */

/* 0 = error; `err` queda como "<path>:<linea>: <motivo>". Vp NO decreciente
 * (chile_1d.crh repite 8.48), profundidad estrictamente creciente, primera 0. */
BpqTt1dModel *bpq_tt1d_crh_load(const char *path, char *err, size_t errn);
void          bpq_tt1d_model_free(BpqTt1dModel *m);
int           bpq_tt1d_model_layers(const BpqTt1dModel *m);
const char   *bpq_tt1d_model_name(const BpqTt1dModel *m);

/* Tiempo de viaje P (s). `delta_km` = distancia epicentral; `depth_km` =
 * profundidad de la fuente bajo el techo del modelo (estacion en 0, como
 * HYPOINVERSE con ELEVMX=0). NAN si la entrada no es finita o no hay rama. */
double        bpq_tt1d_time(const BpqTt1dModel *m, double delta_km, double depth_km);

/* --- el conjunto de bandas de un .hyp -------------------------------------- */

/* Lee `CRH n file`, `NOD lat_deg lat_min lon_deg oeste+ lon_min RAD1 DRAD MOD`
 * y `MUL T|F def`. Los paths de CRH se resuelven con `base_dir` (cwd =
 * EW_PARAMS, igual que el refinador). 0 = error; `err` con el motivo. */
BpqTt1dSet *bpq_tt1d_hyp_load(const char *hyp_path, const char *base_dir,
                              char *err, size_t errn);
void        bpq_tt1d_set_free(BpqTt1dSet *s);
int         bpq_tt1d_set_models(const BpqTt1dSet *s);
const char *bpq_tt1d_set_model_name(const BpqTt1dSet *s, int idx);

/* Bandas para un epicentro (lat/lon en grados, longitud ESTE-positivo como el
 * ARC; el .hyp la guarda oeste-positivo y aqui se convierte). 1 = hay algo. */
int    bpq_tt1d_select(const BpqTt1dSet *s, double lat, double lon,
                       BpqTt1dSel *out);

/* T ponderado de las bandas elegidas (NAN si ninguna tiene valor). */
double bpq_tt1d_eval(const BpqTt1dSet *s, const BpqTt1dSel *sel,
                     double delta_km, double depth_km);

#ifdef __cplusplus
}
#endif

#endif /* BPQ_TT1D_H */
