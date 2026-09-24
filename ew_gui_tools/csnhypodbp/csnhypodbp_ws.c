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
            
            StaArray[NumEstaciones].lRawCircSize = lMaxBufSamps;
            StaArray[NumEstaciones].plRawCircBuff = (int32_t *) calloc(lMaxBufSamps, sizeof(int32_t));
            StaArray[NumEstaciones].plFiltCircBuff = (int32_t *) calloc(lMaxBufSamps, sizeof(int32_t));
            
            if (StaArray[NumEstaciones].plRawCircBuff == NULL || StaArray[NumEstaciones].plFiltCircBuff == NULL) {
                logit("e", ">> TRACER FATAL [WS]: Sin memoria RAM para buffer. Sistema OOM.\n");
                exit(-1);
            }
            NumEstaciones++;
        }
    }
    fclose(fp); 
}

int ParseY2K_Hypo(char *msg, double *otime, double *lat, double *lon, double *depth, double *res, int *nps, int *azm, int *qid, int *qver, double *pref_mag, char *mag_type) {
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
    strncpy(str, msg+160, 2); str[2] = '\0'; *qver = atoi(str);
    strncpy(str, msg+147, 3); str[3] = '\0'; *pref_mag = atof(str) / 100.0;
    strncpy(mag_type, msg+150, 3); mag_type[3] = '\0';

    int cache_slot = -1;
    for(int c=0; c<MAX_CACHED_EVENTS; c++) { if(PickCache[c].qid == *qid) { cache_slot = c; break; } }
    if (cache_slot == -1) { cache_slot = pick_cache_idx; pick_cache_idx = (pick_cache_idx + 1) % MAX_CACHED_EVENTS; }
    PickCache[cache_slot].qid = *qid; PickCache[cache_slot].num_picks = 0;

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

void SaveHistoryFile(GtkWidget *tree) {
    char TempFile[512]; snprintf(TempFile, sizeof(TempFile), "%s.tmp", HistoryFile);
    FILE *fp = fopen(TempFile, "w"); if (!fp) return;
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)));
    GtkTreeIter iter; gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    int count = 0;
    while (valid && count < 50) { 
        gchar *fecha=NULL, *hora=NULL, *lat_s=NULL, *lon_s=NULL, *dep_s=NULL, *res_s=NULL, *azm_s=NULL, *stn_s=NULL, *id_s=NULL, *ml_s=NULL, *mwp_s=NULL;
        double otime=0, lat=0, lon=0, depth=0; int qver=0, qid=0;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &fecha, 1, &hora, 2, &lat_s, 3, &lon_s, 4, &dep_s, 5, &res_s, 6, &azm_s, 7, &stn_s, 8, &id_s, 9, &ml_s, 10, &mwp_s, 14, &otime, 15, &qver, 16, &qid, 17, &lat, 18, &lon, 19, &depth, -1);
        fprintf(fp, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%lf,%d,%d,%lf,%lf,%lf\n", fecha ? fecha : "-", hora ? hora : "-", lat_s ? lat_s : "-", lon_s ? lon_s : "-", dep_s ? dep_s : "-", res_s ? res_s : "-", azm_s ? azm_s : "-", stn_s ? stn_s : "-", id_s ? id_s : "-", ml_s ? ml_s : "-", mwp_s ? mwp_s : "-", otime, qver, qid, lat, lon, depth);
        if (fecha) g_free(fecha); if (hora) g_free(hora); if (lat_s) g_free(lat_s); if (lon_s) g_free(lon_s); if (dep_s) g_free(dep_s); if (res_s) g_free(res_s); if (azm_s) g_free(azm_s); if (stn_s) g_free(stn_s); if (id_s) g_free(id_s); if (ml_s) g_free(ml_s); if (mwp_s) g_free(mwp_s);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter); count++;
    }
    fclose(fp); rename(TempFile, HistoryFile);
}

void cargar_sismos_iniciales(GtkWidget *tree) {
    FILE *fp = fopen(HistoryFile, "r"); if (!fp) return;
    char line[512]; GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))); GtkTreeIter iter;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '\n' || line[0] == '\r') continue;
        char fecha[32], hora[32], lat_s[32], lon_s[32], dep_s[32], res_s[32], azm_s[32], stn_s[32], id_s[32], ml_s[32], mwp_s[32];
        double otime, lat, lon, depth; int qver, qid;
        int parsed = sscanf(line, "%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%31[^,],%lf,%d,%d,%lf,%lf,%lf", fecha, hora, lat_s, lon_s, dep_s, res_s, azm_s, stn_s, id_s, ml_s, mwp_s, &otime, &qver, &qid, &lat, &lon, &depth);
        if (parsed == 17) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, fecha, 1, hora, 2, lat_s, 3, lon_s, 4, dep_s, 5, res_s, 6, azm_s, 7, stn_s, 8, id_s, 9, ml_s, 10, mwp_s, 14, otime, 15, qver, 16, qid, 17, lat, 18, lon, 19, depth, -1);
        }
    }
    fclose(fp);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreePath *path = gtk_tree_path_new_first();
    if (path) { gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree), path, NULL, FALSE, 0.0, 0.0); gtk_tree_selection_select_path(selection, path); gtk_tree_path_free(path); }
}

int procesar_mensaje_sismo(GtkWidget *tree, const char *payload) {
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)));
    double otime, lat, lon, depth, res, pref_mag; int nps, azm, qid, qver; char mag_type[16] = "";
    if (ParseY2K_Hypo((char*)payload, &otime, &lat, &lon, &depth, &res, &nps, &azm, &qid, &qver, &pref_mag, mag_type) < 0) return 0;
    
    char fecha[32], hora[32], szLat[32], szLon[32], szDep[32], szRes[32], szAzm[32], szStn[32], szID[32];
    time_t rawtime = (time_t)(otime); struct tm *ptm = gmtime(&rawtime); 
    if (ptm) { snprintf(fecha, sizeof(fecha), "%02d/%02d", ptm->tm_mon + 1, ptm->tm_mday); snprintf(hora, sizeof(hora), "%02d:%02d:%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec); } 
    else { strcpy(fecha, "--/--"); strcpy(hora, "--:--:--"); }
    snprintf(szLat, sizeof(szLat), "%.2f%c", fabs(lat), lat < 0 ? 'S' : 'N'); snprintf(szLon, sizeof(szLon), "%.2f%c", fabs(lon), lon < 0 ? 'W' : 'E');
    snprintf(szDep, sizeof(szDep), "%.0f", depth); snprintf(szRes, sizeof(szRes), "%.1f", res); snprintf(szAzm, sizeof(szAzm), "%d", azm);
    snprintf(szStn, sizeof(szStn), "%d", nps); snprintf(szID, sizeof(szID), "%010d", qid);

    GtkTreeIter iter, match_iter; gboolean existe = FALSE; gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    while (valid) {
        int row_qid; gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 16, &row_qid, -1);
        if (row_qid == qid) { existe = TRUE; match_iter = iter; break; }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
    int result_status = 0;
    if (existe) {
        int existing_qver; gtk_tree_model_get(GTK_TREE_MODEL(store), &match_iter, 15, &existing_qver, -1);
        if (qver >= existing_qver) {
            gchar *old_ml = NULL, *old_mwp = NULL; char szMl[32] = "-", szMwp[32] = "-";
            gtk_tree_model_get(GTK_TREE_MODEL(store), &match_iter, 9, &old_ml, 10, &old_mwp, -1);
            if (old_ml) { strncpy(szMl, old_ml, 31); szMl[31]='\0'; g_free(old_ml); }
            if (old_mwp) { strncpy(szMwp, old_mwp, 31); szMwp[31]='\0'; g_free(old_mwp); }
            gtk_list_store_set(store, &match_iter, 0, fecha, 1, hora, 2, szLat, 3, szLon, 4, szDep, 5, szRes, 6, szAzm, 7, szStn, 8, szID, 9, szMl, 10, szMwp, 14, otime, 15, qver, 16, qid, 17, lat, 18, lon, 19, depth, -1); 
            if (qid == selected_qid) result_status = 1;
            g_history_needs_saving = TRUE; 
        }
    } else {
        char szMl[32] = "-", szMwp[32] = "-"; gtk_list_store_append(store, &match_iter);
        gtk_list_store_set(store, &match_iter, 0, fecha, 1, hora, 2, szLat, 3, szLon, 4, szDep, 5, szRes, 6, szAzm, 7, szStn, 8, szID, 9, szMl, 10, szMwp, 14, otime, 15, qver, 16, qid, 17, lat, 18, lon, 19, depth, -1);
        result_status = 2; g_history_needs_saving = TRUE;
    }
    return result_status;
}

/* FIX 1: Lectura segura de Magnitudes utilizando rd_mag() nativo de Earthworm */
int procesar_mensaje_mag(GtkWidget *tree, const char *payload) {
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))); 
    MAG_INFO mag;
    
    memset(&mag, 0, sizeof(MAG_INFO)); /* Asegurar que no hay basura en memoria */
    
    if (rd_mag((char*)payload, strlen(payload), &mag) < 0) {
        logit("e", "csnhypodbp: Error en rd_mag al parsear TYPE_MAGNITUDE\n");
        return 0;
    }
    
    int qid = atoi(mag.qid);
    char mag_str[32]; 
    snprintf(mag_str, sizeof(mag_str), "%.1f-%d", mag.mag, mag.nstations);
    
    GtkTreeIter iter; gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    while (valid) {
        int row_qid; gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 16, &row_qid, -1);
        if (row_qid == qid) {
            if (strcmp(mag.szmagtype, "ML") == 0 || strcmp(mag.szmagtype, "Ml") == 0) {
                gtk_list_store_set(store, &iter, 9, mag_str, -1);
                logit("t", "csnhypodbp: Mag actualizada ID %d -> ML: %s\n", qid, mag_str);
            } else if (strcmp(mag.szmagtype, "Mwp") == 0 || strcmp(mag.szmagtype, "MWP") == 0) {
                gtk_list_store_set(store, &iter, 10, mag_str, -1);
                logit("t", "csnhypodbp: Mag actualizada ID %d -> Mwp: %s\n", qid, mag_str);
            }
            g_history_needs_saving = TRUE; 
            return 1;
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
    return 0;
}

void FetchWaveformsForEvent(double otime, double eq_lat, double eq_lon, int qid) {
    for (int i = 0; i < NumEstaciones; i++) {
        bHasData[i] = 0; StaArray[i].lRawCircCtr = 0;
        StaArray[i].iNumPicks = 0; StaArray[i].dManualPickTime = 0.0; g_StaDist[i] = 0.0;
        for (long k = 0; k < StaArray[i].lRawCircSize; k++) {
            StaArray[i].plRawCircBuff[k] = INT_MAX;
            StaArray[i].plFiltCircBuff[k] = INT_MAX;
        }
    }
    if (ws_menu.head == NULL) {
        if (wsAppendMenu(WsIP, WsPort, &ws_menu, WsTimeout) != WS_ERR_NONE) return;
    }
    /* Ventana de fetch escalada con el tiempo de visualizacion (g_dScreenTime).
       Cubre: 30 s previos al origen + la ventana de visualizacion completa +
       el tiempo de viaje de la onda P de la estacion mas lejana (conservador,
       ~5.5 km/s) y la alineacion por P (g_align_lead). */
    double req_start = otime - 30.0;
    double req_end   = otime + g_dScreenTime + g_max_dist_km / 5.5 + g_align_lead;
    long max_trace_buf = 2000000; 
    char *trace_buffer = malloc(max_trace_buf);
    if (!trace_buffer) return;

    for (int i = 0; i < NumEstaciones; i++) {
        StaArray[i].dOldestTime = req_start;
        for(int c = 0; c < MAX_CACHED_EVENTS; c++) {
            if (PickCache[c].qid == qid) {
                for (int p = 0; p < PickCache[c].num_picks; p++) {
                    if (!strcmp(StaArray[i].szStation, PickCache[c].picks[p].sta) && !strcmp(StaArray[i].szChannel, PickCache[c].picks[p].chan)) {
                        int spIdx = StaArray[i].iNumPicks % MAX_PICKS_PER_STA;
                        StaArray[i].picks[spIdx].dTime = PickCache[c].picks[p].pTime;
                        strcpy(StaArray[i].picks[spIdx].szPhase, PickCache[c].picks[p].phase); StaArray[i].iNumPicks++;
                    }
                }
                break;
            }
        }
        if (StaArray[i].dLat != 0.0 && StaArray[i].dLon != 0.0 && eq_lat != 0.0) {
            double rlat1 = eq_lat * M_PI / 180.0, rlat2 = StaArray[i].dLat * M_PI / 180.0;
            double dlon = (StaArray[i].dLon - eq_lon) * M_PI / 180.0, dlat = (StaArray[i].dLat - eq_lat) * M_PI / 180.0;
            double a = sin(dlat/2.0)*sin(dlat/2.0) + cos(rlat1)*cos(rlat2)*sin(dlon/2.0)*sin(dlon/2.0);
            if (a < 0.0) a = 0.0; if (a > 1.0) a = 1.0; 
            g_StaDist[i] = 2.0 * atan2(sqrt(a), sqrt(1.0-a)) * 180.0 / M_PI; 
        }
        if (g_StaDist[i] * 111.19 > g_max_dist_km && g_StaDist[i] != 0.0) continue;

        TRACE_REQ req; memset(&req, 0, sizeof(TRACE_REQ));
        snprintf(req.sta, sizeof(req.sta), "%s", StaArray[i].szStation);
        snprintf(req.net, sizeof(req.net), "%s", StaArray[i].szNetID);
        snprintf(req.chan, sizeof(req.chan), "%s", StaArray[i].szChannel);
        snprintf(req.loc, sizeof(req.loc), "%s", StaArray[i].szLocation);
        req.reqStarttime = req_start; req.reqEndtime = req_end; req.pBuf = trace_buffer; req.bufLen = max_trace_buf; req.timeout = WsTimeout; req.fill = 0;

        int ws_res = wsGetTraceBinL(&req, &ws_menu, WsTimeout);
        if (ws_res == WS_ERR_NONE && req.actLen > 0) {
            bHasData[i] = 1; 
            char *ptr = req.pBuf, *end_ptr = req.pBuf + req.actLen; double rate = 0.0;
            while (ptr < end_ptr) {
                if (ptr + sizeof(TRACE2_HEADER) > end_ptr) break;
                TRACE2_HEADER *trh = (TRACE2_HEADER *)ptr;
                if (trh->nsamp < 0 || trh->nsamp > 500000 || trh->samprate <= 0.0) break;
                if (rate == 0.0) rate = trh->samprate;
                int dsize = (trh->datatype[1] == '2') ? 2 : 4; char *dptr = ptr + sizeof(TRACE2_HEADER);
                if (dptr + (trh->nsamp * dsize) > end_ptr) break;

                double t_start_paq = trh->starttime;
                for (int s = 0; s < trh->nsamp; s++) {
                    double x = (dsize == 4) ? (double)*((int32_t*)(dptr + s*4)) : (double)*((int16_t*)(dptr + s*2));
                    double t_samp = t_start_paq + ((double)s / rate);
                    long k = (long)((t_samp - req_start) * rate + 0.5); 
                    if (k >= 0 && k < StaArray[i].lRawCircSize) {
                        StaArray[i].plRawCircBuff[k] = (int32_t)x; 
                        if (k >= StaArray[i].lRawCircCtr) StaArray[i].lRawCircCtr = k + 1;
                    }
                }
                ptr += sizeof(TRACE2_HEADER) + trh->nsamp * dsize;
            }
            StaArray[i].dSampRate = rate; 
        } else if (ws_res == WS_ERR_BROKEN_CONNECTION || ws_res == WS_ERR_TIMEOUT) {
            wsKillMenu(&ws_menu); ws_menu.head = NULL; break; 
        }
    }

    /* Post-proceso por estacion (portado de EW7 ReloadWaveforms):
       interpola los gaps cortos, recorta la cola de datos futuros y
       aplica el filtro seleccionado (Raw/HP/LP/BP) + demean. */
    for (int i = 0; i < NumEstaciones; i++) {
        if (bHasData[i]) {
            interpolate_short_gaps(&StaArray[i]);
            find_data_end_station(&StaArray[i]);
            if (StaArray[i].lRawCircCtr <= 0) bHasData[i] = 0;
        }
    }
    ApplySelectedFilter();

    free(trace_buffer);
}
