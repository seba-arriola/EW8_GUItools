/*
 * bpq_internal.h - Declaraciones internas de la capa C que SI llevan cairo.
 *
 * Solo lo incluyen los .c de c/. Ningun translation unit de ui/ lo ve (la
 * frontera publica es bpq_capi.h, sin cairo), que es la regla R1/R5 del plan.
 */
#ifndef BPQ_INTERNAL_H
#define BPQ_INTERNAL_H

#include <cairo.h>

#include "bpq_capi.h"

/* Escenas reales (bpq_map.c / bpq_profile.c). */
void bpq_map_draw(cairo_t *cr, int w, int h, const BpqMapCtx *c, const BpqMap *m);
void bpq_profile_draw(cairo_t *cr, int w, int h, const BpqProfileCtx *c);
void bpq_traces_draw(cairo_t *cr, int w, int h, const BpqTracesCtx *c);
void bpq_traces_draw_axis(cairo_t *cr, int w, int h, const BpqTracesCtx *c);

/* Sub-pestana magnitudes (fase 6.1): traza procesada + franja de la ventana. */
void bpq_mag_draw(cairo_t *cr, int w, int h, const BpqMagCtx *c);
void bpq_mag_draw_axis(cairo_t *cr, int w, int h, const BpqMagCtx *c);

/* Panel Mww (fase 6.3): playera del mecanismo focal. El resultado se pide
 * opaco (`bpq_mww_out_raw`) para que este .c sea el unico que ve csnwphase.h. */
void        bpq_mww_ball_draw(cairo_t *cr, int w, int h, const BpqMww *m);
const void *bpq_mww_out_raw(const BpqMww *m);

/* Handle crudo de la conexion (lo usa bpq_mww_save; no sale de c/). */
void *bpq_db_ewguidb(BpqDb *d);

/* Marcador de posicion de las escenas que aun no tienen dibujo real (bpq_canvas.c). */
void bpq_scene_draw_placeholder(cairo_t *cr, int w, int h, BpqScene s);

#endif /* BPQ_INTERNAL_H */
