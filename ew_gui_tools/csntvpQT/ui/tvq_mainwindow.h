#ifndef TVQ_MAINWINDOW_H
#define TVQ_MAINWINDOW_H

/* Ventana de csntvpQT: port de `on_activate` (csntvp_main.c:97-195).
 *
 * La UI solo traduce gestos y menus a operaciones de la capa C; el estado y el
 * dibujo viven en c/ (tvq_core.c / tvq_paint.c). Reglas del port:
 *   - menu QMenuBar NATIVO (nada de desplegables, WSLg #1299);
 *   - los selectores numericos son QInputDialog modales;
 *   - HOLD congela la ventana de tiempo; P/S eligen fase y el clic pica.
 */

#include <QMainWindow>
#include <QString>

#include "tvq_capi.h"

class QAction;
class QLabel;
class QPushButton;
class QScrollBar;
class QTimer;

namespace ewguiqt {
class CanvasWidget;
}

class TvqMainWindow : public QMainWindow {
    Q_OBJECT

public:
    TvqMainWindow(TvqConfig *cfg, TvqCore *core, QWidget *parent = nullptr);
    ~TvqMainWindow() override;

    void setInstrumented(bool on) { m_instrumented = on; }

    QString dumpTree() const;
    QString dumpLayout() const;
    bool saveShot(const QString &path);
    QString interactProbe();

protected:
    void keyPressEvent(QKeyEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;

private slots:
    void onTick();
    void onBackground();

private:
    void buildMenu();
    void buildUi();
    void updateScrollRange();
    void repaintAll();
    void syncPhaseLabel();
    void updatePickCursor();   /* circulo con la letra de la fase bajo el cursor */

    void onHoldToggled(bool on);
    void onPlace(double x, double y);
    void onStationsPerScreen();
    void onTimeWindow();
    void onCleanView();
    void onReloadStations();
    void onFilterOptions();
    void onColour(int role);
    void onExport();
    void onToggleDark(bool on);
    void onCommands();

    TvqConfig *m_cfg = nullptr;
    TvqCore *m_core = nullptr;

    ewguiqt::CanvasWidget *m_traces = nullptr;
    ewguiqt::CanvasWidget *m_axis = nullptr;
    QScrollBar *m_scroll = nullptr;
    QPushButton *m_hold = nullptr;
    QLabel *m_phase = nullptr;
    QAction *m_dark = nullptr;
    QTimer *m_timer = nullptr;
    QTimer *m_bg = nullptr;

    int m_scrollY = 0;
    bool m_updating = false;
    bool m_instrumented = false;
};

#endif /* TVQ_MAINWINDOW_H */
