#ifndef ECQ_MAINWINDOW_H
#define ECQ_MAINWINDOW_H

/* Ventana principal del port Qt6 de ew_controller.
 *
 * No tiene logica propia: todo el estado y las reglas estan en la capa C
 * (c/ecq_capi.h). Esta clase solo traduce ese estado a widgets y traduce los
 * clics a acciones. Eso es lo que permite testear la logica sin Qt.
 *
 * Paridad con el modulo GTK (ew_controller_main.c / _gui.c):
 *   - tablas Modules (Module/PID/Status/Details) y Rings (Ring/Key/Size KB),
 *     ordenables por cabecera, con el MISMO comparador (commonqt);
 *   - Stop/Restart/Start/Reconfig por PID, con confirmacion;
 *   - "?" y botones apagados si startstop no responde en PollInt*4;
 *   - pestana Configuration con el editor del .d (refresco congelado);
 *   - logs por prefijo del TYPE_STATUS, el `<prefijo>_*.log` mas reciente.
 */

#include <QMainWindow>
#include <QString>
#include <QVector>

#include "ecq_capi.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStandardItemModel;
class QTabWidget;
class QTableView;
class QTimer;
class QItemSelection;
class QGridLayout;
class QScrollArea;

namespace ewguiqt {
class LogView;
class SortProxy;
}

class EcqMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit EcqMainWindow(const EcqConfig *cfg, QWidget *parent = nullptr);
    ~EcqMainWindow() override;

    /* Ganchos de test (ECQ_MODE). Deterministas, sin DISPLAY de por medio. */
    QString dumpTree() const;
    QString dumpLayout() const;
    bool saveShot(const QString &path);

    /* Espera a que el estado aplicado llegue a `seq` (o al timeout). */
    bool waitForSeq(unsigned seq, int timeout_ms);
    /* Selecciona una fila por PID (gancho de test y de `ECQ_SELECT_PID`). */
    void selectPidForTest(int pid) { selectPid(pid); }

protected:
    void closeEvent(QCloseEvent *ev) override;

private slots:
    void onRead();
    void onTick();
    void onTryAttach();
    void onSelectionChanged(const QItemSelection &sel, const QItemSelection &prev);
    void onStop();
    void onRestart();
    void onStart();
    void onReconfig();
    void onRefresh();
    void onTabChanged(int index);
    void onPickLog();
    void onPickCfg();
    void onCfgEdited();
    void onCfgSave();
    void onCfgReconfig();
    void onCfgReload();

private:
    void buildUi();
    void applyTable(bool force);
    void applySelection();
    void applyButtons();
    void applyHeader();
    void applyStatusText(const QString &text);
    void rebuildCfgGrid();
    void refreshLog();
    int  selectedPid() const;
    void selectPid(int pid);
    const EcqRow *rowOfPid(int pid) const;
    void confirmAction(EcqAction action);

    EcqConfig m_cfg;
    EcqCore  *m_core = nullptr;

    /* Widgets */
    QLabel *m_header = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTableView *m_modView = nullptr;
    QTableView *m_ringView = nullptr;
    QStandardItemModel *m_modModel = nullptr;
    QStandardItemModel *m_ringModel = nullptr;
    ewguiqt::SortProxy *m_modProxy = nullptr;
    ewguiqt::SortProxy *m_ringProxy = nullptr;
    QPushButton *m_btnStop = nullptr;
    QPushButton *m_btnRestart = nullptr;
    QPushButton *m_btnStart = nullptr;
    QPushButton *m_btnReconfig = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    QLabel *m_status = nullptr;

    /* Pestana Configuration */
    QPushButton *m_cfgPick = nullptr;
    QLabel *m_cfgPath = nullptr;
    QLabel *m_cfgMsg = nullptr;
    QScrollArea *m_cfgScroll = nullptr;
    QGridLayout *m_cfgGrid = nullptr;
    QVector<QLineEdit *> m_cfgEdits;
    EcqCfg *m_cfgFile = nullptr;
    QString m_cfgName;          /* .d del modulo elegido */
    int     m_cfgPid = 0;
    bool    m_frozen = false;

    /* Pestana Logs */
    QPushButton *m_logPick = nullptr;
    ewguiqt::LogView *m_logView = nullptr;
    int m_logPid = 0;

    /* Timers */
    QTimer *m_readTimer = nullptr;
    QTimer *m_tickTimer = nullptr;
    QTimer *m_attachTimer = nullptr;

    unsigned m_appliedSeq = 0;
    int      m_lastNoreply = -1;
    bool     m_attachedOnce = false;
    bool     m_forceTable = false;
};

#endif /* ECQ_MAINWINDOW_H */
