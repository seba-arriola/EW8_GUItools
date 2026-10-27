#ifndef TVQ_INTERNAL_H
#define TVQ_INTERNAL_H

/* Estructuras internas de la capa C de csntvpQT. NO la incluye ui/. */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "earthworm.h"
#include "transport.h"
#include "trace_buf.h"

#include "ewgui/dsp.h"
#include "ewgui/ring.h"
#include "ewgui/wave.h"

#include "tvq_capi.h"

#define TVQ_MAX_PICKS_PER_STA 50
#define TVQ_TRACE_MSG_MAX     4096

/* Muestra ausente (paridad con csntvp.h). */
#define TVQ_GAP INT_MAX

#define TVQ_CIRC_IDX(abs_val, size) \
    (int)(((abs_val) % (size) + (size)) % (size))

typedef struct {
    double t;
    long   index;      /* seq del anillo (0..999999) o local */
    char   label[8];   /* "P(A)" | "P(M)" | "S(A)" | "S(M)" */
    char   phase;      /* 'P' | 'S' */
    char   origin;     /* 'A' | 'M' */
    int    in_use;
} TvqPick;

typedef struct {
    char    sta[16], chan[16], net[8], loc[8];
    double  scale;
    double  rate;
    int32_t *circ;
    long    circ_size;
    int64_t last_abs;
    double  last_packet_wall;
    int     has_data;
    TvqPick picks[TVQ_MAX_PICKS_PER_STA];
    int     n_picks;
    long    pick_next;
    EwTraceCache cache;
} TvqStation;

struct TvqCore {
    const TvqConfig *cfg;             /* prestada: vive mas que el core */

    EwGuiRing wave, pick;
    unsigned char instid, modid;
    unsigned char type_trace, type_pick, type_hb, type_err;
    MSG_LOGO wave_logo[1], pick_logo[1];

    TvqStation *sta;
    int         n_sta;

    int    hold;
    double hold_t;
    double smooth_t;
    double latest_t;
    double last_frame_wall;
    double last_data_wall;
    int    stale;

    double zoom;
    char   pick_phase;
    int    pick_seq;

    int    filter_type;               /* 0=raw 1=HP 2=LP 3=BP */
    double f1, f2;
    int    order;

    EwGuiHeartbeat hb;

    int warned_datatype;
    int logged_first_trace;

    /* ultimo tamano pintado (px LOGICOS) */
    int view_w, view_h;

    /* cache de la decision de recalcular envolventes */
    int    env_w;
    double env_t_right;
    double env_wall;
    int    env_ok;
    int    env_repaint;               /* una envolvente se recalculo: repintar */

    double colour[TVQ_COLOUR_COUNT][3];

    unsigned changed;                 /* bits TVQ_CH_* acumulados */
    unsigned last_change;             /* lo ultimo devuelto por tick */

    char last_pick_msg[TVQ_PICK_MSG_MAX];
    int  have_last_pick;
};

/* --- helpers entre ficheros de c/ ---------------------------------------- */

void tvq_env_update(TvqCore *c, int width, int force);
void tvq_env_invalidate(TvqCore *c);

void tvq_pick_add(TvqStation *s, long index, double t, char phase, char origin);
void tvq_pick_replace_near(TvqCore *c, TvqStation *s, double t, double window);

int tvq_colour_for_chan(const TvqCore *c, const char *chan, const double **rgb);

/* Nombre legible de las claves obligatorias que falten (para el error). */
void tvq_missing_keys(char *out, size_t n, const int have[6]);

#endif /* TVQ_INTERNAL_H */
