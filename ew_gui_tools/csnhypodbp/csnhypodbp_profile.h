/******************************************************************************
 * csnhypodbp_profile.h                                                        *
 *                                                                            *
 * Nucleo PURO (sin GTK) de los perfiles de profundidad del panel inferior:    *
 * la recta del perfil, la distancia perpendicular, la eleccion del perfil     *
 * mas cercano a una solucion y la lectura de las polilineas de slab/topo.     *
 * Se testea headless.                                                        *
 *                                                                            *
 * La recta se declara por dos extremos (lon1,lat1)-(lon2,lat2) y se usa en    *
 * forma general   |A*lon + B*lat + C| / sqrt(A^2+B^2)   con                    *
 *   A = lat1-lat2,  B = lon2-lon1,  C = lon1*lat2 - lon2*lat1                  *
 * que es equivalente a la formula cartesiana de los scripts del repo test_db   *
 * (|a*lon - lat + b| / sqrt(a^2+1), en grados, sin haversine) y ademas vale    *
 * para una recta vertical.                                                    *
 ******************************************************************************/
#ifndef CSNHYPODBP_PROFILE_H
#define CSNHYPODBP_PROFILE_H

#include <stddef.h>

#define CSNHYPODBP_MAX_PROFILES     64
#define CSNHYPODBP_PROFILE_NAME_MAX 32
#define CSNHYPODBP_PROFILE_PATH_MAX 256
#define CSNHYPODBP_PROFILE_MAX_PTS  65536   /* tope de puntos por polilinea */

typedef struct {
    char   name[CSNHYPODBP_PROFILE_NAME_MAX];
    double lon1, lat1, lon2, lat2;                  /* extremos declarados */
    double a, b;                                    /* lat = a*lon + b (dibujo) */
    char   slab_file[CSNHYPODBP_PROFILE_PATH_MAX];  /* subduccion (km) */
    char   topo_file[CSNHYPODBP_PROFILE_PATH_MAX];  /* topografia (m) */
} CsnhypodbpProfile;

typedef struct {
    CsnhypodbpProfile p[CSNHYPODBP_MAX_PROFILES];
    int n;
} CsnhypodbpProfiles;

/* Polilinea (slab o topo) de un perfil. `val` puede traer NaN: es un hueco y
 * rompe la polilinea (no se une a traves de el). El bbox solo considera los
 * valores finitos; si no hay ninguno queda con lon_min > lon_max (sin datos). */
typedef struct {
    int     n;
    int     truncated;                              /* 1 = archivo mas largo */
    double *lon;
    double *val;
    double  lon_min, lon_max, val_min, val_max;
} CsnhypodbpProfileLine;

/* Agrega un perfil validando la recta (extremos distintos). `slab_file`/
 * `topo_file` pueden ser NULL o "". Devuelve 1 si OK, 0 si error (msg en err). */
int csnhypodbp_profile_add(CsnhypodbpProfiles *ps, const char *name,
                           double lon1, double lat1, double lon2, double lat2,
                           const char *slab_file, const char *topo_file,
                           char *err, size_t errn);

/* Agrega un perfil a partir del resto de una linea de declaracion
 * ("<lon1> <lat1> <lon2> <lat2> [slab_file] [topo_file]", ya sin el nombre).
 * `rest` se consume in situ. Devuelve 1 si OK, 0 si error (msg en err). */
int csnhypodbp_profile_add_tokens(const char *name, char *rest,
                                  CsnhypodbpProfiles *ps,
                                  char *err, size_t errn);

/* Distancia perpendicular en grados de (lat,lon) a la recta del perfil.
 * Devuelve -1.0 si `p` es NULL. */
double csnhypodbp_profile_dist(const CsnhypodbpProfile *p, double lat, double lon);

/* Indice del perfil mas cercano a (lat,lon); -1 si no hay perfiles. */
int csnhypodbp_profile_nearest(const CsnhypodbpProfiles *ps, double lat, double lon);

/* Rellena slab_file/topo_file por convencion "<dir>/slab<name>.tmp" y
 * "<dir>/topo<name>.tmp" en los perfiles que no los traigan ya. `dir` vacio o
 * NULL deja solo el nombre del archivo. */
void csnhypodbp_profiles_default_files(CsnhypodbpProfiles *ps, const char *dir);

/* Lee un archivo de declaraciones, una por linea:
 *   Profile <name> <lon1> <lat1> <lon2> <lat2> [slab_file] [topo_file]
 * Ignora lineas vacias y comentarios (#). `out` se REINICIA. Devuelve el numero
 * de perfiles leidos (>=0) o -1 si hubo error (msg en err). */
int csnhypodbp_profiles_parse(const char *path, CsnhypodbpProfiles *out,
                              char *err, size_t errn);

/* Lee una polilinea de perfil: columnas 1-based `lon_col` y `val_col`
 * separadas por espacio/tab. `NaN` (o columna vacia) es un hueco. Devuelve 0
 * si OK o -1 si error (msg en err). `out` debe liberarse con _line_free. */
int csnhypodbp_profile_line_read(const char *path, int lon_col, int val_col,
                                 CsnhypodbpProfileLine *out, char *err, size_t errn);
void csnhypodbp_profile_line_free(CsnhypodbpProfileLine *l);

#endif /* CSNHYPODBP_PROFILE_H */
