#ifndef CRV_MAINWINDOW_H
#define CRV_MAINWINDOW_H

#include <QMainWindow>

#include "crv_capi.h"

class QLabel;
class QTableWidget;
class QTimer;
class QAction;
class CrvCanvasWidget;

/* Ventana del visor. `cfg` y `core` se PRESTAN: la ventana no los libera. */
class CrvMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    CrvMainWindow(CrvConfig *cfg, CrvCore *core, QWidget *parent = nullptr);
    ~CrvMainWindow() override;

    void setSnapshot(const CrvSnapshot &s);

    /* Modos instrumentados (offscreen, sin anillo). */
    QString dumpTree() const;
    QString dumpLayout() const;
    bool saveShot(const QString &path);
    /* Inyecta rueda/arrastre sinteticos y reporta zoom, pan y deriva del
     * ancla, para `CSNRVQT_MODE=--interact`. */
    QString interactProbe();

private slots:
    void onTick();
    void onExport();
    void onLayers();
    void onReloadOverlays();
    void onToggleDark(bool on);

private:
    void buildMenu();
    void buildUi();
    void loadMapImage();
    void refreshMags();
    void refreshElapsed();

    CrvConfig *m_cfg;
    CrvCore *m_core;
    CrvMap *m_map = nullptr;
    CrvMapView m_view{};
    CrvSnapshot m_snap{};

    QLabel *m_origin = nullptr;
    QLabel *m_coords = nullptr;
    QLabel *m_depth = nullptr;
    QLabel *m_elapsed = nullptr;
    QTableWidget *m_mags = nullptr;
    CrvCanvasWidget *m_canvas = nullptr;
    QTimer *m_timer = nullptr;
    QAction *m_dark = nullptr;
};

#endif /* CRV_MAINWINDOW_H */
