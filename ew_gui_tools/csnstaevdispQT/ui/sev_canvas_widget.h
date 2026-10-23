#ifndef SEV_CANVAS_WIDGET_H
#define SEV_CANVAS_WIDGET_H

/* Adaptador fino sobre ewguiqt::CanvasWidget.
 *
 * Es el UNICO punto donde la UI toca el mapa: la capa C dibuja en BGRA
 * (sev_map_paint_bgra) y este widget solo le pasa el buffer y el estado. No
 * incluye cairo ni EarthWorm.
 */

#include <cstring>

#include "ewguiqt/canvas.h"

#include "sev_capi.h"

class SevCanvasWidget : public ewguiqt::CanvasWidget
{
public:
    explicit SevCanvasWidget(QWidget *parent = nullptr)
        : ewguiqt::CanvasWidget(parent)
    {
        setMinSizeHint(320, 320);
        setPaintFn([this](unsigned char *bgra, int w, int h, int stride) {
            /* Limpia el buffer completo: si el usuario panea fuera del mapa,
             * lo que no cubre el fondo no debe mostrar memoria sin inicializar
             * (el GTK pinta ahi el fondo del widget). */
            std::memset(bgra, 0xdd, std::size_t(stride) * std::size_t(h));
            if (m_map && m_view) {
                sev_map_paint_bgra(m_map, m_view,
                                   m_snap ? m_snap : &m_empty,
                                   bgra, w, h, stride);
            }
        });
    }

    void setMap(const SevMap *m) { m_map = m; requestRepaint(); }
    void setView(const SevMapView *v) { m_view = v; requestRepaint(); }
    void setSnapshot(const SevSnapshot *s) { m_snap = s; requestRepaint(); }

private:
    const SevMap *m_map = nullptr;
    const SevMapView *m_view = nullptr;
    const SevSnapshot *m_snap = nullptr;
    SevSnapshot m_empty{};
};

#endif /* SEV_CANVAS_WIDGET_H */
