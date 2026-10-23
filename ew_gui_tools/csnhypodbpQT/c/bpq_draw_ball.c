/*
 * bpq_draw_ball.c - Playera (mecanismo focal) del panel `Mww` (fase 6.3).
 *
 * Misma proyeccion y misma formula de radiacion P que el modulo de referencia
 * (`csnhypodbp_mww.c:288-343`): hemisferio inferior en proyeccion ortografica,
 * compresion (>0) sombreada y dilatacion en blanco. No hay geometria de
 * poligonos: es un raster por pixel, asi que no puede "mentir" en los bordes.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "csnwphase.h"

#include "bpq_internal.h"

void bpq_mww_ball_draw(cairo_t *cr, int w, int h, const BpqMww *m)
{
    const CsnwphaseOut *o = (const CsnwphaseOut *)bpq_mww_out_raw(m);
    cairo_surface_t *surf;
    unsigned char *data;
    int stride, n, x, y, ox, oy;
    double R;

    if (!cr || w <= 0 || h <= 0) return;

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    n = (w < h ? w : h) - 6;
    if (n < 40) return;
    if (!o || !o->have_mt) {
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, 8.0, h / 2.0);
        cairo_show_text(cr, o ? "sin mecanismo focal" : "sin resultado");
        return;
    }

    surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return;
    }
    data = cairo_image_surface_get_data(surf);
    stride = cairo_image_surface_get_stride(surf);
    R = n / 2.0 - 1.0;

    for (y = 0; y < n; y++) {
        for (x = 0; x < n; x++) {
            const double dx = (x + 0.5 - n / 2.0) / R;   /* + = este  */
            const double dy = (n / 2.0 - (y + 0.5)) / R; /* + = norte */
            const double r = sqrt(dx * dx + dy * dy);
            unsigned char *px = data + (size_t)y * stride + (size_t)x * 4;
            double ci, az, ur, ut, up, amp;

            if (r > 1.0) {
                px[0] = px[1] = px[2] = px[3] = 0;       /* fuera del circulo */
                continue;
            }
            ci = sqrt(1.0 - r * r);                      /* cos(incidencia) */
            az = atan2(dx, dy);                          /* azimut desde el norte */
            /* Vector unitario (r=arriba, t=sur, p=este) en el hemisferio
             * inferior: la radiacion P se evalua en la direccion de salida. */
            ur = -ci;
            ut = -r * cos(az);
            up = r * sin(az);
            amp = o->m_rr * ur * ur + o->m_tt * ut * ut + o->m_pp * up * up
                + 2.0 * o->m_rt * ur * ut + 2.0 * o->m_rp * ur * up
                + 2.0 * o->m_tp * ut * up;
            /* Compresion sombreada, dilatacion blanca (ARGB premultiplicado: sin
             * alpha, B/G/R = 0x33/0x44/0x66 en little-endian = 0xFF334466). */
            if (amp > 0.0) {
                px[0] = 0x66;
                px[1] = 0x44;
                px[2] = 0x33;
            } else {
                px[0] = px[1] = px[2] = 0xFF;
            }
            px[3] = 0xFF;
        }
    }
    cairo_surface_mark_dirty(surf);

    ox = (w - n) / 2;
    oy = (h - n) / 2;
    cairo_set_source_surface(cr, surf, ox, oy);
    cairo_paint(cr);
    cairo_surface_destroy(surf);

    /* Contorno del circulo: sin el, un mecanismo dilatado se confunde con el
     * fondo blanco. */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
    cairo_set_line_width(cr, 1.0);
    cairo_arc(cr, ox + n / 2.0, oy + n / 2.0, R, 0.0, 2.0 * M_PI);
    cairo_stroke(cr);
}
