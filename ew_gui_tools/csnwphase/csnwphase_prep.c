#include "csnwphase.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Generación del run dir del motor (i_master + CMTSOLUTION) y de la tabla de
 * parámetros que la GUI muestra antes de ejecutar.
 *
 * El formato de i_master y CMTSOLUTION reproduce BYTE A BYTE el generador del
 * CSN (fasew/wphase/libdir/i_master.py e cmtsolution.py) porque los binarios
 * del motor los leen con strncmp/sscanf y porque así la tabla de parámetros y
 * los archivos comparten una sola fuente de verdad. Los fixtures de
 * tests/data/ se generan con el Python original (ver tools/gen_fixtures.sh).
 */

/* ------------------------------------------------------------------ */
/* Formato numérico                                                     */
/* ------------------------------------------------------------------ */

/* str() de Python para un float "limpio": 5 -> "5.0", 0.1 -> "0.1". */
static void py_num(char *dst, size_t n, double v)
{
    snprintf(dst, n, "%g", v);
    if (!strpbrk(dst, ".eEni") && strlen(dst) + 2 < n)
        strcat(dst, ".0");
}

/*
 * Campo numérico de ancho fijo como {x:14.4} de Python: presentación 'g' con
 * `prec` cifras significativas, alineado a la derecha, y con al menos un dígito
 * tras el punto decimal (Python lo garantiza; %g no).
 */
static void py_g_field(char *dst, size_t n, double v, int width, int prec)
{
    char num[64];

    snprintf(num, sizeof(num), "%.*g", prec, v);
    if (!strpbrk(num, ".eEni") && strlen(num) + 2 < sizeof(num))
        strcat(num, ".0");
    snprintf(dst, n, "%*s", width, num);
}

/* Banda de la inversión por magnitud (i_master.py:31-40). */
static void band_for_magnitude(double mag, double *cf1, double *cf2)
{
    if (mag >= 8.0) {
        *cf1 = 0.001;
        *cf2 = 0.005;
    } else if (mag >= 7.5) {
        *cf1 = 0.0017;
        *cf2 = 0.0067;
    } else if (mag >= 7.0) {
        *cf1 = 0.002;
        *cf2 = 0.0083;
    } else if (mag >= 6.5) {
        *cf1 = 0.004;
        *cf2 = 0.01;
    } else {
        *cf1 = 0.0067;
        *cf2 = 0.02;
    }
}

/* Media duración / time shift del CMT semilla (cmtsolution.py:39-41). */
static double half_duration(double magnitude)
{
    return (1.2 * pow(10.0, -8.0)) *
           pow(pow(10.0, 1.5 * (magnitude + 10.73)), 1.0 / 3.0);
}

/* ------------------------------------------------------------------ */
/* i_master y CMTSOLUTION                                               */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Guardas de preparacion: ABI y coherencia del i_master               */
/* ------------------------------------------------------------------ */

/*
 * Guardia de ABI. El consumidor (p. ej. el panel `Mww`) compila sus objetos con
 * el `csnwphase.h` que tenga a mano; si es mas viejo que el de esta libreria, los
 * structs tienen OTRO layout y aqui se lee memoria ajena: el i_master salia con
 * los `int` del evento (ano/mes/dia/hora) leidos como `double` y con lat/lon/prof
 * en WP_WIN, y el motor moria mas tarde con un error opaco (`fc=-nan` ->
 * `coeffs_rec_lut` vacio -> `run.sh` rc!=0). El consumidor llama aqui con SUS
 * tamanos y se compara.
 */
int csnwphase_abi_check(size_t cfg_size, size_t in_size)
{
    return (cfg_size == sizeof(CsnwphaseCfg)
            && in_size == sizeof(CsnwphaseInput)) ? 0 : -1;
}

size_t csnwphase_cfg_size(void)   { return sizeof(CsnwphaseCfg); }
size_t csnwphase_input_size(void) { return sizeof(CsnwphaseInput); }

static int near_val(double a, double b)
{
    double d = a - b;

    if (d < 0)
        d = -d;
    return d <= 1e-6 * (1.0 + (b < 0 ? -b : b));
}

static const char *imaster_line(const char *text, const char *tag)
{
    const char *p = text;
    size_t n = strlen(tag);

    while (p && *p) {
        if (strncmp(p, tag, n) == 0)
            return p;
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    return NULL;
}

static int imaster_bad(char *err, size_t errlen, const char *what)
{
    if (err && errlen)
        snprintf(err, errlen,
                 "i_master inconsistente: %s (¿objetos desfasados? recompila el "
                 "modulo con `make clean && make`)", what);
    return -1;
}

/*
 * Compara los valores numericos del i_master recien generado con la config. Si no
 * coinciden, el texto NO salio de esta config (memoria ajena) y no tiene sentido
 * correr el motor: fallaria mucho despues y sin explicacion.
 */
int csnwphase_i_master_selfcheck(const CsnwphaseCfg *cfg, const char *text,
                                 char *err, size_t errlen)
{
    const char *line;
    double a, b, c;
    int nf;

    if (err && errlen)
        err[0] = '\0';
    if (!cfg || !text)
        return imaster_bad(err, errlen, "sin configuracion");

    line = imaster_line(text, "IDEC_2:");
    if (!line || sscanf(line, "IDEC_2: %lf %lf %lf", &a, &b, &c) != 3)
        return imaster_bad(err, errlen, "IDEC_2 ilegible");
    if (!near_val(a, cfg->idec2_idtr) || !near_val(b, cfg->idec2_preevent)
        || !near_val(c, cfg->idec2_fend))
        return imaster_bad(err, errlen, "IDEC_2 no coincide con el .d");

    line = imaster_line(text, "IDEC_3:");
    if (!line || sscanf(line, "IDEC_3: %lf %lf %d %lf", &a, &b, &nf, &c) != 4)
        return imaster_bad(err, errlen, "IDEC_3 ilegible");
    if (!near_val(a, cfg->idec3_fl) || !near_val(b, cfg->idec3_fh)
        || nf != cfg->idec3_nf || !near_val(c, cfg->idec3_tol))
        return imaster_bad(err, errlen, "IDEC_3 no coincide con el .d");

    line = imaster_line(text, "WP_WIN:");
    if (!line || sscanf(line, "WP_WIN: %lf %lf %lf", &a, &b, &c) != 3)
        return imaster_bad(err, errlen, "WP_WIN ilegible");
    if (!near_val(a, cfg->wpwin[0]) || !near_val(b, cfg->wpwin[1])
        || !near_val(c, cfg->wpwin[2]))
        return imaster_bad(err, errlen, "WP_WIN no coincide con el .d");

    return 0;
}

int csnwphase_i_master_text(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                            char *out, size_t outsz)
{
    double cf1, cf2;
    char dmin[32], dmax[32], idtr[32], pre[32], fend[32];
    char fl[32], fh[32], tol[32], tend[32];
    char evname[64];
    int n = 0;

    if (!cfg || !out || outsz == 0)
        return -1;

    band_for_magnitude(in ? in->magnitude : 6.5, &cf1, &cf2);
    py_num(dmin, sizeof(dmin), cfg->dmin_deg);
    py_num(dmax, sizeof(dmax), cfg->dmax_deg);
    /* IDEC_2/IDEC_3 son *strings* en el generador del CSN: se re-renderizan
       con %g (2 280  0.1), no con el formateador de float de Python. */
    snprintf(idtr, sizeof(idtr), "%g", cfg->idec2_idtr);
    snprintf(pre, sizeof(pre), "%g", cfg->idec2_preevent);
    snprintf(fend, sizeof(fend), "%g", cfg->idec2_fend);
    snprintf(fl, sizeof(fl), "%g", cfg->idec3_fl);
    snprintf(fh, sizeof(fh), "%g", cfg->idec3_fh);
    snprintf(tol, sizeof(tol), "%g", cfg->idec3_tol);
    snprintf(tend, sizeof(tend), "%g.", cfg->wpwin[0]);
    snprintf(evname, sizeof(evname), "%s",
             (in && in->location[0]) ? in->location : cfg->evname);

    /* i_master.py:17-29 (espacios y saltos de línea literales). */
    n += snprintf(out + n, outsz - (size_t)n, "EVNAME:     %s\n", evname);
    n += snprintf(out + n, outsz - (size_t)n, "SEED:       %s\n", cfg->seed_ref);
    n += snprintf(out + n, outsz - (size_t)n, "DMIN:       %s\n", dmin);
    n += snprintf(out + n, outsz - (size_t)n, "DMAX:       %s\n", dmax);
    n += snprintf(out + n, outsz - (size_t)n, "CMTFILE:    %s\n\n", cfg->cmtfile);
    n += snprintf(out + n, outsz - (size_t)n, "filt_order: %d\n", cfg->filt_order);
    n += snprintf(out + n, outsz - (size_t)n, "filt_cf1:   %g\n", cf1);
    n += snprintf(out + n, outsz - (size_t)n, "filt_cf2:   %g\n", cf2);
    n += snprintf(out + n, outsz - (size_t)n, "filt_pass:  %d\n", cfg->filt_pass);
    n += snprintf(out + n, outsz - (size_t)n, "IDEC_2:  %s %s  %s\n",
                  idtr, pre, fend);
    n += snprintf(out + n, outsz - (size_t)n, "IDEC_3:  %s  %s  %d  %s\n\n",
                  fl, fh, cfg->idec3_nf, tol);
    n += snprintf(out + n, outsz - (size_t)n, "GFDIR:   %s\n", cfg->gfdir_rel);
    n += snprintf(out + n, outsz - (size_t)n, "WP_WIN:  %s %.1f %.1f\n",
                  tend, cfg->wpwin[1], cfg->wpwin[2]);

    if ((size_t)n >= outsz)
        return -1;

    return n;
}

int csnwphase_cmt_text(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                       char *out, size_t outsz)
{
    double hd;
    char latg[64];
    char loc[64];
    int n = 0;

    if (!cfg || !in || !out || outsz == 0)
        return -1;

    hd = half_duration(in->magnitude);
    py_g_field(latg, sizeof(latg), in->latitude, 14, 4);
    snprintf(loc, sizeof(loc), "%s", in->location[0] ? in->location : cfg->evname);

    /* cmtsolution.py:18-34 (formatos literales). */
    n += snprintf(out + n, outsz - (size_t)n,
                  " PDE %4d %2d %2d %2d %2d %5.2f %8.4f %9.4f %5.1f %3.1f %3.1f %s\n",
                  in->year, in->month, in->day, in->hour, in->minute, in->second,
                  in->latitude, in->longitude, in->depth_km, in->magnitude,
                  in->magnitude, loc);
    n += snprintf(out + n, outsz - (size_t)n, "event name:      %-15s\n", loc);
    n += snprintf(out + n, outsz - (size_t)n, "time shift:%12.4f\n", hd);
    n += snprintf(out + n, outsz - (size_t)n, "half duration:%9.4f\n", hd);
    n += snprintf(out + n, outsz - (size_t)n, "latitude:%s\n", latg);
    n += snprintf(out + n, outsz - (size_t)n, "longitude:%13.4f\n", in->longitude);
    n += snprintf(out + n, outsz - (size_t)n, "depth:%17.4f\n", in->depth_km);
    n += snprintf(out + n, outsz - (size_t)n, "Mrr:     %14.6f\n", 0.0);
    n += snprintf(out + n, outsz - (size_t)n, "Mtt:     %14.6f\n", 0.0);
    n += snprintf(out + n, outsz - (size_t)n, "Mpp:     %14.6f\n", 0.0);
    n += snprintf(out + n, outsz - (size_t)n, "Mrt:     %14.6f\n", 0.0);
    n += snprintf(out + n, outsz - (size_t)n, "Mrp:     %14.6f\n", 0.0);
    n += snprintf(out + n, outsz - (size_t)n, "Mtp:     %14.6e\n", 0.0);

    if ((size_t)n >= outsz)
        return -1;

    return n;
}

/* ------------------------------------------------------------------ */
/* Guard anti-borrado de la base de funciones de Green                  */
/* ------------------------------------------------------------------ */

/* Normaliza lexicográficamente (resuelve "." y "..") y quita el '/' final. */
static void path_normalize(const char *in, char *out, size_t n)
{
    int absolute = (in && in[0] == '/');
    char buf[1024];
    char *comp[64];
    int ncomp = 0;
    int i;
    size_t len = 0;
    char *tok, *save = NULL;

    if (!in || !out || n == 0)
        return;

    snprintf(buf, sizeof(buf), "%s", in);
    for (tok = strtok_r(buf, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (strcmp(tok, ".") == 0 || tok[0] == '\0')
            continue;
        if (strcmp(tok, "..") == 0) {
            if (ncomp > 0)
                ncomp--;
            continue;
        }
        if (ncomp < (int)(sizeof(comp) / sizeof(comp[0])))
            comp[ncomp++] = tok;
    }

    out[0] = '\0';
    if (absolute) {
        snprintf(out, n, "/");
        len = 1;
    }
    for (i = 0; i < ncomp; i++) {
        len = strlen(out);
        if (len > 0 && out[len - 1] != '/')
            snprintf(out + len, n - len, "/");
        len = strlen(out);
        snprintf(out + len, n - len, "%s", comp[i]);
    }

    if (out[0] == '\0')
        snprintf(out, n, absolute ? "/" : ".");
}

int csnwphase_gfdir_guard(const CsnwphaseCfg *cfg, const char *rundir,
                          char *err, size_t errlen)
{
    char base[1024], cand[1024], joined[1024];
    const char *slash;

    if (err && errlen)
        err[0] = '\0';

    if (!cfg || !cfg->gf_dir[0] || !cfg->gfdir_rel[0]) {
        if (err && errlen)
            snprintf(err, errlen, "GfDir/GfdirRel sin definir");
        return -1;
    }

    /* GFDIR debe ser relativo al run dir: prep_kernels hace crea_dir() ahí. */
    if (cfg->gfdir_rel[0] == '/') {
        if (err && errlen)
            snprintf(err, errlen, "GfdirRel debe ser relativo al run dir, no '%s'",
                     cfg->gfdir_rel);
        return -1;
    }

    if (access(cfg->gf_dir, R_OK | X_OK) != 0) {
        if (err && errlen)
            snprintf(err, errlen, "GfDir no accesible: %s", cfg->gf_dir);
        return -1;
    }

    path_normalize(cfg->gf_dir, base, sizeof(base));

    /* Rechazo directo: el nombre de GFDIR apunta al directorio de la base. */
    slash = strrchr(base, '/');
    if (slash && strcmp(cfg->gfdir_rel, slash + 1) == 0) {
        if (err && errlen)
            snprintf(err, errlen,
                     "GfdirRel '%s' es la propia base de Green (prep_kernels la borraria)",
                     cfg->gfdir_rel);
        return -1;
    }

    if (rundir && rundir[0]) {
        snprintf(joined, sizeof(joined), "%s/%s", rundir, cfg->gfdir_rel);
        path_normalize(joined, cand, sizeof(cand));
        if (strcmp(cand, base) == 0) {
            if (err && errlen)
                snprintf(err, errlen,
                         "GfdirRel resuelve a la base de Green (%s): abortado antes de crea_dir()",
                         base);
            return -1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Escritura del run dir                                                */
/* ------------------------------------------------------------------ */

/*
 * Crea `path` y TODOS sus padres (mkdir -p). El run dir del panel `Mww` cuelga
 * de `MwwWorkDir`, que puede no existir en la primera corrida: con un mkdir de
 * un solo nivel la preparacion fallaba y el panel se quedaba sin resultado.
 */
int csnwphase_mkdir_p(const char *path)
{
    char tmp[1024];
    char *p;

    if (!path || !path[0] || strlen(path) >= sizeof(tmp))
        return -1;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) == 0)
        return 0;
    {
        struct stat st;

        /* Ya existia como DIRECTORIO: vale. Si es un fichero, es un error. */
        return (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
    }
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    if (!f)
        return -1;
    if (fputs(text, f) == EOF || fclose(f) != 0) {
        fclose(f);
        return -1;
    }
    return 0;
}

int csnwphase_prep_write(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                         const char *rundir, char *err, size_t errlen)
{
    char path[1024], text[8192];

    if (err && errlen)
        err[0] = '\0';

    if (!cfg || !in || !rundir || !rundir[0]) {
        if (err && errlen)
            snprintf(err, errlen, "argumentos invalidos");
        return -1;
    }

    if (csnwphase_gfdir_guard(cfg, rundir, err, errlen) != 0)
        return -1;

    if (csnwphase_mkdir_p(rundir) != 0) {
        if (err && errlen)
            snprintf(err, errlen, "no se pudo crear el run dir %s", rundir);
        return -1;
    }

    if (csnwphase_i_master_text(cfg, in, text, sizeof(text)) < 0) {
        if (err && errlen)
            snprintf(err, errlen, "i_master demasiado largo");
        return -1;
    }
    if (csnwphase_i_master_selfcheck(cfg, text, err, errlen) != 0)
        return -1;
    snprintf(path, sizeof(path), "%s/i_master", rundir);
    if (write_text(path, text) != 0) {
        if (err && errlen)
            snprintf(err, errlen, "no se pudo escribir %s", path);
        return -1;
    }

    if (csnwphase_cmt_text(cfg, in, text, sizeof(text)) < 0) {
        if (err && errlen)
            snprintf(err, errlen, "CMTSOLUTION demasiado largo");
        return -1;
    }
    snprintf(path, sizeof(path), "%s/%s", rundir, cfg->cmtfile);
    if (write_text(path, text) != 0) {
        if (err && errlen)
            snprintf(err, errlen, "no se pudo escribir %s", path);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Tabla de parámetros                                                  */
/* ------------------------------------------------------------------ */

static void row(CsnwphaseParamRow *rows, int max, int *n,
                const char *name, const char *source, const char *fmt, ...)
{
    va_list ap;

    if (*n >= max)
        return;
    snprintf(rows[*n].name, sizeof(rows[*n].name), "%s", name);
    snprintf(rows[*n].source, sizeof(rows[*n].source), "%s", source);
    va_start(ap, fmt);
    vsnprintf(rows[*n].value, sizeof(rows[*n].value), fmt, ap);
    va_end(ap);
    (*n)++;
}

int csnwphase_params_build(const CsnwphaseCfg *cfg, const CsnwphaseInput *in,
                           CsnwphaseParamRow *rows, int max, int *n)
{
    double cf1, cf2;

    if (!cfg || !in || !rows || !n || max <= 0)
        return -1;

    *n = 0;
    band_for_magnitude(in->magnitude, &cf1, &cf2);

    row(rows, max, n, "evento", "ARC", "%s",
        in->event_id[0] ? in->event_id : "(sin id)");
    row(rows, max, n, "origin_id (semilla)", "ARC", "%s",
        in->origin_id[0] ? in->origin_id : "(sin id)");
    row(rows, max, n, "origen t0 (UTC)", "ARC", "%04d-%02d-%02d %02d:%02d:%05.2f",
        in->year, in->month, in->day, in->hour, in->minute, in->second);
    row(rows, max, n, "lat / lon [deg]", "ARC", "%.4f / %.4f",
        in->latitude, in->longitude);
    row(rows, max, n, "profundidad [km]", "ARC", "%.1f", in->depth_km);
    row(rows, max, n, "magnitud semilla", "ARC", "%.1f", in->magnitude);
    row(rows, max, n, "banda de inversion [Hz]", "derivado de mag",
        "%g - %g", cf1, cf2);
    row(rows, max, n, "filt_order / filt_pass", "config", "%d / %d",
        cfg->filt_order, cfg->filt_pass);
    row(rows, max, n, "IDEC_2 (idtr preevent fend)", "config", "%g %g  %g",
        cfg->idec2_idtr, cfg->idec2_preevent, cfg->idec2_fend);
    row(rows, max, n, "IDEC_3 (fl fh nf tol)", "config", "%g  %g  %d  %g",
        cfg->idec3_fl, cfg->idec3_fh, cfg->idec3_nf, cfg->idec3_tol);
    row(rows, max, n, "WP_WIN (t_end clip1 clip2)", "config", "%g. %.1f %.1f",
        cfg->wpwin[0], cfg->wpwin[1], cfg->wpwin[2]);
    row(rows, max, n, "DMIN / DMAX [deg]", "config", "%g / %g",
        cfg->dmin_deg, cfg->dmax_deg);
    row(rows, max, n, "ventana de ondas [s]", "config", "t0-%d / t0+%d",
        cfg->win_pre_sec, cfg->win_post_sec);
    row(rows, max, n, "GFDIR (relativo al run dir)", "config", "%s",
        cfg->gfdir_rel);
    row(rows, max, n, "GF_PATH (base de Green)", "config", "%s", cfg->gf_dir);
    row(rows, max, n, "screening RMS (-th)", "config", "%g %g %g",
        cfg->screen_th[0], cfg->screen_th[1], cfg->screen_th[2]);
    row(rows, max, n, "screening ratio (-nr)", "config", "%g", cfg->screen_nr);
    if (cfg->grid_search) {
        row(rows, max, n, "grid search ts [s] (min paso max)", "config", "%g %g %g",
            cfg->gs_ts_min, cfg->gs_ts_dt, cfg->gs_ts_max);
        row(rows, max, n, "grid search ts (iteraciones)", "config", "%d", cfg->gs_ts_nit);
        row(rows, max, n, "grid search xy (paso/Nx/Nopt)", "config", "%g %d %d",
            cfg->gs_xy_dx, cfg->gs_xy_nx, cfg->gs_xy_nopt);
        row(rows, max, n, "grid search xy (iteraciones)", "config", "%d", cfg->gs_xy_nit);
    }
    row(rows, max, n, "grid search (ts+xy)", "config", "%s",
        cfg->grid_search ? "on" : "off");
    row(rows, max, n, "WPHASE_HOME", "config", "%s", cfg->wphase_home);
    row(rows, max, n, "timeout [s] / KeepTmp", "config", "%d / %d",
        cfg->timeout_sec, cfg->keep_tmp);
    row(rows, max, n, "pipeline", "csnwphase",
        "decim->trim->resp->deconv->kernels->inv(-med,-th,-nr)->gridsearch");

    return 0;
}
