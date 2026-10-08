/******************************************************************************
 * nlloc_ring.c                                                               *
 *                                                                            *
 * Modulo EarthWorm de ANILLO que refina hipocentros con NonLinLoc (NLLoc).   *
 *                                                                            *
 * Igual que hyp2000_ring: lee los ARC de csnloc (HYPO_RING), convierte las   *
 * fases a observaciones NLLOC_OBS, llama a NLLoc() ENLAZADO como libreria,   *
 * y escribe el ARC refinado en otro anillo. NO usa la cadena sausage.        *
 *                                                                            *
 *   InRing (TYPE_HYP2000ARC) -> obs NLLOC -> NLLoc() -> OutRing (ARC)        *
 *                                                                            *
 * El control de NLLoc es un fichero plantilla (ControlFile); el modulo solo  *
 * reemplaza la linea LOCFILES (obs + ttimeRoot + outRoot). NLLoc escribe el  *
 * ARC Y2K en <outRoot>.sum.grid0.loc.arc (LOCHYPOUT SAVE_HYPOINVERSE_Y2000_ARC*
 * en la plantilla).                                                          *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "kom.h"
#include "earthworm.h"
#include "transport.h"

#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif

/* NLLoc() (libreria NonLinLoc enlazada). LocNode se usa solo como puntero. */
typedef struct LocNode LocNode;
extern int NLLoc(char *pid_main, char *fn_control_main,
                 char **param_line_array, int n_param_lines,
                 char **obs_line_array, int n_obs_lines,
                 int return_locations, int return_oct_tree_grid,
                 int return_scatter_sample, LocNode **ploc_list_head);

#define MAX_STR  256
#define MAX_ARC  65536
#define MAX_MSG  MAX_ARC
#define MAX_OBS  (256 * 1024)

#ifndef RAD
#define RAD 0.017453292519943
#endif

static char  MyModName[MAX_STR]   = "MOD_NLLOC_RING";
static char  InRingName[MAX_STR]  = "HYPO_RING";
static char  OutRingName[MAX_STR] = "HYPO_RING_REF2";
static char  ControlFile[MAX_STR] = "";
static char  TtimeRoot[MAX_STR]   = "";
static char  OutRoot[MAX_STR]     = "";
static char  WorkDir[MAX_STR]     = "";
static char  GridFile[MAX_STR]    = "";
static char  SourceCode[2]        = "W";
static int   HeartbeatInt = 30;
static int   LogFile      = 1;
static int   Debug        = 0;
static int   MinPhases    = 0;
static double MaxRMS      = 0.0;
static int   NllIntervalSec = 300;   /* 0 = sin rate-limit (procesa cada version) */
static int   NllTTLSec      = 1800;  /* olvidar un evento sin versiones nuevas */
static int   NllMaxPending  = 64;

/* Volumen de busqueda: si la plantilla lo deja pequeno/fijo, NLLoc devuelve la
   solucion pegada al borde. Estas claves reescriben TRANS y LOCGRID. */
static int    HaveTrans   = 0;
static double TransLat    = 0.0, TransLon = 0.0;
static int    HaveLocGrid = 0;
static int    LG_nx = 0, LG_ny = 0, LG_nz = 0;
static double LG_x0 = 0.0, LG_y0 = 0.0, LG_z0 = 0.0;
static double LG_dx = 0.0, LG_dy = 0.0, LG_dz = 0.0;
/* Si > 0, re-centra el LOCGRID de la banda elegida en el EPICENTRO DE ENTRADA
   con este lado (km), recortado al alcance del modelo. NLLoc no acepta
   hipocentro semilla, asi que el volumen de busqueda es la unica forma de
   decirle donde mirar: con un LOCGRID fijo la solucion se pega al borde, y con
   uno gigante (p.ej. el modelo fusionado de todo Chile, 409x945x80) el octree
   NO converge. 0 = usar el LOCGRID de la plantilla tal cual. */
static double LocGridKm = 0.0;

/* Bandas de modelo 3D: cada una aporta su TtimeRoot y ControlFile. Las bandas
   se solapan (~4 grados), asi que entre las que contienen el hipocentro gana
   la que cubre MAS estaciones del evento (ver select_model). */
#define MAX_BANDS 16
#define MAX_STA   128
typedef struct {
    char   name[MAX_STR];
    double latmin, latmax, lonmin, lonmax;
    char   ttroot[MAX_STR];
    char   ctl[MAX_STR];
} MODEL_BAND;
static MODEL_BAND bands[MAX_BANDS];
static int        nbands = 0;
static char       FallbackTtRoot[MAX_STR] = "";
static char       FallbackCtl[MAX_STR]    = "";
/* TransOrigin/LocGrid son globales: solo se aplican si el modelo elegido NO es
   una banda (si lo fueran, pisarian el TRANS/LOCGRID propio de la banda). */
static int        g_use_global_grid = 1;

static SHM_INFO      InRegion, OutRegion;
static long          InKey = 0, OutKey = 0;
static unsigned char MyInstId, MyModId;
static unsigned char TypeHyp2000Arc, TypeHeartbeat, TypeError;

static int    HaveGrid = 0;
static double GLatMin, GLatMax, GLonMin, GLonMax;

/* ------------------------------------------------------------------------- */
/* Configuracion                                                             */
/* ------------------------------------------------------------------------- */
static int read_config(const char *file)
{
    char *com, *str;

    if (!k_open(file)) { fprintf(stderr, "nlloc_ring: no se pudo abrir %s\n", file); return -1; }
    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;
        if      (k_its("MyModuleId"))   { str = k_str(); if (str) strncpy(MyModName, str, sizeof(MyModName)-1); }
        else if (k_its("InRing"))       { str = k_str(); if (str) strncpy(InRingName, str, sizeof(InRingName)-1); }
        else if (k_its("OutRing"))      { str = k_str(); if (str) strncpy(OutRingName, str, sizeof(OutRingName)-1); }
        else if (k_its("HeartBeatInt")) HeartbeatInt = k_int();
        else if (k_its("LogFile"))      LogFile = k_int();
        else if (k_its("Debug"))        Debug = k_int();
        else if (k_its("SourceCode"))   { str = k_str(); if (str) SourceCode[0] = str[0]; }
        else if (k_its("ControlFile"))  { str = k_str(); if (str) strncpy(ControlFile, str, sizeof(ControlFile)-1); }
        else if (k_its("TtimeRoot"))    { str = k_str(); if (str) strncpy(TtimeRoot, str, sizeof(TtimeRoot)-1); }
        else if (k_its("OutRoot"))      { str = k_str(); if (str) strncpy(OutRoot, str, sizeof(OutRoot)-1); }
        else if (k_its("WorkDir"))      { str = k_str(); if (str) strncpy(WorkDir, str, sizeof(WorkDir)-1); }
        else if (k_its("GridFile"))     { str = k_str(); if (str) strncpy(GridFile, str, sizeof(GridFile)-1); }
        else if (k_its("MinPhases"))    MinPhases = k_int();
        else if (k_its("MaxRMS"))       MaxRMS = k_val();
        else if (k_its("NllIntervalSec")) NllIntervalSec = k_int();
        else if (k_its("NllTTLSec"))      NllTTLSec = k_int();
        else if (k_its("NllMaxPending"))  NllMaxPending = k_int();
        else if (k_its("LocGridKm"))      LocGridKm = k_val();
        else if (k_its("TransOrigin")) {
            str = k_str();
            if (str) { TransLat = atof(str); TransLon = k_val(); HaveTrans = 1; }
        }
        else if (k_its("LocGrid")) {
            LG_nx = k_int(); LG_ny = k_int(); LG_nz = k_int();
            LG_x0 = k_val(); LG_y0 = k_val(); LG_z0 = k_val();
            LG_dx = k_val(); LG_dy = k_val(); LG_dz = k_val();
            HaveLocGrid = 1;
        }
        else if (k_its("ModelBand")) {
            if (nbands >= MAX_BANDS) {
                fprintf(stderr, "nlloc_ring: demasiadas ModelBand (max %d)\n", MAX_BANDS);
                k_close(); return -1;
            }
            str = k_str();
            if (str) strncpy(bands[nbands].name, str, sizeof(bands[nbands].name)-1);
            bands[nbands].latmin = k_val(); bands[nbands].latmax = k_val();
            bands[nbands].lonmin = k_val(); bands[nbands].lonmax = k_val();
            str = k_str();
            if (str) strncpy(bands[nbands].ttroot, str, sizeof(bands[nbands].ttroot)-1);
            str = k_str();
            if (str) strncpy(bands[nbands].ctl, str, sizeof(bands[nbands].ctl)-1);
            nbands++;
        }
        else if (k_its("ModelFallback")) {
            str = k_str();
            if (str) strncpy(FallbackTtRoot, str, sizeof(FallbackTtRoot)-1);
            str = k_str();
            if (str) strncpy(FallbackCtl, str, sizeof(FallbackCtl)-1);
        }
        else continue;
        if (k_err()) { fprintf(stderr, "nlloc_ring: error de config en '%s'\n", com); k_close(); return -1; }
    }
    k_close();
    return 0;
}

static int load_grid_bbox(const char *path)
{
    FILE *fp = fopen(path, "r");
    char  line[512];
    int   got = 0;
    /* El modulo hace chdir(WorkDir): si GridFile es relativo, resolverlo
       contra EW_PARAMS (donde vive el .grid de csnloc). */
    if (!fp && path[0] != '/') {
        const char *ep = getenv("EW_PARAMS");
        if (ep && ep[0]) {
            char full[MAX_STR];
            snprintf(full, sizeof(full), "%s/%s", ep, path);
            fp = fopen(full, "r");
            if (fp) logit("t", "nlloc_ring: GridFile resuelto contra EW_PARAMS: %s\n", full);
        }
    }
    if (!fp) { logit("et", "nlloc_ring: no se pudo abrir GridFile <%s>\n", path); return -1; }
    while (fgets(line, sizeof(line), fp)) {
        char *h = strchr(line, '#'); if (h) *h = '\0';
        if      (sscanf(line, "LatMin %lf", &GLatMin) == 1) got |= 1;
        else if (sscanf(line, "LatMax %lf", &GLatMax) == 1) got |= 2;
        else if (sscanf(line, "LonMin %lf", &GLonMin) == 1) got |= 4;
        else if (sscanf(line, "LonMax %lf", &GLonMax) == 1) got |= 8;
    }
    fclose(fp);
    if (got != 15) { logit("et", "nlloc_ring: GridFile sin bbox completo\n"); return -1; }
    HaveGrid = 1;
    logit("t", "nlloc_ring: region lat[%.3f..%.3f] lon[%.3f..%.3f]\n",
          GLatMin, GLatMax, GLonMin, GLonMax);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* ARC -> observaciones NLLOC_OBS                                            */
/* ------------------------------------------------------------------------- */
static int arc_header(const char *msg, int len,
                      double *lat, double *lon, double *z,
                      int *nph, double *rms, double *gap)
{
    char b[8];
    if (len < 52) return -1;
    if (lat) {
        double d, m; char dir;
        b[0]=msg[16]; b[1]=msg[17]; b[2]=0; d=atof(b); dir=msg[18];
        b[0]=msg[19]; b[1]=msg[20]; b[2]=msg[21]; b[3]=msg[22]; b[4]=0; m=atof(b)/100.0;
        *lat = d + m/60.0; if (dir=='S') *lat = -*lat;
    }
    if (lon) {
        double d, m; char dir;
        b[0]=msg[23]; b[1]=msg[24]; b[2]=msg[25]; b[3]=0; d=atof(b); dir=msg[26];
        b[0]=msg[27]; b[1]=msg[28]; b[2]=msg[29]; b[3]=msg[30]; b[4]=0; m=atof(b)/100.0;
        *lon = d + m/60.0; if (dir=='W') *lon = -*lon;
    }
    if (z)   { b[0]=msg[31]; b[1]=msg[32]; b[2]=msg[33]; b[3]=msg[34]; b[4]=msg[35]; b[5]=0; *z=atof(b)/100.0; }
    if (nph) { b[0]=msg[39]; b[1]=msg[40]; b[2]=msg[41]; b[3]=0; *nph=atoi(b); }
    if (gap) { b[0]=msg[42]; b[1]=msg[43]; b[2]=msg[44]; b[3]=0; *gap=atof(b); }
    if (rms) { b[0]=msg[48]; b[1]=msg[49]; b[2]=msg[50]; b[3]=msg[51]; b[4]=0; *rms=atof(b)/100.0; }
    return 0;
}

/* qid (offset 136,10) y version (offset 178,4 con fallback 161,1) del ARC. */
static void arc_parse_id(const char *arc, int len,
                         char *qid, size_t qsz, char *ver, size_t vsz)
{
    if (qsz) {
        size_t n = (qsz - 1 < 10) ? qsz - 1 : 10;
        if (len >= (int)(136 + n)) {
            size_t i;
            memcpy(qid, arc + 136, n); qid[n] = '\0';
            for (i = n; i > 0 && qid[i-1] == ' '; i--) qid[i-1] = '\0';
        } else qid[0] = '\0';
    }
    if (vsz >= 2) {
        if (len >= 182) {
            size_t i;
            memcpy(ver, arc + 178, 4); ver[4] = '\0';
            for (i = 4; i > 0 && ver[i-1] == ' '; i--) ver[i-1] = '\0';
            if (ver[0] == '\0' && len >= 162) { ver[0] = arc[161]; ver[1] = '\0'; }
        } else if (len >= 162) { ver[0] = arc[161]; ver[1] = '\0'; }
        else ver[0] = '\0';
    }
}

/* Reescribe el ARC de salida de NLLoc: copia qid/version del ARC de csnloc
   (para que csnhypodbp agrupe el refinado con el evento de csnloc) y normaliza
   longitudes (header -> 197, fases -> 114). Devuelve la nueva longitud. */
static int arc_rewrite_id(char *arc, int len, const char *arc_in, int in_len)
{
    static char tmp[MAX_ARC];
    int tlen = 0, first = 1;
    const char *p = arc, *end = arc + len;
    char qid[11], v178[5] = "0000";
    char v161 = '0';

    if (in_len >= 146) memcpy(qid, arc_in + 136, 10);
    else               memset(qid, ' ', 10);
    qid[10] = '\0';
    if (in_len >= 162) v161 = arc_in[161];
    if (in_len >= 182) { memcpy(v178, arc_in + 178, 4); v178[4] = '\0'; }

    while (p < end && tlen < MAX_ARC - 260) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        int l = nl ? (int)(nl - p) : (int)(end - p);
        int is_header, want;
        if (l > 0 && p[l-1] == '\r') l--;
        if (l <= 0) { if (nl) { p = nl + 1; continue; } break; }

        is_header = first;
        want = is_header ? 197 : 114;
        if (l < want) {
            memcpy(tmp + tlen, p, (size_t)l);
            memset(tmp + tlen + l, ' ', (size_t)(want - l));
            l = want;
        } else {
            memcpy(tmp + tlen, p, (size_t)l);
        }
        if (is_header) {
            memcpy(tmp + tlen + 136, qid, 10);
            tmp[tlen + 161] = v161;
            memcpy(tmp + tlen + 178, v178, 4);
            /* NLLoc escribe 'E' para el este y BLANCO para el oeste
               (NLLocLib.c:12965); csnloc/csnhypodbp usan 'W'. Normalizamos. */
            if (tmp[tlen + 26] != 'E') tmp[tlen + 26] = 'W';
            first = 0;
        } else if (l >= 34) {
            /* El estandar Y2K de la linea de fase pone el tiempo en centesimas
               enteras (SSCCC, sin punto); csnloc/csnhypodbp usan SS.ss. */
            char sec[6], buf[8];
            memcpy(sec, tmp + tlen + 29, 5); sec[5] = '\0';
            if (!strchr(sec, '.')) {
                int cs = atoi(sec);
                snprintf(buf, sizeof(buf), "%05.2f", cs / 100.0);
                memcpy(tmp + tlen + 29, buf, 5);
            }
        }
        tmp[tlen + l] = '\n';
        tlen += l + 1;
        if (!nl) break;
        p = nl + 1;
    }
    memcpy(arc, tmp, (size_t)tlen);
    arc[tlen] = '\0';
    return tlen;
}

/* Una linea de log por refinamiento, con entrada y salida. */
static void log_result(const char *mod, const char *arc_in, int in_len,
                       const char *arc_out, int out_len, long dt_ms)
{
    double ilat=0, ilon=0, iz=0, irms=0, igap=0;
    double olat=0, olon=0, oz=0, orms=0, ogap=0;
    int    inph=0, onph=0;
    char   qid[24] = "?", ver[16] = "?";

    arc_header(arc_in, in_len, &ilat, &ilon, &iz, &inph, &irms, &igap);
    arc_header(arc_out, out_len, &olat, &olon, &oz, &onph, &orms, &ogap);
    arc_parse_id(arc_in, in_len, qid, sizeof(qid), ver, sizeof(ver));

    logit("t", "%s: LOC ev=%s v=%s in{nph=%d lat=%.3f lon=%.3f z=%.2f rms=%.2f} "
               "out{nph=%d lat=%.3f lon=%.3f z=%.2f rms=%.2f gap=%.0f} dt_ms=%ld\n",
          mod, qid, ver, inph, ilat, ilon, iz, irms,
          onph, olat, olon, oz, orms, ogap, dt_ms);
}

/* Recorre las lineas de fase del ARC y escribe NLLOC_OBS en `path`.        */
static int write_obs(const char *msg, int len, const char *path)
{
    FILE *fp = fopen(path, "w");
    const char *p = msg;
    const char *end = msg + len;
    int nph = 0;

    if (!fp) return -1;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        int l = nl ? (int)(nl - p) : (int)(end - p);
        if (l >= 114 && p[0] != '$' && (p[14] == 'P' || p[14] == 'S')) {
            char sta[8], net[4], chan[8], phase[4];
            char date[16], hhmm[8];
            double ss;
            /* linea de fase ARC: sta[0:5] net[5:7] chan[9:12] fase[14] time[17:34] */
            memcpy(sta, p, 5); sta[5] = '\0';
            memcpy(net, p + 5, 2); net[2] = '\0';
            memcpy(chan, p + 9, 3); chan[3] = '\0';
            phase[0] = p[14]; phase[1] = '\0';
            memcpy(date, p + 17, 8); date[8] = '\0';
            memcpy(hhmm, p + 25, 4); hhmm[4] = '\0';
            {
                char sec[8];
                memcpy(sec, p + 29, 5); sec[5] = '\0';
                ss = atof(sec);
            }
            if (phase[0] != 'P' && phase[0] != 'S') { phase[0] = 'P'; }
            fprintf(fp, "%-5s ? %-3s ? %s ? %s %s %7.3f GAU 0.1 0.0 0.0 0.0\n",
                    sta, chan, phase, date, hhmm, ss);
            nph++;
        }
        if (!nl) break;
        p = nl + 1;
    }
    fclose(fp);
    return nph;
}

/* ------------------------------------------------------------------------- */
/* Control de NLLoc: copia la plantilla reemplazando LOCFILES               */
/*                                                                            */
/* Si `LocGridKm > 0` y el modelo elegido es una banda, ademas RE-CENTRA el   */
/* LOCGRID en el epicentro de entrada (recortado al alcance del modelo).      */
/* ------------------------------------------------------------------------- */
static int build_control(const char *tmpl, const char *out,
                         const char *obs, const char *ttroot, const char *outroot,
                         double epi_lat, double epi_lon)
{
    FILE *fi = fopen(tmpl, "r");
    FILE *fo = fopen(out, "w");
    char  line[2048];
    int   replaced = 0, trans = 0, grid = 0;
    double tlat = 0.0, tlon = 0.0;
    int   have_trans = 0;

    if (!fi || !fo) { if (fi) fclose(fi); if (fo) fclose(fo); return -1; }
    while (fgets(line, sizeof(line), fi)) {
        if (!replaced && strncmp(line, "LOCFILES", 8) == 0) {
            fprintf(fo, "LOCFILES %s NLLOC_OBS %s %s 0\n", obs, ttroot, outroot);
            replaced = 1;
            continue;
        }
        /* El origen del TRANS de la banda hace falta para pasar el epicentro a
           la grilla en km; la linea TRANS va antes del LOCGRID. */
        if (strncmp(line, "TRANS", 5) == 0) {
            if (sscanf(line, "TRANS SIMPLE %lf %lf", &tlat, &tlon) == 2)
                have_trans = 1;
            if (HaveTrans && g_use_global_grid && !trans) {
                fprintf(fo, "TRANS SIMPLE %.6f %.6f 0.0\n", TransLat, TransLon);
                trans = 1;
                continue;
            }
        }
        if (strncmp(line, "LOCGRID", 7) == 0) {
            if (HaveLocGrid && g_use_global_grid && !grid) {
                fprintf(fo, "LOCGRID %d %d %d %.4f %.4f %.4f %.4f %.4f %.4f PROB_DENSITY SAVE\n",
                        LG_nx, LG_ny, LG_nz, LG_x0, LG_y0, LG_z0, LG_dx, LG_dy, LG_dz);
                grid = 1;
                continue;
            }
            if (LocGridKm > 0.0 && have_trans && !grid) {
                int    nx, ny, nz, n;
                double x0, y0, z0, dx, dy, dz;
                char   rest[128] = "";
                n = sscanf(line,
                           "LOCGRID %d %d %d %lf %lf %lf %lf %lf %lf%127[^\n]",
                           &nx, &ny, &nz, &x0, &y0, &z0, &dx, &dy, &dz, rest);
                if (n >= 9 && nx > 0 && ny > 0 && dx > 0.0 && dy > 0.0) {
                    int    nx2, ny2;
                    double klon, xe, ye, xa, xb, ya, yb;
                    klon = 111.32 * cos(tlat * RAD);
                    xe = (epi_lon - tlon) * klon;
                    ye = (epi_lat - tlat) * 111.32;
                    nx2 = (int)(LocGridKm / dx) + 1;
                    ny2 = (int)(LocGridKm / dy) + 1;
                    if (nx2 > nx) nx2 = nx;
                    if (ny2 > ny) ny2 = ny;
                    if (nx2 < 3) nx2 = (nx < 3 ? nx : 3);
                    if (ny2 < 3) ny2 = (ny < 3 ? ny : 3);
                    xa = x0; xb = x0 + (nx - nx2) * dx;
                    ya = y0; yb = y0 + (ny - ny2) * dy;
                    x0 = xe - (nx2 - 1) * dx / 2.0;
                    y0 = ye - (ny2 - 1) * dy / 2.0;
                    if (x0 < xa) x0 = xa; else if (x0 > xb) x0 = xb;
                    if (y0 < ya) y0 = ya; else if (y0 > yb) y0 = yb;
                    fprintf(fo, "LOCGRID %d %d %d %.4f %.4f %.4f %.4f %.4f %.4f%s\n",
                            nx2, ny2, nz, x0, y0, z0, dx, dy, dz, rest);
                    if (Debug >= 1)
                        logit("t", "nlloc_ring: LOCGRID %.0f km centrado en %.3f/%.3f"
                                   " -> %dx%d desde %.1f/%.1f km\n",
                              LocGridKm, epi_lat, epi_lon, nx2, ny2, x0, y0);
                    grid = 1;
                    continue;
                }
            }
        }
        fputs(line, fo);
    }
    fclose(fi);
    fclose(fo);
    return replaced ? 0 : -1;
}

/* ------------------------------------------------------------------------- */
/* Estaciones unicas del ARC (mismas lineas de fase que usa write_obs).       */
/* ------------------------------------------------------------------------- */
static int arc_stations(const char *msg, int len, char stas[][6], int max)
{
    const char *p = msg, *end = msg + len;
    int n = 0;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        int l = nl ? (int)(nl - p) : (int)(end - p);
        if (l >= 114 && p[0] != '$' && (p[14] == 'P' || p[14] == 'S')) {
            char s[6];
            int i, dup = 0;
            memcpy(s, p, 5);
            s[5] = '\0';
            for (i = 4; i >= 0 && s[i] == ' '; i--) s[i] = '\0';
            for (i = 0; i < n; i++) if (strcmp(stas[i], s) == 0) { dup = 1; break; }
            if (!dup && s[0] != '\0' && n < max) { snprintf(stas[n], 6, "%s", s); n++; }
        }
        if (!nl) break;
        p = nl + 1;
    }
    return n;
}

/* Cuantas de las estaciones del evento tienen grilla de tiempos en la banda. */
static int band_covers(const char *ttroot, char stas[][6], int nsta)
{
    char path[MAX_STR];
    int i, c = 0;

    for (i = 0; i < nsta; i++) {
        snprintf(path, sizeof(path), "%s.P.%s.time.hdr", ttroot, stas[i]);
        if (access(path, R_OK) == 0) { c++; continue; }
        snprintf(path, sizeof(path), "%s.S.%s.time.hdr", ttroot, stas[i]);
        if (access(path, R_OK) == 0) c++;
    }
    return c;
}

/* ------------------------------------------------------------------------- */
/* Elige TtimeRoot + plantilla de control.                                    */
/*                                                                            */
/* Las bandas 3D se SOLAPAN, asi que varias contienen el epicentro. Quedarse  */
/* con la primera es un error: un evento en el borde sur de una banda puede   */
/* tener TODAS sus estaciones en la banda vecina, y entonces esa banda no le  */
/* da ni un tiempo de viaje -> NLLoc se queda sin observaciones y el evento   */
/* se pierde en silencio. Ahora, entre las bandas que contienen el epicentro, */
/* gana la que cubre mas estaciones del evento (empate -> la primera, como    */
/* antes). Sin bandas usa las claves globales, y si el evento cae fuera de    */
/* todas usa el fallback (si esta definido).                                  */
/* ------------------------------------------------------------------------- */
static int select_model(double lat, double lon, char stas[][6], int nsta,
                        char *ttroot, char *tmpl)
{
    int i, best = -1, bestc = -1;

    for (i = 0; i < nbands; i++) {
        int c;
        if (!(lat >= bands[i].latmin && lat <= bands[i].latmax &&
              lon >= bands[i].lonmin && lon <= bands[i].lonmax)) continue;
        c = (nsta > 0) ? band_covers(bands[i].ttroot, stas, nsta) : 0;
        if (c > bestc) { bestc = c; best = i; }
    }
    if (best >= 0) {
        snprintf(ttroot, MAX_STR, "%s", bands[best].ttroot);
        snprintf(tmpl,   MAX_STR, "%s", bands[best].ctl);
        g_use_global_grid = 0;   /* la banda trae su propio TRANS/LOCGRID */
        if (Debug >= 1)
            logit("t", "nlloc_ring: banda %s (cubre %d/%d estaciones)\n",
                  bands[best].name, bestc, nsta);
        return 0;
    }
    if (nbands == 0) {
        snprintf(ttroot, MAX_STR, "%s", TtimeRoot);
        snprintf(tmpl,   MAX_STR, "%s", ControlFile);
        g_use_global_grid = 1;
        return 0;
    }
    if (FallbackCtl[0] != '\0') {
        snprintf(ttroot, MAX_STR, "%s", FallbackTtRoot);
        snprintf(tmpl,   MAX_STR, "%s", FallbackCtl);
        g_use_global_grid = 1;
        if (Debug >= 1) logit("t", "nlloc_ring: fuera de banda -> fallback\n");
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Localiza un ARC. Devuelve 0 y llena arc_out si hubo refinamiento.         */
/* ------------------------------------------------------------------------- */
static int locate_arc(const char *arc_in, int in_len, char *arc_out, int *out_len)
{
    char obs[MAX_STR], ctl[MAX_STR], arcname[MAX_STR];
    char tmpl[MAX_STR], ttroot[MAX_STR];
    char stas[MAX_STA][6];
    char *nllarc;
    FILE *fp;
    size_t n;
    int    nph, nsta;
    double lat, lon, rms;
    int    nhead;

    if (arc_header(arc_in, in_len, &lat, &lon, NULL, &nhead, &rms, NULL) != 0) return -1;
    if (MinPhases > 0 && nhead < MinPhases) return -1;
    if (MaxRMS > 0.0 && rms > MaxRMS) return -1;
    if (HaveGrid && !(lat >= GLatMin && lat <= GLatMax &&
                      lon >= GLonMin && lon <= GLonMax)) {
        if (Debug >= 1) logit("t", "nlloc_ring: fuera de region lat=%.3f lon=%.3f\n", lat, lon);
        return -1;
    }
    nsta = arc_stations(arc_in, in_len, stas, MAX_STA);
    if (select_model(lat, lon, stas, nsta, ttroot, tmpl) != 0) {
        if (Debug >= 1) logit("t", "nlloc_ring: sin banda ni fallback lat=%.3f lon=%.3f\n", lat, lon);
        return -1;
    }

    snprintf(obs, sizeof(obs), "obs.nll");
    nph = write_obs(arc_in, in_len, obs);
    if (nph <= 0) {
        if (Debug >= 1) logit("t", "nlloc_ring: ARC sin lineas de fase utilizables\n");
        return -1;
    }

    snprintf(ctl, sizeof(ctl), "nll_ring.in");
    if (build_control(tmpl, ctl, obs, ttroot, OutRoot, lat, lon) != 0) {
        logit("et", "nlloc_ring: la plantilla <%s> no tiene LOCFILES\n", tmpl);
        return -1;
    }

    snprintf(arcname, sizeof(arcname), "%s.sum.grid0.loc.arc", OutRoot);
    remove(arcname);   /* si NLLoc no lo reescribe no reutilizamos el evento previo */

    /* NLLoc en un proceso HIJO: si la libreria llama exit()/abort() con un
       evento problematico, el modulo no muere (mismo criterio que hyp2000_ring). */
    {
        pid_t pid = fork();
        int   status = 0;
        if (pid < 0) { logit("et", "nlloc_ring: fork fallo\n"); return -1; }
        if (pid == 0) {
            NLLoc("nlloc_ring", ctl, NULL, -1, NULL, -1, 0, 0, 0, NULL);
            _exit(0);
        }
        if (waitpid(pid, &status, 0) < 0) {
            logit("et", "nlloc_ring: waitpid fallo\n");
            return -1;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            logit("et", "nlloc_ring: NLLoc termino anormalmente (status=0x%x); evento descartado\n",
                  (unsigned)status);
            return -1;
        }
    }
    if (Debug >= 1) logit("t", "nlloc_ring: NLLoc ok (%d fases)\n", nph);

    fp = fopen(arcname, "r");
    if (!fp) { if (Debug >= 1) logit("t", "nlloc_ring: sin ARC de salida <%s>\n", arcname); return -1; }
    nllarc = (char *)malloc(MAX_ARC);
    if (!nllarc) { fclose(fp); return -1; }
    n = fread(nllarc, 1, MAX_ARC - 1, fp);
    fclose(fp);
    nllarc[n] = '\0';
    if (n == 0) {
        if (Debug >= 1) logit("t", "nlloc_ring: ARC de salida vacio <%s>\n", arcname);
        free(nllarc); return -1;
    }

    memcpy(arc_out, nllarc, n);
    arc_out[n] = '\0';
    *out_len = (int)n;
    free(nllarc);
    return 0;
}

static void status(unsigned char type, short ierr)
{
    MSG_LOGO logo;
    char     msg[256];
    logo.instid = MyInstId;
    logo.mod    = MyModId;
    logo.type   = type;
    snprintf(msg, sizeof(msg), "%ld %d\n", (long)time(NULL), (int)ierr);
    if (tport_putmsg(&OutRegion, &logo, (long)strlen(msg), msg) != PUT_OK)
        logit("et", "nlloc_ring: fallo enviando mensaje tipo %d\n", type);
}

/* ------------------------------------------------------------------------- */
/* Rate-limit por evento: NLLoc es caro, asi que se guarda el ultimo ARC de  */
/* cada evento (qid) y se relanza como mucho cada NllIntervalSec, siempre    */
/* sobre la version mas reciente recibida. Ademas se lleva un flag `dirty`:  */
/* si el ARC guardado es identico al de la ultima corrida (salvo el campo    */
/* `version`, que cambia en cada re-emision) no hay nada nuevo que calcular   */
/* y se omite NLLoc.                                                          */
/* ------------------------------------------------------------------------- */
#define PEND_MAX 64
typedef struct {
    int    used;
    char   qid[16];
    char  *arc;
    int    len;
    time_t last_seen;   /* ultima version recibida */
    time_t last_run;    /* ultima vez que se corrio NLLoc (0 = nunca) */
    int    dirty;       /* 1 = el ARC guardado cambio desde la ultima corrida */
} PEND_EV;

static PEND_EV pend[PEND_MAX];
static char    g_arc_out[MAX_ARC];

static int pend_count(void);

static int emit_arc(char *arc, int len,
                    const char *arc_in, int in_len, long dt_ms)
{
    MSG_LOGO logo;
    logo.instid = MyInstId;
    logo.mod    = MyModId;
    logo.type   = TypeHyp2000Arc;
    if (tport_putmsg(&OutRegion, &logo, (long)len, arc) != PUT_OK) {
        logit("et", "nlloc_ring: fallo escribiendo ARC refinado\n");
        return -1;
    }
    log_result("nlloc_ring", arc_in, in_len, arc, len, dt_ms);
    if (Debug >= 1)
        logit("t", "nlloc_ring: ARC refinado (%d bytes) -> %s\n", len, OutRingName);
    return 0;
}

static void process_now(const char *arc_in, int in_len)
{
    int  out_len = 0;
    struct timespec ts0, ts1;
    long dt_ms;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    if (locate_arc(arc_in, in_len, g_arc_out, &out_len) == 0 && out_len > 0) {
        out_len = arc_rewrite_id(g_arc_out, out_len, arc_in, in_len);
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        dt_ms = (ts1.tv_sec - ts0.tv_sec) * 1000L
              + (ts1.tv_nsec - ts0.tv_nsec) / 1000000L;
        emit_arc(g_arc_out, out_len, arc_in, in_len, dt_ms);
    }
}

/* Compara dos ARC ignorando el campo `version` (178-181 en formato Y2000,
   161 en el antiguo): dos versiones del mismo evento con el mismo contenido
   son exactamente el mismo trabajo para NLLoc. */
static int arc_same_content(const char *a, int la, const char *b, int lb)
{
    int i, n, vs, vn;
    if (la != lb) return 0;
    n  = la;
    vs = (la >= 182) ? 178 : 161;
    vn = (la >= 182) ?   4 :   1;
    for (i = 0; i < n; i++) {
        if (i >= vs && i < vs + vn) continue;
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void pend_upsert(const char *qid, const char *arc, int len)
{
    time_t now = time(NULL);
    int    maxp, i, slot = -1, is_new = 0;

    maxp = (NllMaxPending > 0 && NllMaxPending < PEND_MAX) ? NllMaxPending : PEND_MAX;

    for (i = 0; i < maxp; i++)
        if (pend[i].used && strcmp(pend[i].qid, qid) == 0) { slot = i; break; }

    if (slot < 0) {
        is_new = 1;
        for (i = 0; i < maxp; i++) if (!pend[i].used) { slot = i; break; }
    }
    if (slot < 0) {   /* evictar el mas antiguo */
        time_t oldest = pend[0].last_seen; slot = 0;
        for (i = 1; i < maxp; i++)
            if (pend[i].last_seen < oldest) { oldest = pend[i].last_seen; slot = i; }
    }

    /* ¿hay algo nuevo que calcular? (evento distinto, sin ARC previo, o
       contenido distinto salvo la version) */
    if (is_new || !pend[slot].arc || strcmp(pend[slot].qid, qid) != 0
        || !arc_same_content(pend[slot].arc, pend[slot].len, arc, len))
        pend[slot].dirty = 1;

    if (pend[slot].arc) free(pend[slot].arc);
    pend[slot].arc = (char *)malloc((size_t)len + 1);
    if (!pend[slot].arc) { pend[slot].used = 0; return; }
    memcpy(pend[slot].arc, arc, (size_t)len);
    pend[slot].arc[len] = '\0';
    pend[slot].len = len;
    strncpy(pend[slot].qid, qid, sizeof(pend[slot].qid) - 1);
    pend[slot].qid[sizeof(pend[slot].qid) - 1] = '\0';
    pend[slot].last_seen = now;
    if (is_new) pend[slot].last_run = 0;
    pend[slot].used = 1;

    if (Debug >= 1)
        logit("t", "nlloc_ring: pendiente ev=%s nph_in=%d (%d pendientes)\n",
              qid, len, pend_count());
}

static int pend_count(void)
{
    int i, n = 0;
    for (i = 0; i < PEND_MAX; i++) if (pend[i].used) n++;
    return n;
}

static void pend_process(void)
{
    time_t now = time(NULL);
    int    i;
    for (i = 0; i < PEND_MAX; i++) {
        if (!pend[i].used) continue;
        if (now - pend[i].last_seen > NllTTLSec) {
            free(pend[i].arc); pend[i].arc = NULL; pend[i].used = 0;
            continue;
        }
        if (!pend[i].dirty) {
            if (Debug >= 2)
                logit("t", "nlloc_ring: ev=%s sin cambios, se omite NLLoc\n",
                      pend[i].qid);
            continue;
        }
        if (pend[i].last_run == 0 || now - pend[i].last_run >= NllIntervalSec) {
            process_now(pend[i].arc, pend[i].len);
            pend[i].last_run = now;
            pend[i].dirty = 0;
        }
    }
}

int main(int argc, char **argv)
{
    MSG_LOGO     reclogo, getlogo[1];
    long         recsize;
    unsigned char msg[MAX_MSG];
    time_t       timeLastBeat;
    pid_t        MyPid = getpid();

    if (argc < 2 || argc > 3) { fprintf(stderr, "Uso: nlloc_ring <config.d> [arcfile]\n"); return 1; }
    if (read_config(argv[1]) != 0) return 1;
    if (OutRoot[0] == '\0' ||
        (nbands == 0 && FallbackCtl[0] == '\0' &&
         (ControlFile[0] == '\0' || TtimeRoot[0] == '\0'))) {
        fprintf(stderr, "nlloc_ring: faltan OutRoot y (ControlFile/TtimeRoot o ModelBand/ModelFallback)\n");
        return 1;
    }
    if (WorkDir[0] != '\0' && chdir(WorkDir) != 0) {
        fprintf(stderr, "nlloc_ring: no se pudo chdir a %s\n", WorkDir); return 1;
    }

    logit_init(argv[1], 0, 256, LogFile);

    /* Modo offline: nlloc_ring <config.d> <arcfile> -> ARC refinado a stdout. */
    if (argc == 3) {
        FILE *fp = fopen(argv[2], "r");
        char  in[MAX_ARC], out[MAX_ARC];
        int   n, on = 0;
        if (!fp) { fprintf(stderr, "nlloc_ring: no se pudo abrir %s\n", argv[2]); return 1; }
        n = (int)fread(in, 1, sizeof(in) - 1, fp);
        fclose(fp);
        in[n] = '\0';
        if (locate_arc(in, n, out, &on) == 0 && on > 0) {
            on = arc_rewrite_id(out, on, in, n);
            log_result("nlloc_ring", in, n, out, on, 0);
            fwrite(out, 1, (size_t)on, stdout);
            return 0;
        }
        fprintf(stderr, "nlloc_ring: sin localizacion\n");
        return 2;
    }

    if (GetLocalInst(&MyInstId) != 0)       { fprintf(stderr, "nlloc_ring: GetLocalInst\n"); return 1; }
    if (GetModId(MyModName, &MyModId) != 0) { fprintf(stderr, "nlloc_ring: GetModId %s\n", MyModName); return 1; }

    InKey  = GetKey(InRingName);
    OutKey = GetKey(OutRingName);
    if (InKey == -1)  { fprintf(stderr, "nlloc_ring: anillo invalido <%s>\n", InRingName);  return 1; }
    if (OutKey == -1) { fprintf(stderr, "nlloc_ring: anillo invalido <%s>\n", OutRingName); return 1; }
    if (GetType("TYPE_HYP2000ARC", &TypeHyp2000Arc) != 0) { fprintf(stderr, "nlloc_ring: GetType HYP2000ARC\n"); return 1; }
    if (GetType("TYPE_HEARTBEAT", &TypeHeartbeat) != 0)   { fprintf(stderr, "nlloc_ring: GetType HEARTBEAT\n"); return 1; }
    if (GetType("TYPE_ERROR", &TypeError) != 0)           { fprintf(stderr, "nlloc_ring: GetType ERROR\n"); return 1; }

    tport_attach(&InRegion, InKey);
    tport_attach(&OutRegion, OutKey);

    if (GridFile[0] != '\0') load_grid_bbox(GridFile);

    logit("t", "nlloc_ring: iniciado (in=%s out=%s ctrl=%s tt=%s bandas=%d fallback=%s)\n",
          InRingName, OutRingName, ControlFile, TtimeRoot, nbands,
          FallbackCtl[0] ? FallbackCtl : "-");

    getlogo[0].instid = INST_WILDCARD;
    getlogo[0].mod    = MOD_WILDCARD;
    getlogo[0].type   = TypeHyp2000Arc;

    timeLastBeat = time(NULL);
    while (tport_getflag(&OutRegion) != TERMINATE &&
           tport_getflag(&OutRegion) != MyPid) {
        if (time(NULL) - timeLastBeat >= HeartbeatInt) {
            timeLastBeat = time(NULL);
            status(TypeHeartbeat, 0);
        }
        if (tport_getmsg(&InRegion, getlogo, 1, &reclogo, &recsize,
                         (char *)msg, MAX_MSG - 1) == GET_OK) {
            char qid[16];
            msg[recsize] = '\0';
            if (NllIntervalSec > 0) {
                arc_parse_id((char *)msg, (int)recsize, qid, sizeof(qid), NULL, 0);
                pend_upsert(qid, (char *)msg, (int)recsize);
            } else {
                process_now((char *)msg, (int)recsize);
            }
        } else {
            sleep_ew(200);
        }
        if (NllIntervalSec > 0) pend_process();
    }
    tport_detach(&InRegion);
    tport_detach(&OutRegion);
    logit("t", "nlloc_ring: terminando\n");
    return 0;
}
