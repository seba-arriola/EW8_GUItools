#include "csnhypodbp_locate.h"

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

static void fmt_pick_time(char *out, size_t n, double t)
{
    time_t sec = (time_t)t;
    double frac = t - (double)sec;
    struct tm *tm = gmtime(&sec);
    if (!tm) { snprintf(out, n, "19700101000000.000"); return; }
    snprintf(out, n, "%04d%02d%02d%02d%02d%06.3f",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, (double)tm->tm_sec + frac);
    for (char *c = out; *c; c++) if (*c == ',') *c = '.';
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
            fmt_pick_time(ts, sizeof(ts), base->picks[i].t_pick);
            fprintf(fp, "%d %d %d %d %s.%s.--.-- ?0 %s 0 0 0 %c %c\n",
                    type, mod, inst, seq++, base->picks[i].sta, base->picks[i].chan,
                    ts, base->picks[i].phase, base->picks[i].manual ? 'M' : 'A');
        }
    }
    for (i = 0; i < nmp; i++) {
        fmt_pick_time(ts, sizeof(ts), mp[i].t);
        fprintf(fp, "%d %d %d %d %s.%s.--.-- ?0 %s 0 0 0 %c M\n",
                type, mod, inst, seq++, mp[i].sta, mp[i].chan, ts, mp[i].phase);
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

int csnhypodbp_arc_extract(const char *logtext, char *out, size_t outn)
{
    const char *p = logtext;
    int in = 0;
    size_t n = 0;
    if (outn) out[0] = '\0';
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[512];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        if (!in && strstr(line, "--- HYP2000ARC evento")) {
            in = 1;
        } else if (in && strstr(line, "--- fin HYP2000ARC evento")) {
            break;
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
    return (n > 0) ? 0 : -1;
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
        put_at(pl + 9, sol->picks[i].chan, 3);
        pl[14] = sol->picks[i].phase;
        snprintf(t, sizeof(t), "%04d%02d%02d%02d%02d%02d%03d",
                 ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
                 ptm->tm_hour, ptm->tm_min, (int)ptm->tm_sec, (int)(pf * 1000.0 + 0.5));
        memcpy(pl + 17, t, 17);
        if (n + 115 >= outn) break;
        memcpy(out + n, pl, 114); n += 114; out[n++] = '\n';
    }
    out[n] = '\0';
    return 0;
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
        fprintf(fp, "|%s:%s:%c:%.3f", sol->picks[i].sta, sol->picks[i].chan,
                sol->picks[i].phase, sol->picks[i].t_pick);
    fprintf(fp, "\n");
    fclose(fp);
    return 0;
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
                    CsnhypodbpPick *pk = &s->picks[np];
                    if (sscanf(tok, "%7[^:]:%7[^:]:%c:%lf", pk->sta, pk->chan,
                               &pk->phase, &pk->t_pick) == 4)
                        np++;
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
                          CsnhypodbpSolution *out, CsnhypodbpMagOut *mag,
                          char *err, size_t errlen)
{
    char tmp[512], picks[600], csnlocd[600], arcf[600], hypos[600], tankf[600], magf[600];
    char cmd[8192], buf[262144], logbuf[524288], errbuf[4096], errpath[600];
    const CsnhypodbpLocator *csn, *sel;
    char csn_bin[512], csn_cfg[512], sel_bin[512], sel_cfg[512], mags_bin[512];
    int rc;

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

    /* 1) picks */
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
        if (csnhypodbp_parse_solution(buf, strlen(buf), 0, out) != 0) {
            if (err) snprintf(err, errlen, "no parseo el ARC de %s", sel->name);
            goto fail;
        }
    }

    /* 5) magnitud offline (hypos + tank -> csnmags) */
    if (ntank > 0 && tank) {
        csnhypodbp_hypos_write(hypos, out, tank, ntank);
        csnhypodbp_tank_write(tankf, tank, ntank);
        snprintf(cmd, sizeof(cmd),
                 "cd '%s' && '%s' '%s/csnmags.d' '%s' '%s' --out '%s' 2>'%s/mags.err'",
                 ew_params, mags_bin, ew_params, hypos, tankf, magf, tmp);
        run_capture(cmd, NULL, 0);
        if (read_file(magf, buf, sizeof(buf)) == 0)
            csnhypodbp_mag_parse(buf, mag);
    }

    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
    system(cmd);
    return 0;

fail:
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
    system(cmd);
    return -1;
}
