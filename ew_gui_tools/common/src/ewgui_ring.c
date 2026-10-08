#include <string.h>

#include "earthworm.h"
#include "transport.h"
#include "ewgui/ring.h"

int ewgui_ring_resolve(const char *ring_name, const char *module_id,
                       long *key, unsigned char *instid, unsigned char *modid)
{
    *key = GetKey((char *)ring_name);
    if (*key == -1)
        return -1;
    if (GetLocalInst(instid) != 0)
        return -1;
    if (GetModId((char *)module_id, modid) != 0)
        return -1;
    return 0;
}

int ewgui_ring_attach(EwGuiRing *r, long key, unsigned char instid,
                      unsigned char modid)
{
    memset(r, 0, sizeof(*r));
    r->key = key;
    r->instid = instid;
    r->modid = modid;
    tport_attach(&r->region, key);
    r->attached = 1;
    return 0;
}

void ewgui_ring_detach(EwGuiRing *r)
{
    if (r && r->attached) {
        tport_detach(&r->region);
        r->attached = 0;
    }
}

int ewgui_ring_put(EwGuiRing *r, MSG_LOGO *logo, long len, char *msg)
{
    return tport_putmsg(&r->region, logo, len, msg);
}

int ewgui_ring_drain(EwGuiRing *r, MSG_LOGO *logos, short nlogo,
                     MSG_LOGO *outlogo, long *outlen, char *buf, long buflen)
{
    return tport_getmsg(&r->region, logos, nlogo, outlogo, outlen, buf, buflen);
}

int ewgui_ring_should_quit(SHM_INFO *region, int mypid)
{
    int flag = tport_getflag(region);
    return (flag == TERMINATE || flag == mypid);
}

int ewgui_heartbeat_due(EwGuiHeartbeat *hb, double now, int interval_s)
{
    if (now - hb->last >= (double)interval_s) {
        hb->last = now;
        return 1;
    }
    return 0;
}
