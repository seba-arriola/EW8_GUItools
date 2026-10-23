/*
 * probe_capi.cxx - Prueba de compilacion (AC3): bpq_capi.h debe ser C++-safe
 * incluso incluyendolo DESPUES de Qt (que define `slots` como macro).
 *
 * Se compila con `g++ -fsyntax-only`; no se enlaza ni se ejecuta. Si este TU
 * falla, es que un header puro ha ganado una dependencia que rompe la frontera
 * C/C++ (GTK, glib, EarthWorm, cairo...) o que el renombrado de `slots` dejo de
 * funcionar.
 */
#include <QWidget>      /* define `slots`: el caso dificil */
#include <QString>
#include <QImage>

#include "bpq_capi.h"

int bpq_probe_use(void)
{
    BpqConfig *cfg = bpq_config_load("no-existe.d", NULL, NULL, 0);
    CsnhypodbpSolution sol;
    CsnhypodbpStaRow row;
    CsnhypodbpPreviewVals pv;
    CsnhypodbpProfiles prof;
    unsigned char px[4] = { 0, 0, 0, 0 };

    if (cfg) bpq_config_free(cfg);

    sol.qid = 0;
    row.idx = 0;
    pv.state = CSNHYPODBP_PREVIEW_IDLE;
    (void)prof;

    bpq_scene_paint(BPQ_SCENE_MAP_PLAN, NULL, px, 1, 1, 4);
    return sol.qid + row.idx + pv.state;
}
