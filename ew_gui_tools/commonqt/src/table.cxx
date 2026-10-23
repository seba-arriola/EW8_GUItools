/* Ver include/ewguiqt/table.h. */
#include "ewguiqt/table.h"

#include <cmath>

#include <QHeaderView>
#include <QSignalBlocker>
#include <QTableView>

/* C puro de common/ (sin GTK): el comparador es el MISMO que usa el modulo
 * GTK, asi el orden de las dos versiones es identico por construccion. */
extern "C" {
#include "ewgui/table_ui.h"
}

namespace ewguiqt {

SortProxy::SortProxy(QObject *parent) : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void SortProxy::setColumnKinds(const QVector<SortKind> &kinds, int tiebreakColumn)
{
    m_kinds = kinds;
    m_tiebreak = tiebreakColumn;
}

void SortProxy::setCellTextFn(std::function<QString(const QAbstractItemModel &, int, int)> fn)
{
    m_textFn = std::move(fn);
}

void SortProxy::setCellNumberFn(std::function<double(const QAbstractItemModel &, int, int)> fn)
{
    m_numberFn = std::move(fn);
}

Qt::SortOrder SortProxy::firstOrder(int column) const
{
    const SortKind kind = column >= 0 && column < m_kinds.size() ? m_kinds.at(column) : SortKind::Text;
    return kind == SortKind::Text ? Qt::AscendingOrder : Qt::DescendingOrder;
}

QString SortProxy::cellText(const QAbstractItemModel &model, int row, int column) const
{
    if (m_textFn)
        return m_textFn(model, row, column);
    return model.data(model.index(row, column), Qt::DisplayRole).toString();
}

bool SortProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QAbstractItemModel *model = sourceModel();
    if (!model || !left.isValid() || !right.isValid())
        return false;

    const int column = left.column();
    const bool numeric = column >= 0 && column < m_kinds.size()
                         && m_kinds.at(column) == SortKind::Number;

    QString a, b;
    if (numeric && m_numberFn) {
        /* Valor ordenable distinto del texto visible (p.ej. "ML-4.2"): se
         * convierte igual que prop_to_text() del GTK, %.6f y NaN = "". */
        const double va = m_numberFn(*model, left.row(), column);
        const double vb = m_numberFn(*model, right.row(), column);
        a = std::isnan(va) ? QString() : QString::number(va, 'f', 6);
        b = std::isnan(vb) ? QString() : QString::number(vb, 'f', 6);
    } else {
        /* Tambien para Number: ewgui_table_cmp_values ya compara como numeros
         * cuando ambos textos lo son ("10" > "5.2"), asi que una columna de
         * pid/Key/Size se ordena numericamente por su texto visible. */
        a = cellText(*model, left.row(), column);
        b = cellText(*model, right.row(), column);
    }

    const QByteArray ba = a.toUtf8();
    const QByteArray bb = b.toUtf8();
    int r = ewgui_table_cmp_values(ba.constData(), bb.constData());
    if (r == 0 && m_tiebreak >= 0) {
        const QByteArray ta = cellText(*model, left.row(), m_tiebreak).toUtf8();
        const QByteArray tb = cellText(*model, right.row(), m_tiebreak).toUtf8();
        r = ewgui_table_cmp_values(ta.constData(), tb.constData());
    }
    return r < 0;
}

void SortProxy::sortInitial(int column)
{
    m_last = column;
    m_order = firstOrder(column);
    sort(column, m_order);
}

void SortProxy::sortByHeaderClick(int column)
{
    if (column == m_last)
        m_order = m_order == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
    else
        m_order = firstOrder(column);
    m_last = column;
    sort(column, m_order);
}

void wire_header_sort(QTableView *view, SortProxy *proxy, int initialColumn)
{
    if (!view || !proxy)
        return;

    view->setSortingEnabled(false);
    QHeaderView *header = view->horizontalHeader();
    if (!header)
        return;

    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);

    QObject::connect(header, &QHeaderView::sectionClicked, proxy,
                     [header, proxy](int column) {
                         proxy->sortByHeaderClick(column);
                         /* QHeaderView mueve su flecha al hacer clic (asc<->desc)
                          * sin saber la direccion del PRIMER clic del proxy: se
                          * re-aplica para que la flecha no mienta. */
                         const QSignalBlocker blocker(header);
                         header->setSortIndicator(column, proxy->lastOrder());
                     });

    proxy->sortInitial(initialColumn);
    const QSignalBlocker blocker(header);
    header->setSortIndicator(initialColumn, proxy->lastOrder());
}

} /* namespace ewguiqt */
