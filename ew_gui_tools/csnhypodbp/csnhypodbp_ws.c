#include "csnhypodbp.h"
#include "csnhypodbp_magchan.h"
#include "csnhypodbp_sds.h"
#include "wstore.h"

#include <math.h>

#include "ewgui/db.h"

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

/* ===================================================================== */
/* Agrupamiento de soluciones hipocentrales                              */
/* ===================================================================== */
typedef struct {
    CsnhypodbpRow *row;      /* prestada (dueña: g_store_hypo) */
    int      qid;            /* qid de ESTA solucion (difiere del grupo si se fusiona) */
    int      manual;
    unsigned version;
    int      nph;
    double   rms;
    double   otime;
    int      mod;
    /* Picadas de ESTA solucion, con su residual. El grupo guarda ademas la
       union, pero la tabla de Ondas muestra los residuales de la solucion
       elegida en el detalle, no los de la union. */
    int      npicks;
    CsnhypodbpPick *picks;
} EventSol;

struct EventGroup {
    int    qid;
    int    npicks;
    CsnhypodbpPick picks[CSNHYPODBP_MAX_PICKS];   /* union de los miembros */
    GPtrArray *sols;                              /* EventSol* (owned) */
    CsnhypodbpRow *preferred;                     /* prestada */
};

static GPtrArray *g_groups = NULL;
static CsnhypodbpMagStore g_pending_mags;  /* magnitudes llegadas antes del grupo */
/* Magnitudes POR ESTACION de los ultimos mensajes (sub-pestaña `magnitudes`). */
static CsnhypodbpMagStaStore g_magsta_live;

/* Agrega una picada a la solucion (copia exacta, con su residual). */
static void event_sol_add_pick(EventSol *es, const CsnhypodbpPick *pk)
{
    if (!es || !pk || es->npicks >= CSNHYPODBP_MAX_PICKS) return;
    es->picks = g_renew(CsnhypodbpPick, es->picks, es->npicks + 1);
    es->picks[es->npicks++] = *pk;
}

/* Reemplaza las picadas de la solucion (una misma solucion puede re-emitirse). */
static void event_sol_set_picks(EventSol *es, const CsnhypodbpPick *picks, int n)
{
    int i;
    if (!es) return;
    g_free(es->picks);
    es->picks = NULL;
    es->npicks = 0;
    for (i = 0; i < n && i < CSNHYPODBP_MAX_PICKS; i++)
        event_sol_add_pick(es, &picks[i]);
}

static void event_sol_free(gpointer p)
{
    EventSol *es = p;
    if (!es) return;
    g_free(es->picks);
    g_free(es);
}

static void event_group_free(gpointer p)
{
    EventGroup *g = p;
    if (!g) return;
    if (g->sols) {
        guint i;
        for (i = 0; i < g->sols->len; i++) event_sol_free(g_ptr_array_index(g->sols, i));
        g_ptr_array_free(g->sols, TRUE);
        g->sols = NULL;
    }
    g_free(g);
}

static void ensure_groups(void)
{
    if (!g_groups) g_groups = g_ptr_array_new_with_free_func(event_group_free);
}

static EventGroup *event_group_new(int qid)
{
    EventGroup *g = g_new0(EventGroup, 1);
    g->qid = qid;
    g->sols = g_ptr_array_new();
    return g;
}

/* Columna de la fila (c9..c12) para un slot de magnitud (0=ML..3=Ms). */
static const char *mag_col(int slot)
{
    static const char *cols[CSNHYPODBP_MAG_SLOTS] = { "c9", "c10", "c11", "c12" };
    return (slot >= 0 && slot < CSNHYPODBP_MAG_SLOTS) ? cols[slot] : NULL;
}

/* Escribe una magnitud: el texto de la columna ("mag-nst") y su valor numerico
   (`nmag<slot>`), que es por donde ordena la cabecera (el texto no es numero). */
static void row_set_mag(CsnhypodbpRow *row, int slot, const char *mag)
{
    char prop[16];
    if (!row || !mag_col(slot)) return;
    g_object_set(row, mag_col(slot), mag, NULL);
    snprintf(prop, sizeof(prop), "nmag%d", slot);
    g_object_set(row, prop, (mag && mag[0] && mag[0] != '-') ? atof(mag) : -1.0, NULL);
}

/* Aplica una magnitud a la(s) fila(s) de la solucion (qid,mod). Una magnitud
   con mod==0 (csnmags viejo, sin modulo) coincide con todas las del qid. */
static int apply_mag_to_solutions(int qid, int mod, int slot, const char *mag)
{
    const char *col = mag_col(slot);
    guint gi, si;
    int applied = 0;
    if (!col || !g_groups) return 0;
    for (gi = 0; gi < g_groups->len; gi++) {
        EventGroup *g = g_ptr_array_index(g_groups, gi);
        for (si = 0; si < g->sols->len; si++) {
            EventSol *es = g_ptr_array_index(g->sols, si);
            if (es->row && csnhypodbp_mag_match(qid, mod, es->qid, es->mod)) {
                row_set_mag(es->row, slot, mag);
                applied++;
            }
        }
    }
    return applied;
}

/* Aplica a la fila las magnitudes pendientes de su (qid,mod): llegaron antes
   de que la solucion existiera. */
static void solution_take_pending(CsnhypodbpRow *row, int qid, int mod)
{
    char pending[CSNHYPODBP_MAG_SLOTS][32];
    int i;
    if (!row) return;
    if (csnhypodbp_mag_store_take(&g_pending_mags, qid, mod, pending)) {
        for (i = 0; i < CSNHYPODBP_MAG_SLOTS; i++)
            if (pending[i][0]) row_set_mag(row, i, pending[i]);
    }
}

static void event_group_add_picks(EventGroup *g, const CsnhypodbpPick *p, int n,
                                  double tol)
{
    int i, j;
    for (i = 0; i < n && g->npicks < CSNHYPODBP_MAX_PICKS; i++) {
        int dup = 0;
        for (j = 0; j < g->npicks; j++)
            if (csnhypodbp_pick_match(&p[i], &g->picks[j], tol)) { dup = 1; break; }
        if (!dup) g->picks[g->npicks++] = p[i];
    }
}

static int event_group_compatible(const EventGroup *g, int qid,
                                  const CsnhypodbpPick *p, int n,
                                  double tol, int min_match)
{
    int mn;
    if (g->qid == qid) return 1;
    if (!p || n <= 0 || g->npicks <= 0) return 0;
    mn = (n < g->npicks) ? n : g->npicks;
    if (mn < min_match) return 0;
    return csnhypodbp_picks_compatible(p, n, g->picks, g->npicks, tol, min_match);
}

static EventSol *event_group_find_sol(const EventGroup *g, const CsnhypodbpRow *row)
{
    guint i;
    for (i = 0; i < g->sols->len; i++) {
        EventSol *es = g_ptr_array_index(g->sols, i);
        if (es->row == row) return es;
    }
    return NULL;
}

static void event_group_update_preferred(EventGroup *g)
{
    CsnhypodbpSolMeta meta[CSNHYPODBP_MAX_PICKS];
    guint i;
    int best;
    if (g->sols->len == 0) { g->preferred = NULL; return; }
    for (i = 0; i < g->sols->len && i < CSNHYPODBP_MAX_PICKS; i++) {
        EventSol *es = g_ptr_array_index(g->sols, i);
        meta[i].manual  = es->manual;
        meta[i].version = es->version;
        meta[i].nph     = es->nph;
        meta[i].rms     = es->rms;
        meta[i].otime   = es->otime;
    }
    best = csnhypodbp_preferred_index(meta, (int)g->sols->len);
    g->preferred = (best >= 0)
        ? ((EventSol *)g_ptr_array_index(g->sols, (guint)best))->row : NULL;
}

EventGroup *csnhypodbp_event_group_of(const CsnhypodbpRow *row)
{
    if (!row) return NULL;
    return (EventGroup *)g_object_get_data(G_OBJECT(row), "event-group");
}

size_t csnhypodbp_event_group_nsol(const EventGroup *g)
{
    return (g && g->sols) ? g->sols->len : 0;
}

CsnhypodbpRow *csnhypodbp_event_group_solution(const EventGroup *g, size_t i)
{
    if (!g || !g->sols || i >= g->sols->len) return NULL;
    return ((EventSol *)g_ptr_array_index(g->sols, i))->row;
}

CsnhypodbpRow *csnhypodbp_event_group_preferred(const EventGroup *g)
{
    return g ? g->preferred : NULL;
}

/* Fila seleccionada en la lista maestra (prestada: transfer-none). */
static gpointer selected_master_row(void)
{
    if (!g_selection_events) return NULL;
    return gtk_single_selection_get_selected_item(g_selection_events);
}

/* Grupo del EVENTO que el usuario tiene abierto, o NULL. Se resuelve por la
   fila (no por el qid): tras un merge, las filas del grupo absorbido ya apuntan
   al superviviente. */
static gpointer viewed_group(void)
{
    gpointer it = selected_master_row();

    if (!it) return NULL;
    return csnhypodbp_event_group_of(CSNHYPODBP_ROW(it));
}

/* Devuelve la seleccion de la maestra al grupo `keep` (por identidad, no por
   indice: el upsert reemplaza la fila y un merge la borra). `g_ring_sync` evita
   que la re-seleccion reinicie la vista del usuario. */
static void sticky_reselect(gpointer keep)
{
    int idx;

    if (!keep || !g_store_events || !g_selection_events) return;
    idx = csnhypodbp_master_find(g_store_events, keep);
    if (idx < 0) return;
    g_ring_sync = 1;
    gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_selection_events),
                                    (guint)idx, TRUE);
    g_ring_sync = 0;
}

/* Inserta/actualiza el item maestro del grupo. La identidad es el GRUPO (no el
   qid de la solucion): un grupo fusionado puede tener soluciones de varios qid
   y la preferida puede ser cualquiera de ellas. */
static void event_store_upsert(EventGroup *g)
{
    char nb[16];
    CsnhypodbpRow *old = NULL;
    gpointer keep_row, keep;
    int idx;

    if (!g_store_events || !g->preferred) return;
    snprintf(nb, sizeof(nb), "%u", g->sols->len);
    g_object_set(g->preferred, "nsol", nb, NULL);

    /* Durante una busqueda la maestra muestra el resultado consultado: los
       eventos vivos se siguen agrupando en memoria, pero no entran a la tabla. */
    if (g_search_active) return;

    /* La seleccion del usuario es PEGAJOSA: se captura ANTES de mutar la maestra
       (el upsert reemplaza la fila y un merge borra la del absorbido) para
       devolversela despues. Nunca se reselecciona al grupo que acaba de llegar:
       eso cambiaba el detalle, la tablita y las trazas que el usuario miraba. */
    keep_row = selected_master_row();
    if (keep_row) g_object_ref(keep_row);

    idx = csnhypodbp_master_find(g_store_events, g);
    if (idx >= 0) {
        gpointer it = g_list_model_get_item(G_LIST_MODEL(g_store_events), (guint)idx);
        old = it ? CSNHYPODBP_ROW(it) : NULL;   /* prestado (ref nuestro) */
    }
    csnhypodbp_master_upsert(g_store_events, g, g->preferred);
    if (old) g_object_unref(old);

    if (keep_row) {
        /* Ya DESPUES de mutar: si el evento del usuario fue absorbido en un
           merge, sus filas apuntan al grupo superviviente. */
        keep = csnhypodbp_event_group_of(CSNHYPODBP_ROW(keep_row));
        if (!keep) keep = csnhypodbp_master_group(CSNHYPODBP_ROW(keep_row));
        sticky_reselect(keep);
        g_object_unref(keep_row);
    }
}

void rebuild_event_store(void)
{
    guint i;
    if (!g_store_events) return;
    ensure_groups();
    g_list_store_remove_all(g_store_events);
    for (i = 0; i < g_groups->len; i++) {
        EventGroup *g = g_ptr_array_index(g_groups, i);
        if (!g->preferred) continue;
        {
            char nb[16];
            snprintf(nb, sizeof(nb), "%u", g->sols->len);
            g_object_set(g->preferred, "nsol", nb, NULL);
        }
        csnhypodbp_master_upsert(g_store_events, g, g->preferred);
    }
}

void reselect_event_by_qid(int qid)
{
    guint i, n;
    if (!g_selection_events) return;
    n = g_list_model_get_n_items(G_LIST_MODEL(g_selection_events));
    for (i = 0; i < n; i++) {
        CsnhypodbpRow *r = CSNHYPODBP_ROW(
            g_list_model_get_item(G_LIST_MODEL(g_selection_events), i));
        int rq = 0;
        if (r) g_object_get(r, "qid", &rq, NULL);
        if (r && rq == qid) {
            gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_selection_events), i, TRUE);
            g_object_unref(r);
            return;
        }
        if (r) g_object_unref(r);
    }
    if (n > 0)
        gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_selection_events), 0, TRUE);
}

/* Re-selecciona la fila maestra POR IDENTIDAD de fila (la preferida puede tener
   un qid distinto al del grupo, asi que no sirve buscar por qid). */
void reselect_event_by_row(CsnhypodbpRow *row)
{
    guint i, n;
    if (!g_selection_events || !row) return;
    n = g_list_model_get_n_items(G_LIST_MODEL(g_selection_events));
    for (i = 0; i < n; i++) {
        gpointer it = g_list_model_get_item(G_LIST_MODEL(g_selection_events), i);
        int hit = (it == row);
        if (it) g_object_unref(it);
        if (hit) {
            gtk_selection_model_select_item(GTK_SELECTION_MODEL(g_selection_events), i, TRUE);
            return;
        }
    }
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
    EventGroup *cur_g = NULL;
    CsnhypodbpRow *cur_row = NULL;
    ensure_groups();
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
            /* Fecha y hora juntas (ISO UTC): ordenan solas. `hora` (c1) se
               mantiene por compatibilidad del archivo de estado. */
            csnhypodbp_search_fmt_utc((time_t)t0, fecha, sizeof(fecha));
            snprintf(hora, sizeof(hora), "%s",
                     strlen(fecha) >= 19 ? fecha + 11 : "--:--:--");
            snprintf(szLat, sizeof(szLat), "%.2f%c", fabs(lat), lat < 0 ? 'S' : 'N');
            snprintf(szLon, sizeof(szLon), "%.2f%c", fabs(lon), lon < 0 ? 'W' : 'E');
            snprintf(szDep, sizeof(szDep), "%.0f", depth);
            snprintf(szRes, sizeof(szRes), "%.1f", rms);
            snprintf(szAzm, sizeof(szAzm), "%.0f", gap);
            snprintf(szStn, sizeof(szStn), "%d", nph);
            snprintf(szID, sizeof(szID), "%010lu", id);
            char szVer[32]; snprintf(szVer, sizeof(szVer), "%u", version);

            CsnhypodbpRow *row = csnhypodbp_row_new(fecha, hora, szLat, szLon, szDep, szRes,
                szAzm, szStn, szID, "-", "-", "-", "-", "csnloc", szVer, t0, (int)version, (int)id,
                lat, lon, depth, 0);
            g_object_set(row, "estado", "A", NULL);
            {
                /* El archivo de estado no trae hora de creacion: se usa la de
                   llegada a este modulo. */
                char created[32];
                csnhypodbp_search_fmt_utc(time(NULL), created, sizeof(created));
                g_object_set(row, "created", created, NULL);
            }
            g_list_store_append(g_store_hypo, row);
            g_object_unref(row);

            cur_g = event_group_new((int)id);
            {
                EventSol *es = g_new0(EventSol, 1);
                es->row = row; es->qid = (int)id; es->manual = 0; es->version = version;
                es->nph = nph; es->rms = rms; es->otime = t0; es->mod = 0;
                g_ptr_array_add(cur_g->sols, es);
            }
            event_group_update_preferred(cur_g);
            g_object_set_data(G_OBJECT(row), "event-group", cur_g);
            g_ptr_array_add(g_groups, cur_g);
            cur_row = row;
            nrec++;
        } else if ((line[0] == 'P' || line[0] == 'X') && cur_g && cur_row) {
            char sta[8] = "", net[4] = "", chan[8] = "", loc[4] = "";
            int phase = 0, weight = 0, manual = 0; double t_epoch = 0.0, resid = 0.0;
            int nf = sscanf(line + 2, "%7s %3s %7s %3s %d %lf %d %lf %d",
                            sta, net, chan, loc, &phase, &t_epoch, &weight, &resid, &manual);
            if (nf >= 7) {
                EventSol *es = event_group_find_sol(cur_g, cur_row);
                CsnhypodbpPick pk; memset(&pk, 0, sizeof(pk));
                strncpy(pk.sta, sta, sizeof(pk.sta) - 1);
                strncpy(pk.net, net, sizeof(pk.net) - 1);
                strncpy(pk.chan, chan, sizeof(pk.chan) - 1);
                strncpy(pk.loc, loc, sizeof(pk.loc) - 1);
                pk.phase = (phase == 1) ? 'S' : 'P';
                pk.t_pick = t_epoch;
                if (nf >= 8) pk.residual = resid;
                if (nf >= 9) pk.manual = manual;
                event_group_add_picks(cur_g, &pk, 1, g_group_pick_tol_sec);
                if (es) {
                    event_sol_add_pick(es, &pk);
                    if (manual) es->manual = 1;
                }
                if (manual) g_object_set(cur_row, "estado", "M", NULL);
            }
        }
    }
    fclose(fp);
    if (nrec > 0) { rebuild_event_store(); reselect_event_by_qid(0); }
    logit("t", "csnhypodbp: recuperados %d eventos de %s\n", nrec, state_path);
}

/* ------------------------------------------------------------------------- */
/* Historial desde la base hipocentral                                        */
/* ------------------------------------------------------------------------- */

static EwGuiDb *g_db = NULL;
static int      g_db_hours = 24;

int csnhypodbp_db_init(const char *envfile, int hours)
{
    EwGuiDbConfig cfg;

    g_db_hours = (hours > 0) ? hours : 24;
    if (!envfile || !*envfile)
        return 0;
    if (ewguidb_config_load(envfile, &cfg) != 0) {
        logit("e", "csnhypodbp: no se pudo leer %s\n", envfile);
        return -1;
    }
    if (ewguidb_open(&cfg, &g_db) != 0) {
        logit("e", "csnhypodbp: BD: %s\n", ewguidb_last_error(g_db));
        ewguidb_close(g_db);
        g_db = NULL;
        return -1;
    }
    return 0;
}

/* 1 si la conexion a la BD esta disponible (historial y busqueda). */
int csnhypodbp_db_ready(void)
{
    return g_db != NULL;
}

/* Handle de la conexion, para los escritores que viven en otros archivos (el
   boton `Save to DB` del panel Mww). NULL si no hay BD. */
EwGuiDb *csnhypodbp_db(void)
{
    return g_db;
}

/* Reabre la conexion (p. ej. tras reiniciar el contenedor de la BD). */
void csnhypodbp_db_reconnect(void)
{
    if (g_db) {
        ewguidb_close(g_db);
        g_db = NULL;
    }
    if (DbEnvFile[0])
        csnhypodbp_db_init(DbEnvFile, g_db_hours);
}

static double iso_epoch(const char *s)
{
    int y, mo, d, h, mi;
    double sec;
    struct tm tm;

    if (!s || sscanf(s, "%4d-%2d-%2d %2d:%2d:%lf",
                     &y, &mo, &d, &h, &mi, &sec) != 6)
        return 0.0;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = (int)sec;
    return (double)timegm(&tm) + (sec - (int)sec);
}

/* "NET.STA.LOC.CHAN" -> STA */
static void nslco_sta(const char *ch, char *out, size_t n)
{
    char work[128], *p, *dot;
    int nf = 0;

    out[0] = '\0';
    if (!ch)
        return;
    snprintf(work, sizeof work, "%s", ch);
    p = work;
    while (p && nf < 2) {
        dot = strchr(p, '.');
        if (nf == 1) {                       /* segundo campo = STA */
            if (dot) *dot = '\0';
            snprintf(out, n, "%s", p);
            return;
        }
        if (!dot)
            return;
        *dot = '\0';
        p = dot + 1;
        nf++;
    }
}

/* Carga en grupos las soluciones de los eventos `ids` (cada id con su origen
   preferido en la BD). Devuelve el nº de soluciones incorporadas. */
static int cargar_grupos_db(const char *const *ids, int nev)
{
    int i, j, nrec = 0;

    ensure_groups();

    for (i = 0; i < nev; i++) {
        EwGuiOriginRow *ors = NULL;
        int nor = 0, qid;
        EventGroup *g;

        qid = atoi(ids[i][0] == 'E' ? ids[i] + 1 : ids[i]);
        if (ewguidb_query_origins(g_db, ids[i], &ors, &nor) <= 0) {
            ewguidb_rows_free(ors);
            continue;
        }

        g = event_group_new(qid);
        for (j = 0; j < nor; j++) {
            const char *colon = strrchr(ors[j].origin_id, ':');
            int mod = colon ? atoi(colon + 1) : 0;
            double otime = iso_epoch(ors[j].origin_time);
            char fecha[32], hora[32], created[32], szLat[32], szLon[32], szDep[32];
            char szRes[32], szAzm[32], szStn[32], szID[32], szMod[32], szVer[32];
            char szMl[32] = "-", szMwp[32] = "-", szMb[32] = "-", szMs[32] = "-";
            EwGuiMagRow *mgs = NULL;
            EwGuiPickRow *pks = NULL;
            int nm = 0, npk = 0, k;
            CsnhypodbpRow *row;
            EventSol *es;

            /* Fecha y hora juntas (ISO UTC) y hora de creacion en la BD (es el
               orden por defecto del detalle). */
            csnhypodbp_search_fmt_utc((time_t)otime, fecha, sizeof(fecha));
            snprintf(hora, sizeof(hora), "%s",
                     strlen(fecha) >= 19 ? fecha + 11 : "--:--:--");
            csnhypodbp_search_fmt_utc(iso_epoch(ors[j].creation_time), created,
                                      sizeof(created));
            snprintf(szLat, sizeof szLat, "%.2f%c",
                     fabs(ors[j].latitude), ors[j].latitude < 0 ? 'S' : 'N');
            snprintf(szLon, sizeof szLon, "%.2f%c",
                     fabs(ors[j].longitude), ors[j].longitude < 0 ? 'W' : 'E');
            snprintf(szDep, sizeof szDep, "%.0f", ors[j].depth);
            snprintf(szRes, sizeof szRes, "%.1f",
                     isnan(ors[j].standard_error) ? 0.0 : ors[j].standard_error);
            snprintf(szAzm, sizeof szAzm, "%.0f",
                     isnan(ors[j].azimuthal_gap) ? 0.0 : ors[j].azimuthal_gap);
            snprintf(szStn, sizeof szStn, "%d",
                     ors[j].station_count < 0 ? 0 : ors[j].station_count);
            snprintf(szID, sizeof szID, "%010d", qid);
            snprintf(szMod, sizeof szMod, "%s", ModLabel(mod));
            snprintf(szVer, sizeof szVer, "0");

            if (ewguidb_query_magnitudes(g_db, ors[j].origin_id, &mgs, &nm) > 0) {
                for (k = 0; k < nm; k++) {
                    char b[32];
                    snprintf(b, sizeof b, "%.1f-%d", mgs[k].magnitude_value,
                             mgs[k].station_count < 0 ? 0
                                                      : mgs[k].station_count);
                    if (strcmp(mgs[k].magnitude_type, "ML") == 0)
                        snprintf(szMl, sizeof szMl, "%s", b);
                    else if (strcmp(mgs[k].magnitude_type, "Mwp") == 0)
                        snprintf(szMwp, sizeof szMwp, "%s", b);
                    else if (strcmp(mgs[k].magnitude_type, "Mb") == 0)
                        snprintf(szMb, sizeof szMb, "%s", b);
                    else if (strcmp(mgs[k].magnitude_type, "Ms") == 0)
                        snprintf(szMs, sizeof szMs, "%s", b);
                }
            }
            ewguidb_rows_free(mgs);

            row = csnhypodbp_row_new(fecha, hora, szLat, szLon, szDep, szRes,
                                     szAzm, szStn, szID, szMl, szMwp, szMb, szMs,
                                     szMod, szVer, otime, 0, qid, ors[j].latitude,
                                     ors[j].longitude, ors[j].depth, mod);
            csnhypodbp_row_set_origin_id(row, ors[j].origin_id);
            g_object_set(row, "estado",
                         strcmp(ors[j].status, "REVIEWED") == 0 ? "M" : "A", NULL);
            g_object_set(row, "created", created, NULL);
            row_set_mag(row, 0, szMl); row_set_mag(row, 1, szMwp);
            row_set_mag(row, 2, szMb); row_set_mag(row, 3, szMs);
            g_list_store_append(g_store_hypo, row);

            es = g_new0(EventSol, 1);
            es->row = row;
            es->qid = qid;
            es->manual = (strcmp(ors[j].status, "REVIEWED") == 0);
            es->version = 0;
            es->nph = ors[j].station_count;
            es->rms = isnan(ors[j].standard_error) ? 0.0 : ors[j].standard_error;
            es->otime = otime;
            es->mod = mod;
            g_ptr_array_add(g->sols, es);

            g_object_set_data(G_OBJECT(row), "event-group", g);
            g_object_unref(row);

            if (ewguidb_query_picks(g_db, ors[j].origin_id, &pks, &npk) > 0) {
                for (k = 0; k < npk; k++) {
                    CsnhypodbpPick pk;
                    memset(&pk, 0, sizeof pk);
                    nslco_sta(pks[k].channel, pk.sta, sizeof pk.sta);
                    pk.phase = (pks[k].phase[0] == 'S') ? 'S' : 'P';
                    pk.t_pick = iso_epoch(pks[k].pick_time);
                    pk.residual = pks[k].residual;   /* columna uncertainty */
                    event_group_add_picks(g, &pk, 1, g_group_pick_tol_sec);
                    event_sol_add_pick(es, &pk);
                }
            }
            ewguidb_rows_free(pks);
            nrec++;
        }
        event_group_update_preferred(g);
        g_ptr_array_add(g_groups, g);
        ewguidb_rows_free(ors);
    }
    return nrec;
}

void cargar_sismos_db(void)
{
    EwGuiEventRow *evs = NULL;
    const char **ids = NULL;
    int nev = 0, i, nrec = 0;

    if (!g_db)
        return;
    if (ewguidb_query_recent_events(g_db, g_db_hours, &evs, &nev) < 0) {
        ewguidb_rows_free(evs);
        return;
    }
    if (nev > 0) {
        ids = g_new(const char *, nev);
        for (i = 0; i < nev; i++)
            ids[i] = evs[i].event_id;
        nrec = cargar_grupos_db(ids, nev);
        g_free(ids);
    }
    ewguidb_rows_free(evs);

    if (nrec > 0) {
        rebuild_event_store();
        reselect_event_by_qid(0);
    }
    logit("t", "csnhypodbp: recuperados %d soluciones de la BD\n", nrec);
}

/* Reemplaza el contenido de la vista (grupos + stores) sin tocar la seleccion
   previa: se usa al aplicar una busqueda o al limpiarla. */
static void search_reset_views(void)
{
    if (g_store_detail) g_list_store_remove_all(g_store_detail);
    if (g_store_events) g_list_store_remove_all(g_store_events);
    if (g_store_hypo)   g_list_store_remove_all(g_store_hypo);
    if (g_groups)       g_ptr_array_set_size(g_groups, 0);
    selected_qid = 0; selected_mod = 0; selected_version = 0;
    selected_otime = 0.0; selected_depth = 0.0;
    selected_lat = 0.0; selected_lon = 0.0;
    selected_id[0] = '\0';
}

/* Consulta la BD por rangos y repuebla la vista. Devuelve el nº de eventos
   (>=0) o -1 si error. Con 0 resultados no se toca la vista actual. */
int search_apply(const CsnhypodbpSearch *s)
{
    EwGuiSearchFilter f;
    EwGuiMapEvent *rows = NULL;
    const char **ids = NULL;
    int n = 0, i, nrec = 0;

    if (!g_db || !s)
        return -1;

    memset(&f, 0, sizeof f);
    f.time_min = s->time_min;
    f.time_max = s->time_max;
    f.have_lat = s->have_lat; f.lat_min = s->lat_min; f.lat_max = s->lat_max;
    f.have_lon = s->have_lon; f.lon_min = s->lon_min; f.lon_max = s->lon_max;
    f.have_depth = s->have_depth;
    f.depth_min = s->depth_min; f.depth_max = s->depth_max;
    f.have_mag = s->have_mag; f.mag_min = s->mag_min; f.mag_max = s->mag_max;
    f.limit = s->limit;

    if (ewguidb_search_events(g_db, &f, &rows, &n) < 0) {
        /* La conexion pudo caer (p. ej. la BD se reinicio): reabrir y reintentar. */
        logit("e", "csnhypodbp: busqueda: %s (reintento de conexion)\n", ewguidb_last_error(g_db));
        csnhypodbp_db_reconnect();
        if (!g_db || ewguidb_search_events(g_db, &f, &rows, &n) < 0) {
            logit("e", "csnhypodbp: busqueda: %s\n",
                  g_db ? ewguidb_last_error(g_db) : "sin conexion a la BD");
            ewguidb_rows_free(rows);
            return -1;
        }
    }
    if (n == 0) {            /* sin resultados: no se toca la vista actual */
        ewguidb_rows_free(rows);
        return 0;
    }

    search_reset_views();
    ids = g_new(const char *, n);
    for (i = 0; i < n; i++)
        ids[i] = rows[i].event_id;
    nrec = cargar_grupos_db(ids, n);
    g_free(ids);
    ewguidb_rows_free(rows);

    g_search_active = 1;
    rebuild_event_store();
    if (nrec > 0)
        reselect_event_by_qid(0);
    csnhypodbp_update_search_ui();
    logit("t", "csnhypodbp: busqueda -> %d eventos (%d soluciones)\n", n, nrec);
    return n;
}

/* Vuelve a la vista en vivo+historial (boton/ítem "Clear search"). */
void search_clear(void)
{
    if (!g_search_active)
        return;
    g_search_active = 0;
    search_reset_views();
    cargar_sismos_iniciales(tree_global);
    cargar_sismos_db();
    csnhypodbp_update_search_ui();
    logit("t", "csnhypodbp: busqueda limpiada -> vista vivo+historial\n");
}

int procesar_mensaje_sismo(GtkWidget *tree, const char *payload, int mod) {
    (void)tree;
    CsnhypodbpSolution sol;
    char fecha[32], hora[32], szLat[32], szLon[32], szDep[32], szRes[32], szAzm[32];
    char szStn[32], szID[32], szMod[32], szVer[32], szEstado[2];
    CsnhypodbpRow *row = NULL;
    gboolean new_row = FALSE, new_group = FALSE;
    EventGroup *g = NULL;
    EventSol *es;
    int matches[64], nm = 0, sel_status = 0, slot, free_slot, p;
    guint gi;

    ensure_groups();
    if (csnhypodbp_parse_solution(payload, strlen(payload), mod, &sol) < 0) return 0;

    {
        csnhypodbp_search_fmt_utc((time_t)(sol.otime), fecha, sizeof(fecha));
        snprintf(hora, sizeof(hora), "%s",
                 strlen(fecha) >= 19 ? fecha + 11 : "--:--:--");
    }
    snprintf(szLat, sizeof(szLat), "%.2f%c", fabs(sol.lat), sol.lat < 0 ? 'S' : 'N');
    snprintf(szLon, sizeof(szLon), "%.2f%c", fabs(sol.lon), sol.lon < 0 ? 'W' : 'E');
    snprintf(szDep, sizeof(szDep), "%.0f", sol.depth);
    snprintf(szRes, sizeof(szRes), "%.1f", sol.rms);
    snprintf(szAzm, sizeof(szAzm), "%d", sol.gap);
    snprintf(szStn, sizeof(szStn), "%d", sol.nph);
    snprintf(szID, sizeof(szID), "%010d", sol.qid);
    snprintf(szMod, sizeof(szMod), "%s", ModLabel(mod));
    snprintf(szVer, sizeof(szVer), "%u", sol.version);
    szEstado[0] = sol.manual ? 'M' : 'A'; szEstado[1] = '\0';

    /* Upsert de la fila de solucion, clave (qid, mod, version). */
    {
        guint n = g_list_model_get_n_items(G_LIST_MODEL(g_store_hypo));
        guint i;
        for (i = 0; i < n; i++) {
            CsnhypodbpRow *r = CSNHYPODBP_ROW(g_list_model_get_item(G_LIST_MODEL(g_store_hypo), i));
            int rq = 0, rm = 0, rv = 0;
            if (r) g_object_get(r, "qid", &rq, "mod", &rm, "qver", &rv, NULL);
            if (r && rq == sol.qid && rm == mod && rv == (int)sol.version) { row = r; g_object_unref(r); break; }
            if (r) g_object_unref(r);
        }
    }
    if (row) {
        /* Conserva magnitudes ya calculadas (llegan por TYPE_MAGNITUDE aparte). */
        char szMl[32] = "-", szMwp[32] = "-", szMb[32] = "-", szMs[32] = "-";
        const char *oml = csnhypodbp_row_col(row, 9), *omwp = csnhypodbp_row_col(row, 10);
        const char *omb = csnhypodbp_row_col(row, 11), *oms = csnhypodbp_row_col(row, 12);
        if (oml && oml[0]) { strncpy(szMl, oml, 31); szMl[31] = '\0'; }
        if (omwp && omwp[0]) { strncpy(szMwp, omwp, 31); szMwp[31] = '\0'; }
        if (omb && omb[0]) { strncpy(szMb, omb, 31); szMb[31] = '\0'; }
        if (oms && oms[0]) { strncpy(szMs, oms, 31); szMs[31] = '\0'; }
        g_object_set(row, "c0",fecha,"c1",hora,"c2",szLat,"c3",szLon,"c4",szDep,"c5",szRes,
                     "c6",szAzm,"c7",szStn,"c8",szID,"c9",szMl,"c10",szMwp,
                     "c11",szMb,"c12",szMs,"c13",szMod,"c14",szVer,
                     "otime",sol.otime,"qver",(int)sol.version,"qid",sol.qid,"lat",sol.lat,
                     "lon",sol.lon,"depth",sol.depth,"mod",mod,"estado",szEstado,NULL);
        row_set_mag(row, 0, szMl); row_set_mag(row, 1, szMwp);
        row_set_mag(row, 2, szMb); row_set_mag(row, 3, szMs);
    } else {
        row = csnhypodbp_row_new(fecha, hora, szLat, szLon, szDep, szRes, szAzm,
            szStn, szID, "-", "-", "-", "-", szMod, szVer, sol.otime,
            (int)sol.version, sol.qid, sol.lat, sol.lon, sol.depth, mod);
        g_object_set(row, "estado", szEstado, NULL);
        {
            /* La solucion acaba de llegar al modulo: es su hora de creacion. */
            char created[32];
            csnhypodbp_search_fmt_utc(time(NULL), created, sizeof(created));
            g_object_set(row, "created", created, NULL);
        }
        g_list_store_append(g_store_hypo, row);
        g_object_unref(row);
        new_row = TRUE;
    }
    (void)new_row;
    g_history_needs_saving = TRUE;

    /* Cache de picks para el overlay de trazas (clave qid+mod+version). */
    slot = -1; free_slot = -1;
    for (int c = 0; c < MAX_CACHED_EVENTS; c++) {
        if (PickCache[c].qid == sol.qid && PickCache[c].mod == mod &&
            PickCache[c].version == (int)sol.version) { slot = c; break; }
        if (free_slot < 0 && PickCache[c].num_picks == 0) free_slot = c;
    }
    if (slot < 0) { slot = (free_slot >= 0) ? free_slot : pick_cache_idx; pick_cache_idx = (pick_cache_idx + 1) % MAX_CACHED_EVENTS; }
    PickCache[slot].qid = sol.qid; PickCache[slot].mod = mod;
    PickCache[slot].version = (int)sol.version; PickCache[slot].num_picks = 0;
    for (p = 0; p < sol.npicks && p < MAX_PICKS_PER_EVENT; p++) {
        CsnhypodbpPick *pk = &sol.picks[p]; int pi = PickCache[slot].num_picks;
        memset(&PickCache[slot].picks[pi], 0, sizeof(CACHED_PICK));
        strncpy(PickCache[slot].picks[pi].sta, pk->sta, sizeof(PickCache[slot].picks[pi].sta) - 1);
        strncpy(PickCache[slot].picks[pi].chan, pk->chan, sizeof(PickCache[slot].picks[pi].chan) - 1);
        PickCache[slot].picks[pi].phase[0] = pk->phase; PickCache[slot].picks[pi].phase[1] = '\0';
        PickCache[slot].picks[pi].pTime = pk->t_pick;
        PickCache[slot].num_picks++;
    }

    /* Grupo: mismo qid o firma compatible. */
    for (gi = 0; gi < g_groups->len && nm < 64; gi++) {
        EventGroup *gg = g_ptr_array_index(g_groups, gi);
        if (event_group_compatible(gg, sol.qid, sol.picks, sol.npicks,
                                   g_group_pick_tol_sec, g_group_min_match_picks))
            matches[nm++] = (int)gi;
    }
    if (nm == 0) {
        g = event_group_new(sol.qid);
        g_ptr_array_add(g_groups, g);
        new_group = TRUE;
    } else {
        int t;
        g = g_ptr_array_index(g_groups, matches[0]);
        for (t = nm - 1; t >= 1; t--) {
            int mi = matches[t];
            EventGroup *mg = g_ptr_array_index(g_groups, mi);
            guint q;
            /* Quitar la fila maestra del absorbido ANTES de reapuntar sus filas:
               la identidad es el grupo, no el qid. */
            if (g_store_events) csnhypodbp_master_remove(g_store_events, mg);
            for (q = 0; q < mg->sols->len; q++) {
                EventSol *mes = g_ptr_array_index(mg->sols, q);
                g_object_set_data(G_OBJECT(mes->row), "event-group", g);
                g_ptr_array_add(g->sols, mes);
            }
            g_ptr_array_set_size(mg->sols, 0);
            event_group_add_picks(g, mg->picks, mg->npicks, g_group_pick_tol_sec);
            g_ptr_array_remove_index(g_groups, mi);
        }
    }

    es = event_group_find_sol(g, row);
    if (!es) { es = g_new0(EventSol, 1); es->row = row; g_ptr_array_add(g->sols, es); }
    es->qid = sol.qid; es->manual = sol.manual; es->version = sol.version; es->nph = sol.nph;
    es->rms = sol.rms; es->otime = sol.otime; es->mod = mod;
    event_group_add_picks(g, sol.picks, sol.npicks, g_group_pick_tol_sec);
    /* Picadas de ESTA solucion, con su residual (misma solucion re-emitida: se
       reemplazan, no se acumulan). */
    event_sol_set_picks(es, sol.picks, sol.npicks);
    g_object_set_data(G_OBJECT(row), "event-group", g);
    event_group_update_preferred(g);
    event_store_upsert(g);
    /* Magnitudes que llegaron antes de existir esta solucion (qid,mod). */
    solution_take_pending(row, sol.qid, mod);
    /* Si la solucion es del evento que el usuario tiene ABIERTO, se refrescan en
       sitio el detalle, la tabla de estaciones y la tablita. Sin esto quedaban
       incoherentes (tablita nueva, tabla vieja) y con la seleccion intacta no se
       dispara ningun fetch (selected_* no cambia). */
    if (viewed_group() == g) {
        csnhypodbp_detail_refresh();
        rebuild_station_list();
        csnhypodbp_preview_update();
    }
    /* La solucion (nueva o actualizada) puede cambiar lo que muestran los mapas
       del evento vigente. */
    csnhypodbp_maps_refresh();

    if (g->preferred == row) {
        sel_status = 1;
        if (sol.qid == selected_qid) sel_status = 1;
    }
    if (new_group) {
        /* No robar la seleccion: el evento nuevo se añade a la lista, pero no
           cambia lo que el usuario mira (evento/trazas/zoom). Solo se
           auto-selecciona si aun no hay ninguna. */
        if (!g_search_active && csnhypodbp_should_autoselect(selected_qid != 0))
            reselect_event_by_qid(sol.qid);
        return 2;
    }
    return sel_status;
}

/* FIX 1: Lectura segura de Magnitudes utilizando rd_mag() nativo de Earthworm */
int procesar_mensaje_mag(GtkWidget *tree, const char *payload) {
    (void)tree;
    MAG_INFO mag;
    /* rd_mag usa sscanf (sensible al locale); garantizamos punto decimal. */
    setlocale(LC_NUMERIC, "C");
    memset(&mag, 0, sizeof(MAG_INFO));
    if (rd_mag((char*)payload, strlen(payload), &mag) < 0) {
        logit("e", "csnhypodbp: Error en rd_mag al parsear TYPE_MAGNITUDE\n");
        return 0;
    }
    int qid = atoi(mag.qid);
    int mod = (int)mag.origin_version;   /* modulo origen del ARC (162/163/164) */
    int slot = -1;
    char mag_str[32];

    if (strcmp(mag.szmagtype, "ML") == 0 || strcmp(mag.szmagtype, "Ml") == 0) slot = 0;
    else if (strcmp(mag.szmagtype, "Mwp") == 0 || strcmp(mag.szmagtype, "MWP") == 0) slot = 1;
    else if (strcmp(mag.szmagtype, "Mb") == 0 || strcmp(mag.szmagtype, "MB") == 0) slot = 2;
    else if (strcmp(mag.szmagtype, "Ms") == 0 || strcmp(mag.szmagtype, "MS") == 0) slot = 3;
    else return 0;

    snprintf(mag_str, sizeof(mag_str), "%.1f-%d", mag.mag, mag.nstations);

    /* Magnitudes POR ESTACION del propio mensaje: las muestra la sub-pestaña
       `magnitudes`. Se guardan por (qid,mod,magnitud); si la tabla no las tiene
       (p. ej. la solucion viene del historial), cae a la BD. */
    {
        static CsnhypodbpMagIn vals[CSNHYPODBP_MAGSTA_MAX];
        MAG_CHAN_INFO *ch = NULL;
        size_t len = strlen(payload);
        int nch, i, n = 0;

        /* Dimensionado y parseo en el helper compartido con hypodb: `rd_chan_mag`
           NO acota sus escrituras contra el tamaño que se le pasa y además destruye
           el mensaje (parsea in-place), así que contar '\n' dejaba el array corto y
           se escribía fuera del heap. Ver csnhypodbp_magchan.c. */
        nch = csnhypodbp_magchan_parse(payload, len, &ch);
        if (nch > 0) {
                for (i = 0; i < nch && n < CSNHYPODBP_MAGSTA_MAX; i++) {
                    if (ch[i].sta[0] == '\0') continue;
                    memset(&vals[n], 0, sizeof(vals[n]));
                    snprintf(vals[n].sta, sizeof(vals[n].sta), "%s", ch[i].sta);
                    snprintf(vals[n].chan, sizeof(vals[n].chan), "%s", ch[i].comp);
                    vals[n].mag = ch[i].mag;
                    vals[n].amp = (double)ch[i].Amp1;
                    vals[n].period = (double)ch[i].Period1;
                    n++;
                }
            free(ch);
            if (n > 0) csnhypodbp_magsta_put(&g_magsta_live, qid, mod, slot, vals, n);
        }
    }

    ensure_groups();
    if (apply_mag_to_solutions(qid, mod, slot, mag_str)) {
        logit("t", "csnhypodbp: Mag ID %d mod %d slot %d: %s\n", qid, mod, slot, mag_str);
        g_history_needs_saving = TRUE;
        /* La tablita del preview muestra ML/Mwp de la solucion elegida: si la
           magnitud llega despues de seleccionarla, hay que repintarla. */
        csnhypodbp_preview_update();
        rebuild_mag_list();   /* y la tabla de magnitudes (llegan las estaciones) */
        return 1;
    }
    /* Sin solucion todavia: guardar para aplicarla cuando exista. */
    csnhypodbp_mag_store_put(&g_pending_mags, qid, mod, slot, mag_str);
    logit("t", "csnhypodbp: Mag pendiente ID %d mod %d slot %d: %s\n", qid, mod, slot, mag_str);
    return 0;
}

/* Magnitudes POR ESTACION de una solucion (fila de la tabla) y una magnitud
   (slot 0..3): primero las que llegaron vivas por el anillo y, si no hay, las
   del historial en la BD. Devuelve cuantas filas dejo en `out`. */
int csnhypodbp_magsta_for(CsnhypodbpRow *row, int slot, CsnhypodbpMagIn *out, int max)
{
    int qid = 0, mod = 0, n;

    if (!row || !out || max <= 0 || slot < 0 || slot >= CSNHYPODBP_MAG_N) return 0;
    g_object_get(row, "qid", &qid, "mod", &mod, NULL);
    n = csnhypodbp_magsta_take(&g_magsta_live, qid, mod, slot, out, max);
    if (n > 0) return n;

    {
        const char *oid = csnhypodbp_row_origin_id(row);
        const char *want = csnhypodbp_magcfg_name(slot);
        EwGuiMagRow *mgs = NULL;
        EwGuiStaMagRow *stas = NULL;
        const EwGuiMagRow *best = NULL;
        CsnhypodbpMagDbRow *raw;
        int nm = 0, ns = 0, i, k = 0;

        if (!oid[0] || !g_db) return 0;
        if (ewguidb_query_magnitudes(g_db, oid, &mgs, &nm) <= 0) {
            ewguidb_rows_free(mgs);
            return 0;
        }
        /* Del mismo origen puede haber la automatica y la ajustada: manda la
           ajustada (misma marca que publica la GUI en `algorithm`). */
        for (i = 0; i < nm; i++) {
            if (strcmp(mgs[i].magnitude_type, want) != 0) continue;
            if (strcmp(mgs[i].calculation_method,
                       CSNHYPODBP_MAG_MANUAL_ALGO) == 0) {
                best = &mgs[i];
                break;
            }
            if (!best) best = &mgs[i];
        }
        if (best && ewguidb_query_station_magnitudes(g_db, best->magnitude_id,
                                                     &stas, &ns) > 0 && ns > 0) {
            raw = g_new0(CsnhypodbpMagDbRow, (gsize)ns);
            for (i = 0; i < ns; i++) {
                raw[k].channel = stas[i].channel;
                raw[k].read_amplitude = stas[i].read_amplitude;
                raw[k].calculated_magnitude = stas[i].calculated_magnitude;
                k++;
            }
            n = csnhypodbp_magrow_from_db(out, max, raw, k);
            g_free(raw);
        }
        ewguidb_rows_free(stas);
        ewguidb_rows_free(mgs);
    }
    return n;
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
    int           version;
    double        eq_lat, eq_lon;
    double        event_otime;      /* hora origen del evento (para la edad) */
    double        req_start, req_end;
    double        max_dist_km;
    int           nsta;
    char          only_sta[16];   /* si no vacio, solo esta estacion */
    WsFetchedSta *stas;
} WsFetchJob;

static int g_ws_busy = 0;

static gboolean ws_fetch_finish(gpointer data);

static WStore *g_wstore = NULL;

/* Lee del store y sintetiza UN registro TRACE2 en *raw, para que
   ws_fetch_finish lo parsee igual que una respuesta de wave_serverV. */
static int store_fetch_sta(const char *sta, const char *net, const char *chan,
                           const char *loc, double t0, double t1,
                           char **raw, long *len) {
    WStoreTrace st;
    TRACE2_HEADER *h;
    char *buf;
    long total, n;

    if (!g_wstore) return 0;
    memset(&st, 0, sizeof(st));
    if (wstore_get(g_wstore, sta, net, chan, loc, t0, t1, &st) != 0) return 0;
    n = st.n;
    if (n <= 0) { wstore_trace_free(&st); return 0; }

    total = (long)sizeof(TRACE2_HEADER) + n * (long)sizeof(int32_t);
    buf = malloc((size_t)total);
    if (!buf) { wstore_trace_free(&st); return 0; }
    h = (TRACE2_HEADER *)buf;
    memset(h, 0, sizeof(*h));
    snprintf(h->sta, sizeof(h->sta), "%s", sta);
    snprintf(h->net, sizeof(h->net), "%s", net);
    snprintf(h->chan, sizeof(h->chan), "%s", chan);
    snprintf(h->loc, sizeof(h->loc), "%s", loc);
    h->starttime = st.t0;
    h->samprate = st.samprate;
    h->nsamp = (int)n;
    h->endtime = st.t0 + (n > 1 ? (n - 1) / st.samprate : 0.0);
    memcpy(h->datatype, "i4", 3);
    h->version[0] = TRACE2_VERSION0;
    h->version[1] = TRACE2_VERSION1;
    memcpy(buf + sizeof(TRACE2_HEADER), st.samples, (size_t)n * sizeof(int32_t));
    wstore_trace_free(&st);

    *raw = buf;
    *len = total;
    return 1;
}

static gpointer ws_fetch_thread(gpointer data)
{
    WsFetchJob *job = data;
    long max_trace_buf = 2000000;
    char *trace_buffer = malloc(max_trace_buf);

    /* Store backend: abrir una vez por job (un solo job a la vez). */
    if (g_wave_backend != 0 && g_wstore == NULL && g_store_root[0])
        wstore_open(&g_wstore, g_store_root, NULL);

    if (trace_buffer && g_wave_backend != 1 && ws_menu.head == NULL) {
        if (wsAppendMenu(WsIP, WsPort, &ws_menu, WsTimeout) != WS_ERR_NONE)
            logit("e", "csnhypodbp: fetch: wsAppendMenu fallo (%s:%s)\n", WsIP, WsPort);
    }

    for (int i = 0; i < job->nsta; i++) {
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
        if (job->only_sta[0] && strcmp(StaArray[i].szStation, job->only_sta) != 0) continue;

        TRACE_REQ req; memset(&req, 0, sizeof(TRACE_REQ));
        snprintf(req.sta, sizeof(req.sta), "%s", StaArray[i].szStation);
        snprintf(req.net, sizeof(req.net), "%s", StaArray[i].szNetID);
        snprintf(req.chan, sizeof(req.chan), "%s", StaArray[i].szChannel);
        snprintf(req.loc, sizeof(req.loc), "%s", StaArray[i].szLocation);
        req.reqStarttime = job->req_start; req.reqEndtime = job->req_end;
        req.pBuf = trace_buffer; req.bufLen = max_trace_buf; req.timeout = WsTimeout; req.fill = 0;

        int got = 0;
        /* Edad del evento: por encima de SdsFallbackHours la retencion local ya
           no lo tiene, asi que se consulta el SDS primero (evita el timeout de
           wave_serverV en eventos viejos). */
        double age_hours = job->event_otime > 0.0
                         ? (double)(time(NULL) - (time_t)job->event_otime) / 3600.0
                         : 0.0;
        int prefer_sds = csnhypodbp_sds_prefer(age_hours, g_sds_enabled,
                                               g_sds_root, g_sds_fallback_hours);

        if (prefer_sds) {
            got = csnhypodbp_sds_fetch(req.sta, req.net, req.chan, req.loc,
                                       job->req_start, job->req_end,
                                       &f->raw, &f->act_len);
            logit("t", "csnhypodbp: fetch sds %s.%s.%s.%s res=%d len=%ld\n",
                  req.sta, req.chan, req.net, req.loc, got,
                  got ? f->act_len : 0L);
            if (got) f->has_data = 1;
        }
        /* Backend store: lectura indexada directa (rapida, sin TCP). */
        if (!got && g_wave_backend != 0 && g_wstore != NULL) {
            got = store_fetch_sta(req.sta, req.net, req.chan, req.loc,
                                  job->req_start, job->req_end, &f->raw, &f->act_len);
            if (got) f->has_data = 1;
        }
        /* Evento antiguo (prefer_sds): wave_serverV ya no lo retiene (misma
           ventana que el store); no se le pregunta para no romper la conexion
           ni ensuciar el log. Aqui solo se llega si SDS y store no dieron nada. */
        if (!got && !prefer_sds && g_wave_backend != 1 && trace_buffer) {
            int ws_res = wsGetTraceBinL(&req, &ws_menu, WsTimeout);
            logit("t", "csnhypodbp: fetch %s.%s.%s.%s res=%d actLen=%ld\n",
                  req.sta, req.chan, req.net, req.loc, ws_res, (long)req.actLen);
            if (ws_res == WS_ERR_NONE && req.actLen > 0) {
                f->raw = malloc(req.actLen);
                if (f->raw) { memcpy(f->raw, req.pBuf, req.actLen); f->act_len = req.actLen; f->has_data = 1; }
            } else if (ws_res == WS_ERR_BROKEN_CONNECTION || ws_res == WS_ERR_TIMEOUT) {
                logit("e", "csnhypodbp: fetch: conexion rota/timeout (res=%d); reinicio el menu\n", ws_res);
                wsKillMenu(&ws_menu); ws_menu.head = NULL;
                if (g_wave_backend == 0) break;
            }
        }
        /* Ultimo recurso: SDS cuando el evento es reciente pero store/ws no dieron datos. */
        if (!got && !prefer_sds && g_sds_enabled && g_sds_root[0]) {
            got = csnhypodbp_sds_fetch(req.sta, req.net, req.chan, req.loc,
                                       job->req_start, job->req_end,
                                       &f->raw, &f->act_len);
            logit("t", "csnhypodbp: fetch sds %s.%s.%s.%s res=%d len=%ld\n",
                  req.sta, req.chan, req.net, req.loc, got,
                  got ? f->act_len : 0L);
            if (got) f->has_data = 1;
        }
    }
    free(trace_buffer);

    g_idle_add(ws_fetch_finish, job);
    return NULL;
}

/* Identidad del ultimo evento RENDERIZADO en el canvas. Distingue un fetch del
   mismo evento (refetch: no debe vaciar nada) de uno de otro evento (todo el
   canvas es de otro sismo). */
static int g_rendered_qid = 0, g_rendered_mod = 0, g_rendered_ver = 0;

static gboolean ws_fetch_finish(gpointer data)
{
    WsFetchJob *job = data;
    int stale = (selected_qid != job->qid || selected_mod != job->mod ||
                 selected_version != job->version);

    if (!stale) {
        for (int i = 0; i < NumEstaciones; i++) {
            if (job->only_sta[0] && strcmp(StaArray[i].szStation, job->only_sta) != 0) continue;
            EwGuiTrace *tr = StaArray[i].trace;
            /* ¿Trae el job datos para esta estacion? Si no los trae y es el MISMO
               evento, se conserva la traza: un refetch que no devuelve nada no
               vacia la pantalla (era el motivo de "desaparecio la traza que
               miraba" tras una solucion nueva). */
            int has = (i < job->nsta && job->stas[i].has_data && job->stas[i].raw) ? 1 : 0;
            StaArray[i].fetched = 1;
            if (csnhypodbp_fetch_clear_on(g_rendered_qid, g_rendered_mod, g_rendered_ver,
                                          job->qid, job->mod, job->version, has)) {
                bHasData[i] = 0;
                if (tr) ewgui_trace_clear(tr);
            }
            StaArray[i].iNumPicks = 0; StaArray[i].dManualPickTime = 0.0;
            g_StaDist[i] = 0.0; g_StaTheoP[i] = 0.0;
            /* oldest/rate/length se fijan con ewgui_trace_set_span al llenar
               la traza, tambien en la primera descarga (cuando tr se crea). */

            for (int c = 0; c < MAX_CACHED_EVENTS; c++) {
                if (PickCache[c].qid == job->qid && PickCache[c].mod == job->mod &&
                    PickCache[c].version == job->version) {
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
                /* P teorica (IASP91) para alinear las trazas sin picada. */
                if (g_StaDist[i] > 0.0 && csnhypodbp_tt_ready()) {
                    double tp = csnhypodbp_tt_p(g_StaDist[i], selected_depth);
                    g_StaTheoP[i] = (tp > 0.0) ? (selected_otime + tp) : selected_otime;
                } else {
                    g_StaTheoP[i] = selected_otime;
                }
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
                    ewgui_trace_set_span(tr, job->req_start, rate, ctr);
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

        {
            int nwith = 0, nreq = 0;
            for (int i = 0; i < NumEstaciones; i++) {
                if (job->only_sta[0] && strcmp(StaArray[i].szStation, job->only_sta) != 0) continue;
                nreq++;
                if (bHasData[i]) nwith++;
            }
            logit("t", "csnhypodbp: fetch fin sta=%s con_datos=%d/%d\n",
                  job->only_sta[0] ? job->only_sta : "(todas)", nwith, nreq);
        }

        g_rendered_qid = job->qid; g_rendered_mod = job->mod; g_rendered_ver = job->version;
        if (window_global) gtk_window_set_title(GTK_WINDOW(window_global), "CSNhypodbp - Hypocenter database picker (EW8)");
        if (canvas_global) {
            rebuild_station_list();
            actualizar_altura_canvas();
            ewgui_canvas_queue_draw(canvas_global);
        }
    } else {
        logit("t", "csnhypodbp: fetch descartado (stale) qid=%d\n", job->qid);
        pending_waveform_reload = TRUE;   /* la seleccion cambio: reintentar */
    }

    for (int i = 0; i < job->nsta; i++) free(job->stas[i].raw);
    free(job->stas);
    free(job);
    g_ws_busy = 0;
    return G_SOURCE_REMOVE;
}

static void ensure_station_channels(const char *sta);

/* Asegura las componentes de TODAS las estaciones antes de pedir. El .sta de
   csnhypodbp trae una linea por estacion y solo la vertical (estaciones_107.txt:
   137 x HHZ); sin fila no hay peticion, y el fetch itera filas de StaArray. El
   dato SI esta (slink2ew pide HH?.D, wstore_ingest ingiere todo SLINK_RING y el
   SDS archiva por canal), pero nunca se pedia. Con esto el canvas de magnitudes
   (y el de picking) muestra siempre todas las componentes de la estacion. */
static void ensure_all_station_channels(void) {
    int i, n = NumEstaciones;

    for (i = 0; i < n && i < MAX_ESTA; i++) {
        char sta[16];

        snprintf(sta, sizeof(sta), "%s", StaArray[i].szStation);
        ensure_station_channels(sta);
    }
}

static void start_fetch(const char *only_sta, double otime, double eq_lat, double eq_lon,
                        int qid, int mod, int version) {
    if (g_ws_busy) { pending_waveform_reload = TRUE; return; }

    /* Antes de congelar job->nsta/job->stas: las filas nuevas tienen que entrar
       al fetch (y asi reciben su g_StaDist y su g_StaTheoP). */
    ensure_all_station_channels();

    WsFetchJob *job = calloc(1, sizeof(*job));
    if (!job) return;
    job->qid = qid; job->mod = mod; job->version = version;
    job->eq_lat = eq_lat; job->eq_lon = eq_lon;
    job->event_otime = otime;
    /* La peticion es LARGA a proposito: Ms mide su ventana (P + Ms_Window, cientos
       de segundos) y la sub-pestaña `magnitudes` necesita ese dato ya descargado
       para no re-pedirlo al cambiar de magnitud. El tope duro es el buffer de la
       traza (MAX_MINUTES*60 s): lo que pase de ahi no cabria. */
    {
        double hard = (double)(MAX_MINUTES * 60) - 30.0;
        double mag_end = (g_mag_fetch_sec > hard) ? hard : g_mag_fetch_sec;
        double pick_end = g_dScreenTime + g_max_dist_km / 5.5 + g_align_lead;

        if (g_mag_fetch_sec > hard)
            logit("t", "csnhypodbp: MagFetchSeconds %.0f excede el buffer (%d s); se usa %.0f s\n",
                  g_mag_fetch_sec, MAX_MINUTES * 60, hard);
        job->req_start = otime - 30.0;
        job->req_end   = otime + ((pick_end > mag_end) ? pick_end : mag_end);
    }
    job->max_dist_km = g_max_dist_km;
    job->nsta = NumEstaciones;
    if (only_sta) snprintf(job->only_sta, sizeof(job->only_sta), "%s", only_sta);
    job->stas = calloc(NumEstaciones > 0 ? (size_t)NumEstaciones : 1, sizeof(WsFetchedSta));
    if (!job->stas) { free(job); return; }

    logit("t", "csnhypodbp: fetch inicio sta=%s ventana=[%.3f..%.3f] %.0fs nsta=%d\n",
          job->only_sta[0] ? job->only_sta : "(todas)",
          job->req_start, job->req_end, job->req_end - job->req_start, job->nsta);

    g_ws_busy = 1;
    GThread *th = g_thread_new("ws_fetch", ws_fetch_thread, job);
    g_thread_unref(th);
}

void FetchWaveformsForEvent(double otime, double eq_lat, double eq_lon, int qid, int mod, int version) {
    start_fetch(NULL, otime, eq_lat, eq_lon, qid, mod, version);
}

void FetchStationWaveforms(const char *sta) {
    start_fetch(sta, selected_otime, selected_lat, selected_lon,
                selected_qid, selected_mod, selected_version);
}

/* Asegura que existan filas DEV_STATION para los canales candidatos de la
   estacion (channelsOrder x componentsOrder), para poder pedirlos al WS. */
static void ensure_station_channels(const char *sta) {
    int base = -1, c, i;
    char cand[64][8];
    int nc;
    if (!sta || !sta[0]) return;
    for (i = 0; i < NumEstaciones; i++)
        if (strcmp(StaArray[i].szStation, sta) == 0) { base = i; break; }
    if (base < 0) return;
    nc = csnhypodbp_channel_candidates(g_channels_order, g_components_order, cand, 64);
    for (c = 0; c < nc; c++) {
        int exists = 0;
        /* Solo bandas que la estacion ya tiene (p. ej. .sta HHZ -> HHZ/HHN/HHE):
           evita pedir bandas inexistentes (BH/HN/LH) y no inflar StaArray. */
        if (!csnhypodbp_station_has_band(StaArray, NumEstaciones, sta, cand[c])) continue;
        for (i = 0; i < NumEstaciones; i++)
            if (strcmp(StaArray[i].szStation, sta) == 0 &&
                strcmp(StaArray[i].szChannel, cand[c]) == 0) { exists = 1; break; }
        if (exists) continue;
        if (NumEstaciones >= MAX_ESTA) break;
        {
            DEV_STATION *d = &StaArray[NumEstaciones];
            char tsta[16], tnet[16], tloc[16];
            snprintf(tsta, sizeof(tsta), "%s", StaArray[base].szStation);
            snprintf(tnet, sizeof(tnet), "%s", StaArray[base].szNetID);
            snprintf(tloc, sizeof(tloc), "%s", StaArray[base].szLocation);
            memset(d, 0, sizeof(*d));
            snprintf(d->szStation, sizeof(d->szStation), "%s", tsta);
            snprintf(d->szNetID, sizeof(d->szNetID), "%s", tnet);
            snprintf(d->szLocation, sizeof(d->szLocation), "%s", tloc);
            snprintf(d->szChannel, sizeof(d->szChannel), "%s", cand[c]);
            d->dLat = StaArray[base].dLat;
            d->dLon = StaArray[base].dLon;
            d->dScreenScale = StaArray[base].dScreenScale;
            d->trace = NULL;
            NumEstaciones++;
        }
    }
}

void EnsureAndFetchStation(const char *sta) {
    if (!sta || !sta[0]) return;
    ensure_station_channels(sta);
    /* Descargar si ALGUNA fila de la estacion sigue sin intentarse (p. ej. los
       canales HHN/HHE recien creados por ensure_station_channels). */
    if (csnhypodbp_station_needs_fetch(StaArray, NumEstaciones, sta) && !g_ws_busy) {
        FetchStationWaveforms(sta);
    } else {
        actualizar_altura_canvas();
        if (canvas_global) ewgui_canvas_queue_draw(canvas_global);
    }
}

/* ------------------------------------------------------------------ */
/* Soluciones manuales (locate + confirm)                              */
/* ------------------------------------------------------------------ */
int csnhypodbp_event_group_picks(const EventGroup *g, CsnhypodbpPick *out, int max)
{
    int n;
    if (!g || !out) return 0;
    n = g->npicks < max ? g->npicks : max;
    if (n > 0) memcpy(out, g->picks, (size_t)n * sizeof(*out));
    return n;
}

/* Picadas de LA solucion de esa fila (con su residual): la tabla de Ondas las usa
   para el `res` cuando no hay candidata de locate. Devuelve el numero copiado. */
int csnhypodbp_event_sol_picks(const CsnhypodbpRow *row, CsnhypodbpPick *out, int max)
{
    EventGroup *g;
    EventSol *es;
    int n;

    if (!row || !out || max <= 0) return 0;
    g = csnhypodbp_event_group_of(row);
    if (!g) return 0;
    es = event_group_find_sol(g, row);
    if (!es || !es->picks || es->npicks <= 0) return 0;
    n = (es->npicks < max) ? es->npicks : max;
    memcpy(out, es->picks, (size_t)n * sizeof(*out));
    return n;
}

/* Codigo de 1 letra de la magnitud preferida: viaja en el 13.er token de la
   picada; csnloc lo copia al BMTYPI del ARC y hypodb lo usa como preferida. */
static char pref_code(int kind)
{
    switch (kind) {
        case 0: return 'L';   /* ML  */
        case 1: return 'W';   /* Mwp */
        case 2: return 'B';   /* Mb  */
        case 3: return 'S';   /* Ms  */
        default: return '-';
    }
}

/* Publica una magnitud calculada a mano por la GUI como TYPE_MAGNITUDE, con la
   marca manual en `algorithm` (mismo convenio que la solucion manual). Va al
   anillo de hipocentros refinados, que es de donde la leen la propia GUI, csnrv
   y hypodb (la base). Sin lineas de canal: wr_mag_scnl escribe solo la cabecera. */
static void emit_mag_scnl(int imagtype, const char *type, const char *txt,
                          int qid, int mod)
{
    char buf[1024];
    double val = 0.0;
    int nst = 0;
    MAG_INFO m;
    MSG_LOGO logo;

    if (!txt || !txt[0] || !csnhypodbp_mag_split(txt, &val, &nst)) return;
    memset(&m, 0, sizeof(m));
    snprintf(m.qid, sizeof(m.qid), "%010d", qid);
    snprintf(m.szmagtype, sizeof(m.szmagtype), "%s", type);
    snprintf(m.algorithm, sizeof(m.algorithm), "%s", "CSNMan");
    m.imagtype = imagtype;
    m.mag = val;
    m.quality = 1.0;
    m.nstations = (nst > 0) ? nst : 1;
    m.nchannels = 0;
    m.origin_version = (unsigned)mod;
    wr_mag_scnl(&m, buf, sizeof(buf));
    logo.instid = MyInstId; logo.mod = MyModId; logo.type = TypeMagnitude;
    if (tport_putmsg(&InRegion, &logo, (long)strlen(buf), buf) != PUT_OK)
        logit("e", "csnhypodbp: error publicando la magnitud %s manual\n", type);
}

static void emit_pick_scnl(const char *sta, const char *chan, const char *net,
                           const char *loc, char phase, double t, char pref)
{
    char out_msg[256], ts[32];
    static unsigned char seq = 0;
    time_t sec = (time_t)t;
    double frac = t - (double)sec;
    struct tm *tm = gmtime(&sec);
    int ms;
    MSG_LOGO logo;
    if (!tm) return;
    if (frac < 0.0) frac = 0.0;
    ms = (int)(frac * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; sec += 1; tm = gmtime(&sec); }
    if (!tm) return;
    /* %02d.%03d y no %06.3f: ese ancho mete un ESPACIO si los segundos son < 10
       y csnloc descarta la picada. */
    snprintf(ts, sizeof(ts), "%04d%02d%02d%02d%02d%02d.%03d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec, ms);
    /* net/loc reales: con "--" HYPOINVERSE no empareja la estacion.
       13.er token: la magnitud preferida elegida en el cuadro de la candidata
       ('-' si no hay eleccion); csnloc lo copia al BMTYPI del ARC. */
    snprintf(out_msg, sizeof(out_msg), "%d %d %d %d %s.%s.%s.%s ?0 %s 0 0 0 %c M %c\n",
             TypePickSCNL, MyModId, MyInstId, seq++, sta, chan,
             (net && net[0]) ? net : "--", (loc && loc[0]) ? loc : "--", ts, phase,
             pref ? pref : '-');
    logo.instid = MyInstId; logo.mod = MyModId; logo.type = TypePickSCNL;
    if (tport_putmsg(&PRegion, &logo, strlen(out_msg), out_msg) != PUT_OK)
        logit("e", "csnhypodbp: error emitiendo pick manual\n");
}

int csnhypodbp_confirm_manual(const CsnhypodbpSolution *sol, const CsnhypodbpMagOut *mag,
                              int pref_kind)
{
    char arc[65536];
    CsnhypodbpSolution s;
    char pref = pref_code(pref_kind);
    int i;
    if (!sol) return -1;
    s = *sol;
    s.manual = 1;
    if (s.version == 0) s.version = (unsigned)time(NULL);
    if (csnhypodbp_arc_build(&s, 1, arc, sizeof(arc)) != 0) return -1;
    procesar_mensaje_sismo(tree_global, arc, s.mod);
    /* Se asignan TODAS las magnitudes calculadas; la preferida es la que eligio
       el radio del cuadro de la candidata. */
    if (mag) {
        if (mag->ml[0])  { apply_mag_to_solutions(s.qid, s.mod, 0, mag->ml);
                           emit_mag_scnl(1, "ML",  mag->ml,  s.qid, s.mod); }
        if (mag->mwp[0]) { apply_mag_to_solutions(s.qid, s.mod, 1, mag->mwp);
                           emit_mag_scnl(5, "Mwp", mag->mwp, s.qid, s.mod); }
        if (mag->mb[0])  { apply_mag_to_solutions(s.qid, s.mod, 2, mag->mb);
                           emit_mag_scnl(3, "Mb",  mag->mb,  s.qid, s.mod); }
        if (mag->ms[0])  { apply_mag_to_solutions(s.qid, s.mod, 3, mag->ms);
                           emit_mag_scnl(4, "Ms",  mag->ms,  s.qid, s.mod); }
    }
    for (i = 0; i < s.npicks; i++)
        emit_pick_scnl(s.picks[i].sta, s.picks[i].chan, s.picks[i].net,
                       s.picks[i].loc, s.picks[i].phase, s.picks[i].t_pick, pref);
    csnhypodbp_manual_state_save(g_manual_state_file, &s, mag);
    logit("t", "csnhypodbp: solucion manual confirmada qid=%d nph=%d (%d picks, pref=%c)\n",
          s.qid, s.nph, s.npicks, pref);
    return 0;
}

void csnhypodbp_load_manual_state(void)
{
    CsnhypodbpSolution sols[64];
    CsnhypodbpMagOut mags[64];
    int n = csnhypodbp_manual_state_load(g_manual_state_file, sols, mags, 64);
    int i;
    for (i = 0; i < n; i++) {
        char arc[65536];
        sols[i].manual = 1;
        if (sols[i].version == 0) sols[i].version = (unsigned)time(NULL) + (unsigned)i;
        if (csnhypodbp_arc_build(&sols[i], 1, arc, sizeof(arc)) != 0) continue;
        procesar_mensaje_sismo(tree_global, arc, sols[i].mod);
        if (mags[i].ml[0])  apply_mag_to_solutions(sols[i].qid, sols[i].mod, 0, mags[i].ml);
        if (mags[i].mwp[0]) apply_mag_to_solutions(sols[i].qid, sols[i].mod, 1, mags[i].mwp);
        if (mags[i].mb[0])  apply_mag_to_solutions(sols[i].qid, sols[i].mod, 2, mags[i].mb);
        if (mags[i].ms[0])  apply_mag_to_solutions(sols[i].qid, sols[i].mod, 3, mags[i].ms);
    }
    if (n) logit("t", "csnhypodbp: %d soluciones manuales recuperadas de %s\n",
                 n, g_manual_state_file);
}

