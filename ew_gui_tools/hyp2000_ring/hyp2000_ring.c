/******************************************************************************
 * hyp2000_ring.c                                                             *
 *                                                                            *
 * Modulo EarthWorm de ANILLO que refina hipocentros con HYPOINVERSE-2000.    *
 *                                                                            *
 * A diferencia de `hyp2000_mgr` (que recibe los ARC por PIPE, dentro de la   *
 * cadena "sausage" eqassemble->eqbuf->eqcoda->eqverify), este modulo lee los *
 * ARC directamente de un anillo (por defecto HYPO_RING, la salida de csnloc) *
 * y escribe el ARC refinado en otro anillo. No usa pipe ni eq*.              *
 *                                                                            *
 *   InRing  (TYPE_HYP2000ARC)  ->  hypoinverse  ->  OutRing (TYPE_HYP2000ARC)*
 *                                                                            *
 * El motor se enlaza como libreria (hypoinv_wrapper -> hypoinv). El          *
 * intercambio de datos con hypoinverse es por ficheros `arcIn`/`arcOut` en   *
 * WorkDir (asi lo hace tambien hyp2000_mgr).                                 *
 *                                                                            *
 * Filtros opcionales: MinPhases y MaxRMS (se descartan los ARC que no pasan).*
 * Region opcional: GridFile (mismo formato .grid que csnloc); si se define,  *
 * solo se refinan los eventos dentro del bbox.                               *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>

#include "kom.h"
#include "earthworm.h"
#include "transport.h"
#include <ewgui/arc_layout.h>

#include "../csnloc/csnloc.h"
#include "../csnloc/live_assoc.h"
#include "../csnloc/live_refine.h"
#include "../csnhypodbp/csnhypodbp_group.h"
#include "../csnhypodbp/csnhypodbp_locator.h"

#ifndef INST_WILDCARD
#define INST_WILDCARD 0
#endif
#ifndef MOD_WILDCARD
#define MOD_WILDCARD 0
#endif

/* Wrapper Fortran (ISO_C_BINDING) de hypoinverse. */
extern void hypoinv_wrapper(const char *cmd, int *ret);

#define MAX_STR     256
#define MAX_ARC     65536
#define MAX_MSG     MAX_ARC

/* ------------------------------------------------------------------------- */
/* Configuracion                                                             */
/* ------------------------------------------------------------------------- */
static char  MyModName[MAX_STR]   = "MOD_HYP2000_RING";
static char  InRingName[MAX_STR]  = "HYPO_RING";
static char  InPickRingName[MAX_STR] = "PICK_RING";
static char  ConfRingName[MAX_STR] = "HYPO_RING_REF2";
static char  OutRingName[MAX_STR] = "HYPO_RING_REF";
static char  CommandFile[MAX_STR] = "";
static char  WorkDir[MAX_STR]     = "";
static char  GridFile[MAX_STR]    = "";
static char  SourceCode[2]        = "W";
static char  VelocityModel[16]    = "CHILE_POTIN";  /* -> origin.velocity_model */
static int   HeartbeatInt = 30;
static int   LogFile      = 1;
static int   Debug        = 0;
static int   MinPhases    = 0;      /* 0 = sin filtro */
static double MaxRMS      = 0.0;    /* 0 = sin filtro */
static double MaxGapDeg   = 0.0;    /* 0 = sin filtro (salida) */
static double FixedDepthKm = -1.0;  /* <0 = profundidad libre */
static char  StaFile[MAX_STR] = "../params/estaciones_107.txt";
static char  TauModel[MAX_STR] = "iasp91";
static double LivePickLookbackSec = 300.0;
static double LivePickResidualSecP = 3.0;
static double LivePickResidualSecS = 5.0;
static int    LiveUsage = 0;
static int    LiveRecalcSec = 20;
static int    LiveNRetry = 5;
static int    LiveMaxIter = 3;         /* podas por corrida (0 = 1 pasada)   */
static double LiveEventTTLSec = 600.0; /* ventana para reabrir un done (s)   */

static SHM_INFO      InRegion, OutRegion, PickRegion, ConfRegion;
static long          InKey = 0, OutKey = 0, PickKey = 0, ConfKey = 0;
static unsigned char MyInstId, MyModId;
static unsigned char TypeHyp2000Arc, TypeHeartbeat, TypeError, TypePickSCNL;

/* Metadatos y tiempos de viaje del modo vivo: los presta el motor comun
 * (csnloc/live_refine) y los carga este modulo desde su .d. */
static StationList LiveStations;
static TTModel     LiveTT;
static LiveAssocCfg  LiveAssoc;
static LiveLocCheckCfg LiveCheck;
static int           LiveTTReady = 0;
static LiveRefine   *g_live = NULL;

/* Region (bbox) opcional, del GridFile. */
static int    HaveGrid = 0;
static double GLatMin, GLatMax, GLonMin, GLonMax;

/* ------------------------------------------------------------------------- */
/* Wrapper de comandos a hypoinverse                                         */
/* ------------------------------------------------------------------------- */
static int callHypo(const char *cmd)
{
    int ret = 0;
    hypoinv_wrapper(cmd, &ret);
    if (ret != 1) {
        logit("et", "hyp2000_ring: comando '%s' fallo (ret=%d)\n", cmd, ret);
        return -1;
    }
    if (Debug >= 1) logit("t", "hyp2000_ring: cmd '%s' ok\n", cmd);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Configuracion                                                             */
/* ------------------------------------------------------------------------- */
static int read_config(const char *file)
{
    char *com, *str;

    if (!k_open(file)) {
        fprintf(stderr, "hyp2000_ring: no se pudo abrir %s\n", file);
        return -1;
    }
    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if      (k_its("MyModuleId"))   { str = k_str(); if (str) strncpy(MyModName, str, sizeof(MyModName)-1); }
        else if (k_its("InRing"))       { str = k_str(); if (str) strncpy(InRingName, str, sizeof(InRingName)-1); }
        else if (k_its("InPickRing"))   { str = k_str(); if (str) strncpy(InPickRingName, str, sizeof(InPickRingName)-1); }
        else if (k_its("OutRing"))      { str = k_str(); if (str) strncpy(OutRingName, str, sizeof(OutRingName)-1); }
        else if (k_its("StaFile"))      { str = k_str(); if (str) strncpy(StaFile, str, sizeof(StaFile)-1); }
        else if (k_its("TauTable"))     { str = k_str(); if (str) {
                                          char t[MAX_STR];
                                          snprintf(t, sizeof(t), "%s", str);
                                          if (strstr(t, ".tbl")) *strstr(t, ".tbl") = '\0';
                                          strncpy(TauModel, t, sizeof(TauModel)-1);
                                          } }
        else if (k_its("LivePickLookbackSec")) LivePickLookbackSec = k_val();
        else if (k_its("LivePickResidualSecP")) LivePickResidualSecP = k_val();
        else if (k_its("LivePickResidualSecS")) LivePickResidualSecS = k_val();
        else if (k_its("LiveMaxIter"))          LiveMaxIter = k_int();
        else if (k_its("LiveEventTTLSec"))      LiveEventTTLSec = k_val();
        else if (k_its("HeartBeatInt")) HeartbeatInt = k_int();
        else if (k_its("LogFile"))      LogFile = k_int();
        else if (k_its("Debug"))        Debug = k_int();
        else if (k_its("SourceCode"))   { str = k_str(); if (str) SourceCode[0] = str[0]; }
        else if (k_its("CommandFile"))  { str = k_str(); if (str) strncpy(CommandFile, str, sizeof(CommandFile)-1); }
        else if (k_its("WorkDir"))      { str = k_str(); if (str) strncpy(WorkDir, str, sizeof(WorkDir)-1); }
        else if (k_its("GridFile"))     { str = k_str(); if (str) strncpy(GridFile, str, sizeof(GridFile)-1); }
        else if (k_its("VelocityModel")){ str = k_str(); if (str) strncpy(VelocityModel, str, sizeof(VelocityModel)-1); }
        else if (k_its("MinPhases"))    MinPhases = k_int();
        else if (k_its("MaxRMS"))       MaxRMS = k_val();
        else if (k_its("MaxGapDeg"))    MaxGapDeg = k_val();
        else if (k_its("FixedDepthKm")) FixedDepthKm = k_val();
        else continue;

        if (k_err()) { fprintf(stderr, "hyp2000_ring: error de config en '%s'\n", com); k_close(); return -1; }
    }
    k_close();
    return 0;
}

/* La tabla de localizadores vive SOLO en $EW_PARAMS/csnhypodbpqt.d. No se
 * declara en este .d: se lee por convencion y se toma la fila del binario. */
static void load_live_config(void)
{
    CsnhypodbpLocator locs[8];
    int n = 0;

    if (csnhypodbp_locator_load_default(locs, &n, 8) != 0) return;
    {
        int i = csnhypodbp_locator_find(locs, n, "hyp2000_ring");

        if (i >= 0) {
            LiveUsage = locs[i].live_usage;
            LiveRecalcSec = locs[i].time_recalc_live > 0
                                ? locs[i].time_recalc_live : 20;
            LiveNRetry = locs[i].n_retry;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* GridFile: lee solo el bbox (mismo formato .grid que csnloc).              */
/* ------------------------------------------------------------------------- */
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
            if (fp) logit("t", "hyp2000_ring: GridFile resuelto contra EW_PARAMS: %s\n", full);
        }
    }
    if (!fp) {
        logit("et", "hyp2000_ring: no se pudo abrir GridFile <%s>\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), fp)) {
        char *h = strchr(line, '#'); if (h) *h = '\0';
        if      (sscanf(line, "LatMin %lf", &GLatMin) == 1) got |= 1;
        else if (sscanf(line, "LatMax %lf", &GLatMax) == 1) got |= 2;
        else if (sscanf(line, "LonMin %lf", &GLonMin) == 1) got |= 4;
        else if (sscanf(line, "LonMax %lf", &GLonMax) == 1) got |= 8;
    }
    fclose(fp);
    if (got != 15) {
        logit("et", "hyp2000_ring: GridFile <%s> sin bbox completo\n", path);
        return -1;
    }
    HaveGrid = 1;
    logit("t", "hyp2000_ring: region lat[%.3f..%.3f] lon[%.3f..%.3f]\n",
          GLatMin, GLatMax, GLonMin, GLonMax);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Parseo del header del ARC Y2K (offsets de hypo_out.c de csnloc).          */
/*   [16:18] lat_deg  [18] dir  [19:23] lat_min*100                         */
/*   [23:26] lon_deg  [26] dir  [27:31] lon_min*100                         */
/*   [39:42] nph  [42:45] gap  [45:48] dmin  [48:52] rms*100                */
/* ------------------------------------------------------------------------- */
static int arc_header(const char *msg, int len,
                      double *lat, double *lon, double *z,
                      int *nph, double *rms, double *gap)
{
    char b[8];

    if (len < 52) return -1;

    if (lat) {
        double d, m;
        char dir;
        b[0]=msg[16]; b[1]=msg[17]; b[2]=0; d = atof(b);
        dir = msg[18];
        b[0]=msg[19]; b[1]=msg[20]; b[2]=msg[21]; b[3]=msg[22]; b[4]=0; m = atof(b)/100.0;
        *lat = d + m/60.0; if (dir == 'S') *lat = -*lat;
    }
    if (lon) {
        double d, m;
        char dir;
        b[0]=msg[23]; b[1]=msg[24]; b[2]=msg[25]; b[3]=0; d = atof(b);
        dir = msg[26];
        b[0]=msg[27]; b[1]=msg[28]; b[2]=msg[29]; b[3]=msg[30]; b[4]=0; m = atof(b)/100.0;
        *lon = d + m/60.0; if (dir == 'W') *lon = -*lon;
    }
    if (z)   { b[0]=msg[31]; b[1]=msg[32]; b[2]=msg[33]; b[3]=msg[34]; b[4]=msg[35]; b[5]=0; *z = atof(b)/100.0; }
    if (nph) { b[0]=msg[39]; b[1]=msg[40]; b[2]=msg[41]; b[3]=0; *nph = atoi(b); }
    if (gap) { b[0]=msg[42]; b[1]=msg[43]; b[2]=msg[44]; b[3]=0; *gap = atof(b); }
    if (rms) { b[0]=msg[48]; b[1]=msg[49]; b[2]=msg[50]; b[3]=msg[51]; b[4]=0; *rms = atof(b)/100.0; }
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

/* Reescribe el ARC de salida: copia qid/version del ARC de entrada (para que
   csnhypodbp agrupe el refinado con el evento de csnloc) y lo emite con el
   layout canonico que HYPOINVERSE exige: L1 197, L2 "$1" 197, y por fase una
   linea de 114 + su continuacion de 114 ("$1" en 104-105), cerrado por el
   terminador. Las lineas "$" del ARC del motor no son fases y se descartan:
   si se copiaran, el ARC tendria mas lineas de fase de las que declara.
   Devuelve la nueva longitud. */
static int arc_rewrite_id(char *arc, int len, const char *arc_in, int in_len)
{
    static char tmp[MAX_ARC];
    int tlen = 0, first = 1;
    const char *p = arc, *end = arc + len;
    char qid[11], v178[5] = "0000";
    char v161 = '0';
    char manual = EWGUI_ARC_AUTO_CHAR;

    if (in_len >= 146) memcpy(qid, arc_in + 136, 10);
    else               memset(qid, ' ', 10);
    qid[10] = '\0';
    if (in_len >= 162) v161 = arc_in[161];
    if (in_len >= 182) { memcpy(v178, arc_in + 178, 4); v178[4] = '\0'; }
    if (in_len >= EWGUI_ARC_MANUAL_OFF + 1) manual = arc_in[EWGUI_ARC_MANUAL_OFF];

    while (p < end && tlen + 2 * 115 + 116 < MAX_ARC) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        int l = nl ? (int)(nl - p) : (int)(end - p);
        char line[200];

        if (l > 0 && p[l-1] == '\r') l--;
        if (l <= 0) { if (nl) { p = nl + 1; continue; } break; }

        memset(line, ' ', sizeof(line));

        if (first) {
            memcpy(line, p, (size_t)(l < 197 ? l : 197));
            memcpy(line + 136, qid, 10);
            line[161] = v161;
            memcpy(line + 178, v178, 4);
            line[EWGUI_ARC_MANUAL_OFF] = manual;
            {
                int vi;
                for (vi = 0; vi < EWGUI_ARC_VMODEL_LEN && VelocityModel[vi]; vi++)
                    line[EWGUI_ARC_VMODEL_OFF + vi] = VelocityModel[vi];
            }
            memcpy(tmp + tlen, line, 197); tlen += 197; tmp[tlen++] = '\n';

            /* Linea 2 "$1": resumen obligatorio del ARC archive-2000. */
            memset(line, ' ', sizeof(line));
            line[0] = '$'; line[1] = '1';
            memcpy(tmp + tlen, line, 197); tlen += 197; tmp[tlen++] = '\n';
            first = 0;
        } else if (p[0] != '$' && l >= 34 &&
                   (p[14] == 'P' || p[14] == 'S')) {
            /* Fase. Normaliza el tiempo a SS.ss (el estandar Y2K lo pone en
               centesimas enteras SSCCC y csnhypodbp espera el punto). */
            memcpy(line, p, (size_t)(l < 114 ? l : 114));
            {
                char sec[6], buf[8];
                memcpy(sec, line + 29, 5); sec[5] = '\0';
                if (!strchr(sec, '.')) {
                    int cs = atoi(sec);
                    snprintf(buf, sizeof(buf), "%05.2f", cs / 100.0);
                    memcpy(line + 29, buf, 5);
                }
            }
            memcpy(tmp + tlen, line, 114); tlen += 114; tmp[tlen++] = '\n';

            /* Continuacion de la fase ("$1" en 104-105). */
            memset(line, ' ', sizeof(line));
            memcpy(line, p, (size_t)(l < 5 ? l : 5));
            if (l > 5) memcpy(line + 5, p + 5, (size_t)((l < 7 ? l : 7) - 5));
            if (l > 9) memcpy(line + 9, p + 9, (size_t)((l < 12 ? l : 12) - 9));
            line[104] = '$'; line[105] = '1';
            if (l > 111) memcpy(line + 111, p + 111,
                                (size_t)((l < 113 ? l : 113) - 111));
            memcpy(tmp + tlen, line, 114); tlen += 114; tmp[tlen++] = '\n';
        }
        /* Cualquier otra linea ('$', terminador) se descarta: el layout se
           reconstruye entero. */

        if (!nl) break;
        p = nl + 1;
    }

    /* ---- terminador de fases ---- */
    if (tlen + 115 < MAX_ARC) {
        memset(tmp + tlen, ' ', 114);
        tlen += 114; tmp[tlen++] = '\n';
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

static int in_region(double lat, double lon)
{
    if (!HaveGrid) return 1;
    return (lat >= GLatMin && lat <= GLatMax &&
            lon >= GLonMin && lon <= GLonMax);
}

/* ------------------------------------------------------------------------- */
/* Arranque del motor: carga el .hyp y fija los ficheros de intercambio.     */
/* ------------------------------------------------------------------------- */
static int hypo_startup(void)
{
    char msg[MAX_STR];

    if (CommandFile[0] == '\0') {
        logit("et", "hyp2000_ring: falta CommandFile en la config\n");
        return -1;
    }
    snprintf(msg, sizeof(msg), "@%s", CommandFile);
    if (callHypo(msg))               return -1;   /* startup (.hyp)      */
    /* Profundidad fija: se emite DESPUES del .hyp, asi pisa su `ZTR <z> F`.
       El 2.o token `T` fija DEPFIXA (todas las profundidades de esta corrida). */
    if (FixedDepthKm >= 0.0) {
        snprintf(msg, sizeof(msg), "ZTR %.3f T", FixedDepthKm);
        if (callHypo(msg))           return -1;
    }
    if (callHypo("200 T 1900 0"))    return -1;   /* formatos Y2K        */
    if (callHypo("COP 5"))           return -1;   /* ARC entrada+shadow  */
    if (callHypo("PHS 'arcIn'"))     return -1;   /* fichero de fases    */
    if (callHypo("CAR 3"))           return -1;   /* ARC salida+shadow   */
    if (callHypo("ARC 'arcOut'"))    return -1;   /* archivo de salida   */
    if (callHypo("SUM 'none'"))      return -1;   /* sin summary         */
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Localiza un ARC: escribe arcIn, corre LOC y lee arcOut.                    */
/* ------------------------------------------------------------------------- */
static int hypo_locate(const char *arc_in, int in_len,
                       char *arc_out, int *out_len)
{
    FILE *fp;
    size_t n;
    pid_t pid;
    int   status = 0;

    fp = fopen("arcIn", "w");
    if (!fp) { logit("et", "hyp2000_ring: no se pudo escribir arcIn\n"); return -1; }
    /* La linea 1 (resumen) de un ARC viejo puede traer el modelo de velocidades
       en EWGUI_ARC_VMODEL_LEGACY_OFF.., justo donde HYPOINVERSE lee magnitudes
       (FORMAT 1212: A1,F3.2,F3.1) -> aborta con "Bad value during floating point
       read". Se le escribe ese bloque en blanco: LOC solo lee, y el modelo se
       conserva en la linea 1 del ARC de salida (arc_rewrite_id). */
    {
        int hdr = 0, b0 = EWGUI_ARC_VMODEL_LEGACY_OFF;
        int b1 = EWGUI_ARC_VMODEL_LEGACY_END, i;
        while (hdr < in_len && arc_in[hdr] != '\n') hdr++;
        if (b0 > hdr) b0 = hdr;
        if (b1 > hdr) b1 = hdr;
        if (b1 > b0) {
            fwrite(arc_in, 1, (size_t)b0, fp);
            for (i = b0; i < b1; i++) fputc(' ', fp);
            fwrite(arc_in + b1, 1, (size_t)(in_len - b1), fp);
        } else {
            fwrite(arc_in, 1, (size_t)in_len, fp);
        }
    }
    /* El ARC debe terminar en '\n' y con una linea en blanco: hyphs.for lee el
       bloque de fases hasta el final y, sin ella, con eventos degenerados da
       "Sequential READ ... after EOF marker" y aborta el proceso. */
    if (in_len == 0 || arc_in[in_len - 1] != '\n') fputc('\n', fp);
    fputc('\n', fp);
    fclose(fp);

    /* Vaciar arcOut: si LOC no lo reescribe (evento abandonado) no habra salida
       y no reutilizamos el ARC del evento anterior. */
    fp = fopen("arcOut", "w");
    if (fp) fclose(fp);

    /* LOC en un proceso HIJO: HYPOINVERSE puede llamar exit()/abort() con
       eventos degenerados (p.ej. "only N readings"); asi el modulo no muere.
       El hijo hereda el motor ya inicializado (hypo_startup() en el padre). */
    pid = fork();
    if (pid < 0) { logit("et", "hyp2000_ring: fork fallo\n"); return -1; }
    if (pid == 0) {
        int ret = 0;
        /* LOC escribe su ARC en arcOut; su chachara de stdout (p.ej.
           "NNN STATIONS READ IN.") contaminaria el ARC que el modo offline
           imprime en stdout y csnhypodbp no podria parsearlo. */
        if (freopen("/dev/null", "w", stdout) == NULL)
            fprintf(stderr, "hyp2000_ring: no pude silenciar stdout\n");
        hypoinv_wrapper("LOC", &ret);
        _exit(0);
    }
    if (waitpid(pid, &status, 0) < 0) {
        logit("et", "hyp2000_ring: waitpid fallo\n");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        logit("et", "hyp2000_ring: LOC termino anormalmente (status=0x%x); evento descartado\n",
              (unsigned)status);
        return -1;
    }

    fp = fopen("arcOut", "r");
    if (!fp) { logit("et", "hyp2000_ring: no se pudo leer arcOut\n"); return -1; }
    n = fread(arc_out, 1, MAX_ARC - 1, fp);
    fclose(fp);
    arc_out[n] = '\0';
    *out_len = (int)n;
    if (n == 0) return -1;   /* evento abandonado: sin ARC de salida */
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Heartbeat / errores                                                       */
/* ------------------------------------------------------------------------- */
static void status(unsigned char type, short ierr)
{
    MSG_LOGO logo;
    char     msg[256];
    long     len;

    logo.instid = MyInstId;
    logo.mod    = MyModId;
    logo.type   = type;
    if (type == TypeHeartbeat) {
        snprintf(msg, sizeof(msg), "%ld %d\n", (long)time(NULL), 0);
    } else {
        snprintf(msg, sizeof(msg), "%ld %d\n", (long)time(NULL), (int)ierr);
    }
    len = (long)strlen(msg);
    if (tport_putmsg(&OutRegion, &logo, len, msg) != PUT_OK)
        logit("et", "hyp2000_ring: fallo enviando mensaje tipo %d\n", type);
}

/* ------------------------------------------------------------------------- */
/* mkdir -p                                                                  */
/* ------------------------------------------------------------------------- */
/* Crea el directorio (y sus padres) si no existe. 0 = existe o se creo.      */
static int mkdir_p(const char *path)
{
    char       tmp[MAX_STR];
    char      *p;
    size_t     len;
    struct stat st;

    if (!path || !path[0]) return -1;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';

    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Locator vivo: semilla de csnloc + TODOS los picks automaticos.            */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* Motor vivo compartido (csnloc/live_refine): callbacks del modulo.          */
/* ------------------------------------------------------------------------- */
static int live_locate_cb(void *ud, const char *in, int in_len,
                          char *out, int out_cap, int *out_len)
{
    (void)ud;
    (void)out_cap;
    return hypo_locate(in, in_len, out, out_len);
}

static int live_rewrite_cb(void *ud, char *arc, int cap, int len,
                           const char *in, int in_len)
{
    (void)ud;
    (void)cap;
    return arc_rewrite_id(arc, len, in, in_len);
}

static int live_publish_cb(void *ud, const char *in, int in_len,
                           const char *out, int out_len)
{
    MSG_LOGO logo;

    (void)ud;
    logo.instid = MyInstId;
    logo.mod    = MyModId;
    logo.type   = TypeHyp2000Arc;
    if (tport_putmsg(&OutRegion, &logo, (long)out_len, (char *)out) != PUT_OK)
        return -1;
    log_result("hyp2000_ring", in, in_len, out, out_len, 0);
    return 0;
}

static int live_region_cb(void *ud, double lat, double lon)
{
    (void)ud;
    return in_region(lat, lon);
}

static int live_start(void)
{
    LiveRefineOps ops;

    memset(&ops, 0, sizeof(ops));
    ops.locate        = live_locate_cb;
    ops.rewrite       = live_rewrite_cb;
    ops.publish       = live_publish_cb;
    ops.in_region     = live_region_cb;
    ops.stations      = &LiveStations;
    ops.tt            = &LiveTT;
    ops.assoc         = LiveAssoc;
    ops.check         = LiveCheck;
    ops.recalc_sec    = LiveRecalcSec;
    ops.max_iter      = LiveMaxIter;
    ops.n_retry       = LiveNRetry;
    ops.max_events    = 8;
    ops.debug         = Debug;
    ops.event_ttl_sec = LiveEventTTLSec;
    ops.tag           = "hyp2000_ring";
    g_live = live_refine_new(&ops);
    return g_live ? 0 : -1;
}

/* ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    MSG_LOGO     reclogo;
    MSG_LOGO     getlogo[1];
    long         recsize;
    unsigned char msg[MAX_MSG];
    time_t       timeLastBeat;
    pid_t        MyPid = getpid();

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Uso: hyp2000_ring <config.d> [arcfile]\n");
        return 1;
    }
    if (read_config(argv[1]) != 0) return 1;
    load_live_config();

    if (CommandFile[0] == '\0') {
        fprintf(stderr, "hyp2000_ring: falta CommandFile\n"); return 1;
    }

    /* WorkDir: hypoinverse escribe arcIn/arcOut en el cwd. Es un directorio del
       PROPIO modulo (relativo a $EW_PARAMS: ../hyp2000_ring), fuera de params/
       y no versionado (runtime, ver .gitignore): lo creamos aqui si falta. */
    if (WorkDir[0] != '\0') {
        if (mkdir_p(WorkDir) != 0) {
            fprintf(stderr, "hyp2000_ring: no se pudo crear el WorkDir %s\n", WorkDir);
            return 1;
        }
        if (chdir(WorkDir) != 0) {
            fprintf(stderr, "hyp2000_ring: no se pudo chdir a %s\n", WorkDir);
            return 1;
        }
    }

    /* Log. */
    logit_init(argv[1], 0, 256, LogFile);

    /* Modo offline: hyp2000_ring <config.d> <arcfile> -> ARC refinado a stdout. */
    if (argc == 3) {
        static char in[MAX_ARC], out[MAX_ARC];
        FILE *fp = fopen(argv[2], "r");
        int   n, on = 0;
        if (!fp) { fprintf(stderr, "hyp2000_ring: no se pudo abrir %s\n", argv[2]); return 1; }
        n = (int)fread(in, 1, sizeof(in) - 1, fp);
        fclose(fp);
        in[n] = '\0';
        if (hypo_startup() != 0) { fprintf(stderr, "hyp2000_ring: arranque de hypoinverse fallo\n"); return 1; }
        if (hypo_locate(in, n, out, &on) == 0 && on > 0) {
            LiveLocCheckCfg lc;
            LiveLocCheck    chk;

            on = arc_rewrite_id(out, on, in, n);
            if (on <= 0) {
                fprintf(stderr, "hyp2000_ring: ARC no reescrito\n");
                return 2;
            }
            memset(&lc, 0, sizeof(lc));
            lc.min_phases  = MinPhases;
            lc.max_rms     = MaxRMS;
            lc.max_gap_deg = MaxGapDeg;
            if (live_check_arc(out, on, &lc, &chk) != 0 || !chk.ok) {
                fprintf(stderr, "hyp2000_ring: salida invalida: %s\n", chk.why);
                return 2;
            }
            log_result("hyp2000_ring", in, n, out, on, 0);
            fwrite(out, 1, (size_t)on, stdout);
            return 0;
        }
        fprintf(stderr, "hyp2000_ring: sin localizacion\n");
        return 2;
    }

    /* IDs y tipos. */
    if (GetLocalInst(&MyInstId) != 0)      { fprintf(stderr, "hyp2000_ring: GetLocalInst\n"); return 1; }
    if (GetModId(MyModName, &MyModId) != 0){ fprintf(stderr, "hyp2000_ring: GetModId %s\n", MyModName); return 1; }
    InKey  = GetKey(InRingName);
    OutKey = GetKey(OutRingName);
    if (InKey == -1)  { fprintf(stderr, "hyp2000_ring: anillo invalido <%s>\n", InRingName);  return 1; }
    if (OutKey == -1) { fprintf(stderr, "hyp2000_ring: anillo invalido <%s>\n", OutRingName); return 1; }
    if (GetType("TYPE_HYP2000ARC", &TypeHyp2000Arc) != 0) { fprintf(stderr, "hyp2000_ring: GetType HYP2000ARC\n"); return 1; }
    if (GetType("TYPE_HEARTBEAT", &TypeHeartbeat) != 0)   { fprintf(stderr, "hyp2000_ring: GetType HEARTBEAT\n"); return 1; }
    if (GetType("TYPE_ERROR", &TypeError) != 0)           { fprintf(stderr, "hyp2000_ring: GetType ERROR\n"); return 1; }

    /* Asociacion viva: metadatos de estaciones + tabla de tiempos. Se carga
       ANTES de adjuntar PICK_RING para que un fallo deje LiveUsage=0 limpio. */
    if (LiveUsage) {
        const char *ewp = getenv("EW_PARAMS");
        char sta_path[2 * MAX_STR];

        if (StaFile[0] == '/' || StaFile[0] == '.')
            snprintf(sta_path, sizeof(sta_path), "%s", StaFile);
        else
            snprintf(sta_path, sizeof(sta_path), "%s/%s",
                     (ewp && ewp[0]) ? ewp : ".", StaFile);

        if (Stations_Load(sta_path, &LiveStations) != 0) {
            logit("et", "hyp2000_ring: no se pudo cargar StaFile <%s>; vivo off\n",
                  sta_path);
            LiveUsage = 0;
        } else if (TTModel_Init(&LiveTT,
                                (ewp && ewp[0]) ? ewp : "../params",
                                TauModel, 0.0, 180.0, 0.5) != 0) {
            logit("et", "hyp2000_ring: no se pudo cargar TauTable <%s>; vivo off\n",
                  TauModel);
            LiveUsage = 0;
        } else {
            LiveTTReady = 1;
            memset(&LiveAssoc, 0, sizeof(LiveAssoc));
            LiveAssoc.lookback_sec = LivePickLookbackSec;
            LiveAssoc.res_p_sec    = LivePickResidualSecP;
            LiveAssoc.res_s_sec    = LivePickResidualSecS;
            memset(&LiveCheck, 0, sizeof(LiveCheck));
            LiveCheck.min_phases  = MinPhases;
            LiveCheck.max_rms     = MaxRMS;
            LiveCheck.max_gap_deg = MaxGapDeg;
        }
    }

    /* Anillos. */
    tport_attach(&InRegion, InKey);
    tport_attach(&OutRegion, OutKey);
    if (LiveUsage) {
        PickKey = GetKey(InPickRingName);
        if (PickKey == -1) {
            logit("et", "hyp2000_ring: anillo de picks invalido <%s>; vivo off\n",
                  InPickRingName);
            LiveUsage = 0;
        } else {
            tport_attach(&PickRegion, PickKey);
        }
        if (GetType("TYPE_PICK_SCNL", &TypePickSCNL) != 0) LiveUsage = 0;
        ConfKey = GetKey(ConfRingName);
        if (ConfKey != -1) tport_attach(&ConfRegion, ConfKey);
    }

    /* Region opcional. */
    if (GridFile[0] != '\0') load_grid_bbox(GridFile);

    /* Arranque del motor. */
    if (hypo_startup() != 0) { logit("et", "hyp2000_ring: arranque de hypoinverse fallo\n"); return 1; }
    if (LiveUsage && live_start() != 0) {
        logit("e", "hyp2000_ring: no se pudo iniciar el motor vivo\n");
        LiveUsage = 0;
    }
    if (LiveUsage) {
        logit("t", "hyp2000_ring: vivo live_usage=1 recalc=%ds n_retry=%d "
                   "lookback=%.0f resP=%.1f resS=%.1f picks=%s\n",
              LiveRecalcSec, LiveNRetry, LivePickLookbackSec,
              LivePickResidualSecP, LivePickResidualSecS, InPickRingName);
    }
    logit("t", "hyp2000_ring: iniciado (in=%s out=%s cmd=%s workdir=%s)\n",
          InRingName, OutRingName, CommandFile, WorkDir[0] ? WorkDir : ".");
    if (!LiveUsage)
        logit("t", "hyp2000_ring: live_usage=0; modo vivo desactivado\n");

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

        if (LiveUsage) {
            MSG_LOGO picklogo[1];
            MSG_LOGO plogo;

            picklogo[0].instid = INST_WILDCARD;
            picklogo[0].mod    = MOD_WILDCARD;
            picklogo[0].type   = TypePickSCNL;

            if (tport_getmsg(&PickRegion, picklogo, 1, &plogo, &recsize,
                             (char *)msg, MAX_MSG - 1) == GET_OK) {
                msg[recsize] = '\0';
                live_refine_add_pick_msg(g_live, (char *)msg, (int)recsize);
            }

            if (tport_getmsg(&InRegion, getlogo, 1, &reclogo, &recsize,
                             (char *)msg, MAX_MSG - 1) == GET_OK) {
                double lat, lon, z, rms, gap;
                int    nph;

                msg[recsize] = '\0';
                if (arc_header((char *)msg, (int)recsize, &lat, &lon, &z,
                               &nph, &rms, &gap) != 0) {
                    logit("et", "hyp2000_ring: ARC malformado (len=%ld)\n", recsize);
                } else if (!in_region(lat, lon)) {
                    /* La semilla puede traer pocas fases: MinPhases se aplica
                       a la SALIDA. El locator asocia despues todas las P/S. */

                    if (Debug >= 1)
                        logit("t", "hyp2000_ring: semilla fuera de region\n");
                } else {
                    live_refine_seed(g_live, (char *)msg, (int)recsize,
                                     (int)reclogo.mod);
                }
            }

            if (ConfKey != -1 &&
                tport_getmsg(&ConfRegion, getlogo, 1, &reclogo, &recsize,
                             (char *)msg, MAX_MSG - 1) == GET_OK) {
                msg[recsize] = '\0';
                live_refine_prefer(g_live, (char *)msg, (int)recsize,
                                   (int)reclogo.mod);
            }

            live_refine_tick(g_live, time(NULL));
            live_refine_prune(g_live, time(NULL));
        }

        sleep_ew(200);
    }

    if (LiveUsage) {
        tport_detach(&PickRegion);
        if (ConfKey != -1) tport_detach(&ConfRegion);
        live_refine_free(g_live);
        g_live = NULL;
    }
    if (LiveTTReady) TTModel_Free(&LiveTT);
    tport_detach(&InRegion);
    tport_detach(&OutRegion);
    logit("t", "hyp2000_ring: terminando\n");
    return 0;
}
