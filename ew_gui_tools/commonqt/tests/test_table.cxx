/* C3: el orden de SortProxy es el MISMO que el comparador compartido
 * ewgui_table_cmp_values (el que usa el modulo GTK), y el primer clic en una
 * columna numerica ordena descendente. Test headless: QT_QPA_PLATFORM=offscreen
 * lo pone el Makefile. */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <QString>
#include <QStringList>
#include <QStandardItemModel>

#include "ewguiqt/table.h"

extern "C" {
#include "ewgui/table_ui.h"
}

static int g_fail = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);          \
            g_fail = 1;                                                          \
        }                                                                        \
    } while (0)

static QStringList rowsOf(const QAbstractItemModel &m, int column)
{
    QStringList out;
    for (int r = 0; r < m.rowCount(); ++r)
        out << m.index(r, column).data(Qt::DisplayRole).toString();
    return out;
}

/* Modelo de 2 columnas: nombre (texto) y pid (numero). */
static void fillModel(QStandardItemModel &m, const QStringList &names, const QStringList &pids)
{
    m.setColumnCount(2);
    for (int i = 0; i < names.size(); ++i) {
        m.setItem(i, 0, new QStandardItem(names.at(i)));
        m.setItem(i, 1, new QStandardItem(pids.at(i)));
    }
}

static void checkDelegation()
{
    /* Orden ascendente por texto == orden del comparador C, para una lista con
     * numeros completos, numeros con cola, vacios y guiones. */
    const QStringList values = {"5", "10", "5.2", "", "-", "2", "Abc", "abc",
                                "3.3807", "180", "5.2-10", "0"};
    QStandardItemModel model;
    model.setColumnCount(1);
    for (int i = 0; i < values.size(); ++i)
        model.setItem(i, 0, new QStandardItem(values.at(i)));

    ewguiqt::SortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setColumnKinds({ewguiqt::SortKind::Text});
    proxy.sortInitial(0);   /* texto: ascendente */

    QStringList got;
    for (int r = 0; r < proxy.rowCount(); ++r)
        got << proxy.index(r, 0).data(Qt::DisplayRole).toString();

    std::vector<int> idx(values.size());
    for (int i = 0; i < int(idx.size()); ++i)
        idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        return ewgui_table_cmp_values(values.at(a).toUtf8().constData(),
                                      values.at(b).toUtf8().constData()) < 0;
    });
    QStringList want;
    for (int i : idx)
        want << values.at(i);

    CHECK(got == want, "el orden ascendente no coincide con ewgui_table_cmp_values");
    if (got != want) {
        std::printf("  got : %s\n", got.join(',').toUtf8().constData());
        std::printf("  want: %s\n", want.join(',').toUtf8().constData());
    }
}

static void checkHeaderClicks()
{
    QStandardItemModel model;
    /* name, pid */
    fillModel(model,
              {"AA", "BB", "CC", "DD", "EE"},
              {"5", "10", "5.2", "", "-"});

    ewguiqt::SortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setColumnKinds({ewguiqt::SortKind::Text, ewguiqt::SortKind::Number});

    /* Primer clic en PID (Number) => descendente: numeros de mayor a menor y
     * los ausentes al final ("-" antes de "", como el comparador). */
    proxy.sortInitial(1);
    CHECK(rowsOf(proxy, 0) == QStringList({"BB", "CC", "AA", "EE", "DD"}),
          "PID descendente mal ordenado");
    CHECK(proxy.lastOrder() == Qt::DescendingOrder, "PID deberia arrancar descendente");

    /* Segundo clic: invierte. */
    proxy.sortByHeaderClick(1);
    CHECK(rowsOf(proxy, 0) == QStringList({"DD", "EE", "AA", "CC", "BB"}),
          "PID ascendente mal ordenado");
    CHECK(proxy.lastOrder() == Qt::AscendingOrder, "el segundo clic deberia invertir");

    /* Primera pulsacion en una columna de texto: ascendente. */
    proxy.sortByHeaderClick(0);
    CHECK(rowsOf(proxy, 0) == QStringList({"AA", "BB", "CC", "DD", "EE"}),
          "Module deberia arrancar ascendente");
    proxy.sortByHeaderClick(0);
    CHECK(rowsOf(proxy, 0) == QStringList({"EE", "DD", "CC", "BB", "AA"}),
          "Module deberia invertir al segundo clic");
}

static void checkTiebreak()
{
    QStandardItemModel model;
    fillModel(model, {"ZZ", "AA", "MM"}, {"5", "5", "6"});

    ewguiqt::SortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setColumnKinds({ewguiqt::SortKind::Text, ewguiqt::SortKind::Number}, 0);
    proxy.sortInitial(1);   /* desc: 6(MM), luego los 5 empatados por nombre */

    /* Desempate: MISMA direccion que la columna (el sorter del GTK invierte el
     * comparador entero, desempate incluido). */
    CHECK(rowsOf(proxy, 0) == QStringList({"MM", "ZZ", "AA"}),
          "el desempate por nombre no se aplica");
}

static void checkNumberFn()
{
    /* Celda visible "ML-4.2" pero el valor ordenable es -4.2 (como las
     * magnitudes de csnhodbp: el texto lleva la estacion). */
    QStandardItemModel model;
    model.setColumnCount(1);
    const QStringList labels = {"ML-0.5", "ML-4.2", "ML-1.0"};
    const double vals[] = {-0.5, -4.2, -1.0};
    for (int i = 0; i < labels.size(); ++i)
        model.setItem(i, 0, new QStandardItem(labels.at(i)));

    ewguiqt::SortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setColumnKinds({ewguiqt::SortKind::Number});
    proxy.setCellNumberFn([&](const QAbstractItemModel &m, int row, int) {
        (void)m;
        return vals[row];
    });
    proxy.sortInitial(0);   /* desc: -0.5, -1.0, -4.2 */

    QStringList got;
    for (int r = 0; r < proxy.rowCount(); ++r)
        got << proxy.index(r, 0).data(Qt::DisplayRole).toString();
    CHECK(got == QStringList({"ML-0.5", "ML-1.0", "ML-4.2"}),
          "el accesor numerico no se usa para ordenar");
}

int main()
{
    checkDelegation();
    checkHeaderClicks();
    checkTiebreak();
    checkNumberFn();

    if (g_fail == 0)
        std::printf("OK: orden de tablas identico al comparador compartido\n");
    return g_fail;
}
