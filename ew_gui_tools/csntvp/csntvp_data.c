#include "csntvp.h"

static void compute_station_envelope(int i, int width);
static void update_wave_envelopes(void);

static int ParseHexColor(const char *s, double rgb[3]) {
    unsigned int r, g, b;
    if (!s || s[0] == '\0') return -1;
    while (*s == '#') s++;
    if (sscanf(s, "%2x%2x%2x", &r, &g, &b) != 3) return -1;
    rgb[0] = (double)r / 255.0;
    rgb[1] = (double)g / 255.0;
    rgb[2] = (double)b / 255.0;
    return 0;
}

static void ColorToHex(const double rgb[3], char out[8]) {
    snprintf(out, 8, "%02X%02X%02X",
             (int)(rgb[0] * 255.0 + 0.5), (int)(rgb[1] * 255.0 + 0.5), (int)(rgb[2] * 255.0 + 0.5));
}

void LogColorConfig(void) {
    char c_wave[8], c_bg[8], c_font[8], c_sep[8];
    ColorToHex(g_color_wave, c_wave);
    ColorToHex(g_color_bg, c_bg);
    ColorToHex(g_color_font, c_font);
    ColorToHex(g_color_sep, c_sep);
    logit("et", "csntvp: Color config (copy to .d):\n");
    logit("et", "  waveformsColor   %s\n", c_wave);
    logit("et", "  backgroundColor  %s\n", c_bg);
    logit("et", "  fontColor        %s\n", c_font);
    logit("et", "  separatorColor   %s\n", c_sep);
}

int ReadConfig(char *configfile) {
    int ncommand = 6, nmiss = 0, i;
    char init[10] = {0};
    char *com, *str;

    if (!k_open(configfile)) {
        fprintf(stderr, "csntvp: Error abriendo archivo config <%s>\n", configfile);
        return -1;
    }

    while (k_rd()) {
        com = k_str();
        if (!com || com[0] == '#') continue;

        if (k_its("MyModuleId")) {
            str = k_str();
            if (str) strcpy(MyModName, str);
            init[0] = 1;
        } else if (k_its("InRing")) {
            str = k_str();
            if (str) strcpy(InRingName, str);
            init[1] = 1;
        } else if (k_its("OutRing")) {
            str = k_str();
            if (str) strcpy(OutRingName, str);
            init[2] = 1;
        } else if (k_its("HeartBeatInt")) {
            HeartBeatInt = k_int();
            init[3] = 1;
        } else if (k_its("LogFile")) {
            LogFile = k_int();
            init[4] = 1;
        } else if (k_its("StaFile")) {
            str = k_str();
            if (str) strcpy(StaFile, str);
            init[5] = 1;
        } else if (k_its("waveformsColor")) {
            str = k_str();
            if (str && ParseHexColor(str, g_color_wave)) logit("et", "csntvp: waveformsColor <%s> invalid, using default\n", str);
        } else if (k_its("backgroundColor")) {
            str = k_str();
            if (str && ParseHexColor(str, g_color_bg)) logit("et", "csntvp: backgroundColor <%s> invalid, using default\n", str);
        } else if (k_its("fontColor")) {
            str = k_str();
            if (str && ParseHexColor(str, g_color_font)) logit("et", "csntvp: fontColor <%s> invalid, using default\n", str);
        } else if (k_its("separatorColor")) {
            str = k_str();
            if (str && ParseHexColor(str, g_color_sep)) logit("et", "csntvp: separatorColor <%s> invalid, using default\n", str);
        } else if (k_its("StationsPerScreen")) {
            str = k_str();
            if (str) { int v = atoi(str); if (v >= 1 && v <= MAX_STATIONS) iVisStas = v; else logit("et", "csntvp: StationsPerScreen <%s> invalid, using default %d\n", str, iVisStas); }
        } else if (k_its("TimeWindow")) {
            str = k_str();
            if (str) { int v = atoi(str); if (v >= 1 && v <= MAX_MINUTES) iTimeWindowMinutes = v; else logit("et", "csntvp: TimeWindow <%s> invalid, using default %d\n", str, iTimeWindowMinutes); }
        } else if (k_its("PickReplaceWindow")) {
            str = k_str();
            if (str) { double v = atof(str); if (v >= 0.0) g_pick_replace_secs = v; else logit("et", "csntvp: PickReplaceWindow <%s> invalid, using default %.1f\n", str, g_pick_replace_secs); }
        } else {
            continue;
        }
        if (k_err()) {
            fprintf(stderr, "csntvp: Error parseando <%s> en <%s>\n", com, configfile);
            return -1;
        }
    }
    for (i = 0; i < ncommand; i++) if (!init[i]) nmiss++;
    k_close();
    if (nmiss > 0) {
        fprintf(stderr, "csntvp: ERROR, faltan parametros en <%s>\n", configfile);
        return -1;
    }
    return 0;
}

void Status(unsigned char type, short ierr, char *note) {
    MSG_LOGO logo;
    char msg[256];
    long size;
    time_t t;

    logo.instid = MyInstId;
    logo.mod    = MyModId;
    logo.type   = type;

    time(&t);
    if (type == TypeHeartBeat) {
        sprintf(msg, "%ld %d\n", (long) t, (int) MyPid);
    } else if (type == TypeError) {
        sprintf(msg, "%ld %hd %s\n", (long) t, ierr, note);
        logit("et", "csntvp: Error: %s\n", note);
    }

    size = strlen(msg);
    tport_putmsg(&WaveRegion, &logo, size, msg);
}

void ConnectToEarthworm() {
    long WaveRingKey = GetKey(InRingName);
    long PickRingKey = GetKey(OutRingName);

    if (WaveRingKey == -1) { logit("e", "Error: Anillo entrada %s invalido.\n", InRingName); exit(-1); }
    if (PickRingKey == -1) { logit("e", "Error: Anillo salida %s invalido.\n", OutRingName); exit(-1); }

    if (GetLocalInst(&MyInstId) != 0) { logit("e", "Error en GetLocalInst.\n"); exit(-1); }
    if (GetModId(MyModName, &MyModId) != 0) { logit("e", "Error en GetModId (%s).\n", MyModName); exit(-1); }

    if (GetType("TYPE_TRACEBUF2", &TypeTrace) != 0) { logit("e", "Falta TYPE_TRACEBUF2.\n"); exit(-1); }
    if (GetType("TYPE_PICK_SCNL", &TypePickSCNL) != 0) { logit("e", "Falta TYPE_PICK_SCNL.\n"); exit(-1); }
    if (GetType("TYPE_HEARTBEAT", &TypeHeartBeat) != 0) { logit("e", "Falta TYPE_HEARTBEAT.\n"); exit(-1); }
    if (GetType("TYPE_ERROR", &TypeError) != 0) { logit("e", "Falta TYPE_ERROR.\n"); exit(-1); }

    unsigned char InstWild, ModWild;
    GetInst("INST_WILDCARD", &InstWild);
    GetModId("MOD_WILDCARD", &ModWild);

    WaveLogo[0].instid = InstWild;
    WaveLogo[0].mod = ModWild;
    WaveLogo[0].type = TypeTrace;
    tport_attach( &WaveRegion, WaveRingKey );
    logit("t", "=== CSNtvp: CONECTADO A INRING: %s ===\n", InRingName);

    PickLogo[0].instid = InstWild;
    PickLogo[0].mod = ModWild;
    PickLogo[0].type = TypePickSCNL;
    tport_attach( &PickRegion, PickRingKey );
    logit("t", "=== CSNtvp: CONECTADO A OUTRING: %s ===\n", OutRingName);
}

gboolean ew_background_tasks(gpointer user_data) {
    static EwGuiHeartbeat hb = {0};
    time_t timeNow;
    time(&timeNow);

    if (ewgui_heartbeat_due(&hb, (double)timeNow, HeartBeatInt)) {
        Status(TypeHeartBeat, 0, "");
    }

    if (ewgui_ring_should_quit(&WaveRegion, MyPid)) {
        logit("t", "csntvp: Señal de terminacion recibida. Cerrando...\n");
        if (g_loop) g_main_loop_quit(g_loop);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void FreeAllStations() {
    if (StaArray) {
        for (int i = 0; i < iNumStas; i++) {
            if (StaArray[i].plRawCircBuff) { free(StaArray[i].plRawCircBuff); StaArray[i].plRawCircBuff = NULL; }
            ewgui_trace_cache_free(&StaArray[i].cache);
        }
        free(StaArray);
        StaArray = NULL;
    }
    iNumStas = 0;
}

void LoadStationsFromFile() {
    FILE *fp = fopen(StaFile, "r");
    if (!fp) { logit("e", "csntvp: Error abriendo %s\n", StaFile); exit(-1); }
    StaArray = (DEV_STATION *) calloc(MAX_STATIONS, sizeof(DEV_STATION));
    iNumStas = 0; char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char sta[20] = "", chan[20] = "", net[20] = "", loc[20] = "--"; double scale = 0.0;
        int parsed = sscanf(line, "%19s %19s %19s %19s %lf", sta, chan, net, loc, &scale);
        if (parsed == 4) { scale = atof(loc); strcpy(loc, "--"); }
        else if (parsed < 4) continue;
        if (iNumStas < MAX_STATIONS) {
            DEV_STATION *s = &StaArray[iNumStas];
            snprintf(s->szStation, sizeof(s->szStation), "%s", sta);
            snprintf(s->szChannel, sizeof(s->szChannel), "%s", chan);
            snprintf(s->szNetID, sizeof(s->szNetID), "%s", net);
            snprintf(s->szLocation, sizeof(s->szLocation), "%s", loc);
            s->dScreenScale = scale;
            /* Raw buffer is allocated lazily on the first data packet, sized by
               the actual sample rate (memory refinement over a fixed max). */
            s->dSampRate = 0.0;
            s->plRawCircBuff = NULL;
            s->lRawCircSize = 0;
            s->iNumPicks = 0;
            s->lPickRingNext = 0;
            s->lLastAbsIdx = 0;
            s->dLastPacketSysTime = 0.0;
            ewgui_trace_cache_init(&s->cache);
            iNumStas++;
        }
    }
    fclose(fp); logit("t", "csntvp: %d estaciones cargadas.\n", iNumStas);
}

static int ReadSampleAt(const char *szType, const char *pData, int idx, int32_t *out) {
    if (strcmp(szType, "i2") == 0 || strcmp(szType, "s2") == 0) {
        int16_t v; memcpy(&v, pData + idx * 2, 2); *out = v; return 0;
    }
    if (strcmp(szType, "i4") == 0 || strcmp(szType, "s4") == 0) {
        int32_t v; memcpy(&v, pData + idx * 4, 4); *out = v; return 0;
    }
    if (strcmp(szType, "f4") == 0 || strcmp(szType, "t4") == 0) {
        float v; memcpy(&v, pData + idx * 4, 4);
        *out = (int32_t)(v + (v >= 0.0f ? 0.5f : -0.5f)); return 0;
    }
    *out = 0;
    return -1;
}

gboolean fetch_realtime_data(gpointer user_data) {
    char msg[MAX_TRACE_BYTES]; MSG_LOGO reclogo; long recsize; int res;
    TRACE2_HEADER *WaveHead;

    int64_t current_realtime = g_get_real_time();
    if (g_last_frame_realtime == 0) g_last_frame_realtime = current_realtime;
    double dt = (current_realtime - g_last_frame_realtime) / 1000000.0;
    g_last_frame_realtime = current_realtime;
    double sys_time = (double)current_realtime / 1000000.0;

    do {
        res = tport_getmsg( &WaveRegion, WaveLogo, 1, &reclogo, &recsize, msg, sizeof(msg) );
        if ( res == GET_OK || res == GET_MISS || res == GET_NOTRACK ) {
            WaveHead = (TRACE2_HEADER *) msg;
            double t_start = WaveHead->starttime;
            double t_end = WaveHead->endtime;
            double rate = WaveHead->samprate;
            if (rate <= 0) rate = 20.0;

            if (fabs(sys_time - t_end) > MAX_REALTIME_SKEW) {
                if (t_end > g_latest_time) g_latest_time = t_end;
            }

            for ( int i = 0; i < iNumStas; i++ ) {
                if ( !strcmp(StaArray[i].szStation, WaveHead->sta) && !strcmp(StaArray[i].szChannel, WaveHead->chan) ) {
                    StaArray[i].dLastPacketSysTime = sys_time;
                    g_last_data_realtime = sys_time;

                    /* Lazily (re)size the raw circular buffer by the actual
                       sample rate (memory refinement). */
                    int rate_int = (int)(rate + 0.5);
                    if (rate_int < 1) rate_int = 1;
                    if (rate_int > MAX_SAMP_RATE) rate_int = MAX_SAMP_RATE;
                    long want_size = (long)MAX_MINUTES * 60 * rate_int;
                    if (StaArray[i].lRawCircSize == 0 || want_size != StaArray[i].lRawCircSize) {
                        int32_t *nb = (int32_t *) realloc(StaArray[i].plRawCircBuff, want_size * sizeof(int32_t));
                        if (!nb) { logit("e", "csntvp: sin memoria para %s (%ld muestras)\n", StaArray[i].szStation, want_size); break; }
                        StaArray[i].plRawCircBuff = nb;
                        StaArray[i].lRawCircSize = want_size;
                        StaArray[i].dSampRate = rate;
                        for (long k = 0; k < want_size; k++) StaArray[i].plRawCircBuff[k] = INT_MAX;
                        StaArray[i].lLastAbsIdx = 0;
                        StaArray[i].cache.proc_valid = 0;
                        StaArray[i].cache.env_valid = 0;
                    }

                    int64_t abs_start = (int64_t)(t_start * rate);
                    int64_t abs_end = (int64_t)(t_end * rate);

                    /* Standard gap fill with the empty flag (INT_MAX) */
                    if (StaArray[i].lLastAbsIdx > 0 && abs_start > StaArray[i].lLastAbsIdx) {
                        int64_t gap_samps = abs_start - StaArray[i].lLastAbsIdx;
                        if (gap_samps > StaArray[i].lRawCircSize) gap_samps = StaArray[i].lRawCircSize;
                        for (int64_t g = 0; g < gap_samps; g++) {
                            int64_t clr_abs = StaArray[i].lLastAbsIdx + g;
                            int idx = CIRC_IDX(clr_abs, StaArray[i].lRawCircSize);
                            StaArray[i].plRawCircBuff[idx] = INT_MAX;
                        }
                        if (StaArray[i].cache.proc_valid && (abs_start - 1) >= StaArray[i].cache.proc_abs_start)
                            StaArray[i].cache.proc_valid = 0;
                    }

                    char szType[3]; strncpy(szType, WaveHead->datatype, 2); szType[2] = '\0';
                    char *pRaw = msg + sizeof(TRACE2_HEADER);

                    for (int s = 0; s < WaveHead->nsamp; s++) {
                        int32_t x;
                        if (ReadSampleAt(szType, pRaw, s, &x) != 0) {
                            if (!g_warned_datatype) {
                                g_warned_datatype = 1;
                                logit("et", "csntvp: datatype <%s> not supported, reading as int32\n", szType);
                            }
                            memcpy(&x, pRaw + s * sizeof(int32_t), sizeof(int32_t));
                        }

                        double t_samp = t_start + ((double)s / rate);
                        int64_t abs_idx = (int64_t)(t_samp * rate);
                        int idx = CIRC_IDX(abs_idx, StaArray[i].lRawCircSize);
                        StaArray[i].plRawCircBuff[idx] = x;
                    }

                    if (abs_end > StaArray[i].lLastAbsIdx) StaArray[i].lLastAbsIdx = abs_end;
                    if (StaArray[i].cache.proc_valid && abs_start <= StaArray[i].cache.proc_abs_end)
                        StaArray[i].cache.proc_valid = 0;
                    break;
                }
            }
        }
    } while ( res != GET_NONE );

    if (!g_is_hold) {
        if (g_last_data_realtime > 0.0 && (sys_time - g_last_data_realtime) > DATA_STALE_SECS) {
            g_data_stale = TRUE;
        } else {
            g_data_stale = FALSE;
            gboolean is_realtime = TRUE;
            if (g_latest_time > 0.0 && fabs(sys_time - g_latest_time) > MAX_REALTIME_SKEW) is_realtime = FALSE;

            if (is_realtime) {
                g_latest_time = sys_time - 2.0;
                g_smooth_time = g_latest_time;
            } else {
                if (g_smooth_time == 0.0) g_smooth_time = g_latest_time;
                else {
                    g_smooth_time += dt;
                    if (fabs(g_latest_time - g_smooth_time) > 2.0) g_smooth_time = g_latest_time;
                    else if (g_latest_time > g_smooth_time) g_smooth_time += dt * 0.1;
                    else if (g_latest_time < g_smooth_time) g_smooth_time -= dt * 0.1;
                }
            }
        }
    }

    gboolean picks_changed = FALSE;

    do {
        res = tport_getmsg( &PickRegion, PickLogo, 1, &reclogo, &recsize, msg, sizeof(msg) - 1 );
        if ( res == GET_OK || res == GET_MISS || res == GET_NOTRACK ) {
            msg[recsize] = '\0'; if (reclogo.type == TypePickSCNL) {
                int t, m, inst, seq; char scnl[64], ph[10], ts[30];
                if (sscanf(msg, "%d %d %d %d %63s %9s %29s", &t, &m, &inst, &seq, scnl, ph, ts) >= 7) {
                    char sta[10]="", ch[10]="", net[10]="", loc[10]=""; sscanf(scnl, "%9[^.].%9[^.].%9[^.].%9s", sta, ch, net, loc);
                    for (int k=0; ts[k]; k++) if (ts[k]==',') ts[k]='.';
                    int py, pm, pd, phh, pmn; double psec;
                    if (sscanf(ts, "%4d%2d%2d%2d%2d%lf", &py, &pm, &pd, &phh, &pmn, &psec) == 6) {
                        struct tm pt = {0}; pt.tm_year=py-1900; pt.tm_mon=pm-1; pt.tm_mday=pd; pt.tm_hour=phh; pt.tm_min=pmn; pt.tm_sec=(int)psec;
                        char *otz=getenv("TZ"); setenv("TZ", "GMT", 1); tzset(); double pT=mktime(&pt)+(psec-(int)psec);
                        if(otz) setenv("TZ", otz, 1); else unsetenv("TZ"); tzset();
                        char dph[8]; if (ph[0]=='U'||ph[0]=='D'||ph[0]=='?') snprintf(dph, sizeof(dph), "P(%.4s)", ph); else snprintf(dph, sizeof(dph), "%.7s", ph);
                        for (int i=0; i<iNumStas; i++) if (!strcmp(StaArray[i].szStation, sta) && !strcmp(StaArray[i].szChannel, ch)) {
                            DEV_STATION *dev = &StaArray[i];
                            /* Upsert our own picks by seq (avoid double count of
                               the manual pick when it comes back through the ring). */
                            int found = -1;
                            if (m == MyModId && inst == MyInstId) {
                                for (int k = 0; k < MAX_PICKS_PER_STA; k++) {
                                    if (dev->picks[k].lPickIndex == seq && dev->picks[k].iUseMe > 0) { found = k; break; }
                                }
                            }
                            if (found >= 0) {
                                dev->picks[found].dTime = pT;
                                snprintf(dev->picks[found].szPhase, sizeof(dev->picks[found].szPhase), "%s", dph);
                                dev->picks[found].iUseMe = 1;
                            } else {
                                int slot = (int)(dev->lPickRingNext % MAX_PICKS_PER_STA);
                                dev->picks[slot].dTime = pT;
                                snprintf(dev->picks[slot].szPhase, sizeof(dev->picks[slot].szPhase), "%s", dph);
                                dev->picks[slot].lPickIndex = seq;
                                dev->picks[slot].iUseMe = 1;
                                dev->lPickRingNext++;
                                if (dev->iNumPicks < MAX_PICKS_PER_STA) dev->iNumPicks++;
                            }
                            picks_changed = TRUE;
                            break;
                        }
                    }
                }
            }
        }
    } while ( res != GET_NONE );

    /* A1/A2/A3: refresh the envelope cache at its rhythm (2Hz) and redraw
       only if something changed (new envelope, picks, or forced change). */
    g_envelope_updated = FALSE;
    update_wave_envelopes();

    if (g_envelope_updated || picks_changed) {
        if (g_drawing_waves) ewgui_canvas_queue_draw(g_drawing_waves);
        if (drawing_axis) ewgui_canvas_queue_draw(drawing_axis);
    }

    return G_SOURCE_CONTINUE;
}

static void compute_station_envelope(int i, int width) {
    DEV_STATION *sta = &StaArray[i];
    double rate = sta->dSampRate > 0 ? sta->dSampRate : 20.0;
    double window_secs = iTimeWindowMinutes * 60.0;
    double t_end = g_is_hold ? g_t_hold_time : g_smooth_time;
    double pad_secs = 30.0;

    EwFilterParams fp;
    fp.filter_type = g_filter_type;
    fp.f1 = g_f1;
    fp.f2 = g_f2;
    fp.order = g_order;

    ewgui_trace_envelope(sta->plRawCircBuff, sta->lRawCircSize, sta->lLastAbsIdx,
                         rate, t_end, window_secs, pad_secs, width, &fp,
                         &sta->cache);
}

static void update_wave_envelopes(void) {
    if (iNumStas == 0 || !g_drawing_waves) return;

    int width = 0;
    ewgui_canvas_get_size(g_drawing_waves, &width, NULL);
    width -= PANEL_WIDTH;
    if (width <= 0) return;

    double t_right = g_is_hold ? g_t_hold_time : g_smooth_time;
    int64_t now_ms = g_get_monotonic_time() / 1000;
    gboolean force = g_bForceEnv;
    g_bForceEnv = FALSE;

    gboolean should = FALSE;
    if (force) {
        should = TRUE;
    } else if (g_is_hold) {
        if (g_last_env_width != width || !g_last_env_ok) should = TRUE;
    } else {
        if (!(fabs(t_right - g_last_env_t_right) < 0.5 && g_data_stale)) {
            double window_secs = iTimeWindowMinutes * 60.0;
            double interval_ms = (window_secs / width) * 1000.0;
            if (interval_ms < ENV_PROCESS_MS) interval_ms = ENV_PROCESS_MS;
            if (interval_ms > 1500.0) interval_ms = 1500.0;

            if ((now_ms - g_last_env_process_ms) >= (int64_t)interval_ms || g_last_env_width != width)
                should = TRUE;
        }
    }
    if (!should) return;

    g_envelope_updated = TRUE;
    g_last_env_process_ms = now_ms;
    g_last_env_t_right = t_right;
    g_last_env_width = width;
    g_last_env_ok = TRUE;

    for (int i = 0; i < iNumStas; i++) compute_station_envelope(i, width);
}
