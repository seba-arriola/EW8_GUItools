/*
 * Capa de acceso a PostgreSQL (libpq) para información hipocentral.
 * Núcleo sin GTK; ver include/ewgui/db.h.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpq-fe.h>

#include "ewgui/db.h"

struct EwGuiDb {
    PGconn *conn;
    char    err[512];
};

/* --- Configuración -------------------------------------------------------- */

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e))
        *e-- = '\0';
    return s;
}

/* Busca `key` en un archivo tipo .env. 0 si la encuentra, -1 si no. */
static int file_lookup(const char *path, const char *key,
                       char *out, size_t outsz)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char line[1024];
    size_t klen = strlen(key);
    int found = -1;

    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == '\0' || *s == '#')
            continue;
        if (strncmp(s, key, klen) != 0)
            continue;

        char *p = s + klen;
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p != '=')
            continue;
        p = trim(p + 1);

        size_t n = strlen(p);
        if (n >= 2 && ((p[0] == '"' && p[n - 1] == '"') ||
                       (p[0] == '\'' && p[n - 1] == '\''))) {
            p[n - 1] = '\0';
            p++;
        }
        snprintf(out, outsz, "%s", p);
        found = 0;
        break;
    }

    fclose(f);
    return found;
}

/* Variable de entorno con el mismo nombre tiene precedencia sobre el archivo. */
static int resolve(const char *envfile, const char *key,
                   char *out, size_t outsz)
{
    const char *env = getenv(key);
    if (env && *env) {
        snprintf(out, outsz, "%s", env);
        return 0;
    }
    return file_lookup(envfile, key, out, outsz);
}

static int is_all_digits(const char *s)
{
    if (!s || !*s)
        return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s))
            return 0;
    return 1;
}

int ewguidb_config_load(const char *envfile, EwGuiDbConfig *out)
{
    if (!envfile || !out)
        return -1;

    memset(out, 0, sizeof(*out));

    if (resolve(envfile, "DB_HOST", out->host, sizeof(out->host)) != 0)
        return -1;
    if (resolve(envfile, "DB_PORT", out->port, sizeof(out->port)) != 0)
        return -1;
    if (resolve(envfile, "DB_NAME", out->name, sizeof(out->name)) != 0)
        return -1;
    if (resolve(envfile, "DB_USER", out->user, sizeof(out->user)) != 0)
        return -1;
    if (resolve(envfile, "DB_PASSWORD", out->password,
                sizeof(out->password)) != 0)
        return -1;

    if (!is_all_digits(out->port))
        return -1;

    return 0;
}

/* --- Conexión ------------------------------------------------------------- */

int ewguidb_open(const EwGuiDbConfig *cfg, EwGuiDb **out)
{
    if (!cfg || !out)
        return -1;

    EwGuiDb *db = calloc(1, sizeof(*db));
    if (!db)
        return -1;

    const char *keywords[] = {
        "host", "port", "dbname", "user", "password", "connect_timeout", NULL
    };
    const char *values[] = {
        cfg->host, cfg->port, cfg->name, cfg->user, cfg->password, "5", NULL
    };

    db->conn = PQconnectdbParams(keywords, values, 0);
    if (!db->conn || PQstatus(db->conn) != CONNECTION_OK) {
        snprintf(db->err, sizeof(db->err), "%s",
                 db->conn ? PQerrorMessage(db->conn) : "sin memoria");
        if (db->conn) {
            PQfinish(db->conn);
            db->conn = NULL;
        }
        *out = db; /* el llamador puede leer el error y cerrar */
        return -1;
    }

    db->err[0] = '\0';
    *out = db;
    return 0;
}

void ewguidb_close(EwGuiDb *db)
{
    if (!db)
        return;
    if (db->conn)
        PQfinish(db->conn);
    free(db);
}

const char *ewguidb_last_error(const EwGuiDb *db)
{
    if (!db)
        return "sin conexión";
    if (db->err[0])
        return db->err;
    if (db->conn)
        return PQerrorMessage(db->conn);
    return "sin conexión";
}

/* --- Utilidades internas -------------------------------------------------- */

static int run_params(EwGuiDb *db, const char *sql, int nparams,
                      const char *const *values)
{
    PGresult *res = PQexecParams(db->conn, sql, nparams, NULL, values,
                                 NULL, NULL, 0);
    ExecStatusType st = PQresultStatus(res);
    int rc = 0;

    if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
        snprintf(db->err, sizeof(db->err), "%s", PQerrorMessage(db->conn));
        rc = -1;
    } else {
        db->err[0] = '\0';
    }

    PQclear(res);
    return rc;
}

int ewguidb_exec(EwGuiDb *db, const char *sql)
{
    if (!db || !db->conn || !sql)
        return -1;
    return run_params(db, sql, 0, NULL);
}

/* NULL para cadena vacía. */
static const char *nn(const char *s)
{
    return (s && *s) ? s : NULL;
}

static const char *dbl_fmt(double v, char *buf, size_t n)
{
    snprintf(buf, n, "%.6f", v);
    return buf;
}

/* Formatea un double; NULL (SQL NULL) si es NaN. */
static const char *dbl_or_null(double v, char *buf, size_t n)
{
    if (isnan(v))
        return NULL;
    snprintf(buf, n, "%.6f", v);
    return buf;
}

/* Formatea un int; NULL (SQL NULL) si es negativo. */
static const char *int_or_null(int v, char *buf, size_t n)
{
    if (v < 0)
        return NULL;
    snprintf(buf, n, "%d", v);
    return buf;
}

static void get_str(PGresult *r, int row, int col, char *out, size_t n)
{
    if (PQgetisnull(r, row, col))
        out[0] = '\0';
    else
        snprintf(out, n, "%s", PQgetvalue(r, row, col));
}

static double get_dbl(PGresult *r, int row, int col)
{
    return PQgetisnull(r, row, col) ? NAN : atof(PQgetvalue(r, row, col));
}

static int get_int(PGresult *r, int row, int col)
{
    return PQgetisnull(r, row, col) ? -1 : atoi(PQgetvalue(r, row, col));
}

static int get_bool(PGresult *r, int row, int col)
{
    return !PQgetisnull(r, row, col) && PQgetvalue(r, row, col)[0] == 't';
}

/* --- Escritura ------------------------------------------------------------ */

/*
 * Escritura de `origin`: misma lista de columnas para el upsert normal y para el
 * de "solo si falta" (el que usa la GUI al confirmar).
 */
static int origin_write(EwGuiDb *db, const EwGuiOriginRow *row, int keep_existing)
{
    static const char *cols =
        "INSERT INTO origin (origin_id,event_id,origin_time,latitude,longitude,"
        "depth,author,software,location_algorithm,velocity_model,azimuthal_gap,"
        "station_count,standard_error,status) "
        "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14) ";
    static const char *upd =
        "ON CONFLICT (origin_id) DO UPDATE SET "
        "origin_time=EXCLUDED.origin_time,latitude=EXCLUDED.latitude,"
        "longitude=EXCLUDED.longitude,depth=EXCLUDED.depth,"
        "author=EXCLUDED.author,software=EXCLUDED.software,"
        "location_algorithm=EXCLUDED.location_algorithm,"
        "velocity_model=EXCLUDED.velocity_model,"
        "azimuthal_gap=EXCLUDED.azimuthal_gap,"
        "station_count=EXCLUDED.station_count,"
        "standard_error=EXCLUDED.standard_error,status=EXCLUDED.status";
    static const char *ins = "ON CONFLICT (origin_id) DO NOTHING";
    char sql[1024];
    char lat[32], lon[32], dep[32], gap[32], rms[32], nsta[16];
    const char *v[14];

    if (!db || !db->conn || !row)
        return -1;

    snprintf(sql, sizeof sql, "%s%s", cols, keep_existing ? ins : upd);
    v[0] = row->origin_id;
    v[1] = row->event_id;
    v[2] = row->origin_time;
    v[3] = dbl_fmt(row->latitude, lat, sizeof lat);
    v[4] = dbl_fmt(row->longitude, lon, sizeof lon);
    v[5] = dbl_fmt(row->depth, dep, sizeof dep);
    v[6] = nn(row->author);
    v[7] = nn(row->software);
    v[8] = nn(row->location_algorithm);
    v[9] = nn(row->velocity_model);
    v[10] = dbl_or_null(row->azimuthal_gap, gap, sizeof gap);
    v[11] = int_or_null(row->station_count, nsta, sizeof nsta);
    v[12] = dbl_or_null(row->standard_error, rms, sizeof rms);
    v[13] = nn(row->status);

    return run_params(db, sql, 14, v);
}

int ewguidb_upsert_event(EwGuiDb *db, const EwGuiEventRow *row)
{
    if (!db || !db->conn || !row)
        return -1;

    static const char *sql =
        "INSERT INTO event (event_id,event_type,felt,published,"
        "preferred_origin_id,preferred_magnitude_id) "
        "VALUES ($1,$2,$3,$4,$5,$6) ON CONFLICT (event_id) DO NOTHING";
    const char *v[6] = {
        row->event_id,
        nn(row->event_type),
        row->felt ? "true" : "false",
        row->published ? "true" : "false",
        nn(row->preferred_origin_id),
        nn(row->preferred_magnitude_id),
    };
    return run_params(db, sql, 6, v);
}

int ewguidb_upsert_origin(EwGuiDb *db, const EwGuiOriginRow *row)
{
    return origin_write(db, row, 0);
}

/*
 * Igual, pero NO pisa un origen que ya exista. Lo usa la GUI al confirmar una
 * solución (`csnhypodbp`): el ARC confirmado no pasa por los anillos, así que
 * hypodb no escribe ese origen y hay que crearlo — pero si YA está (la solución
 * vino del historial, o lo escribió el ingester) no es la GUI quien debe
 * pisarlo: solo aporta su magnitud y su tensor.
 */
int ewguidb_upsert_origin_if_absent(EwGuiDb *db, const EwGuiOriginRow *row)
{
    return origin_write(db, row, 1);
}

int ewguidb_upsert_magnitude(EwGuiDb *db, const EwGuiMagRow *row)
{
    if (!db || !db->conn || !row)
        return -1;

    static const char *sql =
        "INSERT INTO magnitude (magnitude_id,origin_id,magnitude_value,"
        "magnitude_type,mag_uncertainty,calculation_method,station_count) "
        "VALUES ($1,$2,$3,$4,$5,$6,$7) "
        "ON CONFLICT (magnitude_id) DO UPDATE SET "
        "magnitude_value=EXCLUDED.magnitude_value,"
        "magnitude_type=EXCLUDED.magnitude_type,"
        "mag_uncertainty=EXCLUDED.mag_uncertainty,"
        "calculation_method=EXCLUDED.calculation_method,"
        "station_count=EXCLUDED.station_count";
    char val[32], unc[32], nsta[16];
    const char *v[7] = {
        row->magnitude_id, row->origin_id,
        dbl_fmt(row->magnitude_value, val, sizeof val),
        nn(row->magnitude_type),
        dbl_or_null(row->mag_uncertainty, unc, sizeof unc),
        nn(row->calculation_method),
        int_or_null(row->station_count, nsta, sizeof nsta),
    };
    return run_params(db, sql, 7, v);
}

int ewguidb_upsert_tensor(EwGuiDb *db, const EwGuiTensorRow *row)
{
    if (!db || !db->conn || !row)
        return -1;

    static const char *sql =
        "INSERT INTO tensor (tensor_id,origin_id,method,mechanism,"
        "strike1,dip1,rake1,strike2,dip2,rake2,"
        "m_rr,m_tt,m_pp,m_rt,m_rp,m_tp,scalar_moment) "
        "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17) "
        "ON CONFLICT (tensor_id) DO UPDATE SET "
        "method=EXCLUDED.method,mechanism=EXCLUDED.mechanism,"
        "strike1=EXCLUDED.strike1,dip1=EXCLUDED.dip1,rake1=EXCLUDED.rake1,"
        "strike2=EXCLUDED.strike2,dip2=EXCLUDED.dip2,rake2=EXCLUDED.rake2,"
        "m_rr=EXCLUDED.m_rr,m_tt=EXCLUDED.m_tt,m_pp=EXCLUDED.m_pp,"
        "m_rt=EXCLUDED.m_rt,m_rp=EXCLUDED.m_rp,m_tp=EXCLUDED.m_tp,"
        "scalar_moment=EXCLUDED.scalar_moment";
    char s1[32], d1[32], r1[32], s2[32], d2[32], r2[32];
    char rr[32], tt[32], pp[32], rt[32], rp[32], tp[32], mo[32];
    const char *v[17] = {
        row->tensor_id, row->origin_id, nn(row->method), nn(row->mechanism),
        /* Los planos nodales son opcionales: NULL si el mecanismo no los trae
           (NAN es el "sin valor" de la casa, igual que el residual de un pick). */
        dbl_or_null(row->have_planes ? row->strike1 : NAN, s1, sizeof s1),
        dbl_or_null(row->have_planes ? row->dip1 : NAN, d1, sizeof d1),
        dbl_or_null(row->have_planes ? row->rake1 : NAN, r1, sizeof r1),
        dbl_or_null(row->have_planes ? row->strike2 : NAN, s2, sizeof s2),
        dbl_or_null(row->have_planes ? row->dip2 : NAN, d2, sizeof d2),
        dbl_or_null(row->have_planes ? row->rake2 : NAN, r2, sizeof r2),
        dbl_fmt(row->m_rr, rr, sizeof rr),
        dbl_fmt(row->m_tt, tt, sizeof tt),
        dbl_fmt(row->m_pp, pp, sizeof pp),
        dbl_fmt(row->m_rt, rt, sizeof rt),
        dbl_fmt(row->m_rp, rp, sizeof rp),
        dbl_fmt(row->m_tp, tp, sizeof tp),
        dbl_or_null(row->scalar_moment, mo, sizeof mo),
    };
    return run_params(db, sql, 17, v);
}

int ewguidb_replace_picks(EwGuiDb *db, const char *origin_id,
                          const EwGuiPickRow *rows, int n)
{
    if (!db || !db->conn || !origin_id || n < 0)
        return -1;
    if (ewguidb_exec(db, "BEGIN") != 0)
        return -1;

    const char *dv[1] = { origin_id };
    if (run_params(db, "DELETE FROM pick WHERE origin_id=$1", 1, dv) != 0)
        goto fail;

    static const char *sql =
        "INSERT INTO pick (pick_id,origin_id,channel,pick_time,phase,weight,"
        "pick_status,uncertainty) VALUES ($1,$2,$3,$4,$5,$6,$7,$8)";
    for (int i = 0; i < n; i++) {
        char w[32], u[32];
        const char *v[8] = {
            rows[i].pick_id, origin_id, nn(rows[i].channel),
            rows[i].pick_time, nn(rows[i].phase),
            dbl_fmt(rows[i].weight, w, sizeof w),
            rows[i].pick_status[0] ? rows[i].pick_status : "automatic",
            /* `uncertainty` guarda el residual del localizador (NULL si no hay). */
            dbl_or_null(rows[i].residual, u, sizeof u),
        };
        if (run_params(db, sql, 8, v) != 0)
            goto fail;
    }
    return ewguidb_exec(db, "COMMIT");

fail:
    ewguidb_exec(db, "ROLLBACK");
    return -1;
}

int ewguidb_replace_station_magnitudes(EwGuiDb *db, const char *magnitude_id,
                                       const EwGuiStaMagRow *rows, int n)
{
    if (!db || !db->conn || !magnitude_id || n < 0)
        return -1;
    if (ewguidb_exec(db, "BEGIN") != 0)
        return -1;

    const char *dv[1] = { magnitude_id };
    if (run_params(db, "DELETE FROM station_magnitude WHERE magnitude_id=$1",
                   1, dv) != 0)
        goto fail;

    static const char *sql =
        "INSERT INTO station_magnitude (station_magnitude_id,magnitude_id,"
        "channel,read_amplitude,calculated_magnitude,amplitude_type) "
        "VALUES ($1,$2,$3,$4,$5,$6)";
    for (int i = 0; i < n; i++) {
        char amp[32], mag[32];
        const char *v[6] = {
            rows[i].station_magnitude_id, magnitude_id, nn(rows[i].channel),
            dbl_or_null(rows[i].read_amplitude, amp, sizeof amp),
            dbl_or_null(rows[i].calculated_magnitude, mag, sizeof mag),
            nn(rows[i].amplitude_type),
        };
        if (run_params(db, sql, 6, v) != 0)
            goto fail;
    }
    return ewguidb_exec(db, "COMMIT");

fail:
    ewguidb_exec(db, "ROLLBACK");
    return -1;
}

int ewguidb_set_preferred(EwGuiDb *db, const char *event_id,
                          const char *origin_id, const char *magnitude_id)
{
    if (!db || !db->conn || !event_id)
        return -1;

    static const char *sql =
        "UPDATE event SET preferred_origin_id=$2, preferred_magnitude_id=$3 "
        "WHERE event_id=$1";
    const char *v[3] = { event_id, nn(origin_id), nn(magnitude_id) };
    return run_params(db, sql, 3, v);
}

/* --- Lectura -------------------------------------------------------------- */

int ewguidb_query_recent_events(EwGuiDb *db, int hours,
                                EwGuiEventRow **out, int *n)
{
    if (!db || !db->conn || !out || !n)
        return -1;
    *out = NULL;
    *n = 0;

    char hbuf[16];
    snprintf(hbuf, sizeof hbuf, "%d", hours < 0 ? 0 : hours);
    const char *v[1] = { hbuf };

    PGresult *res = PQexecParams(db->conn,
        "SELECT event_id,event_type,felt,published,preferred_origin_id,"
        "preferred_magnitude_id FROM event "
        "WHERE creation_time >= now() - ($1::int * interval '1 hour') "
        "ORDER BY creation_time DESC, event_id", 1, NULL, v, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        snprintf(db->err, sizeof(db->err), "%s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    int rows = PQntuples(res);
    EwGuiEventRow *arr = NULL;
    if (rows > 0) {
        arr = calloc((size_t)rows, sizeof *arr);
        if (!arr) {
            PQclear(res);
            return -1;
        }
        for (int i = 0; i < rows; i++) {
            get_str(res, i, 0, arr[i].event_id, sizeof arr[i].event_id);
            get_str(res, i, 1, arr[i].event_type, sizeof arr[i].event_type);
            arr[i].felt = get_bool(res, i, 2);
            arr[i].published = get_bool(res, i, 3);
            get_str(res, i, 4, arr[i].preferred_origin_id,
                    sizeof arr[i].preferred_origin_id);
            get_str(res, i, 5, arr[i].preferred_magnitude_id,
                    sizeof arr[i].preferred_magnitude_id);
        }
    }
    PQclear(res);
    *out = arr;
    *n = rows;
    return rows;
}

int ewguidb_query_map_events(EwGuiDb *db, int hours,
                             EwGuiMapEvent **out, int *n)
{
    if (!db || !db->conn || !out || !n)
        return -1;
    *out = NULL;
    *n = 0;

    char hbuf[16];
    snprintf(hbuf, sizeof hbuf, "%d", hours < 0 ? 0 : hours);
    const char *v[1] = { hbuf };

    PGresult *res = PQexecParams(db->conn,
        "SELECT e.event_id,e.preferred_origin_id,o.origin_time,o.latitude,"
        "o.longitude,o.depth,COALESCE(m.magnitude_value,0.0),"
        "COALESCE(m.magnitude_type,'') "
        "FROM event e JOIN origin o ON o.origin_id=e.preferred_origin_id "
        "LEFT JOIN magnitude m ON m.magnitude_id=e.preferred_magnitude_id "
        "WHERE e.creation_time >= now() - ($1::int * interval '1 hour') "
        "ORDER BY o.origin_time DESC, e.event_id", 1, NULL, v, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        snprintf(db->err, sizeof(db->err), "%s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    int rows = PQntuples(res);
    EwGuiMapEvent *arr = NULL;
    if (rows > 0) {
        arr = calloc((size_t)rows, sizeof *arr);
        if (!arr) {
            PQclear(res);
            return -1;
        }
        for (int i = 0; i < rows; i++) {
            get_str(res, i, 0, arr[i].event_id, sizeof arr[i].event_id);
            get_str(res, i, 1, arr[i].origin_id, sizeof arr[i].origin_id);
            get_str(res, i, 2, arr[i].origin_time, sizeof arr[i].origin_time);
            arr[i].latitude = get_dbl(res, i, 3);
            arr[i].longitude = get_dbl(res, i, 4);
            arr[i].depth = get_dbl(res, i, 5);
            arr[i].magnitude = get_dbl(res, i, 6);
            get_str(res, i, 7, arr[i].magnitude_type,
                    sizeof arr[i].magnitude_type);
        }
    }
    PQclear(res);
    *out = arr;
    *n = rows;
    return rows;
}

int ewguidb_search_events(EwGuiDb *db, const EwGuiSearchFilter *f,
                          EwGuiMapEvent **out, int *n)
{
    if (!db || !db->conn || !f || !out || !n)
        return -1;
    *out = NULL;
    *n = 0;

    if (!f->time_min || !*f->time_min || !f->time_max || !*f->time_max)
        return -1;  /* las fechas son obligatorias */

    if ((f->have_lat   && f->lat_min   > f->lat_max) ||
        (f->have_lon   && f->lon_min   > f->lon_max) ||
        (f->have_depth && f->depth_min > f->depth_max) ||
        (f->have_mag   && f->mag_min   > f->mag_max))
        return -1;

    int limit = f->limit > 0 ? f->limit : 1000;

    const char *v[11];
    char vbuf[10][40];
    int np = 0, nbuf = 0;

    char sql[1024];
    int off = snprintf(sql, sizeof sql,
        "SELECT e.event_id,e.preferred_origin_id,o.origin_time,o.latitude,"
        "o.longitude,o.depth,COALESCE(m.magnitude_value,0.0),"
        "COALESCE(m.magnitude_type,'') "
        "FROM event e JOIN origin o ON o.origin_id=e.preferred_origin_id "
        "LEFT JOIN magnitude m ON m.magnitude_id=e.preferred_magnitude_id "
        "WHERE o.origin_time >= $1::timestamptz "
        "AND o.origin_time <= $2::timestamptz");

    v[0] = f->time_min;
    v[1] = f->time_max;
    np = 2;

    if (f->have_lat) {
        snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%.6f", f->lat_min);
        v[np++] = vbuf[nbuf++];
        snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%.6f", f->lat_max);
        v[np++] = vbuf[nbuf++];
        off += snprintf(sql + off, sizeof sql - off,
                        " AND o.latitude >= $%d AND o.latitude <= $%d",
                        np - 1, np);
    }
    if (f->have_lon) {
        snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%.6f", f->lon_min);
        v[np++] = vbuf[nbuf++];
        snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%.6f", f->lon_max);
        v[np++] = vbuf[nbuf++];
        off += snprintf(sql + off, sizeof sql - off,
                        " AND o.longitude >= $%d AND o.longitude <= $%d",
                        np - 1, np);
    }
    if (f->have_depth) {
        snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%.6f", f->depth_min);
        v[np++] = vbuf[nbuf++];
        snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%.6f", f->depth_max);
        v[np++] = vbuf[nbuf++];
        off += snprintf(sql + off, sizeof sql - off,
                        " AND o.depth >= $%d AND o.depth <= $%d",
                        np - 1, np);
    }
    if (f->have_mag) {
        snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%.6f", f->mag_min);
        v[np++] = vbuf[nbuf++];
        snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%.6f", f->mag_max);
        v[np++] = vbuf[nbuf++];
        off += snprintf(sql + off, sizeof sql - off,
                        " AND m.magnitude_value >= $%d AND m.magnitude_value <= $%d",
                        np - 1, np);
    }

    snprintf(vbuf[nbuf], sizeof vbuf[nbuf], "%d", limit);
    v[np++] = vbuf[nbuf++];
    snprintf(sql + off, sizeof sql - off,
             " ORDER BY o.origin_time DESC, e.event_id LIMIT $%d", np);

    PGresult *res = PQexecParams(db->conn, sql, np, NULL, v, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        snprintf(db->err, sizeof(db->err), "%s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    int rows = PQntuples(res);
    EwGuiMapEvent *arr = NULL;
    if (rows > 0) {
        arr = calloc((size_t)rows, sizeof *arr);
        if (!arr) {
            PQclear(res);
            return -1;
        }
        for (int i = 0; i < rows; i++) {
            get_str(res, i, 0, arr[i].event_id, sizeof arr[i].event_id);
            get_str(res, i, 1, arr[i].origin_id, sizeof arr[i].origin_id);
            get_str(res, i, 2, arr[i].origin_time, sizeof arr[i].origin_time);
            arr[i].latitude = get_dbl(res, i, 3);
            arr[i].longitude = get_dbl(res, i, 4);
            arr[i].depth = get_dbl(res, i, 5);
            arr[i].magnitude = get_dbl(res, i, 6);
            get_str(res, i, 7, arr[i].magnitude_type,
                    sizeof arr[i].magnitude_type);
        }
    }
    PQclear(res);
    *out = arr;
    *n = rows;
    return rows;
}

int ewguidb_query_origins(EwGuiDb *db, const char *event_id,
                          EwGuiOriginRow **out, int *n)
{
    if (!db || !db->conn || !event_id || !out || !n)
        return -1;
    *out = NULL;
    *n = 0;

    const char *v[1] = { event_id };
    PGresult *res = PQexecParams(db->conn,
        "SELECT origin_id,event_id,origin_time,latitude,longitude,depth,"
        "author,software,location_algorithm,velocity_model,azimuthal_gap,"
        "station_count,standard_error,status,creation_time FROM origin "
        "WHERE event_id=$1 ORDER BY creation_time DESC, origin_time DESC, origin_id",
        1, NULL, v, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        snprintf(db->err, sizeof(db->err), "%s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    int rows = PQntuples(res);
    EwGuiOriginRow *arr = NULL;
    if (rows > 0) {
        arr = calloc((size_t)rows, sizeof *arr);
        if (!arr) {
            PQclear(res);
            return -1;
        }
        for (int i = 0; i < rows; i++) {
            get_str(res, i, 0, arr[i].origin_id, sizeof arr[i].origin_id);
            get_str(res, i, 1, arr[i].event_id, sizeof arr[i].event_id);
            get_str(res, i, 2, arr[i].origin_time, sizeof arr[i].origin_time);
            arr[i].latitude = get_dbl(res, i, 3);
            arr[i].longitude = get_dbl(res, i, 4);
            arr[i].depth = get_dbl(res, i, 5);
            get_str(res, i, 6, arr[i].author, sizeof arr[i].author);
            get_str(res, i, 7, arr[i].software, sizeof arr[i].software);
            get_str(res, i, 8, arr[i].location_algorithm,
                    sizeof arr[i].location_algorithm);
            get_str(res, i, 9, arr[i].velocity_model,
                    sizeof arr[i].velocity_model);
            arr[i].azimuthal_gap = get_dbl(res, i, 10);
            arr[i].station_count = get_int(res, i, 11);
            arr[i].standard_error = get_dbl(res, i, 12);
            get_str(res, i, 13, arr[i].status, sizeof arr[i].status);
            get_str(res, i, 14, arr[i].creation_time, sizeof arr[i].creation_time);
        }
    }
    PQclear(res);
    *out = arr;
    *n = rows;
    return rows;
}

int ewguidb_query_picks(EwGuiDb *db, const char *origin_id,
                        EwGuiPickRow **out, int *n)
{
    if (!db || !db->conn || !origin_id || !out || !n)
        return -1;
    *out = NULL;
    *n = 0;

    const char *v[1] = { origin_id };
    PGresult *res = PQexecParams(db->conn,
        "SELECT pick_id,origin_id,channel,pick_time,phase,weight,pick_status,"
        "uncertainty "
        "FROM pick WHERE origin_id=$1 ORDER BY pick_time, pick_id",
        1, NULL, v, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        snprintf(db->err, sizeof(db->err), "%s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    int rows = PQntuples(res);
    EwGuiPickRow *arr = NULL;
    if (rows > 0) {
        arr = calloc((size_t)rows, sizeof *arr);
        if (!arr) {
            PQclear(res);
            return -1;
        }
        for (int i = 0; i < rows; i++) {
            get_str(res, i, 0, arr[i].pick_id, sizeof arr[i].pick_id);
            get_str(res, i, 1, arr[i].origin_id, sizeof arr[i].origin_id);
            get_str(res, i, 2, arr[i].channel, sizeof arr[i].channel);
            get_str(res, i, 3, arr[i].pick_time, sizeof arr[i].pick_time);
            get_str(res, i, 4, arr[i].phase, sizeof arr[i].phase);
            arr[i].weight = get_dbl(res, i, 5);
            get_str(res, i, 6, arr[i].pick_status, sizeof arr[i].pick_status);
            arr[i].residual = get_dbl(res, i, 7);   /* uncertainty; NULL -> NAN */
        }
    }
    PQclear(res);
    *out = arr;
    *n = rows;
    return rows;
}

int ewguidb_query_magnitudes(EwGuiDb *db, const char *origin_id,
                             EwGuiMagRow **out, int *n)
{
    if (!db || !db->conn || !origin_id || !out || !n)
        return -1;
    *out = NULL;
    *n = 0;

    const char *v[1] = { origin_id };
    PGresult *res = PQexecParams(db->conn,
        "SELECT magnitude_id,origin_id,magnitude_value,magnitude_type,"
        "mag_uncertainty,calculation_method,station_count FROM magnitude "
        "WHERE origin_id=$1 ORDER BY magnitude_type, magnitude_id",
        1, NULL, v, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        snprintf(db->err, sizeof(db->err), "%s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    int rows = PQntuples(res);
    EwGuiMagRow *arr = NULL;
    if (rows > 0) {
        arr = calloc((size_t)rows, sizeof *arr);
        if (!arr) {
            PQclear(res);
            return -1;
        }
        for (int i = 0; i < rows; i++) {
            get_str(res, i, 0, arr[i].magnitude_id,
                    sizeof arr[i].magnitude_id);
            get_str(res, i, 1, arr[i].origin_id, sizeof arr[i].origin_id);
            arr[i].magnitude_value = get_dbl(res, i, 2);
            get_str(res, i, 3, arr[i].magnitude_type,
                    sizeof arr[i].magnitude_type);
            arr[i].mag_uncertainty = get_dbl(res, i, 4);
            get_str(res, i, 5, arr[i].calculation_method,
                    sizeof arr[i].calculation_method);
            arr[i].station_count = get_int(res, i, 6);
        }
    }
    PQclear(res);
    *out = arr;
    *n = rows;
    return rows;
}

int ewguidb_query_station_magnitudes(EwGuiDb *db, const char *magnitude_id,
                                     EwGuiStaMagRow **out, int *n)
{
    if (!db || !db->conn || !magnitude_id || !out || !n)
        return -1;
    *out = NULL;
    *n = 0;

    const char *v[1] = { magnitude_id };
    PGresult *res = PQexecParams(db->conn,
        "SELECT station_magnitude_id,magnitude_id,channel,read_amplitude,"
        "calculated_magnitude,amplitude_type FROM station_magnitude "
        "WHERE magnitude_id=$1 ORDER BY channel, station_magnitude_id",
        1, NULL, v, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        snprintf(db->err, sizeof(db->err), "%s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    int rows = PQntuples(res);
    EwGuiStaMagRow *arr = NULL;
    if (rows > 0) {
        arr = calloc((size_t)rows, sizeof *arr);
        if (!arr) {
            PQclear(res);
            return -1;
        }
        for (int i = 0; i < rows; i++) {
            get_str(res, i, 0, arr[i].station_magnitude_id,
                    sizeof arr[i].station_magnitude_id);
            get_str(res, i, 1, arr[i].magnitude_id,
                    sizeof arr[i].magnitude_id);
            get_str(res, i, 2, arr[i].channel, sizeof arr[i].channel);
            arr[i].read_amplitude = get_dbl(res, i, 3);
            arr[i].calculated_magnitude = get_dbl(res, i, 4);
            get_str(res, i, 5, arr[i].amplitude_type,
                    sizeof arr[i].amplitude_type);
        }
    }
    PQclear(res);
    *out = arr;
    *n = rows;
    return rows;
}

void ewguidb_rows_free(void *rows)
{
    free(rows);
}

