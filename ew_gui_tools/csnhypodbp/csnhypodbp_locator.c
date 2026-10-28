#include "csnhypodbp_locator.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *base_name(const char *path)
{
    const char *p;

    if (!path) return "";
    p = strrchr(path, '/');
    return p ? p + 1 : path;
}

int csnhypodbp_locator_add(CsnhypodbpLocator *list, int *n, int cap,
                           const char *name, const char *bin, const char *cfg)
{
    if (!list || !n || *n < 0 || *n >= cap || !name || !name[0]) return -1;
    snprintf(list[*n].name, sizeof(list[*n].name), "%s", name);
    snprintf(list[*n].bin, sizeof(list[*n].bin), "%s", bin ? bin : "");
    snprintf(list[*n].cfg, sizeof(list[*n].cfg), "%s", cfg ? cfg : "");
    list[*n].mod = 0;
    list[*n].live_usage = 0;
    list[*n].time_recalc_live = 0;
    list[*n].n_retry = 0;
    (*n)++;
    return 0;
}

int csnhypodbp_locator_add_full(CsnhypodbpLocator *list, int *n, int cap,
                                const char *bin, const char *cfg,
                                int live_usage, int time_recalc_live,
                                int n_retry)
{
    const char *name = base_name(bin);

    if (!name[0]) return -1;
    if (csnhypodbp_locator_add(list, n, cap, name, bin, cfg) != 0) return -1;
    list[*n - 1].live_usage = live_usage ? 1 : 0;
    list[*n - 1].time_recalc_live = (time_recalc_live > 0) ? time_recalc_live : 0;
    list[*n - 1].n_retry = (n_retry > 0) ? n_retry : 0;
    return 0;
}

int csnhypodbp_locator_load_file(const char *path, CsnhypodbpLocator *list,
                                 int *n, int cap)
{
    FILE *fp;
    char line[1024];

    if (!path || !path[0] || !list || !n) return -1;
    fp = fopen(path, "r");
    if (!fp) return -1;
    while (fgets(line, sizeof(line), fp)) {
        char key[32], bin[256], cfg[256];
        int live = 0, recalc = 0, retry = 0;
        char *h = strchr(line, '#');

        if (h) *h = '\0';
        if (sscanf(line, "%31s %255s %255s %d %d %d",
                   key, bin, cfg, &live, &recalc, &retry) != 6)
            continue;
        if (strcmp(key, "locator") != 0) continue;
        if (csnhypodbp_locator_add_full(list, n, cap, bin, cfg,
                                        live, recalc, retry) != 0) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

int csnhypodbp_locator_default_file(char *out, size_t n)
{
    const char *ep = getenv("EW_PARAMS");

    if (!out || n == 0) return -1;
    snprintf(out, n, "%s/csnhypodbpqt.d", (ep && ep[0]) ? ep : ".");
    return 0;
}

int csnhypodbp_locator_load_default(CsnhypodbpLocator *list, int *n, int cap)
{
    char path[600];

    if (!list || !n || *n > 0) return 0;
    if (csnhypodbp_locator_default_file(path, sizeof(path)) != 0) return -1;
    return csnhypodbp_locator_load_file(path, list, n, cap);
}

int csnhypodbp_locator_find(const CsnhypodbpLocator *list, int n, const char *name)
{
    int i;
    if (!list || !name) return -1;
    for (i = 0; i < n; i++)
        if (strcmp(list[i].name, name) == 0) return i;
    return -1;
}

int csnhypodbp_locator_resolve(const CsnhypodbpLocator *l,
                               const char *ew_home, const char *ew_version,
                               const char *ew_params, char *bin_out, size_t bn,
                               char *cfg_out, size_t cn)
{
    if (!l) return -1;
    if (bin_out && bn) {
        if (strchr(l->bin, '/') || !ew_home)
            snprintf(bin_out, bn, "%s", l->bin);
        else if (ew_version)
            snprintf(bin_out, bn, "%s/%s/bin/%s", ew_home, ew_version, l->bin);
        else
            snprintf(bin_out, bn, "%s/bin/%s", ew_home, l->bin);
    }
    if (cfg_out && cn) {
        if (strchr(l->cfg, '/') || !ew_params)
            snprintf(cfg_out, cn, "%s", l->cfg);
        else
            snprintf(cfg_out, cn, "%s/%s", ew_params, l->cfg);
    }
    return 0;
}

int csnhypodbp_locator_read_module(const CsnhypodbpLocator *l,
                                   char *symbol, size_t n,
                                   char *err, size_t errn)
{
    char path[600], line[600];
    FILE *fp;

    if (err && errn) err[0] = '\0';
    if (!l || !symbol || n == 0) return -1;
    symbol[0] = '\0';
    if (csnhypodbp_locator_resolve(l, getenv("EW_HOME"), getenv("EW_VERSION"),
                                   getenv("EW_PARAMS"), NULL, 0,
                                   path, sizeof path) != 0) {
        if (err) snprintf(err, errn, "no se pudo resolver <%s>", l->cfg);
        return -1;
    }
    fp = fopen(path, "r");
    if (!fp) {
        if (err) snprintf(err, errn, "no se pudo abrir %s", path);
        return -1;
    }
    while (fgets(line, sizeof line, fp)) {
        char key[64], val[64];
        char *h = strchr(line, '#');

        if (h) *h = '\0';
        if (sscanf(line, "%63s %63s", key, val) != 2) continue;
        if (strcmp(key, "MyModuleId") != 0) continue;
        snprintf(symbol, n, "%s", val);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    if (err) snprintf(err, errn, "%s no declara MyModuleId", path);
    return -1;
}

int csnhypodbp_locator_resolve_mods(CsnhypodbpLocator *list, int n,
                                    CsnhypodbpGetModId get_modid)
{
    int i, fails = 0;

    if (!list || n <= 0) return 0;
    for (i = 0; i < n; i++) {
        char sym[64], err[256];
        unsigned char id = 0;

        list[i].mod = 0;
        err[0] = '\0';
        if (csnhypodbp_locator_read_module(&list[i], sym, sizeof sym,
                                           err, sizeof err) != 0 ||
            !get_modid || get_modid(sym, &id) != 0 || id == 0) {
            fprintf(stderr, "csnhypodbp_locator: %s: %s\n", list[i].name,
                    err[0] ? err
                           : "MyModuleId no resuelto en earthworm.d (GetModId)");
            fails++;
            continue;
        }
        list[i].mod = (int)id;
    }
    return fails;
}

int csnhypodbp_locator_mod(const CsnhypodbpLocator *l)
{
    return l ? l->mod : 0;
}

const char *csnhypodbp_locator_name_for_mod(const CsnhypodbpLocator *list,
                                            int n, int mod)
{
    int i;

    if (!list || n <= 0 || mod <= 0) return NULL;
    for (i = 0; i < n; i++)
        if (list[i].mod == mod) return list[i].name;
    return NULL;
}

int csnhypodbp_locator_is_live_mod(const CsnhypodbpLocator *list, int n, int mod)
{
    int i;

    if (!list || n <= 0 || mod <= 0)
        return 0;
    for (i = 0; i < n; i++)
        if (list[i].live_usage && list[i].mod == mod)
            return 1;
    return 0;
}

const char *csnhypodbp_mod_label(int mod, CsnhypodbpGetModIdName get_name,
                                 char *buf, size_t bufn)
{
    const char *sym, *p;
    size_t i = 0;

    if (!buf || bufn == 0) return "";
    buf[0] = '\0';
    if (mod <= 0 || mod > 255 || !get_name) return buf;

    sym = get_name((unsigned char)mod);
    if (!sym || !sym[0]) return buf;

    /* `MOD_` es el prefijo de los simbolos de earthworm.d; se acepta en
     * cualquier caja y se elimina del nombre visible. */
    p = sym;
    if (p[0] && p[1] && p[2] && p[3] == '_' &&
        toupper((unsigned char)p[0]) == 'M' &&
        toupper((unsigned char)p[1]) == 'O' &&
        toupper((unsigned char)p[2]) == 'D')
        p += 4;

    for (; *p && i + 1 < bufn; p++)
        buf[i++] = (char)tolower((unsigned char)*p);
    buf[i] = '\0';
    return buf;
}
