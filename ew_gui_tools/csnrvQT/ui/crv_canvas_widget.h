#ifndef CRV_CANVAS_WIDGET_H
#define CRV_CANVAS_WIDGET_H

/* Adaptador fino sobre ewguiqt::CanvasWidget.
 *
 * Es el UNICO punto donde la UI toca el mapa: la capa C dibuja en BGRA
 * (crv_map_paint_bgra) y este widget solo le pasa el buffer y el estado. No
 * incluye cairo ni EarthWorm.
 */

#include <cstring>

#include "ewguiqt/canvas.h"

#include "crv_capi.h"

class CrvCanvasWidget : public ewguiqt::CanvasWidget
{
public:
    explicit CrvCanvasWidget(QWidget *parent = nullptr)
        : ewguiqt::CanvasWidget(parent)
    {
        setMinSizeHint(320, 320);
        setPaintFn([this](unsigned char *bgra, int w, int h, int stride) {
            if (m_map && m_view) {
                crv_map_paint_bgra(m_map, m_view,
                                   m_snap ? m_snap : &m_empty,
                                   bgra, w, h, stride);
            } else {
                std::memset(bgra, 0xdd, std::size_t(stride) * std::size_t(h));
            }
        });
    }

    void setMap(const CrvMap *m) { m_map = m; requestRepaint(); }
    void setView(const CrvMapView *v) { m_view = v; requestRepaint(); }
    void setSnapshot(const CrvSnapshot *s) { m_snap = s; requestRepaint(); }

private:
    const CrvMap *m_map = nullptr;
    const CrvMapView *m_view = nullptr;
    const CrvSnapshot *m_snap = nullptr;
    CrvSnapshot m_empty{};
};

#endif /* CRV_CANVAS_WIDGET_H */
