#ifndef SEV_MAINWINDOW_H
#define SEV_MAINWINDOW_H

#include <QMainWindow>

#include "sev_capi.h"

class QAction;
class QTimer;
class SevCanvasWidget;

/* Ventana del visor. `cfg` y `core` se PRESTAN: la ventana no los libera. */
class SevMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    SevMainWindow(SevConfig *cfg, SevCore *core, QWidget *parent = nullptr);
    ~SevMainWindow() override;

    void setSnapshot(const SevSnapshot &s);

    /* Modos instrumentados (offscreen, sin anillo ni BD). */
    QString dumpTree() const;
    QString dumpLayout() const;
    bool saveShot(const QString &path);
    /* Inyecta rueda/arrastre sinteticos y reporta zoom, pan y deriva del
     * ancla, para `CSNSTAEVDISPQT_MODE=--interact`. */
    QString interactProbe();

private slots:
    void onTickData();
    void onTickBackground();
    void onExport();
    void onLayers();
    void onReloadOverlays();
    void onEventCount();
    void onToggleDark(bool on);

private:
    void buildMenu();
    void buildUi();
    void loadMapImage();

    SevConfig *m_cfg;
    SevCore *m_core;
    SevMap *m_map = nullptr;
    SevMapView m_view{};
    SevSnapshot m_snap{};

    SevCanvasWidget *m_canvas = nullptr;
    QTimer *m_dataTimer = nullptr;
    QTimer *m_bgTimer = nullptr;
    QAction *m_dark = nullptr;
};

#endif /* SEV_MAINWINDOW_H */
