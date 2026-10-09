#include <stdio.h>
#include <string.h>

#include "ewgui/ctrl.h"

void ewgui_ctrl_campo_despues(char *line, const char *label, char *out, size_t n)
{
    char *c = strstr(line, label);
    if (!c) { out[0] = '\0'; return; }
    c = strchr(c, ':');
    if (!c) { out[0] = '\0'; return; }
    c++;
    while (*c == ' ') c++;
    strncpy(out, c, n - 1);
    out[n - 1] = '\0';
    char *nl = strchr(out, '\n');
    if (nl) *nl = '\0';
    char *e = out + strlen(out) - 1;
    while (e >= out && (*e == ' ' || *e == '\r')) { *e = '\0'; e--; }
}

void ewgui_ctrl_extraer_config(char *detalle, char *config, size_t n)
{
    char tmp[192], *tok;
    config[0] = '\0';
    strncpy(tmp, detalle, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    tok = strtok(tmp, " ");
    while (tok) {
        int len = strlen(tok);
        if (len > 3 && !strcmp(&tok[len - 2], ".d")) {
            char *slash = strrchr(tok, '/');
            if (slash) tok = slash + 1;
            char *dot = strchr(tok, '.');
            if (dot) *dot = '\0';
            strncpy(config, tok, n - 1);
        }
        tok = strtok(NULL, " ");
    }
    config[n - 1] = '\0';
}

void ewgui_ctrl_extraer_cfgfile(char *detalle, char *cfgfile, size_t n)
{
    char tmp[192], *tok;
    cfgfile[0] = '\0';
    strncpy(tmp, detalle, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    tok = strtok(tmp, " ");
    while (tok) {
        int len = strlen(tok);
        if (len > 3 && !strcmp(&tok[len - 2], ".d")) {
            char *slash = strrchr(tok, '/');
            if (slash) tok = slash + 1;
            strncpy(cfgfile, tok, n - 1);
        }
        tok = strtok(NULL, " ");
    }
    cfgfile[n - 1] = '\0';
}

void ewgui_ctrl_parse_status(char *buf, EwCtrlStatus *st)
{
    char *save = NULL;
    char *line;

    st->nmods = 0;
    st->nrings = 0;

    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char name[32], status[16], rest[192];

        if (strstr(line, "name/key/size")) {
            char *v = strchr(line, ':');
            if (v) {
                EwCtrlRing r;
                if (sscanf(v + 1, "%63s / %d / %d", r.name, &r.key, &r.size) == 3) {
                    r.name[sizeof(r.name) - 1] = '\0';
                    if (st->nrings < EW_CTRL_MAX_ROWS) st->rings[st->nrings++] = r;
                }
            }
            continue;
        }
        if (strstr(line, "Hostname-OS")) { ewgui_ctrl_campo_despues(line, "Hostname-OS", st->hostname, sizeof(st->hostname)); continue; }
        if (strstr(line, "Start time (UTC)")) { ewgui_ctrl_campo_despues(line, "Start time (UTC)", st->starttime, sizeof(st->starttime)); continue; }
        if (strstr(line, "Current time (UTC)")) { ewgui_ctrl_campo_despues(line, "Current time (UTC)", st->curtime, sizeof(st->curtime)); continue; }
        if (strstr(line, "Disk space avail")) { ewgui_ctrl_campo_despues(line, "Disk space avail", st->disk, sizeof(st->disk)); continue; }
        if (strstr(line, "Stopstart Version")) { ewgui_ctrl_campo_despues(line, "Stopstart Version", st->version, sizeof(st->version)); continue; }
        if (strstr(line, "Process  Process") || strstr(line, "Name      Id")) continue;
        if (strstr(line, "-------")) continue;

        if (st->nmods >= EW_CTRL_MAX_ROWS) break;
        if (sscanf(line, "%31s %d %15s", name, &st->mods[st->nmods].pid, status) == 3) {
            if (st->mods[st->nmods].pid <= 0) continue;
            strncpy(st->mods[st->nmods].name, name, 31);
            st->mods[st->nmods].name[31] = '\0';
            strncpy(st->mods[st->nmods].status, status, 15);
            st->mods[st->nmods].status[15] = '\0';

            char *reststart = strstr(line, status) + strlen(status);
            while (*reststart == ' ') reststart++;
            strncpy(rest, reststart, sizeof(rest) - 1);
            rest[sizeof(rest) - 1] = '\0';
            strncpy(st->mods[st->nmods].detalle, rest, sizeof(st->mods[st->nmods].detalle) - 1);
            st->mods[st->nmods].detalle[sizeof(st->mods[st->nmods].detalle) - 1] = '\0';

            ewgui_ctrl_extraer_config(rest, st->mods[st->nmods].config, sizeof(st->mods[st->nmods].config));
            if (st->mods[st->nmods].config[0] == '\0')
                strncpy(st->mods[st->nmods].config, st->mods[st->nmods].name, sizeof(st->mods[st->nmods].config) - 1);

            ewgui_ctrl_extraer_cfgfile(rest, st->mods[st->nmods].cfgfile, sizeof(st->mods[st->nmods].cfgfile));
            if (st->mods[st->nmods].cfgfile[0] == '\0') {
                strncpy(st->mods[st->nmods].cfgfile, st->mods[st->nmods].name, sizeof(st->mods[st->nmods].cfgfile) - 1);
                strncat(st->mods[st->nmods].cfgfile, ".d", sizeof(st->mods[st->nmods].cfgfile) - 1);
                st->mods[st->nmods].cfgfile[sizeof(st->mods[st->nmods].cfgfile) - 1] = '\0';
            }
            if (!strcmp(st->mods[st->nmods].name, "startstop"))
                strncpy(st->mods[st->nmods].cfgfile, "startstop_unix.d", sizeof(st->mods[st->nmods].cfgfile) - 1);
            st->nmods++;
        }
    }
}

static int ec_mod_same(const EwCtrlMod *a, const EwCtrlMod *b)
{
    return a->pid == b->pid
        && !strcmp(a->name, b->name)
        && !strcmp(a->status, b->status)
        && !strcmp(a->detalle, b->detalle)
        && !strcmp(a->config, b->config)
        && !strcmp(a->cfgfile, b->cfgfile);
}

static int ec_ring_same(const EwCtrlRing *a, const EwCtrlRing *b)
{
    return a->key == b->key && a->size == b->size && !strcmp(a->name, b->name);
}

EwCtrlSync ewgui_ctrl_sync_plan(const EwCtrlStatus *prev, const EwCtrlStatus *next)
{
    int i;

    if (!prev || !next) return EWCTRL_SYNC_STRUCTURE;

    /* Estructura: mismo número y misma secuencia de (name,pid)/(name,key). */
    if (prev->nmods != next->nmods || prev->nrings != next->nrings)
        return EWCTRL_SYNC_STRUCTURE;
    for (i = 0; i < next->nmods; i++)
        if (prev->mods[i].pid != next->mods[i].pid ||
            strcmp(prev->mods[i].name, next->mods[i].name))
            return EWCTRL_SYNC_STRUCTURE;
    for (i = 0; i < next->nrings; i++)
        if (prev->rings[i].key != next->rings[i].key ||
            strcmp(prev->rings[i].name, next->rings[i].name))
            return EWCTRL_SYNC_STRUCTURE;

    /* Valores: ¿cambió algún campo visible de las filas? */
    for (i = 0; i < next->nmods; i++)
        if (!ec_mod_same(&prev->mods[i], &next->mods[i])) return EWCTRL_SYNC_VALUES;
    for (i = 0; i < next->nrings; i++)
        if (!ec_ring_same(&prev->rings[i], &next->rings[i])) return EWCTRL_SYNC_VALUES;

    return EWCTRL_SYNC_NONE;
}

int ewgui_ctrl_send(SHM_INFO *region, unsigned char instid, unsigned char modid,
                    unsigned char type, const char *payload)
{
    MSG_LOGO logo;
    char msg[512];
    logo.instid = instid;
    logo.mod    = modid;
    logo.type   = type;
    strncpy(msg, payload, sizeof(msg) - 2);
    msg[sizeof(msg) - 2] = '\0';
    strcat(msg, "\n");
    return (tport_putmsg(region, &logo, strlen(msg), msg) == PUT_OK) ? 0 : -1;
}
