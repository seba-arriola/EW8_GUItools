#ifndef CSNHYPODBP_SDS_H
#define CSNHYPODBP_SDS_H

/*
 * Lee un canal desde el archivo SDS de SeisComP (layout de `ewgui_sds_path`) y
 * devuelve un buffer `TRACE2_HEADER + int32` con `datatype "i4"`, el MISMO
 * contrato que `store_fetch_sta` en csnhypodbp_ws.c (lo consume
 * ws_fetch_finish sin cambios).
 *
 * Devuelve 1 con datos, 0 sin datos (archivo/dia ausente o ventana fuera), <0 error.
 */
int csnhypodbp_sds_fetch(const char *sta, const char *net, const char *chan,
                         const char *loc, double t0, double t1,
                         char **raw, long *len);

/*
 * Predicado puro: 1 si el evento es tan antiguo que la retencion local ya no lo
 * tiene (edad > `fallback_hours` y SDS habilitado con root): se consulta el SDS
 * primero y se omite wave_serverV.
 */
int csnhypodbp_sds_prefer(double age_hours, int enabled, const char *root,
                          int fallback_hours);

#endif /* CSNHYPODBP_SDS_H */
