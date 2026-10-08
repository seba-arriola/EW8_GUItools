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
static char  OutRingName[MAX_STR] = "HYPO_RING_REF";
static char  CommandFile[MAX_STR] = "";
static char  WorkDir[MAX_STR]     = "";
static char  GridFile[MAX_STR]    = "";
static char  SourceCode[2]        = "W";
static int   HeartbeatInt = 30;
static int   LogFile      = 1;
static int   Debug        = 0;
static int   MinPhases    = 0;      /* 0 = sin filtro */
static double MaxRMS      = 0.0;    /* 0 = sin filtro */

static SHM_INFO      InRegion, OutRegion;
static long          InKey = 0, OutKey = 0;
static unsigned char MyInstId, MyModId;
static unsigned char TypeHyp2000Arc, TypeHeartbeat, TypeError;

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
        else if (k_its("OutRing"))      { str = k_str(); if (str) strncpy(OutRingName, str, sizeof(OutRingName)-1); }
        else if (k_its("HeartBeatInt")) HeartbeatInt = k_int();
        else if (k_its("LogFile"))      LogFile = k_int();
        else if (k_its("Debug"))        Debug = k_int();
        else if (k_its("SourceCode"))   { str = k_str(); if (str) SourceCode[0] = str[0]; }
        else if (k_its("CommandFile"))  { str = k_str(); if (str) strncpy(CommandFile, str, sizeof(CommandFile)-1); }
        else if (k_its("WorkDir"))      { str = k_str(); if (str) strncpy(WorkDir, str, sizeof(WorkDir)-1); }
        else if (k_its("GridFile"))     { str = k_str(); if (str) strncpy(GridFile, str, sizeof(GridFile)-1); }
        else if (k_its("MinPhases"))    MinPhases = k_int();
        else if (k_its("MaxRMS"))       MaxRMS = k_val();
        else continue;

        if (k_err()) { fprintf(stderr, "hyp2000_ring: error de config en '%s'\n", com); k_close(); return -1; }
    }
    k_close();
    return 0;
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
    fwrite(arc_in, 1, (size_t)in_len, fp);
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

    if (CommandFile[0] == '\0') {
        fprintf(stderr, "hyp2000_ring: falta CommandFile\n"); return 1;
    }

    /* WorkDir: hypoinverse escribe arcIn/arcOut en el cwd. */
    if (WorkDir[0] != '\0') {
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
            on = arc_rewrite_id(out, on, in, n);
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

    /* Anillos. */
    tport_attach(&InRegion, InKey);
    tport_attach(&OutRegion, OutKey);

    /* Region opcional. */
    if (GridFile[0] != '\0') load_grid_bbox(GridFile);

    /* Arranque del motor. */
    if (hypo_startup() != 0) { logit("et", "hyp2000_ring: arranque de hypoinverse fallo\n"); return 1; }
    logit("t", "hyp2000_ring: iniciado (in=%s out=%s cmd=%s workdir=%s)\n",
          InRingName, OutRingName, CommandFile, WorkDir[0] ? WorkDir : ".");

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
                         (char *)msg, MAX_MSG - 1) != GET_OK) {
            sleep_ew(200);
            continue;
        }
        msg[recsize] = '\0';

        /* Filtros de calidad. */
        {
            double lat, lon, z, rms, gap;
            int    nph;
            if (arc_header((char *)msg, (int)recsize, &lat, &lon, &z, &nph, &rms, &gap) != 0) {
                logit("et", "hyp2000_ring: ARC malformado (len=%ld)\n", recsize);
                continue;
            }
            if (MinPhases > 0 && nph < MinPhases) {
                if (Debug >= 1) logit("t", "hyp2000_ring: descartado nph=%d < %d\n", nph, MinPhases);
                continue;
            }
            if (MaxRMS > 0.0 && rms > MaxRMS) {
                if (Debug >= 1) logit("t", "hyp2000_ring: descartado rms=%.2f > %.2f\n", rms, MaxRMS);
                continue;
            }
            if (!in_region(lat, lon)) {
                if (Debug >= 1) logit("t", "hyp2000_ring: fuera de region lat=%.3f lon=%.3f\n", lat, lon);
                continue;
            }
        }

        out_len = 0;
        {
            struct timespec ts0, ts1;
            long dt_ms;
            clock_gettime(CLOCK_MONOTONIC, &ts0);
            if (hypo_locate((char *)msg, (int)recsize, arc_out, &out_len) == 0 && out_len > 0) {
                out_len = arc_rewrite_id(arc_out, out_len, (char *)msg, (int)recsize);
                clock_gettime(CLOCK_MONOTONIC, &ts1);
                dt_ms = (ts1.tv_sec - ts0.tv_sec) * 1000L
                      + (ts1.tv_nsec - ts0.tv_nsec) / 1000000L;
                logo.instid = MyInstId;
                logo.mod    = MyModId;
                logo.type   = TypeHyp2000Arc;
                if (tport_putmsg(&OutRegion, &logo, (long)out_len, arc_out) != PUT_OK)
                    logit("et", "hyp2000_ring: fallo escribiendo ARC refinado\n");
                else {
                    log_result("hyp2000_ring", (char *)msg, (int)recsize, arc_out, out_len, dt_ms);
                    if (Debug >= 1)
                        logit("t", "hyp2000_ring: ARC refinado (%d bytes) -> %s\n", out_len, OutRingName);
                }
            }
        }
    }

    tport_detach(&InRegion);
    tport_detach(&OutRegion);
    logit("t", "hyp2000_ring: terminando\n");
    return 0;
}
