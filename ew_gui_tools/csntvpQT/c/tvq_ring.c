/* Conexion a EarthWorm: anillos, tipos, latido y terminacion.
 *
 * Port de csntvp_data.c:109-178. El logo del latido va al anillo de ondas,
 * como el GTK. El id del modulo es el del .d (MOD_CSNTVPQT en csntvpqt.d), no
 * el del GTK: eso es lo que mantiene separados los ecos de picadas.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "earthworm.h"
#include "earthworm_simple_funcs.h"
#include "transport.h"

#include "tvq_capi.h"
#include "tvq_internal.h"

static int resolve_type(const char *name, unsigned char *out, char *err,
                        size_t errlen)
{
    if (GetType((char *)name, out) != 0) {
        if (err)
            snprintf(err, errlen, "falta <%s> en earthworm.d", name);
        return -1;
    }
    return 0;
}

int tvq_core_open_rings(TvqCore *c, char *err, size_t errlen)
{
    long wave_key = -1, pick_key = -1;
    unsigned char inst = 0, mod = 0;
    unsigned char iw = 0, mw = 0;

    if (err && errlen)
        err[0] = '\0';
    if (!c)
        return -1;

    if (c->cfg->module_id < 0) {
        if (err)
            snprintf(err, errlen,
                     "'%s' no esta declarado en earthworm.d; edita "
                     "earthworm.d.base y regenera con "
                     "ew_gui_tools/common/tools/worktree_bootstrap.sh . --refresh",
                     c->cfg->my_module);
        return -1;
    }

    if (ewgui_ring_resolve(c->cfg->in_ring, c->cfg->my_module, &wave_key, &inst,
                           &mod) != 0) {
        if (err)
            snprintf(err, errlen, "anillo de entrada <%s> inexistente",
                     c->cfg->in_ring);
        return -1;
    }

    pick_key = GetKey((char *)c->cfg->out_ring);
    if (pick_key == -1) {
        if (err)
            snprintf(err, errlen, "anillo de salida <%s> inexistente",
                     c->cfg->out_ring);
        return -1;
    }

    if (resolve_type("TYPE_TRACEBUF2", &c->type_trace, err, errlen) != 0 ||
        resolve_type("TYPE_PICK_SCNL", &c->type_pick, err, errlen) != 0 ||
        resolve_type("TYPE_HEARTBEAT", &c->type_hb, err, errlen) != 0 ||
        resolve_type("TYPE_ERROR", &c->type_err, err, errlen) != 0)
        return -1;

    if (GetInst((char *)"INST_WILDCARD", &iw) != 0)
        iw = 0;
    if (GetModId((char *)"MOD_WILDCARD", &mw) != 0)
        mw = 0;

    c->instid = inst;
    c->modid = mod;

    c->wave_logo[0].instid = iw;
    c->wave_logo[0].mod = mw;
    c->wave_logo[0].type = c->type_trace;

    c->pick_logo[0].instid = iw;
    c->pick_logo[0].mod = mw;
    c->pick_logo[0].type = c->type_pick;

    (void)ewgui_ring_attach(&c->wave, wave_key, inst, mod);
    (void)ewgui_ring_attach(&c->pick, pick_key, inst, mod);

    tvq_logf("csntvpQT: conectado a InRing <%s> y OutRing <%s> (mod=%d inst=%d)\n",
             c->cfg->in_ring, c->cfg->out_ring, (int)mod, (int)inst);
    c->hb.last = 0.0;   /* primer latido en el primer background */
    return 0;
}

void tvq_core_heartbeat(TvqCore *c, double now_wall)
{
    char msg[256];
    MSG_LOGO logo;
    long t;
    int n;

    if (!c)
        return;
    if (!ewgui_heartbeat_due(&c->hb, now_wall, c->cfg->heartbeat_int))
        return;
    if (!c->wave.attached)
        return;

    logo.instid = c->instid;
    logo.mod = c->modid;
    logo.type = c->type_hb;
    t = (long)now_wall;
    n = snprintf(msg, sizeof msg, "%ld %d\n", t, (int)getpid());
    (void)ewgui_ring_put(&c->wave, &logo, (long)n, msg);
}

int tvq_core_should_quit(const TvqCore *c)
{
    if (!c || !c->wave.attached)
        return 0;
    return ewgui_ring_should_quit((SHM_INFO *)&c->wave.region, (int)getpid());
}

int tvq_core_module_id(const TvqCore *c)
{
    return c ? (int)c->modid : 0;
}
