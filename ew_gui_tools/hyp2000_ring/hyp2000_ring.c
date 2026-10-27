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

/* Cola de picks automaticos y eventos vivos. */
static PickBuffer LivePicks;
static StationList LiveStations;
static TTModel     LiveTT;
static LiveAssocCfg  LiveAssoc;
static LiveLocCheckCfg LiveCheck;
static int           LiveTTReady = 0;

#define LIVE_EV_MAX 8
typedef struct {
    int                  active;
    int                  done;
    int                  qid;
    double               otime;
    double               last_run;
    unsigned             version;
    int                  no_new;
    int                  seed_prio;   /* 0=csnloc, 1=refinada, 2=confirmada */
    int                  last_npicks;
    uint64_t             last_hash;   /* (sta,phase,t) de las asociadas    */
    unsigned long        last_pick_seq; /* para reabrir un done            */
    int                  reopens;     /* reaperturas consumidas            */
    CsnhypodbpSolution   seed;        /* ultima solucion preferida         */
} LiveEvent;

/* Tope de reaperturas por evento: N_retry corta el gasto, pero un enjambre no
   debe reactivar el motor sin fin. Una confirmada (prio 2) siempre reabre. */
#define LIVE_MAX_REOPEN 3
static LiveEvent LiveEv[LIVE_EV_MAX];

/* Contador global de picadas aceptadas en el buffer: un salto significa que
   llego evidencia nueva y un evento detenido por N_retry puede reabrirse. */
static unsigned long g_pick_seq = 0;

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
   csnhypodbp agrupe el refinado con el evento de csnloc) y normaliza
   longitudes (header -> 197, fases -> 114). Devuelve la nueva longitud. */
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
            tmp[tlen + EWGUI_ARC_MANUAL_OFF] = manual;
            {
                int vi;
                for (vi = 0; vi < EWGUI_ARC_VMODEL_LEN && VelocityModel[vi]; vi++)
                    tmp[tlen + EWGUI_ARC_VMODEL_OFF + vi] = VelocityModel[vi];
            }
            first = 0;
        } else if (l >= 34) {
            /* El estandar Y2K de la linea de fase pone el tiempo en centesimas
               enteras (SSCCC, sin punto, offset 29-33); csnloc/csnhypodbp usan
               SS.ss. Normalizamos para que csnhypodbp lo parsee. */
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
static LiveEvent *live_find(int qid)
{
    int i;
    for (i = 0; i < LIVE_EV_MAX; i++)
        if (LiveEv[i].active && LiveEv[i].qid == qid) return &LiveEv[i];
    return NULL;
}

static LiveEvent *live_new(int qid)
{
    int i;
    for (i = 0; i < LIVE_EV_MAX; i++)
        if (!LiveEv[i].active) return &LiveEv[i];
    for (i = 0; i < LIVE_EV_MAX; i++)
        if (LiveEv[i].done) return &LiveEv[i];
    /* Reutiliza el mas viejo si se lleno. */
    return &LiveEv[0];
}

static void live_seed(const char *arc, int len)
{
    CsnhypodbpSolution sol;
    LiveEvent *ev;

    if (csnhypodbp_parse_solution(arc, (size_t)len, 163, &sol) != 0) return;
    ev = live_find(sol.qid);

    /* Una confirmada (prio 2) no se pisa nunca. Un seed de csnloc solo
       actualiza si trae MAS fases que el vigente: asi una correccion v2 del
       asociador (mismo qid) si entra, y un seed repetido no reinicia el
       trabajo. */
    if (ev && (ev->seed_prio >= 2 || sol.nph <= ev->seed.nph)) {
        if (Debug >= 1)
            logit("t", "hyp2000_ring: semilla ev=%d ignorada (prio=%d nph=%d "
                       "vs %d)\n",
                  sol.qid, ev->seed_prio, sol.nph, ev->seed.nph);
        return;
    }
    if (!ev) {
        ev = live_new(sol.qid);
        memset(ev, 0, sizeof(*ev));
        ev->active = 1;
        ev->qid = sol.qid;
        ev->version = 0;
        ev->seed_prio = 0;
    }
    ev->seed = sol;
    ev->otime = sol.otime;
    ev->done = 0;
    ev->no_new = 0;
    ev->last_run = 0;          /* fuerza la primera corrida */
    ev->last_npicks = 0;
    ev->last_hash = 0;
    ev->last_pick_seq = g_pick_seq;
    if (Debug >= 1)
        logit("t", "hyp2000_ring: semilla ev=%d nph=%d\n", sol.qid, sol.nph);
}

/* Solucion preferida: confirmada a mano (`HYPO_RING_REF2`). Sustituye a la
 * semilla de csnloc (o a la ultima refinada) y fuerza una corrida inmediata. */
static void live_prefer(const char *arc, int len, int mod)
{
    CsnhypodbpSolution sol;
    LiveEvent *ev;

    if (csnhypodbp_parse_solution(arc, (size_t)len, mod, &sol) != 0) return;
    ev = live_find(sol.qid);
    if (!ev) {
        ev = live_new(sol.qid);
        memset(ev, 0, sizeof(*ev));
        ev->active = 1;
        ev->qid = sol.qid;
    }
    ev->seed = sol;
    ev->otime = sol.otime;
    ev->done = 0;
    ev->no_new = 0;
    ev->last_run = 0;
    ev->last_npicks = 0;
    ev->last_hash = 0;
    ev->last_pick_seq = g_pick_seq;
    ev->reopens = 0;           /* confirmada: puede reabrir siempre */
    ev->seed_prio = 2;         /* confirmada: maxima prioridad */
    if (Debug >= 1)
        logit("t", "hyp2000_ring: preferida ev=%d nph=%d\n", sol.qid, sol.nph);
}

static void live_add_pick(const char *msg, int len)
{
    Pick p;

    if (PickSCNL_Parse(msg, len, &p) != 0) return;
    if (p.manual) return;      /* el vivo usa solo automaticas */
    PickBuffer_AddOrReplace(&LivePicks, &p);
    g_pick_seq++;              /* evidencia nueva: puede reabrir un done */
}

/* Construye el ARC con TODAS las P/S asociadas por tiempo de viaje al seed.
 * El seed solo aporta el hipocentro; sus fases viejas NO se copian. */
static int live_build(LiveEvent *ev, char *arc, size_t arcn,
                      LivePickSet *set_out, int *np_out)
{
    LiveSeed s;

    if (!LiveTTReady) return -1;
    s.qid     = ev->seed.qid;
    s.version = ev->seed.version;
    s.t0      = ev->seed.otime;
    s.lat     = ev->seed.lat;
    s.lon     = ev->seed.lon;
    s.depth   = ev->seed.depth;

    return live_build_arc_seeded(&s, ev->seed.picks, ev->seed.npicks,
                                 LivePicks.items, LivePicks.n, &LiveTT,
                                 &LiveStations, &LiveAssoc, arc, arcn,
                                 set_out, np_out);
}

static int live_event_retry(LiveEvent *ev)
{
    if (live_retry_exhausted(ev->no_new, LiveNRetry)) {
        ev->done = 1;
        return 1;
    }
    return 0;
}

static void live_tick(time_t now)
{
    int i;

    for (i = 0; i < LIVE_EV_MAX; i++) {
        LiveEvent *ev = &LiveEv[i];
        char arc[MAX_ARC], out[MAX_ARC], pruned[MAX_ARC];
        LivePickSet set;
        LiveLocCheck chk;
        CsnhypodbpSolution refined;
        int np = 0, out_len = 0, fresh, valid = 0, iter;
        const char *why = "sin localizacion";

        if (!ev->active) continue;

        /* N_retry corta el gasto de CPU, NO sentencia el evento: si llego
           evidencia nueva (una picada mas) y el sismo es reciente, se reabre.
           Antes un evento que fallaba 5 veces no volvia a localizarse nunca,
           aunque despues llegaran 30 picadas suyas. */
        if (ev->done) {
            if (g_pick_seq != ev->last_pick_seq &&
                (LiveEventTTLSec <= 0.0 ||
                 (double)now - ev->otime <= LiveEventTTLSec) &&
                (ev->seed_prio >= 2 || ev->reopens < LIVE_MAX_REOPEN)) {
                ev->done = 0;
                ev->no_new = 0;
                ev->reopens++;
                ev->last_pick_seq = g_pick_seq;
                ev->last_run = 0;      /* no esperar RecalcSec para reintentar */
                logit("t", "hyp2000_ring: ev=%d reabierto (picadas nuevas, "
                           "%d/%d)\n", ev->qid, ev->reopens, LIVE_MAX_REOPEN);
            } else {
                continue;
            }
        }

        if (ev->last_run != 0.0 && (double)now - ev->last_run < LiveRecalcSec)
            continue;

        memset(&set, 0, sizeof(set));
        memset(&chk, 0, sizeof(chk));
        if (live_build(ev, arc, sizeof(arc), &set, &np) != 0) {
            ev->last_run = (double)now;
            ev->last_pick_seq = g_pick_seq;   /* el fallo consume lo ya visto */
            ev->no_new++;
            if (live_event_retry(ev))
                logit("t", "hyp2000_ring: ev=%d sin picadas asociadas tras %d "
                           "intentos; se detiene\n", ev->qid, ev->no_new);
            continue;
        }

        fresh = (ev->last_run == 0.0) ||
                set.hash != ev->last_hash ||
                np != ev->last_npicks;
        if (!fresh) {
            ev->last_run = (double)now;   /* el intento tambien espera RecalcSec */
            ev->last_pick_seq = g_pick_seq;
            ev->no_new++;
            if (live_event_retry(ev))
                logit("t", "hyp2000_ring: ev=%d sin picadas nuevas tras %d "
                           "intentos; se detiene\n", ev->qid, ev->no_new);
            continue;
        }

        /* Refinamiento ITERATIVO: si la salida no es valida, se poda el ARC de
           entrada con el veredicto del motor (los residuales del ARC de
           salida) y se reintenta. Una sola pasada no puede arreglar un ARC con
           fases espurias: medido el 2026-10-27, 39 fases asociadas por tt
           daban RMS 14.5 y el evento se perdia. Con poda, las fases que el
           motor no puede explicar salen y la solucion converge o se descarta
           limpiamente. */
        for (iter = 0; iter < (LiveMaxIter > 0 ? LiveMaxIter : 1); iter++) {
            valid = 0;
            why = "sin localizacion";

            if (hypo_locate(arc, (int)strlen(arc), out, &out_len) != 0 ||
                out_len <= 0)
                break;

            out_len = arc_rewrite_id(out, out_len, arc, (int)strlen(arc));
            if (out_len <= 0) {
                why = "ARC no reescrito";
                break;
            }

            if (live_check_arc(out, out_len, &LiveCheck, &chk) == 0 &&
                chk.ok &&
                csnhypodbp_parse_solution(out, (size_t)out_len, 163,
                                          &refined) == 0) {
                if (in_region(refined.lat, refined.lon)) {
                    valid = 1;
                    break;
                }
                why = "fuera de region";
            } else {
                why = chk.why[0] ? chk.why : "salida invalida";
            }

            /* Podar y reintentar, salvo en la ultima iteracion. */
            if (iter + 1 >= (LiveMaxIter > 0 ? LiveMaxIter : 1)) break;
            {
                int np2 = 0;

                if (live_prune_arc(out, out_len, arc, (int)strlen(arc),
                                   LivePickResidualSecP, LivePickResidualSecS,
                                   pruned, sizeof(pruned), &np2) != 0)
                    break;              /* nada sobrevive: no insistir */
                if (np2 >= np) break;   /* no podo nada: no insistir */
                snprintf(arc, sizeof(arc), "%s", pruned);
                np = np2;
                if (Debug >= 1)
                    logit("t", "hyp2000_ring: ev=%d poda it=%d -> %d fases\n",
                          ev->qid, iter + 1, np);
            }
        }

        if (valid) {
            MSG_LOGO logo;

            logo.instid = MyInstId;
            logo.mod = MyModId;
            logo.type = TypeHyp2000Arc;
            if (tport_putmsg(&OutRegion, &logo, (long)out_len, out) == PUT_OK)
                log_result("hyp2000_ring", arc, (int)strlen(arc), out,
                           out_len, 0);

            ev->seed = refined;
            ev->otime = refined.otime;
            ev->version++;
            if (ev->seed_prio < 2) ev->seed_prio = 1;
            ev->last_run = (double)now;
            /* Par COHERENTE con el hash: set.n es el conjunto pre-poda, que es
               el que se reconstruye en el siguiente tick. Guardar el np podado
               hacia que `fresh` fuera siempre 1 (hash igual, np distinto) y el
               evento se republicara cada RecalcSec sin picadas nuevas. */
            ev->last_npicks = set.n;
            ev->last_hash = set.hash;
            ev->last_pick_seq = g_pick_seq;   /* lo ya usado no reabre */
            ev->no_new = 0;
        } else {
            /* No se publica ni se contamina el seed: la siguiente corrida
             * parte de la ultima solucion VALIDA (o del seed inicial). */
            ev->last_run = (double)now;
            ev->last_pick_seq = g_pick_seq;
            ev->no_new++;
            logit("t", "hyp2000_ring: LOC invalida ev=%d asoc=%d %s "
                       "(nph=%d gap=%.0f rms=%.2f)\n",
                  ev->qid, np, why, chk.nph, chk.gap, chk.rms);
            if (live_event_retry(ev))
                logit("t", "hyp2000_ring: ev=%d sin solucion valida tras %d "
                           "intentos; se detiene\n", ev->qid, ev->no_new);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* main                                                                      */
/* ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    MSG_LOGO     reclogo, logo;
    MSG_LOGO     getlogo[1];
    long         recsize;
    unsigned char msg[MAX_MSG];
    char         arc_out[MAX_ARC];
    int          out_len;
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
    if (LiveUsage) {
        PickBuffer_Init(&LivePicks, CSLOC_MAX_PICKS, 10.0);
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
                live_add_pick((char *)msg, (int)recsize);
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
                    live_seed((char *)msg, (int)recsize);
                }
            }

            if (ConfKey != -1 &&
                tport_getmsg(&ConfRegion, getlogo, 1, &reclogo, &recsize,
                             (char *)msg, MAX_MSG - 1) == GET_OK) {
                msg[recsize] = '\0';
                live_prefer((char *)msg, (int)recsize, (int)reclogo.mod);
            }

            live_tick(time(NULL));
            PickBuffer_Prune(&LivePicks, (double)time(NULL),
                             LivePickLookbackSec > 0.0
                                 ? LivePickLookbackSec + 60.0 : 300.0);
        }

        sleep_ew(200);
    }

    if (LiveUsage) {
        tport_detach(&PickRegion);
        if (ConfKey != -1) tport_detach(&ConfRegion);
        PickBuffer_Free(&LivePicks);
    }
    if (LiveTTReady) TTModel_Free(&LiveTT);
    tport_detach(&InRegion);
    tport_detach(&OutRegion);
    logit("t", "hyp2000_ring: terminando\n");
    return 0;
}
