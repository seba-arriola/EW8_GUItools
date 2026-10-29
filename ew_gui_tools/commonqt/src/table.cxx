/* Ver include/ewguiqt/table.h. */
#include "ewguiqt/table.h"

#include <cmath>

#include <QEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QStyle>
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
    if (column < 0) {
        /* Sin columna inicial: se conserva el orden natural del modelo fuente
         * hasta el primer clic (p.ej. estaciones/mags por distancia). */
        m_order = Qt::AscendingOrder;
        return;
    }
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
                         header->setSortIndicatorShown(true);
                         header->setSortIndicator(column, proxy->lastOrder());
                     });

    proxy->sortInitial(initialColumn);
    const QSignalBlocker blocker(header);
    if (initialColumn >= 0) {
        header->setSortIndicatorShown(true);
        header->setSortIndicator(initialColumn, proxy->lastOrder());
    } else {
        /* -1 = orden natural hasta que el usuario pulse una cabecera. */
        header->setSortIndicatorShown(false);
    }
}

QVector<int> column_min_widths(QTableView *view, int padding, int maxColumnWidth)
{
    if (!view || padding < 0)
        return QVector<int>();

    QAbstractItemModel *model = view->model();
    QHeaderView *header = view->horizontalHeader();
    if (!model || !header)
        return QVector<int>();

    const QFontMetrics cellFm(view->font());
    const QFontMetrics headFm(header->font());
    const int margin = view->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr,
                                                  header);
    const int mark = view->style()->pixelMetric(QStyle::PM_HeaderMarkSize, nullptr,
                                                header);
    const int rows = model->rowCount();
    const int n = model->columnCount();
    QVector<int> out(n, 0);

    for (int c = 0; c < n; ++c) {
        if (view->isColumnHidden(c))
            continue;

        /* Cabecera: texto con su propia fuente + margen + hueco de la flecha
         * (se reserva en todas para que al cambiar el orden no se recorte). */
        const QString head = model->headerData(c, Qt::Horizontal).toString();
        int width = headFm.horizontalAdvance(head) + 2 * margin + mark;

        for (int r = 0; r < rows; ++r) {
            const QString cell = model->data(model->index(r, c),
                                             Qt::DisplayRole).toString();
            const int cw = cellFm.horizontalAdvance(cell) + padding;

            if (cw > width)
                width = cw;
        }
        if (maxColumnWidth > 0 && width > maxColumnWidth)
            width = maxColumnWidth;
        if (width < 8)
            width = 8;
        out[c] = width;
    }
    return out;
}

int fit_columns(QTableView *view, int padding, int maxColumnWidth)
{
    if (!view || padding < 0)
        return 0;

    QAbstractItemModel *model = view->model();
    QHeaderView *header = view->horizontalHeader();
    if (!model || !header)
        return 0;

    const QVector<int> minima = column_min_widths(view, padding, maxColumnWidth);
    int total = 0;

    header->setStretchLastSection(false);
    for (int c = 0; c < minima.size(); ++c) {
        if (minima.at(c) <= 0)
            continue;

        /* Interactive (no Fixed): el ancho queda exacto, pero las secciones no
         * inflan el minimo de la ventana (trampa WSLg al maximizar). */
        header->setSectionResizeMode(c, QHeaderView::Interactive);
        header->resizeSection(c, minima.at(c));
        total += minima.at(c);
    }
    return total;
}

int layout_columns(QTableView *view, const QVector<int> &minima)
{
    if (!view)
        return 0;

    QAbstractItemModel *model = view->model();
    QHeaderView *header = view->horizontalHeader();
    if (!model || !header)
        return 0;

    const int n = model->columnCount();
    if (minima.size() != n)
        return 0;

    header->setStretchLastSection(false);

    /* 1. Fijar los minimos y leer el tamano EFECTIVO: Qt puede subirlo por
     * `minimumSectionSize` del estilo. El reparto se calcula sobre ese valor,
     * no sobre el pedido, para que la suma sea exacta. */
    QVector<int> eff(n, 0);
    int sum = 0, nvis = 0;

    for (int c = 0; c < n; ++c) {
        if (view->isColumnHidden(c) || minima.at(c) <= 0)
            continue;
        header->setSectionResizeMode(c, QHeaderView::Interactive);
        header->resizeSection(c, minima.at(c));
        eff[c] = header->sectionSize(c);
        sum += eff[c];
        ++nvis;
    }

    /* 2. Repartir el sobrante por igual; el resto de 1 px va de izquierda a
     * derecha. Si no alcanza, quedan los minimos (scroll horizontal). */
    const int vw = view->viewport()->width();

    if (vw > 0 && sum > 0 && nvis > 0 && sum < vw) {
        int extra = vw - sum;
        const int base = extra / nvis;
        int rem = extra % nvis;

        for (int c = 0; c < n; ++c) {
            if (eff.at(c) <= 0)
                continue;
            int w = eff.at(c) + base;

            if (rem > 0) {
                ++w;
                --rem;
            }
            header->resizeSection(c, w);
        }
    }

    int applied = 0;
    for (int c = 0; c < n; ++c) {
        if (eff.at(c) > 0)
            applied += header->sectionSize(c);
    }
    return applied;
}

ColumnFitter::ColumnFitter(QTableView *view, int padding, int maxColumnWidth,
                           QObject *parent)
    : QObject(parent), m_view(view), m_padding(padding), m_max(maxColumnWidth)
{
    if (m_view && m_view->viewport())
        m_view->viewport()->installEventFilter(this);
}

void ColumnFitter::refresh()
{
    if (!m_view)
        return;
    m_min = column_min_widths(m_view, m_padding, m_max);
    apply();
}

void ColumnFitter::apply()
{
    if (!m_view || m_applying)
        return;

    QAbstractItemModel *model = m_view->model();
    if (!model)
        return;
    if (m_min.size() != model->columnCount()) {
        /* Modelo cambiado desde el ultimo refresh: recalcular aca mismo (el
         * caso normal es que `fit()` lo haga despues de refrescar los datos). */
        m_min = column_min_widths(m_view, m_padding, m_max);
        if (m_min.size() != model->columnCount())
            return;
    }

    m_applying = true;
    layout_columns(m_view, m_min);
    m_applying = false;
}

void ColumnFitter::fit()
{
    refresh();
}

int ColumnFitter::natural_total() const
{
    int total = 0;

    for (int w : m_min)
        total += w;
    return total;
}

bool ColumnFitter::eventFilter(QObject *obj, QEvent *ev)
{
    if (m_view && obj == m_view->viewport() && ev->type() == QEvent::Resize &&
        !m_applying)
        apply();
    return QObject::eventFilter(obj, ev);
}

} /* namespace ewguiqt */
