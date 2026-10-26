#include "csnhypodbp_locate.h"
#include "csnhypodbp_magmarks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <trace_buf.h>
#include <ewgui/arc_layout.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static int read_file(const char *path, char *out, size_t outn)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp) { if (outn) out[0] = '\0'; return -1; }
    n = fread(out, 1, outn - 1, fp);
    out[n] = '\0';
    fclose(fp);
    return 0;
}

/* Crea el directorio (y sus padres) si no existe. 0 = existe o se creo. */
static int mkdir_p(const char *path)
{
    char        tmp[1024];
    char       *p;
    size_t      len;
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

/* Concatena todos los *.log de un directorio (los ARC DumpHypo viven ahi). */
static int read_logs(const char *dir, char *out, size_t outn)
{
    char cmd[1024], path[1024];
    FILE *fp;
    size_t n = 0;
    int i;
    if (outn) out[0] = '\0';
    for (i = 0; i < 64; i++) {
        snprintf(path, sizeof(path), "%s/csnloc_%d.log", dir, i);
        fp = fopen(path, "rb");
        if (fp) {
            n += fread(out + n, 1, outn - 1 - n, fp);
            out[n] = '\0';
            fclose(fp);
        }
    }
    /* Fallback: cualquier .log del dir via shell (glob). */
    if (n == 0) {
        snprintf(cmd, sizeof(cmd), "cat '%s'/*.log 2>/dev/null", dir);
        fp = popen(cmd, "r");
        if (fp) { n = fread(out, 1, outn - 1, fp); out[n] = '\0'; pclose(fp); }
    }
    return (int)n;
}

/* Ejecuta `cmd` capturando stdout, con timeout y cancelacion.
 * Devuelve 0 = OK, -1 = error local, -2 = cancelado, -3 = timeout; en otro caso
 * el exit status normalizado del hijo. `err` recibe el motivo de -1/-2/-3.
 * El hijo corre en su propio grupo para poder matar tambien a sus descendientes
 * (nlloc_ring/hyp2000_ring lanzan el motor en un fork). */
static int run_capture(const char *cmd, char *out, size_t outn,
                       int timeout_sec, volatile int *cancel,
                       char *err, size_t errlen)
{
    int fds[2];
    pid_t pid;
    int status = 0, timedout = 0, cancelled = 0;
    size_t n = 0;
    time_t t0;

    if (err && errlen) err[0] = '\0';
    if (out && outn) out[0] = '\0';
    if (pipe(fds) != 0) {
        if (err) snprintf(err, errlen, "pipe: %s", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(fds[0]); close(fds[1]);
        if (err) snprintf(err, errlen, "fork: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(fds[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    t0 = time(NULL);
    for (;;) {
        struct pollfd pfd;
        char chunk[4096];
        ssize_t r;

        pfd.fd = fds[0];
        pfd.events = POLLIN | POLLHUP;
        pfd.revents = 0;
        poll(&pfd, 1, 200);
        r = read(fds[0], chunk, sizeof(chunk));
        if (r > 0) {
            if (out && outn && n < outn - 1) {
                size_t cp = (size_t)r;
                if (cp > outn - 1 - n) cp = outn - 1 - n;
                memcpy(out + n, chunk, cp);
                n += cp;
                out[n] = '\0';
            }
        } else if (r == 0) {
            break;                       /* EOF: el hijo cerro stdout */
        }
        if (cancel && *cancel) { cancelled = 1; break; }
        if (timeout_sec > 0 && (time(NULL) - t0) >= timeout_sec) {
            timedout = 1;
            break;
        }
    }
    if (cancelled || timedout) {
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
    }
    waitpid(pid, &status, 0);
    close(fds[0]);
    if (out && outn) out[n] = '\0';
    if (cancelled) {
        if (err) snprintf(err, errlen, "cancelado");
        return -2;
    }
    if (timedout) {
        if (err) snprintf(err, errlen, "timeout tras %d s", timeout_sec);
        return -3;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        if (err) snprintf(err, errlen, "senal %d", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return -1;
}

static const CsnhypodbpLocator *find_locator(const CsnhypodbpLocator *list, int n, const char *name)
{
    int i;
    if (!list || !name) return NULL;
    for (i = 0; i < n; i++)
        if (strcmp(list[i].name, name) == 0) return &list[i];
    return NULL;
}

/* Escribe una copia de un .d reemplazando/anadiendo overrides por clave.
 * El primer token de la linea manda: `DumpHypoX` no casa con `DumpHypo`. */
int csnhypodbp_cfg_copy(const char *src, const char *dst,
                        const CsnhypodbpCfgOverride *ovr, int novr)
{
    FILE *in, *out;
    char line[1024];
    int *done;
    int i, rc = 0;

    if (!src || !dst || novr < 0 || (novr > 0 && !ovr)) return -1;
    in = fopen(src, "r");
    if (!in) return -1;
    out = fopen(dst, "w");
    if (!out) { fclose(in); return -1; }
    done = calloc((size_t)(novr > 0 ? novr : 1), sizeof(*done));
    if (!done) { fclose(in); fclose(out); return -1; }

    while (fgets(line, sizeof(line), in)) {
        const char *p = line;
        int matched = -1;

        while (*p == ' ' || *p == '\t') p++;
        for (i = 0; i < novr; i++) {
            size_t klen = strlen(ovr[i].key);

            if (strncmp(p, ovr[i].key, klen) == 0 &&
                (p[klen] == '\0' || p[klen] == ' ' || p[klen] == '\t' ||
                 p[klen] == '\n' || p[klen] == '\r')) {
                matched = i;
                break;
            }
        }
        if (matched >= 0) {
            if (!done[matched]) {
                fprintf(out, "%s %s\n", ovr[matched].key, ovr[matched].value);
                done[matched] = 1;
            }
        } else {
            fputs(line, out);
        }
    }
    for (i = 0; i < novr; i++)
        if (!done[i]) fprintf(out, "%s %s\n", ovr[i].key, ovr[i].value);
    if (ferror(in) || ferror(out)) rc = -1;
    free(done);
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    return rc;
}

int csnhypodbp_cfg_with_fixdepth(const char *src, const char *dst, double fix_depth_km)
{
    return csnhypodbp_cfg_with_refiner_opts(src, dst, NULL, fix_depth_km);
}

int csnhypodbp_cfg_with_refiner_opts(const char *src, const char *dst,
                                     const char *workdir, double fix_depth_km)
{
    CsnhypodbpCfgOverride ovr[2];
    char value[32];
    int n = 0;

    if (workdir && workdir[0]) {
        ovr[n].key = "WorkDir";
        ovr[n].value = workdir;
        n++;
    }
    if (fix_depth_km >= 0.0) {
        snprintf(value, sizeof(value), "%.3f", fix_depth_km);
        ovr[n].key = "FixedDepthKm";
        ovr[n].value = value;
        n++;
    }
    return csnhypodbp_cfg_copy(src, dst, n > 0 ? ovr : NULL, n);
}

/* Copia el .d de csnloc forzando DumpHypo 1 (el ARC se extrae del log) y, si
 * se pide, la profundidad fija de esta corrida. */
static int write_csnloc_cfg(const char *src, const char *dst, double fix_depth_km)
{
    CsnhypodbpCfgOverride ovr[2];
    char value[32];
    int n = 0;

    ovr[n].key = "DumpHypo";
    ovr[n].value = "1";
    n++;
    if (fix_depth_km >= 0.0) {
        snprintf(value, sizeof(value), "%.3f", fix_depth_km);
        ovr[n].key = "FixedDepthKm";
        ovr[n].value = value;
        n++;
    }
    return csnhypodbp_cfg_copy(src, dst, ovr, n);
}

/* 1 = conservar el directorio temporal (picks.txt, event.arc, csnloc.err,
   ref.err) para poder diagnosticar un `locate` que falla. */
static int g_keep_tmp = 0;

void csnhypodbp_locate_keep_tmp(int on) { g_keep_tmp = on ? 1 : 0; }

static void fmt_pick_time(char *out, size_t n, double t)
{
    time_t sec = (time_t)t;
    double frac = t - (double)sec;
    struct tm *tm;
    int ms;

    if (frac < 0.0) frac = 0.0;
    ms = (int)(frac * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; sec += 1; }   /* redondeo de milisegundos */
    tm = gmtime(&sec);
    if (!tm) { snprintf(out, n, "19700101000000.000"); return; }
    /* %02d.%03d y NO %06.3f: ese ancho mete un ESPACIO cuando los segundos son
       < 10 (p.ej. " 7.000"), el token del tiempo se parte en dos y csnloc
       descarta la picada (su sscanf de YYYYMMDDHHMMSS.sss falla). */
    {
        char b[32];
        snprintf(b, sizeof(b), "%04d%02d%02d%02d%02d%02d.%03d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec, ms);
        snprintf(out, n, "%s", b);
    }
}

/* ------------------------------------------------------------------ */
/* Writers puros                                                       */
/* ------------------------------------------------------------------ */
int csnhypodbp_picks_write(const char *path, int type, int mod, int inst,
                           const CsnhypodbpSolution *base,
                           const CsnhypodbpManualPick *mp, int nmp)
{
    FILE *fp = fopen(path, "w");
    unsigned char seq = 0;
    char ts[32];
    int i;
    if (!fp) return -1;
    if (base) {
        for (i = 0; i < base->npicks; i++) {
            const CsnhypodbpPick *pk = &base->picks[i];
            fmt_pick_time(ts, sizeof(ts), pk->t_pick);
            /* net/loc REALES del ARC, no "--": HYPOINVERSE empareja por
               STA+NET+COMP3+LOC contra estaciones_hyp.sta (LET 5 2 3 0 0) y con
               "--" rechaza TODAS las fases; LOC abandona el evento y el
               refinador devuelve "sin localizacion". */
            fprintf(fp, "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 %c %c\n",
                    type, mod, inst, seq++, pk->sta, pk->chan,
                    pk->net[0] ? pk->net : "--", pk->loc[0] ? pk->loc : "--",
                    ts, pk->phase, pk->manual ? 'M' : 'A');
        }
    }
    for (i = 0; i < nmp; i++) {
        /* El overlay manual no guarda net/loc: se heredan de la solucion base
           por estacion (si la tiene); si no, "--". */
        const char *mnet = "--", *mloc = "--";
        int k;
        for (k = 0; base && k < base->npicks; k++) {
            if (strcmp(base->picks[k].sta, mp[i].sta) == 0) {
                if (base->picks[k].net[0]) mnet = base->picks[k].net;
                if (base->picks[k].loc[0]) mloc = base->picks[k].loc;
                break;
            }
        }
        fmt_pick_time(ts, sizeof(ts), mp[i].t);
        fprintf(fp, "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 %c M\n",
                type, mod, inst, seq++, mp[i].sta, mp[i].chan, mnet, mloc,
                ts, mp[i].phase);
    }
    fclose(fp);
    return (base ? base->npicks : 0) + nmp;
}

int csnhypodbp_hypos_write(const char *path, const CsnhypodbpSolution *sol,
                           const CsnhypodbpTrace *trace, int ntr)
{
    FILE *fp = fopen(path, "w");
    int i, k;
    if (!fp || !sol) { if (fp) fclose(fp); return -1; }
    fprintf(fp, "{\"event\":\"%010d\",\"id\":%d,\"version\":%u,\"t0\":%.3f,"
                "\"lat\":%.4f,\"lon\":%.4f,\"depth_km\":%.1f,\"nphases\":%d,"
                "\"rms_sec\":%.3f,\"phases\":[",
            sol->qid, sol->qid, sol->version, sol->otime,
            sol->lat, sol->lon, sol->depth, sol->nph, sol->rms);
    for (i = 0; i < sol->npicks; i++) {
        const CsnhypodbpPick *pk = &sol->picks[i];
        char net[16] = "", loc[16] = "", chan[16] = "";

        /* El SCNL de la picada manda: el trace solo completa lo que falte. Antes
         * se arrancaba con "--" y, si el trace no tenia la traza (p. ej. una P
         * manual fuera de las 48), csnmags descartaba la estacion en silencio. */
        snprintf(net, sizeof(net), "%s", pk->net);
        snprintf(loc, sizeof(loc), "%s", pk->loc);
        snprintf(chan, sizeof(chan), "%s", pk->chan);
        for (k = 0; k < ntr; k++) {
            if (strcmp(trace[k].sta, pk->sta) != 0) continue;
            if (chan[0] && strcmp(trace[k].chan, chan) != 0) continue;
            if (!chan[0]) snprintf(chan, sizeof(chan), "%s", trace[k].chan);
            if (!net[0] || strcmp(net, "--") == 0)
                snprintf(net, sizeof(net), "%s", trace[k].net);
            if (!loc[0] || strcmp(loc, "--") == 0)
                snprintf(loc, sizeof(loc), "%s", trace[k].loc);
            break;
        }
        if (!net[0]) snprintf(net, sizeof(net), "%s", "--");
        if (!loc[0]) snprintf(loc, sizeof(loc), "%s", "--");
        fprintf(fp, "%s{\"sta\":\"%s\",\"net\":\"%s\",\"chan\":\"%s\",\"loc\":\"%s\","
                    "\"phase\":\"%c\",\"t_epoch\":%.3f}",
                i ? "," : "", pk->sta, net, chan, loc,
                pk->phase, pk->t_pick);
    }
    fprintf(fp, "]}\n");
    fclose(fp);
    return 0;
}

int csnhypodbp_hypos_write_mag(const char *path,
                               const CsnhypodbpSolution *refined,
                               const CsnhypodbpSolution *picked,
                               const CsnhypodbpTrace *trace, int ntr)
{
    CsnhypodbpSolution magsol;
    int i, n = 0;

    if (!refined || !picked) return -1;
    magsol = *refined;
    magsol.npicks = 0;
    for (i = 0; i < picked->npicks && n < CSNHYPODBP_MAX_PICKS; i++) {
        if (picked->picks[i].phase != 'P') continue;
        magsol.picks[n++] = picked->picks[i];
    }
    magsol.npicks = n;
    magsol.nph = n;
    return csnhypodbp_hypos_write(path, &magsol, trace, ntr);
}

int csnhypodbp_trace_write(const char *path, const CsnhypodbpTrace *tr, int n)
{
    FILE *fp = fopen(path, "wb");
    int i;
    if (!fp) return -1;
    for (i = 0; i < n; i++) {
        TRACE2_HEADER h;
        int32_t *buf;
        long k;
        if (tr[i].nsamp <= 0 || !tr[i].samples || tr[i].rate <= 0.0) continue;
        memset(&h, 0, sizeof(h));
        h.nsamp     = (int)tr[i].nsamp;
        h.starttime = tr[i].oldest;
        h.endtime   = tr[i].oldest + (double)(tr[i].nsamp - 1) / tr[i].rate;
        h.samprate  = tr[i].rate;
        snprintf(h.sta, sizeof(h.sta), "%s", tr[i].sta);
        snprintf(h.net, sizeof(h.net), "%s", tr[i].net);
        snprintf(h.chan, sizeof(h.chan), "%s", tr[i].chan);
        snprintf(h.loc, sizeof(h.loc), "%s", tr[i].loc);
        h.version[0] = '2'; h.version[1] = '0';
        h.datatype[0] = 'i'; h.datatype[1] = '4'; h.datatype[2] = '\0';
        fwrite(&h, sizeof(h), 1, fp);
        buf = malloc((size_t)tr[i].nsamp * sizeof(int32_t));
        if (!buf) { fclose(fp); return -1; }
        for (k = 0; k < tr[i].nsamp; k++)
            buf[k] = (tr[i].samples[k] == INT32_MAX) ? 0 : tr[i].samples[k];
        fwrite(buf, sizeof(int32_t), (size_t)tr[i].nsamp, fp);
        free(buf);
    }
    fclose(fp);
    return 0;
}

/* Salta el preambulo que algunos refinadores escriben en stdout antes del ARC
   (HYPOINVERSE imprime "NNN STATIONS READ IN." al leer el .sta). Devuelve el
   puntero a la linea que parece cabecera de ARC (>=190 chars y 14 digitos
   iniciales = YYYYMMDDHHMMSS), o NULL si no hay ninguna. */
char *csnhypodbp_arc_skip_preamble(char *buf)
{
    char *p = buf;
    while (p && *p) {
        char *eol = strchr(p, '\n');
        size_t l = eol ? (size_t)(eol - p) : strlen(p);
        if (l >= 190) {
            int i, ok = 1;
            for (i = 0; i < 14; i++) {
                if (p[i] < '0' || p[i] > '9') { ok = 0; break; }
            }
            if (ok) return p;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return NULL;
}

int csnhypodbp_arc_extract(const char *logtext, char *out, size_t outn)
{
    const char *p = logtext;
    int in = 0, have = 0;
    size_t n = 0;
    if (outn) out[0] = '\0';
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[512];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        if (strstr(line, "--- HYP2000ARC evento")) {
            /* Bloque NUEVO: se descarta el anterior. La ventana deslizante emite
               una solucion por pasada y el representante es la ULTIMA (mismo
               criterio que el JSONL de csnloc), no la
               primera: la primera suele ser una solucion parcial y con ella los
               refinadores se quedan sin fases utiles. */
            in = 1;
            n = 0;
            if (outn) out[0] = '\0';
        } else if (in && strstr(line, "--- fin HYP2000ARC evento")) {
            in = 0;
            have = (n > 0);
        } else if (in) {
            if (n + len + 1 < outn) {
                memcpy(out + n, line, len);
                n += len;
                out[n++] = '\n';
                out[n] = '\0';
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    return have ? 0 : -1;
}

static void put_at(char *dst, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (l >= n) memcpy(dst, s, n);
    else { memcpy(dst, s, l); memset(dst + l, ' ', n - l); }
}

int csnhypodbp_arc_build(const CsnhypodbpSolution *sol, int manual, char *out, size_t outn)
{
    char hdr[200], t[64];
    size_t n = 0;
    int i;
    time_t sec;
    double frac;
    struct tm *tm;
    if (!sol || !out || outn < 512) return -1;
    memset(hdr, ' ', sizeof(hdr));

    sec = (time_t)sol->otime;
    frac = sol->otime - (double)sec;
    tm = gmtime(&sec);
    if (!tm) return -1;
    snprintf(t, sizeof(t), "%04d%02d%02d%02d%02d%02d%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, (int)tm->tm_sec, (int)(frac * 100.0 + 0.5));
    memcpy(hdr, t, 16);
    {
        double a = fabs(sol->lat); int d = (int)a;
        snprintf(t, sizeof(t), "%02d%c%04d", d, sol->lat < 0 ? 'S' : 'N',
                 (int)((a - d) * 60.0 * 100.0 + 0.5));
        memcpy(hdr + 16, t, 7);
    }
    {
        double a = fabs(sol->lon); int d = (int)a;
        snprintf(t, sizeof(t), "%03d%c%04d", d, sol->lon < 0 ? 'W' : 'E',
                 (int)((a - d) * 60.0 * 100.0 + 0.5));
        memcpy(hdr + 23, t, 8);
    }
    snprintf(t, sizeof(t), "%05d", (int)(sol->depth * 100.0 + 0.5)); memcpy(hdr + 31, t, 5);
    snprintf(t, sizeof(t), "%03d", sol->nph); memcpy(hdr + 39, t, 3);
    snprintf(t, sizeof(t), "%03d", sol->gap); memcpy(hdr + 42, t, 3);
    snprintf(t, sizeof(t), "%04d", (int)(sol->rms * 100.0 + 0.5)); memcpy(hdr + 48, t, 4);
    snprintf(t, sizeof(t), "%010d", sol->qid); memcpy(hdr + 136, t, 10);
    hdr[161] = (char)('0' + (int)(sol->version % 10));
    hdr[EWGUI_ARC_MANUAL_OFF] = manual ? EWGUI_ARC_MANUAL_CHAR : EWGUI_ARC_AUTO_CHAR;
    snprintf(t, sizeof(t), "%04u", sol->version); memcpy(hdr + 178, t, 4);

    memcpy(out + n, hdr, 197); n += 197; out[n++] = '\n';

    for (i = 0; i < sol->npicks; i++) {
        char pl[130];
        time_t ps = (time_t)sol->picks[i].t_pick;
        double pf = sol->picks[i].t_pick - (double)ps;
        struct tm *ptm = gmtime(&ps);
        if (!ptm) continue;
        memset(pl, ' ', sizeof(pl));
        put_at(pl + 0, sol->picks[i].sta, 5);
        put_at(pl + 5, sol->picks[i].net[0] ? sol->picks[i].net : "--", 2);
        put_at(pl + 9, sol->picks[i].chan, 3);
        pl[14] = sol->picks[i].phase;
        snprintf(t, sizeof(t), "%04d%02d%02d%02d%02d%02d%03d",
                 ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
                 ptm->tm_hour, ptm->tm_min, (int)ptm->tm_sec, (int)(pf * 1000.0 + 0.5));
        memcpy(pl + 17, t, 17);
        if (!isnan(sol->picks[i].residual)) {
            /* Residual Y2K: P en [34,37], S en [50,53] (entero x100). */
            int off = (sol->picks[i].phase == 'S') ? 50 : 34;
            snprintf(t, sizeof(t), "%4ld", lround(sol->picks[i].residual * 100.0));
            put_at(pl + off, t, 4);
        }
        if (n + 115 >= outn) break;
        put_at(pl + 111, sol->picks[i].loc[0] ? sol->picks[i].loc : "--", 2);
        memcpy(out + n, pl, 114); n += 114; out[n++] = '\n';
    }
    out[n] = '\0';
    return 0;
}

int csnhypodbp_confirmation_arc(const CsnhypodbpSolution *sol, char *out,
                                size_t outn)
{
    if (!sol || !out)
        return -1;
    /* La version es el campo de 4 caracteres del ARC (y el sufijo de
       `origin_id`): sin ella la confirmacion no se puede identificar. */
    if (sol->version == 0 || sol->version > 9999u)
        return -1;
    if (sol->qid <= 0)
        return -1;
    return csnhypodbp_arc_build(sol, 1, out, outn);
}

/* Valor de "key" dentro del objeto JSON [obj,end): copia el contenido de un
 * string o el token de un numero/booleano. Devuelve 1 si lo encontro. */
static int json_get(const char *obj, const char *end, const char *key,
                    char *out, size_t outn)
{
    size_t klen = strlen(key);
    const char *p = obj;

    if (!outn) return 0;
    out[0] = '\0';
    while (p < end) {
        const char *c, *q;
        size_t n;

        p = memchr(p, '"', (size_t)(end - p));
        if (!p) return 0;
        if ((size_t)(end - p) < klen + 3 ||
            strncmp(p + 1, key, klen) != 0 || p[1 + klen] != '"') {
            p++;
            continue;
        }
        c = p + klen + 2;
        while (c < end && (*c == ' ' || *c == '\t')) c++;
        if (c >= end || *c != ':') { p++; continue; }
        c++;
        while (c < end && (*c == ' ' || *c == '\t')) c++;
        if (c < end && *c == '"') {
            q = memchr(c + 1, '"', (size_t)(end - (c + 1)));
            if (!q) return 0;
            n = (size_t)(q - (c + 1));
            if (n >= outn) n = outn - 1;
            memcpy(out, c + 1, n);
            out[n] = '\0';
            return 1;
        }
        for (q = c; q < end && *q != ',' && *q != '}' && *q != ']' &&
                    *q != '\n' && *q != '\r' && *q != ' '; q++)
            ;
        n = (size_t)(q - c);
        if (n >= outn) n = outn - 1;
        memcpy(out, c, n);
        out[n] = '\0';
        return 1;
    }
    return 0;
}

int csnhypodbp_sol_apply_jsonl(CsnhypodbpSolution *sol, const char *jsonl)
{
    const char *p, *end;
    int applied = 0;

    if (!sol || !jsonl) return 0;
    end = jsonl + strlen(jsonl);

    /* Primer array "phases" (csnloc emite un evento por linea; el ARC que se
       extrae del log es el del primero). */
    p = strstr(jsonl, "\"phases\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;

    for (p++; p < end; ) {
        const char *ob = memchr(p, '{', (size_t)(end - p));
        const char *oe, *nx;
        char sta[16], chan[16], ph[8], rs[32];
        int i;

        if (!ob) break;
        nx = memchr(ob, ']', (size_t)(end - ob));
        oe = memchr(ob, '}', (size_t)(end - ob));
        if (!oe || (nx && nx < oe)) break;

        if (json_get(ob, oe, "sta", sta, sizeof(sta)) &&
            json_get(ob, oe, "phase", ph, sizeof(ph)) &&
            json_get(ob, oe, "residual", rs, sizeof(rs)) &&
            rs[0] && strncmp(rs, "null", 4) != 0) {
            json_get(ob, oe, "chan", chan, sizeof(chan));
            for (i = 0; i < sol->npicks; i++) {
                CsnhypodbpPick *pk = &sol->picks[i];
                if (strcmp(pk->sta, sta) != 0) continue;
                if (pk->phase != ph[0]) continue;
                if (chan[0] && pk->chan[0] && strcmp(pk->chan, chan) != 0) continue;
                /* El JSONL es el residual de ESTA corrida: manda siempre (las
                   picadas que se envian al localizador van sin residual). */
                pk->residual = strtod(rs, NULL);
                applied++;
                break;
            }
        }
        p = oe + 1;
    }
    return applied;
}

static double json_num(const char *json, const char *key, int *found){
    char pat[32];
    const char *p;
    if (found) *found = 0;
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    p = strstr(json, pat);
    if (!p) return 0.0;
    p += strlen(pat);
    if (strncmp(p, "null", 4) == 0) return 0.0;
    if (found) *found = 1;
    return atof(p);
}

/* Corrida de SOLO magnitudes: hypos + trace -> csnmags con las ventanas ajustadas
   a mano por estacion (clave `WindowOverridesFile`). NO localiza: deja la
   solucion como esta y solo recalcula las magnitudes. `kind_mask` elige que
   magnitudes llevan ventana ajustada (bit 0=ML, 1=Mwp, 2=Mb, 3=Ms; 0xF = las
   cuatro). Devuelve 0 si OK. */
/* Config temporal de csnmags: la del .d tal cual + `WindowOverridesFile` si hay
   ventanas ajustadas a mano. El cwd sera EW_PARAMS, asi que las rutas relativas
   del .d siguen resolviendo (csnmags no hace chdir). */
static int mag_cfg_with_overrides(const char *src, const char *dst, const char *ovr_path)
{
    FILE *in, *out;
    char line[2048];

    if (!src || !dst) return -1;
    in = fopen(src, "r");
    if (!in) return -1;
    out = fopen(dst, "w");
    if (!out) { fclose(in); return -1; }
    while (fgets(line, sizeof(line), in)) fputs(line, out);
    if (ovr_path && ovr_path[0]) {
        fprintf(out, "\n# anadido por csnhypodbp (GUI): ventanas ajustadas a mano\n");
        fprintf(out, "WindowOverridesFile %s\n", ovr_path);
    }
    fclose(out);
    fclose(in);
    return 0;
}

/* Filas de ventana que van al motor: solo las magnitudes de la mascara (bit
   0=ML..3=Ms). Devuelve cuantas quedaron. */
static int mag_ovr_filtered(const CsnhypodbpMagOvrRow *in, int nin, unsigned kind_mask,
                            CsnhypodbpMagOvrRow *out, int max)
{
    int i, n = 0;

    if (!in || !out || max <= 0) return 0;
    if (kind_mask == 0u) kind_mask = (1u << CSNHYPODBP_MAGMARKS_KIND) - 1u;
    for (i = 0; i < nin && n < max; i++) {
        if (in[i].kind < 0 || in[i].kind >= CSNHYPODBP_MAGMARKS_KIND) continue;
        if (!(kind_mask & (1u << in[i].kind))) continue;
        out[n++] = in[i];
    }
    return n;
}

/* Primera linea con contenido de `path` (para meterla en el mensaje de error). */
static void first_line(const char *path, char *out, size_t outn)
{
    FILE *fp;

    if (out && outn) out[0] = '\0';
    if (!path || !out || outn == 0) return;
    fp = fopen(path, "r");
    if (!fp) return;
    while (fgets(out, (int)outn, fp)) {
        size_t l = strlen(out);
        while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r')) out[--l] = '\0';
        if (l > 0) break;
        out[0] = '\0';
    }
    fclose(fp);
}

int csnhypodbp_mag_result_ok(const CsnhypodbpMagOut *mag, unsigned kind_mask)
{
    if (!mag) return 0;
    if (kind_mask == 0u) kind_mask = (1u << CSNHYPODBP_MAGMARKS_KIND) - 1u;
    if ((kind_mask & 1u) && mag->ml[0])  return 1;
    if ((kind_mask & 2u) && mag->mwp[0]) return 1;
    if ((kind_mask & 4u) && mag->mb[0])  return 1;
    if ((kind_mask & 8u) && mag->ms[0])  return 1;
    return 0;
}

int csnhypodbp_mag_run(const char *ew_params, const char *ew_home, const char *ew_version,
                       const char *tmp_root,
                       const CsnhypodbpSolution *base,
                       const CsnhypodbpTrace *trace, int ntr,
                       const CsnhypodbpMagOvrRow *ovr, int novr, unsigned kind_mask,
                       CsnhypodbpMagOut *mag, char *err, size_t errlen)
{
    char tmp[512], hypos[600], tracef[600], magf[600], ovrf[600], magsd[600], src[600];
    char mags_bin[512], cmd[8192], buf[262144], erp[600];
    int rc = -1, novr_used = 0;

    if (err && errlen) err[0] = '\0';
    if (mag) memset(mag, 0, sizeof(*mag));
    if (!base || !trace || ntr <= 0) {
        if (err) snprintf(err, errlen, "no hay trazas para calcular magnitudes");
        return -1;
    }
    /* Sin picadas no hay ventana que anclar: csnmags devolveria todo vacio y la
       barra se quedaria sin valores (mejor decirlo y no marcar candidata). */
    if (base->npicks <= 0) {
        if (err) snprintf(err, errlen, "sin picadas para el calculo (revisa la tabla "
                                       "de Ondas y los filtros A/M/Teo)");
        return -1;
    }
    if (!ew_params || !ew_params[0]) {
        if (err) snprintf(err, errlen, "EW_PARAMS sin definir");
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s/csnhypodbp_mag_XXXXXX",
             (tmp_root && tmp_root[0]) ? tmp_root : "/tmp");
    if (!mkdtemp(tmp)) { if (err) snprintf(err, errlen, "mkdtemp fallo en %s", tmp); return -1; }
    snprintf(hypos, sizeof(hypos), "%s/hypos.jsonl", tmp);
    snprintf(tracef, sizeof(tracef), "%s/event.traces", tmp);
    snprintf(magf,  sizeof(magf),  "%s/mags.json", tmp);
    snprintf(ovrf,  sizeof(ovrf),  "%s/winovr.txt", tmp);
    snprintf(magsd, sizeof(magsd), "%s/csnmags.d", tmp);
    snprintf(src,   sizeof(src),   "%s/csnmags.d", ew_params);
    snprintf(mags_bin, sizeof(mags_bin), "%s/%s/bin/csnmags",
             ew_home ? ew_home : "", ew_version ? ew_version : "");

    csnhypodbp_hypos_write(hypos, base, trace, ntr);
    csnhypodbp_trace_write(tracef, trace, ntr);

    /* Ventanas ajustadas: solo las magnitudes de la mascara. */
    {
        CsnhypodbpMagOvrRow rows[CSNHYPODBP_MAGMARKS_MAX];

        novr_used = mag_ovr_filtered(ovr, novr, kind_mask, rows, CSNHYPODBP_MAGMARKS_MAX);
        if (csnhypodbp_mag_ovr_write(ovrf, rows, novr_used) < 0) {
            if (err) snprintf(err, errlen, "no escribo el archivo de ventanas <%s>", ovrf);
            goto done;
        }
    }

    /* Config temporal: la del .d tal cual + la clave de ventanas ajustadas. */
    if (mag_cfg_with_overrides(src, magsd, novr_used > 0 ? ovrf : NULL) != 0) {
        if (err) snprintf(err, errlen, "no escribo la config temporal <%s>", magsd);
        goto done;
    }

    snprintf(cmd, sizeof(cmd),
             "cd '%s' && '%s' '%s' '%s' '%s' --out '%s' 2>'%s/mags.err'",
             ew_params, mags_bin, magsd, hypos, tracef, magf, tmp);
    run_capture(cmd, NULL, 0, 0, NULL, NULL, 0);
    snprintf(erp, sizeof(erp), "%s/mags.err", tmp);
    if (read_file(magf, buf, sizeof(buf)) == 0) {
        csnhypodbp_mag_parse(buf, mag);
        if (csnhypodbp_mag_result_ok(mag, kind_mask)) {
            rc = 0;
        } else if (err) {
            char e1[160];

            first_line(erp, e1, sizeof(e1));
            snprintf(err, errlen, "csnmags no calculo las magnitudes pedidas%s%s",
                     e1[0] ? ": " : "", e1);
        }
    } else if (err) {
        char e1[160];

        first_line(erp, e1, sizeof(e1));
        snprintf(err, errlen, "csnmags no dejo resultado%s%s (ver %s/mags.err)",
                 e1[0] ? ": " : "", e1, tmp);
    }

done:
    if (!g_keep_tmp) {
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
        system(cmd);
    } else if (err && errlen > 0 && rc != 0) {
        size_t l = strlen(err);
        if (l < errlen - 1) snprintf(err + l, errlen - l, " [tmp=%s]", tmp);
    }
    return rc;
}

int csnhypodbp_mag_parse(const char *json, CsnhypodbpMagOut *mag)
{
    struct { const char *name; char *dst; } m[4];
    int i;
    if (!json || !mag) return -1;
    memset(mag, 0, sizeof(*mag));
    m[0].name = "ML";  m[0].dst = mag->ml;
    m[1].name = "Mwp"; m[1].dst = mag->mwp;
    m[2].name = "Mb";  m[2].dst = mag->mb;
    m[3].name = "Ms";  m[3].dst = mag->ms;
    for (i = 0; i < 4; i++) {
        char nkey[32];
        int has = 0, nsta = 0;
        double v = json_num(json, m[i].name, &has);
        if (!has || v <= 0.0) { m[i].dst[0] = '\0'; continue; }
        snprintf(nkey, sizeof(nkey), "n%s", m[i].name);
        nsta = (int)json_num(json, nkey, NULL);
        snprintf(m[i].dst, CSNHYPODBP_MAG_MAX, "%.1f-%d", v, nsta);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Persistencia de soluciones manuales                                 */
/* ------------------------------------------------------------------ */
int csnhypodbp_manual_state_save(const char *path, const CsnhypodbpSolution *sol,
                                 const CsnhypodbpMagOut *mag)
{
    char lines[256][4096];
    char dir[1024];
    char *slash;
    int n = 0, i;
    FILE *fp;
    if (!path || !sol) return -1;
    /* El estado manual vive en la carpeta PROPIA de la GUI (p. ej.
       ../csnhypodbp/csnhypodbp_manual.events), que no se versiona: la creamos si
       falta (el .d no garantiza que exista). */
    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL) { *slash = '\0'; mkdir_p(dir); }
    fp = fopen(path, "r");
    if (fp) {
        while (n < 256 && fgets(lines[n], sizeof(lines[n]), fp)) n++;
        fclose(fp);
    }
    fp = fopen(path, "w");
    if (!fp) return -1;
    for (i = 0; i < n; i++) {
        if (atoi(lines[i]) == sol->qid) continue;   /* reemplaza por qid */
        fputs(lines[i], fp);
    }
    fprintf(fp, "%d|%u|%.3f|%.4f|%.4f|%.2f|%.3f|%d|%s|%s|%s|%s|%d",
            sol->qid, sol->version, sol->otime, sol->lat, sol->lon, sol->depth, sol->rms,
            sol->nph, mag ? mag->ml : "", mag ? mag->mwp : "", mag ? mag->mb : "",
            mag ? mag->ms : "", sol->npicks);
    for (i = 0; i < sol->npicks; i++)
        fprintf(fp, "|%s:%s:%s:%s:%c:%.3f", sol->picks[i].sta, sol->picks[i].chan,
                sol->picks[i].net, sol->picks[i].loc,
                sol->picks[i].phase, sol->picks[i].t_pick);
    fprintf(fp, "\n");
    fclose(fp);
    return 0;
}

/* Separa un token de picada persistido. Formato nuevo
 * "sta:chan:net:loc:phase:t"; formato viejo "sta:chan:phase:t" (net/loc quedan
 * vacios y los completa la metadata de la estacion al armar la tabla).
 * Devuelve 1 si pudo parsearlo. */
static int parse_pick_tok(const char *tok, CsnhypodbpPick *pk)
{
    char buf[128];
    char *f[6];
    int nf = 0;
    char *p;

    snprintf(buf, sizeof(buf), "%s", tok);
    p = buf;
    while (nf < 6) {
        char *c;
        f[nf++] = p;
        c = strchr(p, ':');
        if (!c) break;
        *c = '\0';
        p = c + 1;
    }
    memset(pk, 0, sizeof(*pk));
    if (nf == 6) {
        snprintf(pk->sta,  sizeof(pk->sta),  "%s", f[0]);
        snprintf(pk->chan, sizeof(pk->chan), "%s", f[1]);
        snprintf(pk->net,  sizeof(pk->net),  "%s", f[2]);
        snprintf(pk->loc,  sizeof(pk->loc),  "%s", f[3]);
        pk->phase  = f[4][0];
        pk->t_pick = atof(f[5]);
    } else if (nf == 4) {
        snprintf(pk->sta,  sizeof(pk->sta),  "%s", f[0]);
        snprintf(pk->chan, sizeof(pk->chan), "%s", f[1]);
        pk->phase  = f[2][0];
        pk->t_pick = atof(f[3]);
    } else {
        return 0;
    }
    return (pk->phase == 'P' || pk->phase == 'S') ? 1 : 0;
}

int csnhypodbp_manual_state_load(const char *path, CsnhypodbpSolution *out,
                                 CsnhypodbpMagOut *magout, int max)
{
    FILE *fp = fopen(path, "r");
    char line[8192];
    int n = 0;
    if (!fp) return 0;
    while (n < max && fgets(line, sizeof(line), fp)) {
        char *fields[80];
        int nf = 0, i, np = 0;
        char *p = line;
        CsnhypodbpSolution *s = &out[n];
        CsnhypodbpMagOut *m = magout ? &magout[n] : NULL;

        /* Separa por '|' preservando campos vacios. */
        while (nf < 80) {
            char *bar;
            fields[nf++] = p;
            bar = strchr(p, '|');
            if (!bar) break;
            *bar = '\0';
            p = bar + 1;
        }
        if (nf > 0) { char *nl = strchr(fields[nf - 1], '\n'); if (nl) *nl = '\0'; }

        memset(s, 0, sizeof(*s));
        if (m) memset(m, 0, sizeof(*m));
        for (i = 0; i < nf; i++) {
            const char *tok = fields[i];
            switch (i) {
            case 0: s->qid = atoi(tok); break;
            case 1: s->version = (unsigned)atoi(tok); break;
            case 2: s->otime = atof(tok); break;
            case 3: s->lat = atof(tok); break;
            case 4: s->lon = atof(tok); break;
            case 5: s->depth = atof(tok); break;
            case 6: s->rms = atof(tok); break;
            case 7: s->nph = atoi(tok); break;
            case 8: if (m) snprintf(m->ml, CSNHYPODBP_MAG_MAX, "%s", tok); break;
            case 9: if (m) snprintf(m->mwp, CSNHYPODBP_MAG_MAX, "%s", tok); break;
            case 10: if (m) snprintf(m->mb, CSNHYPODBP_MAG_MAX, "%s", tok); break;
            case 11: if (m) snprintf(m->ms, CSNHYPODBP_MAG_MAX, "%s", tok); break;
            case 12: break;   /* npicks declarado: se recuenta al parsear */
            default:
                if (i >= 13 && np < CSNHYPODBP_MAX_PICKS) {
                    if (parse_pick_tok(tok, &s->picks[np])) np++;
                }
                break;
            }
        }
        s->npicks = np;
        s->manual = 1;
        s->mod = 0;
        n++;
    }
    fclose(fp);
    return n;
}

/* Siembra la cabecera del ARC que recibe el refinador con la solucion BASE
 * (la elegida por el usuario): sin esto, el reajuste interno de csnloc puede
 * mover el epicentro (p.ej. a -56) y NLLoc busca en una banda/grilla que no
 * contiene el evento. Tambien conserva qid/version/manual del evento elegido,
 * para que el candidato refinado siga perteneciendo a esa solucion. */
static void arc_seed_base(char *arc, const CsnhypodbpSolution *base)
{
    int latd, latm, lond, lonm, dep;

    if (!arc || !base) return;
    if (!(isfinite(base->lat) && isfinite(base->lon) && isfinite(base->depth))) return;
    if (fabs(base->lat) > 90.0 || fabs(base->lon) > 180.0) return;

    latd = (int)fabs(base->lat);
    latm = (int)((fabs(base->lat) - latd) * 6000.0 + 0.5);
    lond = (int)fabs(base->lon);
    lonm = (int)((fabs(base->lon) - lond) * 6000.0 + 0.5);
    dep  = (int)(base->depth * 100.0 + 0.5);
    if (dep > 99999) dep = 99999;

    /* La linea 1 del ARC es de ancho fijo y NO lleva NUL: snprintf escribe el
       terminador dentro de la linea y el refinador la ve truncada ("header del
       ARC ilegible"). Se formatea aparte y se copia el ancho exacto. */
    {
        char b[32];

        snprintf(b, sizeof(b), "%02d", latd);  memcpy(arc + 16, b, 2);
        arc[18] = base->lat < 0.0 ? 'S' : 'N';
        snprintf(b, sizeof(b), "%04d", latm);  memcpy(arc + 19, b, 4);
        snprintf(b, sizeof(b), "%03d", lond);  memcpy(arc + 23, b, 3);
        arc[26] = base->lon < 0.0 ? 'W' : 'E';
        snprintf(b, sizeof(b), "%04d", lonm);  memcpy(arc + 27, b, 4);
        snprintf(b, sizeof(b), "%05d", dep);   memcpy(arc + 31, b, 5);
        if (base->qid > 0) {
            snprintf(b, sizeof(b), "%010d", base->qid);
            memcpy(arc + EWGUI_ARC_QID_OFF, b, EWGUI_ARC_QID_LEN);
            snprintf(b, sizeof(b), "%04u", base->version);
            memcpy(arc + EWGUI_ARC_QVER_OFF, b, EWGUI_ARC_QVER_LEN);
        }
    }
    arc[EWGUI_ARC_MANUAL_OFF] = base->manual ? EWGUI_ARC_MANUAL_CHAR
                                             : EWGUI_ARC_AUTO_CHAR;
}

/* ------------------------------------------------------------------ */
/* Orquestador                                                         */
/* ------------------------------------------------------------------ */
void csnhypodbp_locate_identity(const CsnhypodbpSolution *base,
                                const CsnhypodbpLocator *sel,
                                CsnhypodbpSolution *out)
{
    if (!out) return;
    out->mod = csnhypodbp_locator_mod(sel);
    if (!base) return;
    if (out->qid <= 0) out->qid = base->qid;
    if (out->version == 0) out->version = base->version;
}

int csnhypodbp_locate_run(const CsnhypodbpLocator *list, int nlist, const char *selected,
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
                          char *err, size_t errlen)
{
    char tmp[512], picks[600], csnlocd[600], arcf[600], hypos[600], tracef[600], magf[600];
    char ovrf[600], magsd[600], src[600], ref_cfg[600];
    char workdir[600], workdir_abs[600];
    char cmd[8192], buf[262144], logbuf[524288], errbuf[4096], errpath[600];
    char jsonl[65536], runerr[256];
    const CsnhypodbpLocator *csn, *sel;
    char csn_bin[512], csn_cfg[512], sel_bin[512], sel_cfg[512], mags_bin[512];
    int rc, have_workdir = 0;
    char *arcp;

    if (err && errlen) err[0] = '\0';
    if (!out || !base) { if (err) snprintf(err, errlen, "argumentos invalidos"); return -1; }
    memset(out, 0, sizeof(*out));
    if (mag) memset(mag, 0, sizeof(*mag));

    snprintf(tmp, sizeof(tmp), "%s/csnhypodbp_loc_XXXXXX", (tmp_root && tmp_root[0]) ? tmp_root : "/tmp");
    if (!mkdtemp(tmp)) { if (err) snprintf(err, errlen, "mkdtemp fallo en %s", tmp); return -1; }
    snprintf(picks,  sizeof(picks),  "%s/picks.txt", tmp);
    snprintf(csnlocd,sizeof(csnlocd),"%s/csnloc.d", tmp);
    snprintf(arcf,   sizeof(arcf),   "%s/event.arc", tmp);
    snprintf(hypos,  sizeof(hypos),  "%s/hypos.jsonl", tmp);
    snprintf(tracef,  sizeof(tracef),  "%s/event.traces", tmp);
    snprintf(magf,   sizeof(magf),   "%s/mags.json", tmp);

    csn = find_locator(list, nlist, "csnloc");
    sel = find_locator(list, nlist, selected);
    if (!csn) csn = sel;
    if (!sel) sel = csn;
    if (!csn) { if (err) snprintf(err, errlen, "no hay localizadores configurados"); return -1; }

    csnhypodbp_locator_resolve(csn, ew_home, ew_version, ew_params,
                               csn_bin, sizeof(csn_bin), csn_cfg, sizeof(csn_cfg));
    csnhypodbp_locator_resolve(sel, ew_home, ew_version, ew_params,
                               sel_bin, sizeof(sel_bin), sel_cfg, sizeof(sel_cfg));
    snprintf(mags_bin, sizeof(mags_bin), "%s/%s/bin/csnmags",
             ew_home ? ew_home : "", ew_version ? ew_version : "");

    /* WorkDir propio del refinador. Va como HERMANO de `params` para que las
       rutas relativas del .hyp (`../params/...`) sigan resolviendo, pero sin
       compartir `obs.nll`/`nll_ring.in`/`arcIn` con el demonio del mismo nombre
       (el modo offline y el daemon usan nombres fijos dentro de WorkDir). */
    if (strcmp(sel->name, "csnloc") != 0) {
        const char *wb;

        snprintf(workdir_abs, sizeof(workdir_abs), "%s/../%s_run_XXXXXX",
                 ew_params ? ew_params : ".", sel->name);
        if (!mkdtemp(workdir_abs)) {
            if (err) snprintf(err, errlen, "no creo el WorkDir de %s", sel->name);
            goto fail;
        }
        have_workdir = 1;
        wb = strrchr(workdir_abs, '/');
        wb = wb ? wb + 1 : workdir_abs;
        snprintf(workdir, sizeof(workdir), "../%s", wb);
        /* `nlloc_ring.d` usa `OutRoot loc/ev`: el `loc/` debe existir en el
           WorkDir o NLLoc no escribe el `.sum.grid0.loc.arc` y el refinador
           devuelve "ARC de salida vacio". El WorkDir del demonio ya lo tiene. */
        if (strcmp(sel->name, "nlloc_ring") == 0) {
            char locdir[700];

            snprintf(locdir, sizeof(locdir), "%s/loc", workdir_abs);
            if (mkdir_p(locdir) != 0) {
                if (err) snprintf(err, errlen, "no creo el OutRoot de %s", sel->name);
                goto fail;
            }
        }
    }

    /* 1) picks (con SCNL completo: sin canal NI red HYPOINVERSE rechaza TODAS
       las fases por STA+NET+COMP3+LOC y el OBS de NLLoc queda con 13 campos) */
    {
        int i, malos = 0;
        for (i = 0; i < base->npicks; i++) {
            if (!base->picks[i].sta[0] || !base->picks[i].chan[0] ||
                !base->picks[i].net[0]) malos++;
        }
        if (malos) {
            if (err) snprintf(err, errlen,
                              "%d de %d picadas sin canal o sin red: el ARC de la solucion base los perdio",
                              malos, base->npicks);
            goto fail;
        }
    }
    csnhypodbp_picks_write(picks, 8, 0, 0, base, mp, nmp);

    /* 2) temp csnloc.d con DumpHypo 1 (+ FixedDepthKm) + 3) csnloc offline */
    if (write_csnloc_cfg(csn_cfg, csnlocd, fix_depth_km) != 0) {
        if (err) snprintf(err, errlen, "no leo config de csnloc <%s>", csn_cfg);
        return -1;
    }
    snprintf(cmd, sizeof(cmd),
             "cd '%s' && EW_LOG='%s' '%s' '%s' '%s' 2>'%s/csnloc.err'",
             ew_params, tmp, csn_bin, csnlocd, picks, tmp);
    rc = run_capture(cmd, buf, sizeof(buf), timeout_sec, cancel,
                     runerr, sizeof(runerr));
    if (rc == -2 || rc == -3) {
        if (err) snprintf(err, errlen, "csnloc offline: %s", runerr);
        goto fail;
    }
    if (rc != 0) {
        snprintf(errpath, sizeof(errpath), "%s/csnloc.err", tmp);
        read_file(errpath, errbuf, sizeof(errbuf));
        if (err) snprintf(err, errlen, "csnloc offline fallo: %s", errbuf);
        goto fail;
    }
    /* El stdout de csnloc es su JSONL (residual por fase); el ARC del log no
       lo trae. Se guarda antes de reutilizar `buf` para el ARC. */
    snprintf(jsonl, sizeof(jsonl), "%s", buf);

    /* ARC del log */
    read_logs(tmp, logbuf, sizeof(logbuf));
    if (csnhypodbp_arc_extract(logbuf, buf, sizeof(buf)) != 0) {
        if (err) snprintf(err, errlen, "csnloc no emitio ARC (revisa DumpHypo/grids)");
        goto fail;
    }
    if (csnhypodbp_parse_solution(buf, strlen(buf), 0, out) != 0) {
        if (err) snprintf(err, errlen, "no parseo el ARC de csnloc");
        goto fail;
    }
    /* Residuales de csnloc: solo su JSONL los trae (el ARC va en blanco). */
    csnhypodbp_sol_apply_jsonl(out, jsonl);
    /* El candidato sigue perteneciendo al evento elegido, no al qid local que
       inventa csnloc offline al reasociar las picadas. El `mod` viaja en el
       logo del anillo, no en el ARC: se fija desde el localizador elegido. */
    csnhypodbp_locate_identity(base, sel, out);
    if (base->qid > 0) {
        out->qid = base->qid;
        out->version = base->version;
    }

    /* Candidato provisional para la UI: csnloc ya termino; el refinador sigue. */
    if (on_stage) on_stage(out, stage_ud);

    /* 4) refinador (si el elegido no es csnloc) */
    if (strcmp(sel->name, "csnloc") != 0) {
        FILE *af;

        /* cfg SIEMPRE temporal: WorkDir propio (siempre) + FixedDepthKm (si se
           pidio). El WorkDir del .d original queda para el demonio. */
        snprintf(ref_cfg, sizeof(ref_cfg), "%s/%s.d", tmp, sel->name);
        if (csnhypodbp_cfg_with_refiner_opts(sel_cfg, ref_cfg,
                                            have_workdir ? workdir : NULL,
                                            fix_depth_km) != 0) {
            if (err) snprintf(err, errlen, "no leo config de %s <%s>", sel->name, sel_cfg);
            goto fail;
        }

        /* El refinador arranca desde la solucion elegida (epicentro/profundidad
           y qid); las fases siguen siendo las de csnloc. */
        arc_seed_base(buf, base);
        af = fopen(arcf, "w");
        if (!af) { if (err) snprintf(err, errlen, "no escribo el ARC"); goto fail; }
        fputs(buf, af); fclose(af);
        snprintf(cmd, sizeof(cmd),
                 "cd '%s' && EW_LOG='%s' '%s' '%s' '%s' 2>'%s/ref.err'",
                 ew_params, tmp, sel_bin, ref_cfg, arcf, tmp);
        rc = run_capture(cmd, buf, sizeof(buf), timeout_sec, cancel,
                         runerr, sizeof(runerr));
        if (rc == -2 || rc == -3) {
            if (err) snprintf(err, errlen, "%s: %s", sel->name, runerr);
            goto fail;
        }
        if (rc != 0) {
            snprintf(errpath, sizeof(errpath), "%s/ref.err", tmp);
            read_file(errpath, errbuf, sizeof(errbuf));
            if (err) snprintf(err, errlen, "%s fallo: %s", sel->name, errbuf);
            goto fail;
        }
        /* Algunos refinadores escriben su chachara antes del ARC en stdout. */
        arcp = csnhypodbp_arc_skip_preamble(buf);
        if (!arcp || csnhypodbp_parse_solution(arcp, strlen(arcp), 0, out) != 0) {
            snprintf(errpath, sizeof(errpath), "%s/ref.err", tmp);
            read_file(errpath, errbuf, sizeof(errbuf));
            if (err) {
                size_t l = 0;

                snprintf(err, errlen, "%s: ARC invalido (stdout %.120s%s)",
                         sel->name, buf,
                         strlen(buf) > 120 ? "..." : "");
                l = strlen(err);
                if (errbuf[0] && l + 3 < errlen)
                    snprintf(err + l, errlen - l, "; stderr: %.180s", errbuf);
            }
            goto fail;
        }
        /* El refinador puede devolver qid/version=0: la candidata sigue siendo
         * del evento elegido. El mod es el del refinador (163/164). */
        csnhypodbp_locate_identity(base, sel, out);
    }

    /* 5) magnitud offline (hypos + trace -> csnmags), con las ventanas ajustadas a
       mano por estacion si las hay: es la misma ruta que usa `calculate`. */
    if (ntr > 0 && trace) {
        CsnhypodbpMagOvrRow rows[CSNHYPODBP_MAGMARKS_MAX];
        int nrows = 0;

        /* Magnitudes: origen refinado + P efectivas elegidas en la tabla
           (incluye manuales y teoricas). No se usan las S ni out->picks: la
           ventana de csnmags se ancla en la fase y solo la P define el ancla. */
        csnhypodbp_hypos_write_mag(hypos, out, base, trace, ntr);
        csnhypodbp_trace_write(tracef, trace, ntr);
        snprintf(ovrf,  sizeof(ovrf),  "%s/winovr.txt", tmp);
        snprintf(magsd, sizeof(magsd), "%s/csnmags.d", tmp);
        snprintf(src,   sizeof(src),   "%s/csnmags.d", ew_params);
        if (ovr && novr > 0) {
            nrows = mag_ovr_filtered(ovr, novr, kind_mask, rows, CSNHYPODBP_MAGMARKS_MAX);
            if (csnhypodbp_mag_ovr_write(ovrf, rows, nrows) < 0) nrows = 0;
        }
        if (mag_cfg_with_overrides(src, magsd, nrows > 0 ? ovrf : NULL) == 0) {
            snprintf(cmd, sizeof(cmd),
                     "cd '%s' && '%s' '%s' '%s' '%s' --out '%s' 2>'%s/mags.err'",
                     ew_params, mags_bin, magsd, hypos, tracef, magf, tmp);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "cd '%s' && '%s' '%s/csnmags.d' '%s' '%s' --out '%s' 2>'%s/mags.err'",
                     ew_params, mags_bin, ew_params, hypos, tracef, magf, tmp);
        }
        /* Sin el timeout del refinador: Ms puede necesitar su ventana completa
           (MagFetchSeconds) y no debe morir por LocatorTimeoutSec. */
        run_capture(cmd, NULL, 0, 0, NULL, runerr, sizeof(runerr));
        if (read_file(magf, buf, sizeof(buf)) == 0)
            csnhypodbp_mag_parse(buf, mag);
    }

    if (!g_keep_tmp) {
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
        system(cmd);
        if (have_workdir) {
            snprintf(cmd, sizeof(cmd), "rm -rf '%s'", workdir_abs);
            system(cmd);
        }
    }
    return 0;

fail:
    if (g_keep_tmp) {
        /* Las rutas viajan en el mensaje: la GUI las loguea con logit. */
        if (err && errlen > 0) {
            size_t l = strlen(err);
            if (l < errlen - 1) snprintf(err + l, errlen - l, " [tmp=%s]", tmp);
            l = strlen(err);
            if (have_workdir && l < errlen - 1)
                snprintf(err + l, errlen - l, " [workdir=%s]", workdir_abs);
        }
    } else {
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
        system(cmd);
        if (have_workdir) {
            snprintf(cmd, sizeof(cmd), "rm -rf '%s'", workdir_abs);
            system(cmd);
        }
    }
    return -1;
}
