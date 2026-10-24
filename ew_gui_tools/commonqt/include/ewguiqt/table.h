#ifndef EWGUIQT_TABLE_H
#define EWGUIQT_TABLE_H

/* Proxy de orden para las tablas de los modulos Qt.
 *
 * Reproduce la semantica del helper GTK (`ewgui/table_ui.h`), que es la que
 * ven los usuarios de las tres tablas de csnhypodbp y de las dos de
 * ew_controller:
 *   - la comparacion de texto DELEGA en `ewgui_table_cmp_values` (C puro): si
 *     ambos textos son numeros completos compara como numeros ("10" > "5.2"),
 *     si no alfabeticamente sin distinguir mayusculas. Un valor ausente ("",
 *     "-") no es numero y cae al final en descendente;
 *   - el PRIMER clic en una columna numerica o de fecha ordena DESCENDENTE; en
 *     texto, ascendente. El segundo clic invierte;
 *   - desempate opcional por otra columna (siempre por texto).
 *
 * `Number` convierte el valor con "%.6f" y NaN a "" (igual que
 * `prop_to_text()` de la implementacion GTK), asi una columna numerica ordena
 * por el numero aunque la celda muestre otra cosa.
 */

#include <functional>

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVector>

class QTableView;

namespace ewguiqt {

enum class SortKind {
    Text,    /* texto visible; primer clic ASC */
    Number,  /* numero formateado; primer clic DESC */
    Date     /* texto ISO (YYYY-MM-DD...); primer clic DESC */
};

class SortProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit SortProxy(QObject *parent = nullptr);

    /* `tiebreakColumn` < 0 = sin desempate. Las columnas no declaradas son Text. */
    void setColumnKinds(const QVector<SortKind> &kinds, int tiebreakColumn = -1);

    /* Accesores para modelos cuya celda visible no es el valor ordenable
     * (p.ej. magnitud "ML-4" que ordena por la magnitud). Por defecto:
     * texto = DisplayRole. `setCellNumberFn` solo se aplica a columnas Number
     * y sustituye el valor (formateado "%.6f", NaN = ""); una columna Number
     * sin accesor ordena por su texto visible, que ewgui_table_cmp_values ya
     * compara numericamente cuando lo es. */
    void setCellTextFn(std::function<QString(const QAbstractItemModel &, int, int)> fn);
    void setCellNumberFn(std::function<double(const QAbstractItemModel &, int, int)> fn);

    /* Orden inicial de la tabla (direccion del primer clic de esa columna). */
    void sortInitial(int column);
    /* Conectar a QHeaderView::sectionClicked: invierte si es la misma columna. */
    void sortByHeaderClick(int column);

    int lastColumn() const { return m_last; }
    Qt::SortOrder lastOrder() const { return m_order; }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString cellText(const QAbstractItemModel &model, int row, int column) const;
    Qt::SortOrder firstOrder(int column) const;

    QVector<SortKind> m_kinds;
    int m_tiebreak = -1;
    int m_last = -1;
    Qt::SortOrder m_order = Qt::AscendingOrder;
    std::function<QString(const QAbstractItemModel &, int, int)> m_textFn;
    std::function<double(const QAbstractItemModel &, int, int)> m_numberFn;
};

/* Cablea una QTableView a un SortProxy, que es lo que hace que las cabeceras
 * ordenen como el modulo GTK:
 *   - desactiva el orden propio de Qt (no conoce ni la direccion del primer
 *     clic ni el comparador compartido);
 *   - conecta `sectionClicked` a `SortProxy::sortByHeaderClick`;
 *   - mantiene la FLECHA del header en el orden REAL del proxy (Qt la mueve
 *     por su cuenta al hacer clic y mentiria en el primer clic numerico);
 *   - aplica el orden inicial con la misma direccion que el primer clic de esa
 *     columna (DESC en numericas/fecha, ASC en texto).
 *
 * Es el equivalente de `ewgui_table_column_sortable()` del helper GTK; se usa
 * en ew_controllerQT (dos tablas) y en csnhypodbpQT (eventos). */
void wire_header_sort(QTableView *view, SortProxy *proxy, int initialColumn);

/* Ajusta cada seccion visible al max(ancho de su cabecera, texto mas largo de
 * sus celdas) + padding, sin estirar la ultima. `maxColumnWidth > 0` acota el
 * resultado. Devuelve el ancho total de las secciones (para repartir el panel).
 * No toca las columnas ocultas ni el modelo. */
int fit_columns(QTableView *view, int padding = 10, int maxColumnWidth = -1);

} /* namespace ewguiqt */

#endif /* EWGUIQT_TABLE_H */
