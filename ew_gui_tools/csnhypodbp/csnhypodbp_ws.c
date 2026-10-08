#include "csnhypodbp.h"

void LoadStationsFromFile() {
    FILE *fp = fopen(StaFile, "r");
    if (!fp) { logit("e", "csnhypodbp: Error abriendo %s\n", StaFile); exit(-1); }
    
    StaArray = (DEV_STATION *) calloc(MAX_ESTA, sizeof(DEV_STATION));
    NumEstaciones = 0; char line[256];
    long lMaxBufSamps = MAX_MINUTES * 60 * 100; 
    
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char s1[20]="", s2[20]="", s3[20]="", s4[20]="";
        double d1=0, d2=0, d3=0, d4=0;
        
        int parsed = sscanf(line, "%19s %19s %19s %19s %lf %lf %lf %lf", s1, s2, s3, s4, &d1, &d2, &d3, &d4);
        if (parsed >= 4 && NumEstaciones < MAX_ESTA) {
            if (parsed >= 8) {
                snprintf(StaArray[NumEstaciones].szStation, 15, "%s", s1);
                snprintf(StaArray[NumEstaciones].szNetID, 15, "%s", s2);
                snprintf(StaArray[NumEstaciones].szChannel, 15, "%s", s3);
                snprintf(StaArray[NumEstaciones].szLocation, 15, "%s", s4);
                StaArray[NumEstaciones].dLat = d1; StaArray[NumEstaciones].dLon = d2; StaArray[NumEstaciones].dScreenScale = 0.005; 
            } else {
                snprintf(StaArray[NumEstaciones].szStation, 15, "%s", s1);
                snprintf(StaArray[NumEstaciones].szChannel, 15, "%s", s2);
                snprintf(StaArray[NumEstaciones].szNetID, 15, "%s", s3);
                if (parsed == 4) { StaArray[NumEstaciones].dScreenScale = atof(s4); strcpy(StaArray[NumEstaciones].szLocation, "--"); } 
                else { snprintf(StaArray[NumEstaciones].szLocation, 15, "%s", s4); StaArray[NumEstaciones].dScreenScale = d1; }
                StaArray[NumEstaciones].dLat = 0.0; StaArray[NumEstaciones].dLon = 0.0;
            }
            
            /* Buffer perezoso: se crea al recibir datos (ver ws_fetch_finish). */
            StaArray[NumEstaciones].trace = NULL;
            (void)lMaxBufSamps;
            NumEstaciones++;
        }
    }
    fclose(fp); 
}

int ParseY2K_Hypo(char *msg, double *otime, double *lat, double *lon, double *depth, double *res, int *nps, int *azm, int *qid, int *qver, double *pref_mag, char *mag_type, int mod) {
    char str[20];
    if (strlen(msg) < 162) return -1; 
    strncpy(str, msg, 14); str[14] = '\0';
    struct tm t; memset(&t, 0, sizeof(struct tm));
    char tmp[5];
    strncpy(tmp, str, 4); tmp[4]=0; t.tm_year = atoi(tmp) - 1900;
    strncpy(tmp, str+4, 2); tmp[2]=0; t.tm_mon = atoi(tmp) - 1;
    strncpy(tmp, str+6, 2); tmp[2]=0; t.tm_mday = atoi(tmp);
    strncpy(tmp, str+8, 2); tmp[2]=0; t.tm_hour = atoi(tmp);
    strncpy(tmp, str+10, 2); tmp[2]=0; t.tm_min = atoi(tmp);
    strncpy(tmp, str+12, 2); tmp[2]=0; t.tm_sec = atoi(tmp);
    char *tz = getenv("TZ"); setenv("TZ", "GMT", 1); tzset();
    *otime = (double)mktime(&t);
    if (tz) setenv("TZ", tz, 1); else unsetenv("TZ"); tzset();
    
    strncpy(str, msg+14, 2); str[2] = '\0'; *otime += (double)atoi(str) / 100.0;
    strncpy(str, msg+16, 2); str[2] = '\0'; double dLat = atof(str);
    char lat_dir = msg[18]; strncpy(str, msg+19, 4); str[4] = '\0'; dLat += (atof(str) / 100.0) / 60.0;
    *lat = (lat_dir == 'S') ? -dLat : dLat;
    strncpy(str, msg+23, 3); str[3] = '\0'; double dLon = atof(str);
    char lon_dir = msg[26]; strncpy(str, msg+27, 4); str[4] = '\0'; dLon += (atof(str) / 100.0) / 60.0;
    *lon = (lon_dir == 'W') ? -dLon : dLon;
    strncpy(str, msg+31, 5); str[5] = '\0'; *depth = atof(str) / 100.0;
    strncpy(str, msg+39, 3); str[3] = '\0'; *nps = atoi(str);
    strncpy(str, msg+42, 3); str[3] = '\0'; *azm = atoi(str);
    strncpy(str, msg+48, 4); str[4] = '\0'; *res = atof(str) / 100.0;
    strncpy(str, msg+136, 10); str[10] = '\0'; *qid = atoi(str);
    /* Version canonica EarthWorm: eventVersion en offset 178 (read_arc.h:215).
       Fallback a version[1] en offset 161 si eventVersion viene vacio. */
    strncpy(str, msg+178, 4); str[4] = '\0'; *qver = atoi(str);
    if (*qver == 0) { strncpy(str, msg+161, 1); str[1] = '\0'; *qver = atoi(str); }
    strncpy(str, msg+147, 3); str[3] = '\0'; *pref_mag = atof(str) / 100.0;
    strncpy(mag_type, msg+150, 3); mag_type[3] = '\0';

    int cache_slot = -1;
    for(int c=0; c<MAX_CACHED_EVENTS; c++) { if(PickCache[c].qid == *qid && PickCache[c].mod == mod) { cache_slot = c; break; } }
    if (cache_slot == -1) { cache_slot = pick_cache_idx; pick_cache_idx = (pick_cache_idx + 1) % MAX_CACHED_EVENTS; }
    PickCache[cache_slot].qid = *qid; PickCache[cache_slot].mod = mod; PickCache[cache_slot].num_picks = 0;

    char *line = strchr(msg, '\n');
    while (line != NULL && *line != '\0') {
        line++; char *next_line = strchr(line, '\n'); int len = next_line ? (next_line - line) : strlen(line);
        if (len >= 114 && line[0] != '$') {
            char sta[6]={0}, net[3]={0}, comp[4]={0}, p_time_str[18]={0}, p_remark[5]={0};
            strncpy(sta, line + 0, 5); strncpy(net, line + 5, 2); strncpy(comp, line + 9, 3);
            strncpy(p_remark, line + 13, 4); strncpy(p_time_str, line + 17, 17); 
            char *p; for(p=sta+4; p>=sta && *p==' '; p--) *p='\0'; for(p=comp+2; p>=comp && *p==' '; p--) *p='\0'; 
            if (p_remark[1] == 'P') {
                struct tm pt; char tmp_p[6]; memset(&pt, 0, sizeof(struct tm));
                strncpy(tmp_p, p_time_str, 4); tmp_p[4]=0; pt.tm_year = atoi(tmp_p) - 1900;
                strncpy(tmp_p, p_time_str+4, 2); tmp_p[2]=0; pt.tm_mon = atoi(tmp_p) - 1;
                strncpy(tmp_p, p_time_str+6, 2); tmp_p[2]=0; pt.tm_mday = atoi(tmp_p);
                strncpy(tmp_p, p_time_str+8, 2); tmp_p[2]=0; pt.tm_hour = atoi(tmp_p);
                strncpy(tmp_p, p_time_str+10, 2); tmp_p[2]=0; pt.tm_min = atoi(tmp_p);
                strncpy(tmp_p, p_time_str+12, 5); tmp_p[5]=0; double p_sec = atof(tmp_p); pt.tm_sec = (int)p_sec;
                setenv("TZ", "GMT", 1); tzset(); time_t p_epoch = mktime(&pt);
                if (tz) setenv("TZ", tz, 1); else unsetenv("TZ"); tzset();
                double exact_ptime = (double)p_epoch + (p_sec - (int)p_sec);
                if (PickCache[cache_slot].num_picks < MAX_PICKS_PER_EVENT) {
                    int pIdx = PickCache[cache_slot].num_picks;
                    strncpy(PickCache[cache_slot].picks[pIdx].sta, sta, 7); strncpy(PickCache[cache_slot].picks[pIdx].chan, comp, 7);
                    PickCache[cache_slot].picks[pIdx].pTime = exact_ptime;
                    if (p_remark[3] == 'U' || p_remark[3] == 'D' || p_remark[3] == '?') snprintf(PickCache[cache_slot].picks[pIdx].phase, 7, "P(%c)", p_remark[3]);
                    else strcpy(PickCache[cache_slot].picks[pIdx].phase, "P");
                    PickCache[cache_slot].num_picks++;
                }
            }
        }
        line = next_line;
    }
    return 1;
}

/* Recuperacion al arrancar: lee el archivo de estado de csnloc (unico
   escritor). Formato:
     E <id> <version> <t0> <lat> <lon> <depth> <nph> <rms> <gap> <dmin>
       <score> <grid_level> <last_update> <emitted> <nstored> <npruned> <t0_detect>
     P <sta> <net> <chan> <loc> <phase> <t_epoch> <weight> <residual>
   El display en tiempo real NO usa este archivo: viene del anillo. */
void cargar_sismos_iniciales(GtkWidget *tree) {
    (void)tree;
    char state_path[MAX_STR];
    if (StateFile[0] == '/') {
        snprintf(state_path, sizeof(state_path), "%s", StateFile);
    } else {
        const char *logdir = getenv("EW_LOG");
        snprintf(state_path, sizeof(state_path), "%s/%s",
                 (logdir && logdir[0]) ? logdir : ".", StateFile);
    }

    FILE *fp = fopen(state_path, "r"); if (!fp) return;
    char line[512];
    int nrec = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == 'E') {
            unsigned long id; unsigned int version; double t0, lat, lon, depth;
            int nph, grid_level, emitted, nstored, npruned; double rms, gap, dmin, score, last_update, t0_detect;
            if (sscanf(line + 2,
                       "%lu %u %lf %lf %lf %lf %d %lf %lf %lf %lf %d %lf %d %d %d %lf",
                       &id, &version, &t0, &lat, &lon, &depth, &nph, &rms,
                       &gap, &dmin, &score, &grid_level, &last_update,
                       &emitted, &nstored, &npruned, &t0_detect) != 17)
                continue;

            char fecha[32], hora[32], szLat[32], szLon[32], szDep[32], szRes[32], szAzm[32], szStn[32], szID[32];
            time_t rawtime = (time_t)t0; struct tm *ptm = gmtime(&rawtime);
            if (ptm) { snprintf(fecha, sizeof(fecha), "%02d/%02d", ptm->tm_mon + 1, ptm->tm_mday); snprintf(hora, sizeof(hora), "%02d:%02d:%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec); }
            else { strcpy(fecha, "--/--"); strcpy(hora, "--:--:--"); }
            snprintf(szLat, sizeof(szLat), "%.2f%c", fabs(lat), lat < 0 ? 'S' : 'N');
            snprintf(szLon, sizeof(szLon), "%.2f%c", fabs(lon), lon < 0 ? 'W' : 'E');
            snprintf(szDep, sizeof(szDep), "%.0f", depth);
            snprintf(szRes, sizeof(szRes), "%.1f", rms);
            snprintf(szAzm, sizeof(szAzm), "%.0f", gap);
            snprintf(szStn, sizeof(szStn), "%d", nph);
            snprintf(szID, sizeof(szID), "%010lu", id);
            char szVer[32]; snprintf(szVer, sizeof(szVer), "%u", version);

            CsnhypodbpRow *row = csnhypodbp_row_new(fecha, hora, szLat, szLon, szDep, szRes,
                szAzm, szStn, szID, "-", "-", "csnloc", szVer, t0, (int)version, (int)id,
                lat, lon, depth, 0);
            g_list_store_append(g_store_hypo, row);
            g_object_unref(row);
            nrec++;
        }
    }
    fclose(fp);
    logit("t", "csnhypodbp: recuperados %d eventos de %s\n", nrec, state_path);
    if (nrec > 0) gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_selection_hypo), 0, TRUE);
}

int procesar_mensaje_sismo(GtkWidget *tree, const char *payload, int mod) {
    (void)tree;
    double otime, lat, lon, depth, res, pref_mag; int nps, azm, qid, qver; char mag_type[16] = "";
    if (ParseY2K_Hypo((char*)payload, &otime, &lat, &lon, &depth, &res, &nps, &azm, &qid, &qver, &pref_mag, mag_type, mod) < 0) return 0;

    char fecha[32], hora[32], szLat[32], szLon[32], szDep[32], szRes[32], szAzm[32], szStn[32], szID[32];
    char szMod[32], szVer[32];
    time_t rawtime = (time_t)(otime); struct tm *ptm = gmtime(&rawtime);
    if (ptm) { snprintf(fecha, sizeof(fecha), "%02d/%02d", ptm->tm_mon + 1, ptm->tm_mday); snprintf(hora, sizeof(hora), "%02d:%02d:%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec); }
    else { strcpy(fecha, "--/--"); strcpy(hora, "--:--:--"); }
    snprintf(szLat, sizeof(szLat), "%.2f%c", fabs(lat), lat < 0 ? 'S' : 'N'); snprintf(szLon, sizeof(szLon), "%.2f%c", fabs(lon), lon < 0 ? 'W' : 'E');
    snprintf(szDep, sizeof(szDep), "%.0f", depth); snprintf(szRes, sizeof(szRes), "%.1f", res); snprintf(szAzm, sizeof(szAzm), "%d", azm);
    snprintf(szStn, sizeof(szStn), "%d", nps); snprintf(szID, sizeof(szID), "%010d", qid);
    snprintf(szMod, sizeof(szMod), "%s", ModLabel(mod)); snprintf(szVer, sizeof(szVer), "%d", qver);

    guint n = g_list_model_get_n_items(G_LIST_MODEL(g_store_hypo));
    int found = -1;
    for (guint i = 0; i < n; i++) {
        CsnhypodbpRow *row = g_list_model_get_item(G_LIST_MODEL(g_store_hypo), i);
        int rqid = 0, rmod = 0; g_object_get(row, "qid", &rqid, "mod", &rmod, NULL);
        g_object_unref(row);
        if (rqid == qid && rmod == mod) { found = (int)i; break; }
    }
    int result_status = 0;
    if (found >= 0) {
        CsnhypodbpRow *row = g_list_model_get_item(G_LIST_MODEL(g_store_hypo), found);
        int existing_qver = 0; g_object_get(row, "qver", &existing_qver, NULL);
        if (qver >= existing_qver) {
            char szMl[32] = "-", szMwp[32] = "-";
            const char *oml = csnhypodbp_row_col(row, 9);
            const char *omwp = csnhypodbp_row_col(row, 10);
            if (oml && oml[0]) { strncpy(szMl, oml, 31); szMl[31]='\0'; }
            if (omwp && omwp[0]) { strncpy(szMwp, omwp, 31); szMwp[31]='\0'; }
            g_object_set(row, "c0",fecha,"c1",hora,"c2",szLat,"c3",szLon,"c4",szDep,"c5",szRes,
                         "c6",szAzm,"c7",szStn,"c8",szID,"c9",szMl,"c10",szMwp,"c11",szMod,"c12",szVer,
                         "otime",otime,"qver",qver,"qid",qid,"lat",lat,"lon",lon,"depth",depth,"mod",mod,NULL);
            if (qid == selected_qid && mod == selected_mod) result_status = 1;
            g_history_needs_saving = TRUE;
        }
        g_object_unref(row);
    } else {
        CsnhypodbpRow *row = csnhypodbp_row_new(fecha, hora, szLat, szLon, szDep, szRes, szAzm,
            szStn, szID, "-", "-", szMod, szVer, otime, qver, qid, lat, lon, depth, mod);
        g_list_store_append(g_store_hypo, row);
        g_object_unref(row);
        result_status = 2; g_history_needs_saving = TRUE;
    }
    return result_status;
}

/* FIX 1: Lectura segura de Magnitudes utilizando rd_mag() nativo de Earthworm */
int procesar_mensaje_mag(GtkWidget *tree, const char *payload) {
    (void)tree;
    MAG_INFO mag;
    memset(&mag, 0, sizeof(MAG_INFO));
    if (rd_mag((char*)payload, strlen(payload), &mag) < 0) {
        logit("e", "csnhypodbp: Error en rd_mag al parsear TYPE_MAGNITUDE\n");
        return 0;
    }
    int qid = atoi(mag.qid);
    char mag_str[32];
    snprintf(mag_str, sizeof(mag_str), "%.1f-%d", mag.mag, mag.nstations);

    guint n = g_list_model_get_n_items(G_LIST_MODEL(g_store_hypo));
    for (guint i = 0; i < n; i++) {
        CsnhypodbpRow *row = g_list_model_get_item(G_LIST_MODEL(g_store_hypo), i);
        int rqid = 0; g_object_get(row, "qid", &rqid, NULL);
        if (rqid == qid) {
            if (strcmp(mag.szmagtype, "ML") == 0 || strcmp(mag.szmagtype, "Ml") == 0) {
                g_object_set(row, "c9", mag_str, NULL);
                logit("t", "csnhypodbp: Mag actualizada ID %d -> ML: %s\n", qid, mag_str);
            } else if (strcmp(mag.szmagtype, "Mwp") == 0 || strcmp(mag.szmagtype, "MWP") == 0) {
                g_object_set(row, "c10", mag_str, NULL);
                logit("t", "csnhypodbp: Mag actualizada ID %d -> Mwp: %s\n", qid, mag_str);
            }
            g_object_unref(row);
            g_history_needs_saving = TRUE;
            return 1;
        }
        g_object_unref(row);
    }
    return 0;
}

/* --- Descarga asincrona de formas de onda (worker + publicacion) ---
 * El fetch a wave_serverV bloquea (TCP con WsTimeout); hacerlo en el hilo
 * principal congelaba la UI al seleccionar un sismo. El worker SOLO hace I/O
 * (y lee metadata de estaciones, estable tras el arranque) y copia los bytes
 * crudos; el parseo a StaArray, el post-proceso y el filtrado corren en el
 * hilo principal (GTK no es thread-safe). Un solo job a la vez (g_ws_busy). */
typedef struct {
    int    idx;
    int    has_data;
    double rate;
    double dist;
    long   act_len;
    char  *raw;
} WsFetchedSta;

typedef struct {
    int           qid;
    int           mod;
    double        eq_lat, eq_lon;
    double        req_start, req_end;
    double        max_dist_km;
    int           nsta;
    WsFetchedSta *stas;
} WsFetchJob;

static int g_ws_busy = 0;

static gboolean ws_fetch_finish(gpointer data);

static gpointer ws_fetch_thread(gpointer data)
{
    WsFetchJob *job = data;
    long max_trace_buf = 2000000;
    char *trace_buffer = malloc(max_trace_buf);

    if (trace_buffer && ws_menu.head == NULL) {
        if (wsAppendMenu(WsIP, WsPort, &ws_menu, WsTimeout) != WS_ERR_NONE) {
            free(trace_buffer);
            trace_buffer = NULL;
        }
    }

    for (int i = 0; trace_buffer && i < job->nsta; i++) {
        WsFetchedSta *f = &job->stas[i];
        f->idx = i; f->has_data = 0; f->rate = 0.0; f->dist = 0.0;
        f->act_len = 0; f->raw = NULL;

        if (StaArray[i].dLat != 0.0 && StaArray[i].dLon != 0.0 && job->eq_lat != 0.0) {
            double rlat1 = job->eq_lat * M_PI / 180.0, rlat2 = StaArray[i].dLat * M_PI / 180.0;
            double dlon = (StaArray[i].dLon - job->eq_lon) * M_PI / 180.0, dlat = (StaArray[i].dLat - job->eq_lat) * M_PI / 180.0;
            double a = sin(dlat/2.0)*sin(dlat/2.0) + cos(rlat1)*cos(rlat2)*sin(dlon/2.0)*sin(dlon/2.0);
            if (a < 0.0) a = 0.0; if (a > 1.0) a = 1.0;
            f->dist = 2.0 * atan2(sqrt(a), sqrt(1.0-a)) * 180.0 / M_PI;
        }
        if (f->dist * 111.19 > job->max_dist_km && f->dist != 0.0) continue;

        TRACE_REQ req; memset(&req, 0, sizeof(TRACE_REQ));
        snprintf(req.sta, sizeof(req.sta), "%s", StaArray[i].szStation);
        snprintf(req.net, sizeof(req.net), "%s", StaArray[i].szNetID);
        snprintf(req.chan, sizeof(req.chan), "%s", StaArray[i].szChannel);
        snprintf(req.loc, sizeof(req.loc), "%s", StaArray[i].szLocation);
        req.reqStarttime = job->req_start; req.reqEndtime = job->req_end;
        req.pBuf = trace_buffer; req.bufLen = max_trace_buf; req.timeout = WsTimeout; req.fill = 0;

        int ws_res = wsGetTraceBinL(&req, &ws_menu, WsTimeout);
        if (ws_res == WS_ERR_NONE && req.actLen > 0) {
            f->raw = malloc(req.actLen);
            if (f->raw) { memcpy(f->raw, req.pBuf, req.actLen); f->act_len = req.actLen; f->has_data = 1; }
        } else if (ws_res == WS_ERR_BROKEN_CONNECTION || ws_res == WS_ERR_TIMEOUT) {
            wsKillMenu(&ws_menu); ws_menu.head = NULL; break;
        }
    }
    free(trace_buffer);

    g_idle_add(ws_fetch_finish, job);
    return NULL;
}

static gboolean ws_fetch_finish(gpointer data)
{
    WsFetchJob *job = data;
    int stale = (selected_qid != job->qid || selected_mod != job->mod);

    if (!stale) {
        for (int i = 0; i < NumEstaciones; i++) {
            EwGuiTrace *tr = StaArray[i].trace;
            bHasData[i] = 0;
            if (tr) ewgui_trace_clear(tr);
            StaArray[i].iNumPicks = 0; StaArray[i].dManualPickTime = 0.0; g_StaDist[i] = 0.0;
            if (tr) ewgui_trace_set_oldest(tr, job->req_start);

            for (int c = 0; c < MAX_CACHED_EVENTS; c++) {
                if (PickCache[c].qid == job->qid && PickCache[c].mod == job->mod) {
                    for (int p = 0; p < PickCache[c].num_picks; p++) {
                        if (!strcmp(StaArray[i].szStation, PickCache[c].picks[p].sta) &&
                            !strcmp(StaArray[i].szChannel, PickCache[c].picks[p].chan)) {
                            int spIdx = StaArray[i].iNumPicks % MAX_PICKS_PER_STA;
                            StaArray[i].picks[spIdx].dTime = PickCache[c].picks[p].pTime;
                            strcpy(StaArray[i].picks[spIdx].szPhase, PickCache[c].picks[p].phase);
                            StaArray[i].iNumPicks++;
                        }
                    }
                    break;
                }
            }

            if (i < job->nsta) {
                g_StaDist[i] = job->stas[i].dist;
                WsFetchedSta *f = &job->stas[i];
                if (f->has_data && f->raw) {
                    if (!tr) { tr = ewgui_trace_new((long)MAX_MINUTES * 60 * 100); StaArray[i].trace = tr; }
                    if (!tr) continue;
                    bHasData[i] = 1;
                    int32_t *rawbuf = ewgui_trace_raw(tr);
                    long cap = ewgui_trace_capacity(tr);
                    long ctr = 0;
                    char *ptr = f->raw, *end_ptr = f->raw + f->act_len; double rate = 0.0;
                    while (ptr < end_ptr) {
                        if (ptr + sizeof(TRACE2_HEADER) > end_ptr) break;
                        TRACE2_HEADER *trh = (TRACE2_HEADER *)ptr;
                        if (trh->nsamp < 0 || trh->nsamp > 500000 || trh->samprate <= 0.0) break;
                        if (rate == 0.0) rate = trh->samprate;
                        int dsize = (trh->datatype[1] == '2') ? 2 : 4;
                        char *dptr = ptr + sizeof(TRACE2_HEADER);
                        if (dptr + (trh->nsamp * dsize) > end_ptr) break;
                        double t_start_paq = trh->starttime;
                        for (int s = 0; s < trh->nsamp; s++) {
                            double x = (dsize == 4) ? (double)*((int32_t*)(dptr + s*4)) : (double)*((int16_t*)(dptr + s*2));
                            double t_samp = t_start_paq + ((double)s / rate);
                            long k = (long)((t_samp - job->req_start) * rate + 0.5);
                            if (k >= 0 && k < cap) {
                                rawbuf[k] = (int32_t)x;
                                if (k >= ctr) ctr = k + 1;
                            }
                        }
                        ptr += sizeof(TRACE2_HEADER) + trh->nsamp * dsize;
                    }
                    ewgui_trace_set_rate(tr, rate);
                    ewgui_trace_set_length(tr, ctr);
                }
            }
        }

        /* Post-proceso por estacion (portado de EW7 ReloadWaveforms):
           interpola los gaps cortos, recorta la cola de datos futuros y
           aplica el filtro seleccionado (Raw/HP/LP/BP) + demean. */
        for (int i = 0; i < NumEstaciones; i++) {
            if (bHasData[i]) {
                ewgui_trace_finish(StaArray[i].trace);
                if (ewgui_trace_length(StaArray[i].trace) <= 0) bHasData[i] = 0;
            }
        }
        ApplySelectedFilter();

        if (window_global) gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Hypocenter database picker (EW8)");
        if (canvas_global) {
            actualizar_altura_canvas();
            ewgui_canvas_queue_draw(canvas_global);
        }
    } else {
        pending_waveform_reload = TRUE;   /* la seleccion cambio: reintentar */
    }

    for (int i = 0; i < job->nsta; i++) free(job->stas[i].raw);
    free(job->stas);
    free(job);
    g_ws_busy = 0;
    return G_SOURCE_REMOVE;
}

void FetchWaveformsForEvent(double otime, double eq_lat, double eq_lon, int qid, int mod) {
    if (g_ws_busy) { pending_waveform_reload = TRUE; return; }

    WsFetchJob *job = calloc(1, sizeof(*job));
    if (!job) return;
    job->qid = qid; job->mod = mod;
    job->eq_lat = eq_lat; job->eq_lon = eq_lon;
    job->req_start = otime - 30.0;
    job->req_end   = otime + g_dScreenTime + g_max_dist_km / 5.5 + g_align_lead;
    job->max_dist_km = g_max_dist_km;
    job->nsta = NumEstaciones;
    job->stas = calloc(NumEstaciones > 0 ? (size_t)NumEstaciones : 1, sizeof(WsFetchedSta));
    if (!job->stas) { free(job); return; }

    g_ws_busy = 1;
    GThread *th = g_thread_new("ws_fetch", ws_fetch_thread, job);
    g_thread_unref(th);
}

