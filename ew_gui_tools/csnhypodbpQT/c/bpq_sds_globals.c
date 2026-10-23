/*
 * bpq_sds_globals.c - Globales que la capa SDS de csnhypodbp espera.
 *
 * csnhypodbp_sds.c usa `extern int g_sds_enabled` / `extern char g_sds_root[512]`
 * (csnhypodbp.h:338-339), que en el modulo GTK se definen en csnhypodbp_main.c.
 * Aqui viven en su PROPIO objeto para que cualquier binario que enlace la capa
 * SDS -- el prototipo o los tests headless -- las tenga sin arrastrar todo el
 * port de configuracion (que a su vez pide filtercfg/magcfg/...).
 *
 * El valor efectivo lo fija bpq_config_load() en cada arranque.
 */
#include "bpq_capi.h"

int  g_sds_enabled = 1;
char g_sds_root[512] = "/mnt/sds/archive";
