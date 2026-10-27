/* Picadas: lectura del anillo, eco local y publicacion de las manuales.
 *
 * Reglas copiadas de csntvp (csntvp_data.c:356-399 y csntvp_gui.c:271-331):
 *   - el emparejamiento con la pista es SOLO por estacion (el canal de una S
 *     puede ser HHE/HHN mientras la traza visible es HHZ);
 *   - el eco de una picada propia se reconoce por (mod,inst) + seq y se
 *     actualiza en su slot en vez de duplicarse. Como este port usa su PROPIO
 *     MyModuleId (MOD_CSNTVPQT 175), el GTK ya no confunde las picadas del port
 *     con las suyas (y al reves);
 *   - PickReplaceWindow desactiva localmente las picadas de la estacion dentro
 *     de +/-N s: TYPE_PICK_SCNL no tiene retract, asi que el asociador vera la
 *     corregida como una picada nueva.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csntvp_util.h"

#include "tvq_capi.h"
#include "tvq_internal.h"

void tvq_pick_add(TvqStation *s, long index, double t, char phase, char origin)
{
    int slot;
    char label[8];

    if (!s)
        return;

    csntvp_pick_label(phase, origin, label, sizeof label);
    slot = (int)(s->pick_next % TVQ_MAX_PICKS_PER_STA);
    s->picks[slot].t = t;
    s->picks[slot].index = index;
    snprintf(s->picks[slot].label, sizeof s->picks[slot].label, "%s", label);
    s->picks[slot].phase = (phase == 'S' || phase == 's') ? 'S' : 'P';
    s->picks[slot].origin = (origin == 'M' || origin == 'm') ? 'M' : 'A';
    s->picks[slot].in_use = 1;
    s->pick_next++;
    if (s->n_picks < TVQ_MAX_PICKS_PER_STA)
        s->n_picks++;
}

void tvq_pick_replace_near(TvqCore *c, TvqStation *s, double t, double window)
{
    if (!s || window <= 0.0)
        return;

    for (int i = 0; i < TVQ_MAX_PICKS_PER_STA; i++) {
        if (s->picks[i].in_use <= 0 || s->picks[i].t <= 0.0)
            continue;
        if (fabs(s->picks[i].t - t) <= window) {
            tvq_logf("csntvpQT: reemplazo picada %ld de %s (%.3f -> %.3f)\n",
                     s->picks[i].index, s->sta, s->picks[i].t, t);
            s->picks[i].in_use = 0;
            if (s->n_picks > 0)
                s->n_picks--;
        }
    }
    (void)c;
}

int tvq_core_ingest_pick(TvqCore *c, const char *msg, int len)
{
    char buf[TVQ_PICK_MSG_MAX];
    CsntvpPickMsg pm;
    TvqStation *s = NULL;
    int cp;

    if (!c || !msg || len <= 0)
        return 0;

    /* El buffer del anillo NO viene terminado en NUL. */
    cp = (len < (int)sizeof(buf) - 1) ? len : (int)sizeof(buf) - 1;
    memcpy(buf, msg, (size_t)cp);
    buf[cp] = '\0';

    if (!csntvp_pick_parse(buf, &pm))
        return 0;

    for (int i = 0; i < c->n_sta; i++) {
        if (strcmp(c->sta[i].sta, pm.sta) == 0) {
            s = &c->sta[i];
            break;
        }
    }
    if (!s)
        return 0;

    if ((unsigned char)pm.mod == c->modid && (unsigned char)pm.inst == c->instid) {
        for (int k = 0; k < TVQ_MAX_PICKS_PER_STA; k++) {
            if (s->picks[k].in_use > 0 && s->picks[k].index == (long)pm.seq) {
                s->picks[k].t = pm.t;
                csntvp_pick_label(pm.phase, pm.origin, s->picks[k].label,
                                  sizeof s->picks[k].label);
                s->picks[k].phase = pm.phase;
                s->picks[k].origin = pm.origin;
                s->picks[k].in_use = 1;
                c->changed |= TVQ_CH_PICKS;
                return 1;
            }
        }
    }

    tvq_pick_add(s, pm.seq, pm.t, pm.phase, pm.origin);
    c->changed |= TVQ_CH_PICKS;
    return 1;
}

void tvq_core_set_pick_phase(TvqCore *c, char phase)
{
    if (!c)
        return;

    /* 'P'/'S' arman; cualquier otra cosa (incluido 0, que es Esc) desarma.
     * El modo nunca queda en un estado que no se pueda dibujar. */
    if (phase == 'P' || phase == 'p')
        c->pick_phase = 'P';
    else if (phase == 'S' || phase == 's')
        c->pick_phase = 'S';
    else
        c->pick_phase = '\0';
}

char tvq_core_pick_phase(const TvqCore *c)
{
    return c ? c->pick_phase : '\0';
}

int tvq_core_place_pick(TvqCore *c, int row, double t, char phase)
{
    TvqStation *s;
    int seq;
    char label[8];

    if (!c || !c->hold)
        return 0;
    if (row < 0 || row >= c->n_sta)
        return 0;

    phase = (char)toupper((unsigned char)phase);
    if (phase != 'P' && phase != 'S')
        return 0;

    s = &c->sta[row];

    tvq_pick_replace_near(c, s, t, c->cfg->pick_replace_secs);

    seq = c->pick_seq;
    c->pick_seq = (c->pick_seq + 1) % 1000000;

    tvq_pick_add(s, seq, t, phase, 'M');
    csntvp_pick_label(phase, 'M', label, sizeof label);

    if (tvq_pick_msg_format(c->last_pick_msg, sizeof c->last_pick_msg,
                            (int)c->type_pick, (int)c->modid, (int)c->instid,
                            seq, s->sta, s->chan, s->net, s->loc, phase,
                            t) != 0) {
        c->last_pick_msg[0] = '\0';
        c->have_last_pick = 0;
        return 0;
    }
    c->have_last_pick = 1;

    if (c->pick.attached) {
        MSG_LOGO logo;

        logo.instid = c->instid;
        logo.mod = c->modid;
        logo.type = c->type_pick;
        if (ewgui_ring_put(&c->pick, &logo, (long)strlen(c->last_pick_msg),
                           c->last_pick_msg) != PUT_OK) {
            tvq_logf("csntvpQT: error inyectando pick: %s", c->last_pick_msg);
        } else {
            tvq_logf("csntvpQT: INYECTADO: %s", c->last_pick_msg);
        }
    }

    c->changed |= TVQ_CH_PICKS;
    return 1;
}

int tvq_core_last_pick(TvqCore *c, char *out, size_t n)
{
    if (!c || !out || n == 0)
        return 0;
    if (!c->have_last_pick) {
        out[0] = '\0';
        return 0;
    }
    snprintf(out, n, "%s", c->last_pick_msg);
    return 1;
}
