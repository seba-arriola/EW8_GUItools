/*
 * csnhypodbp_magrow.h - Modelo puro de la tabla de magnitudes (sub-pestaña
 * `magnitudes`): UNA fila por estacion, con el valor de la magnitud activa, su
 * residual respecto de la magnitud de red y la marca M/A.
 *
 * Las magnitudes por estacion llegan de dos sitios y con formas distintas:
 *   - en vivo, `MAG_CHAN_INFO` del mensaje TYPE_MAGNITUDE (sta/comp/net/loc);
 *   - del historial, `station_magnitude.channel` = "NET.STA.LOC.CHAN".
 * Por eso la entrada del builder es un registro normalizado y hay un parser
 * para el formato NSLC de la BD.
 */
#ifndef CSNHYPODBP_MAGROW_H
#define CSNHYPODBP_MAGROW_H

#include <stddef.h>

#include "csnhypodbp_statable.h"

#define CSNHYPODBP_MAG_STA_LEN   8
#define CSNHYPODBP_MAG_CHAN_LEN 16
#define CSNHYPODBP_MAGSTA_MAX 512

/* Procedencia que marca una magnitud ajustada en la GUI. Viaja en
 * MAG_INFO.algorithm (MAG_ALG_LEN = 8) y hypodb lo guarda en
 * magnitude.calculation_method: asi la tabla prefiere la ajustada sobre la
 * automatica cuando el origen tiene las dos. */
#define CSNHYPODBP_MAG_MANUAL_ALGO "CSNMan"

/* Valor por estacion de UNA magnitud (ya normalizado). */
typedef struct {
    char   sta[CSNHYPODBP_MAG_STA_LEN];
    char   chan[CSNHYPODBP_MAG_CHAN_LEN];
    double mag;      /* NAN si la estacion no tiene valor */
    double amp;      /* amplitud leida (informativa) */
    double period;
    int    manual;   /* 1 = ajustada por el usuario (columna `stat` = M) */
} CsnhypodbpMagIn;

/* Fila de la tabla. */
typedef struct {
    char   sta[CSNHYPODBP_MAG_STA_LEN];
    char   chan[CSNHYPODBP_MAG_CHAN_LEN];
    double dist_km;
    double mag;
    double res;      /* mag - magnitud de red */
    double amp;
    double period;
    int    manual;
    int    used;     /* 1 = entra al calculo de la magnitud de red */
    int    has_data; /* 1 = la estacion tiene traza descargada */
    int    in_range; /* 1 = dentro de la puerta de distancia de la magnitud */
    int    eligible; /* 1 = has_data && in_range && el filtro A/M/Teo la admite */
    char   win_src;  /* 'M' | 'A' | 'T': de donde sale la ventana */
    char   anchor;   /* 'M' | 'A' | 'T' (teorica) | 0 = sin ancla */
    double anchor_t; /* epoch del ancla (0 si no hay) */
    double dist_deg; /* distancia epicentral (grados) */
    double t0, t1;   /* ventana vigente (s respecto del ancla); NAN si no hay */
} CsnhypodbpMagRow;

/* "NET.STA.LOC.CHAN" (formato de station_magnitude.channel) -> campos. 1 si
 * tiene los 4 campos; si no, los deja vacios. */
int csnhypodbp_magrow_split_nslc(const char *nslc, char *net, size_t nnet,
                                 char *sta, size_t nsta, char *loc, size_t nloc,
                                 char *chan, size_t nchan);

/* Una fila por estacion de `stas` (TODAS, tengan o no valor), con el valor de
 * `vals` emparejado por NOMBRE de estacion (como hace csnloc; si hay repetidos
 * gana el primero) y el residual contra `red`. El ancla de la fila es la P
 * efectiva de la estacion (`stas[i].p`: M > A > Teo) y `t0`/`t1` la ventana por
 * defecto de la magnitud activa, en segundos respecto de esa ancla. Una estacion
 * sin traza (`has_data == 0`) no entra al calculo. Devuelve el numero de filas. */
int csnhypodbp_magrow_build(CsnhypodbpMagRow *out, int max,
                            const CsnhypodbpStaRow *stas, int nsta,
                            const CsnhypodbpMagIn *vals, int nval,
                            double red, double t0, double t1);

/* Fila cruda de `station_magnitude` tal como la necesita este modulo (la BD
 * guarda el canal como "NET.STA.LOC.CHAN"). */
typedef struct {
    const char *channel;
    double read_amplitude;
    double calculated_magnitude;
} CsnhypodbpMagDbRow;

/* Normaliza las filas de `station_magnitude`: parte el canal y toma la magnitud
 * y la amplitud. La capa de BD no distingue NULL de 0, asi que un 0 se trata
 * como "sin dato" (una magnitud jamas vale 0). Devuelve cuantas filas salieron
 * (las que no tengan 4 campos NSLC se descartan). */
int csnhypodbp_magrow_from_db(CsnhypodbpMagIn *out, int max,
                              const CsnhypodbpMagDbRow *rows, int n);

/* Celdas ya formateadas: `mag` ("5.23" o "-"), `res` ("+0.31"/"-1.20"/"-") y
 * `stat` ("M" si es manual, "A" si tiene valor y "-" si no hay dato). */
void csnhypodbp_magrow_mag_text(const CsnhypodbpMagRow *r, char *out, size_t n);
void csnhypodbp_magrow_res_text(const CsnhypodbpMagRow *r, char *out, size_t n);
const char *csnhypodbp_magrow_stat_text(const CsnhypodbpMagRow *r);

/* `ancla`: "M"/"A"/"Teo" (la P efectiva de la estacion) o "-" si no hay. */
const char *csnhypodbp_magrow_anchor_text(const CsnhypodbpMagRow *r);

/* `t0`/`t1`: la ventana vigente como "+10"/"+45" (s respecto del ancla), "-" si
 * la fila no tiene ventana. */
void csnhypodbp_magrow_t0_text(const CsnhypodbpMagRow *r, char *out, size_t n);
void csnhypodbp_magrow_t1_text(const CsnhypodbpMagRow *r, char *out, size_t n);

/* `vent`: de donde sale la ventana ("M" ajustada a mano / "A" default con P
 * observada / "Teo" ancla teorica), "-" si la fila no tiene ventana. */
const char *csnhypodbp_magrow_vent_text(const CsnhypodbpMagRow *r);

/* Recuento por estado de las filas (para la linea de estado):
 *   con_dato  = filas con valor de magnitud
 *   fuera     = filas descartadas por la puerta de distancia
 *   filtradas = con traza y en rango, pero fuera del filtro A/M/Teo
 *   enuso     = filas marcadas para entrar al calculo
 * Cualquiera de las salidas puede ser NULL. */
void csnhypodbp_magrow_counts(const CsnhypodbpMagRow *rows, int n, int *con_dato,
                              int *fuera, int *filtradas, int *enuso);

#endif
