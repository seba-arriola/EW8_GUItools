#include "csnhypodbp_locate.h"
#include "csnhypodbp_magmarks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static int run_capture(const char *cmd, char *out, size_t outn)
{
    FILE *fp = popen(cmd, "r");
    size_t n = 0;
    int rc;
    if (!fp) return -1;
    if (out && outn) {
        n = fread(out, 1, outn - 1, fp);
        out[n] = '\0';
    } else {
        char tmp[4096];
        while (fread(tmp, 1, sizeof(tmp), fp) > 0) { }
    }
    rc = pclose(fp);
    return rc;
}

static const CsnhypodbpLocator *find_locator(const CsnhypodbpLocator *list, int n, const char *name)
{
    int i;
    if (!list || !name) return NULL;
    for (i = 0; i < n; i++)
        if (strcmp(list[i].name, name) == 0) return &list[i];
    return NULL;
}

/* Copia el .d de csnloc forzando DumpHypo 1 (el ARC se extrae del log). */
static int write_csnloc_cfg(const char *src, const char *dst)
{
    FILE *in = fopen(src, "r");
    FILE *out;
    char line[1024];
    int dumped = 0;
    if (!in) return -1;
    out = fopen(dst, "w");
    if (!out) { fclose(in); return -1; }
    while (fgets(line, sizeof(line), in)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "DumpHypo", 8) == 0) { fputs("DumpHypo 1\n", out); dumped = 1; }
        else fputs(line, out);
    }
    if (!dumped) fputs("DumpHypo 1\n", out);
    fclose(in); fclose(out);
    return 0;
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
                           const CsnhypodbpTankTrace *tank, int ntank)
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
        const char *net = "--", *loc = "--";
        for (k = 0; k < ntank; k++) {
            if (strcmp(tank[k].sta, sol->picks[i].sta) == 0 &&
                strcmp(tank[k].chan, sol->picks[i].chan) == 0) {
                net = tank[k].net; loc = tank[k].loc; break;
            }
        }
        fprintf(fp, "%s{\"sta\":\"%s\",\"net\":\"%s\",\"chan\":\"%s\",\"loc\":\"%s\","
                    "\"phase\":\"%c\",\"t_epoch\":%.3f}",
                i ? "," : "", sol->picks[i].sta, net, sol->picks[i].chan, loc,
                sol->picks[i].phase, sol->picks[i].t_pick);
    }
    fprintf(fp, "]}\n");
    fclose(fp);
    return 0;
}

int csnhypodbp_tank_write(const char *path, const CsnhypodbpTankTrace *tr, int n)
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
               criterio que offline_report.py y que el JSONL de csnloc), no la
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

/* Corrida de SOLO magnitudes: hypos + tank -> csnmags con las ventanas ajustadas
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
                       const CsnhypodbpTankTrace *tank, int ntank,
                       const CsnhypodbpMagOvrRow *ovr, int novr, unsigned kind_mask,
                       CsnhypodbpMagOut *mag, char *err, size_t errlen)
{
    char tmp[512], hypos[600], tankf[600], magf[600], ovrf[600], magsd[600], src[600];
    char mags_bin[512], cmd[8192], buf[262144], erp[600];
    int rc = -1, novr_used = 0;

    if (err && errlen) err[0] = '\0';
    if (mag) memset(mag, 0, sizeof(*mag));
    if (!base || !tank || ntank <= 0) {
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
    snprintf(tankf, sizeof(tankf), "%s/event.tank", tmp);
    snprintf(magf,  sizeof(magf),  "%s/mags.json", tmp);
    snprintf(ovrf,  sizeof(ovrf),  "%s/winovr.txt", tmp);
    snprintf(magsd, sizeof(magsd), "%s/csnmags.d", tmp);
    snprintf(src,   sizeof(src),   "%s/csnmags.d", ew_params);
    snprintf(mags_bin, sizeof(mags_bin), "%s/%s/bin/csnmags",
             ew_home ? ew_home : "", ew_version ? ew_version : "");

    csnhypodbp_hypos_write(hypos, base, tank, ntank);
    csnhypodbp_tank_write(tankf, tank, ntank);

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
             ew_params, mags_bin, magsd, hypos, tankf, magf, tmp);
    run_capture(cmd, NULL, 0);
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
    int n = 0, i;
    FILE *fp;
    if (!path || !sol) return -1;
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

/* ------------------------------------------------------------------ */
/* Orquestador                                                         */
/* ------------------------------------------------------------------ */
int csnhypodbp_locate_run(const CsnhypodbpLocator *list, int nlist, const char *selected,
                          const char *ew_params, const char *ew_home, const char *ew_version,
                          const char *tmp_root,
                          const CsnhypodbpSolution *base,
                          const CsnhypodbpManualPick *mp, int nmp,
                          const CsnhypodbpTankTrace *tank, int ntank,
                          const CsnhypodbpMagOvrRow *ovr, int novr, unsigned kind_mask,
                          CsnhypodbpSolution *out, CsnhypodbpMagOut *mag,
                          char *err, size_t errlen)
{
    char tmp[512], picks[600], csnlocd[600], arcf[600], hypos[600], tankf[600], magf[600];
    char ovrf[600], magsd[600], src[600];
    char cmd[8192], buf[262144], logbuf[524288], errbuf[4096], errpath[600];
    char jsonl[65536];
    const CsnhypodbpLocator *csn, *sel;
    char csn_bin[512], csn_cfg[512], sel_bin[512], sel_cfg[512], mags_bin[512];
    int rc;
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
    snprintf(tankf,  sizeof(tankf),  "%s/event.tank", tmp);
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

    /* 2) temp csnloc.d con DumpHypo 1 + 3) csnloc offline */
    if (write_csnloc_cfg(csn_cfg, csnlocd) != 0) {
        if (err) snprintf(err, errlen, "no leo config de csnloc <%s>", csn_cfg);
        return -1;
    }
    snprintf(cmd, sizeof(cmd),
             "cd '%s' && EW_LOG='%s' '%s' '%s' '%s' 2>'%s/csnloc.err'",
             ew_params, tmp, csn_bin, csnlocd, picks, tmp);
    rc = run_capture(cmd, buf, sizeof(buf));
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

    /* 4) refinador (si el elegido no es csnloc) */
    if (strcmp(sel->name, "csnloc") != 0) {
        FILE *af = fopen(arcf, "w");
        if (!af) { if (err) snprintf(err, errlen, "no escribo el ARC"); goto fail; }
        fputs(buf, af); fclose(af);
        snprintf(cmd, sizeof(cmd),
                 "cd '%s' && EW_LOG='%s' '%s' '%s' '%s' 2>'%s/ref.err'",
                 ew_params, tmp, sel_bin, sel_cfg, arcf, tmp);
        rc = run_capture(cmd, buf, sizeof(buf));
        if (rc != 0) {
            snprintf(errpath, sizeof(errpath), "%s/ref.err", tmp);
            read_file(errpath, errbuf, sizeof(errbuf));
            if (err) snprintf(err, errlen, "%s fallo: %s", sel->name, errbuf);
            goto fail;
        }
        /* Algunos refinadores escriben su chachara antes del ARC en stdout. */
        arcp = csnhypodbp_arc_skip_preamble(buf);
        if (!arcp || csnhypodbp_parse_solution(arcp, strlen(arcp), 0, out) != 0) {
            if (err) snprintf(err, errlen, "no parseo el ARC de %s", sel->name);
            goto fail;
        }
    }

    /* 5) magnitud offline (hypos + tank -> csnmags), con las ventanas ajustadas a
       mano por estacion si las hay: es la misma ruta que usa `calculate`. */
    if (ntank > 0 && tank) {
        CsnhypodbpMagOvrRow rows[CSNHYPODBP_MAGMARKS_MAX];
        int nrows = 0;

        csnhypodbp_hypos_write(hypos, out, tank, ntank);
        csnhypodbp_tank_write(tankf, tank, ntank);
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
                     ew_params, mags_bin, magsd, hypos, tankf, magf, tmp);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "cd '%s' && '%s' '%s/csnmags.d' '%s' '%s' --out '%s' 2>'%s/mags.err'",
                     ew_params, mags_bin, ew_params, hypos, tankf, magf, tmp);
        }
        run_capture(cmd, NULL, 0);
        if (read_file(magf, buf, sizeof(buf)) == 0)
            csnhypodbp_mag_parse(buf, mag);
    }

    if (!g_keep_tmp) {
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
        system(cmd);
    }
    return 0;

fail:
    if (g_keep_tmp) {
        /* La ruta viaja en el mensaje: la GUI la loguea con logit. */
        if (err && errlen > 0) {
            size_t l = strlen(err);
            if (l < errlen - 1) snprintf(err + l, errlen - l, " [tmp=%s]", tmp);
        }
    } else {
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
        system(cmd);
    }
    return -1;
}
