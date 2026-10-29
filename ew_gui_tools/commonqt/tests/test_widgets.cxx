/* T1.3/T1.4: selector modal, confirmacion, tema y visor de log.
 * Todo headless con QT_QPA_PLATFORM=offscreen (lo pone el Makefile). */
#include <cstdio>

#include <QApplication>
#include <QAbstractButton>
#include <QColor>
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>

#include "ewguiqt/confirm.h"
#include "ewguiqt/list_select.h"
#include "ewguiqt/log_view.h"
#include "ewguiqt/table.h"
#include "ewguiqt/theme.h"

static int g_fail = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);          \
            g_fail = 1;                                                          \
        }                                                                        \
    } while (0)

/* Pulsa el boton con ese rol en la ventana modal activa. */
static void clickModalButton(QDialogButtonBox::ButtonRole role, bool close = false)
{
    QTimer::singleShot(0, [role, close]() {
        QWidget *w = QApplication::activeModalWidget();
        if (!w)
            return;
        if (close) {
            w->close();
            return;
        }
        if (auto *msg = qobject_cast<QMessageBox *>(w)) {
            const QList<QAbstractButton *> buttons = msg->buttons();
            for (QAbstractButton *b : buttons) {
                if (int(msg->buttonRole(b)) == int(role)) {
                    b->click();
                    return;
                }
            }
            return;
        }
        if (auto *box = w->findChild<QDialogButtonBox *>()) {
            const QList<QAbstractButton *> buttons = box->buttons();
            for (QAbstractButton *b : buttons) {
                if (int(box->buttonRole(b)) == int(role)) {
                    b->click();
                    return;
                }
            }
        }
    });
}

/* Boton de un QDialogButtonBox por ROL (button() solo acepta StandardButton). */
static QAbstractButton *buttonWithRole(QDialogButtonBox *box,
                                       QDialogButtonBox::ButtonRole role)
{
    const QList<QAbstractButton *> buttons = box->buttons();
    for (QAbstractButton *b : buttons) {
        if (int(box->buttonRole(b)) == int(role))
            return b;
    }
    return nullptr;
}

static void checkListSelect()
{
    ewguiqt::ListSelectDialog dlg(QStringLiteral("Elegir"), {"uno", "dos", "tres"}, 1);
    CHECK(dlg.chosenIndex() == -1, "chosenIndex deberia empezar en -1");
    auto *list = dlg.findChild<QListWidget *>();
    CHECK(list && list->currentRow() == 1, "no se preselecciono el indice actual");

    auto *box = dlg.findChild<QDialogButtonBox *>();
    CHECK(box != nullptr, "falta el QDialogButtonBox");
    if (box) {
        QAbstractButton *accept = buttonWithRole(box, QDialogButtonBox::AcceptRole);
        CHECK(accept != nullptr, "falta el boton Accept");
        if (accept) {
            accept->click();
            CHECK(dlg.result() == QDialog::Accepted, "Accept no cerro con Accepted");
            CHECK(dlg.chosenIndex() == 1, "Accept no devolvio el indice seleccionado");
        }
    }

    /* pick() con Accept automatico. */
    clickModalButton(QDialogButtonBox::AcceptRole);
    CHECK(ewguiqt::ListSelectDialog::pick(nullptr, "t", {"a", "b", "c"}, 2) == 2,
          "pick() no devolvio el indice elegido");

    /* pick() cancelado (Esc/cierre) => -1. */
    clickModalButton(QDialogButtonBox::RejectRole);
    CHECK(ewguiqt::ListSelectDialog::pick(nullptr, "t", {"a", "b"}, 0) == -1,
          "pick() cancelado deberia devolver -1");
}

static void checkConfirm()
{
    clickModalButton(QDialogButtonBox::AcceptRole);
    CHECK(ewguiqt::confirm(nullptr, "Seguro?"), "confirm() con Yes deberia ser true");

    clickModalButton(QDialogButtonBox::RejectRole);
    CHECK(!ewguiqt::confirm(nullptr, "Seguro?"), "confirm() con No deberia ser false");

    clickModalButton(QDialogButtonBox::RejectRole, /*close=*/true);
    CHECK(!ewguiqt::confirm(nullptr, "Seguro?"), "cerrar la ventana deberia ser false");
}

static void checkTheme(QApplication &app)
{
    const QColor before = app.palette().color(QPalette::Window);
    CHECK(!ewguiqt::dark_mode(app), "el modo oscuro arranca apagado");

    ewguiqt::set_dark_mode(app, true);
    CHECK(ewguiqt::dark_mode(app), "set_dark_mode(true) no se refleja");
    const QColor dark = app.palette().color(QPalette::Window);
    CHECK(dark.lightness() < 128, "la paleta oscura no es oscura");

    ewguiqt::set_dark_mode(app, true);   /* idempotente */
    CHECK(app.palette().color(QPalette::Window) == dark, "set_dark_mode no es idempotente");

    ewguiqt::set_dark_mode(app, false);
    CHECK(!ewguiqt::dark_mode(app), "set_dark_mode(false) no se refleja");
    CHECK(app.palette().color(QPalette::Window) == before,
          "apagar el modo oscuro no restaura la paleta");
}

static void checkWireHeaderSort()
{
    /* Fila 0 = numero, fila 1 = texto. */
    QStandardItemModel model(3, 2);
    const QString nums[3] = {QStringLiteral("10"), QStringLiteral("5"), QStringLiteral("5.2")};
    const QString names[3] = {QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")};
    for (int i = 0; i < 3; ++i) {
        model.setItem(i, 0, new QStandardItem(nums[i]));
        model.setItem(i, 1, new QStandardItem(names[i]));
    }

    QTableView view;
    ewguiqt::SortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setColumnKinds({ewguiqt::SortKind::Number, ewguiqt::SortKind::Text});
    view.setModel(&proxy);

    ewguiqt::wire_header_sort(&view, &proxy, 0);

    QHeaderView *h = view.horizontalHeader();
    CHECK(!view.isSortingEnabled(), "wire_header_sort debe desactivar el orden propio de Qt");
    CHECK(h->isSortIndicatorShown(), "la flecha de orden debe estar visible");
    CHECK(proxy.lastColumn() == 0, "el orden inicial debe fijar la columna");
    CHECK(proxy.lastOrder() == Qt::DescendingOrder,
          "una columna numerica debe arrancar DESC (primer clic del GTK)");
    CHECK(h->sortIndicatorSection() == 0 && h->sortIndicatorOrder() == Qt::DescendingOrder,
          "la flecha debe coincidir con el orden real");

    /* Primer clic del usuario en la misma columna: invierte a ASC y la flecha
     * acompana (es el fallo que tenia el QTableView con setSortingEnabled). */
    QMetaObject::invokeMethod(h, "sectionClicked", Qt::DirectConnection, Q_ARG(int, 0));
    CHECK(proxy.lastOrder() == Qt::AscendingOrder, "el segundo clic debe invertir a ASC");
    CHECK(h->sortIndicatorOrder() == Qt::AscendingOrder,
          "la flecha no acompano la inversion del proxy");
    CHECK(view.model()->index(0, 0).data().toString() == QStringLiteral("5"),
          "ASC numerico mal ordenado");

    /* Columna de texto: ASC al primer clic. */
    QMetaObject::invokeMethod(h, "sectionClicked", Qt::DirectConnection, Q_ARG(int, 1));
    CHECK(proxy.lastColumn() == 1 && proxy.lastOrder() == Qt::AscendingOrder,
          "una columna de texto debe arrancar ASC");
    CHECK(view.model()->index(0, 1).data().toString() == QStringLiteral("a"),
          "ASC de texto mal ordenado");

    /* Y el segundo clic en texto invierte. */
    QMetaObject::invokeMethod(h, "sectionClicked", Qt::DirectConnection, Q_ARG(int, 1));
    CHECK(proxy.lastOrder() == Qt::DescendingOrder, "el segundo clic en texto debe invertir");
    CHECK(view.model()->index(0, 1).data().toString() == QStringLiteral("c"),
          "DESC de texto mal ordenado");
}

static void checkWireHeaderSortNoInitial()
{
    QStandardItemModel model(3, 2);
    const QString nums[3] = {QStringLiteral("10"), QStringLiteral("5"), QStringLiteral("5.2")};
    const QString names[3] = {QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")};

    for (int i = 0; i < 3; ++i) {
        model.setItem(i, 0, new QStandardItem(nums[i]));
        model.setItem(i, 1, new QStandardItem(names[i]));
    }

    QTableView view;
    ewguiqt::SortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setColumnKinds({ewguiqt::SortKind::Number, ewguiqt::SortKind::Text});
    view.setModel(&proxy);

    ewguiqt::wire_header_sort(&view, &proxy, -1);

    QHeaderView *h = view.horizontalHeader();
    CHECK(proxy.lastColumn() == -1, "sin orden inicial lastColumn debe ser -1");
    CHECK(!h->isSortIndicatorShown(), "sin orden inicial no debe haber flecha");
    CHECK(view.model()->index(0, 1).data().toString() == QStringLiteral("b"),
          "sin orden inicial se debe conservar el orden natural");

    /* El primer clic ordena y enciende la flecha. */
    QMetaObject::invokeMethod(h, "sectionClicked", Qt::DirectConnection, Q_ARG(int, 1));
    CHECK(proxy.lastColumn() == 1 && proxy.lastOrder() == Qt::AscendingOrder,
          "el primer clic de texto debe arrancar ASC");
    CHECK(h->isSortIndicatorShown(), "el primer clic debe mostrar la flecha");
    CHECK(view.model()->index(0, 1).data().toString() == QStringLiteral("a"),
          "el primer clic de texto ordena mal");
}

static void checkFitColumns()
{
    QStandardItemModel model(1, 2);

    model.setHeaderData(0, Qt::Horizontal, QStringLiteral("station"));
    model.setHeaderData(1, Qt::Horizontal, QStringLiteral("d"));
    model.setItem(0, 0, new QStandardItem(QStringLiteral("A")));
    model.setItem(0, 1, new QStandardItem(QStringLiteral("123456.7")));

    QTableView view;
    view.setModel(&model);
    const int total = ewguiqt::fit_columns(&view, 10);
    QHeaderView *h = view.horizontalHeader();

    const QFontMetrics headFm(h->font());
    const QFontMetrics cellFm(view.font());
    const int margin = view.style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, h);
    const int mark = view.style()->pixelMetric(QStyle::PM_HeaderMarkSize, nullptr, h);
    const int max0 = headFm.horizontalAdvance(QStringLiteral("station"))
                     + 2 * margin + mark + 2;
    const int max1 = cellFm.horizontalAdvance(QStringLiteral("123456.7")) + 10 + 2;

    CHECK(total > 0, "fit_columns devolvio un ancho total invalido");
    CHECK(!h->stretchLastSection(), "fit_columns debe desactivar el stretch de la ultima");
    CHECK(h->sectionSize(0) >= headFm.horizontalAdvance(QStringLiteral("station")),
          "la cabecera no manda en una columna de valores cortos");
    CHECK(h->sectionSize(0) <= max0, "la columna de la cabecera quedo demasiado ancha");
    CHECK(h->sectionSize(1) >= cellFm.horizontalAdvance(QStringLiteral("123456.7")),
          "el valor mas largo no manda en su columna");
    CHECK(h->sectionSize(1) <= max1, "la columna del valor quedo demasiado ancha");
    CHECK(h->sectionSize(0) < h->sectionSize(1),
          "una cabecera corta no puede quedar mas ancha que un valor largo");

    QStandardItemModel empty(0, 2);
    empty.setHeaderData(0, Qt::Horizontal, QStringLiteral("a"));
    empty.setHeaderData(1, Qt::Horizontal, QStringLiteral("bb"));
    QTableView view2;
    view2.setModel(&empty);
    CHECK(ewguiqt::fit_columns(&view2, 10) > 0,
          "un modelo vacio debe ajustarse a sus cabeceras");
    CHECK(ewguiqt::fit_columns(nullptr, 10) == 0,
          "fit_columns(NULL) debe devolver 0");
}

static void checkFillColumns()
{
    QStandardItemModel model(2, 2);

    model.setHeaderData(0, Qt::Horizontal, QStringLiteral("a"));
    model.setHeaderData(1, Qt::Horizontal, QStringLiteral("bb"));
    model.setItem(0, 0, new QStandardItem(QStringLiteral("x")));
    model.setItem(0, 1, new QStandardItem(QStringLiteral("123456.7")));
    model.setItem(1, 0, new QStandardItem(QStringLiteral("yy")));
    model.setItem(1, 1, new QStandardItem(QStringLiteral("9")));

    QTableView view;
    view.setModel(&model);
    view.resize(900, 200);
    view.show();
    QApplication::processEvents();

    ewguiqt::ColumnFitter fit(&view, 10);
    fit.fit();
    QHeaderView *h = view.horizontalHeader();
    const QVector<int> raw = ewguiqt::column_min_widths(&view, 10);

    CHECK(raw.size() == 2, "column_min_widths debe devolver una entrada por columna");
    CHECK(fit.natural_total() == raw.at(0) + raw.at(1),
          "natural_total no coincide con los minimos");
    CHECK(h->sectionSize(0) >= raw.at(0) && h->sectionSize(1) >= raw.at(1),
          "el reparto no puede encoger por debajo del minimo");
    CHECK(!h->stretchLastSection(),
          "el reparto debe desactivar el stretch de la ultima");

    /* Minimos EFECTIVOS: Qt puede subirlos por `minimumSectionSize`. Se miden
     * con un viewport mas angosto que la suma de minimos (no hay sobrante). */
    view.resize(60, 200);
    QApplication::processEvents();
    const int eff0 = h->sectionSize(0), eff1 = h->sectionSize(1);

    CHECK(eff0 >= raw.at(0) && eff1 >= raw.at(1),
          "el minimo efectivo no puede quedar por debajo del calculado");
    CHECK(h->sectionSize(0) == eff0 && h->sectionSize(1) == eff1,
          "con el viewport angosto deben quedar los minimos");
    CHECK(h->length() > view.viewport()->width(),
          "con el viewport angosto la tabla debe exceder el ancho");
    CHECK(view.horizontalScrollBar()->isVisible(),
          "con el viewport angosto debe aparecer scroll horizontal");

    /* Volver a ensanchar: la tabla ocupa el viewport y el extra es igual. */
    view.resize(900, 200);
    QApplication::processEvents();
    int sum = h->sectionSize(0) + h->sectionSize(1);
    CHECK(qAbs(sum - view.viewport()->width()) <= 1,
          "con sobra la tabla debe ocupar todo el viewport");
    CHECK(qAbs((h->sectionSize(0) - eff0) - (h->sectionSize(1) - eff1)) <= 1,
          "el sobrante debe repartirse por igual");
    CHECK(!view.horizontalScrollBar()->isVisible(),
          "sin sobra no debe aparecer scroll horizontal");

    /* Tercera columna: el resto de 1 px no rompe la igualdad. */
    {
        QStandardItemModel m3(1, 3);
        m3.setHeaderData(0, Qt::Horizontal, QStringLiteral("a"));
        m3.setHeaderData(1, Qt::Horizontal, QStringLiteral("bb"));
        m3.setHeaderData(2, Qt::Horizontal, QStringLiteral("ccc"));
        m3.setItem(0, 0, new QStandardItem(QStringLiteral("1")));
        m3.setItem(0, 1, new QStandardItem(QStringLiteral("2")));
        m3.setItem(0, 2, new QStandardItem(QStringLiteral("3")));
        QTableView v3;
        v3.setModel(&m3);
        v3.resize(60, 120);
        v3.show();
        QApplication::processEvents();
        ewguiqt::ColumnFitter f3(&v3, 10);
        f3.fit();
        QHeaderView *h3 = v3.horizontalHeader();
        const int eff[3] = { h3->sectionSize(0), h3->sectionSize(1),
                             h3->sectionSize(2) };

        v3.resize(500, 120);
        QApplication::processEvents();
        const int e0 = h3->sectionSize(0) - eff[0];
        const int e1 = h3->sectionSize(1) - eff[1];
        const int e2 = h3->sectionSize(2) - eff[2];
        const int emax = qMax(e0, qMax(e1, e2));
        const int emin = qMin(e0, qMin(e1, e2));

        CHECK(emax - emin <= 1, "el resto de 1 px debe repartirse sin romper la igualdad");
        CHECK(qAbs(h3->length() - v3.viewport()->width()) <= 1,
              "tres columnas tambien deben llenar el viewport");
    }

    /* Barra vertical: el viewport se angosta y el reparto la sigue. */
    for (int i = 0; i < 200; ++i)
        model.appendRow({ new QStandardItem(QStringLiteral("r")),
                          new QStandardItem(QStringLiteral("1")) });
    view.resize(900, 120);
    QApplication::processEvents();
    CHECK(view.verticalScrollBar()->isVisible(),
          "con 200 filas y 120 px debe aparecer la barra vertical");
    sum = h->sectionSize(0) + h->sectionSize(1);
    CHECK(qAbs(sum - view.viewport()->width()) <= 1,
          "la aparicion de la barra vertical debe reajustar el reparto");

    /* Columna oculta: no recibe extra y se excluye de la suma. */
    view.setColumnHidden(0, true);
    fit.fit();
    const QVector<int> hiddenMin = ewguiqt::column_min_widths(&view, 10);
    CHECK(hiddenMin.size() == 2 && hiddenMin.at(0) == 0,
          "una columna oculta debe quedar fuera de los minimos");
    CHECK(qAbs(h->sectionSize(1) - view.viewport()->width()) <= 1,
          "la columna visible debe quedarse con todo el ancho");
    view.setColumnHidden(0, false);

    /* Modelo vacio y vista nula. */
    {
        QStandardItemModel empty(0, 2);

        empty.setHeaderData(0, Qt::Horizontal, QStringLiteral("a"));
        empty.setHeaderData(1, Qt::Horizontal, QStringLiteral("bb"));
        QTableView v2;
        v2.setModel(&empty);
        v2.resize(400, 120);
        v2.show();
        QApplication::processEvents();
        ewguiqt::ColumnFitter f2(&v2, 10);
        f2.fit();
        CHECK(qAbs(v2.horizontalHeader()->length() - v2.viewport()->width()) <= 1,
              "un modelo vacio debe llenar el viewport con sus cabeceras");
    }
    CHECK(ewguiqt::column_min_widths(nullptr, 10).isEmpty(),
          "column_min_widths(NULL) debe devolver un vector vacio");
    CHECK(ewguiqt::layout_columns(nullptr, QVector<int>()) == 0,
          "layout_columns(NULL) debe devolver 0");
}

static void checkLogView()
{
    QTemporaryDir dir;
    CHECK(dir.isValid(), "no se pudo crear el directorio temporal");
    const QString path = dir.filePath("probe_1.log");

    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text), "no se pudo escribir el log");
    for (int i = 0; i < 200; ++i)
        f.write(QStringLiteral("linea %1 de relleno\n").arg(i, 4, 10, QChar('0')).toUtf8());
    f.close();

    ewguiqt::LogView view;
    view.setTailBytes(40);
    CHECK(view.loadPath(path), "loadPath fallo con un fichero valido");
    const QString text = view.toPlainText();
    CHECK(!text.isEmpty(), "el visor quedo vacio");
    CHECK(text.startsWith(QStringLiteral("linea 0")), "el recorte no empieza en borde de linea");
    CHECK(!text.contains(QStringLiteral("de relleno\nlinea")), "el recorte no conserva lineas enteras");

    CHECK(!view.loadPath(dir.filePath("no-existe.log")), "loadPath deberia fallar");
    CHECK(view.toPlainText().isEmpty(), "un fallo de loadPath deberia vaciar el visor");

    view.setStatus(QStringLiteral("(no log file)"));
    CHECK(view.toPlainText() == QStringLiteral("(no log file)"), "setStatus no pinta el mensaje");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    checkListSelect();
    checkConfirm();
    checkTheme(app);
    checkWireHeaderSort();
    checkWireHeaderSortNoInitial();
    checkFitColumns();
    checkFillColumns();
    checkLogView();

    if (g_fail == 0)
        std::printf("OK: selectores, confirmacion, tema, orden de cabecera y visor de log\n");
    return g_fail;
}
