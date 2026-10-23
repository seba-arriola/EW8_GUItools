/* Ver include/ewguiqt/list_select.h. */
#include "ewguiqt/list_select.h"

#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace ewguiqt {

ListSelectDialog::ListSelectDialog(const QString &title, const QStringList &labels,
                                   int current, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(360);

    auto *layout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->addItems(labels);
    if (current >= 0 && current < labels.size()) {
        m_list->setCurrentRow(current);
        m_list->scrollToItem(m_list->item(current));
    }
    layout->addWidget(m_list);

    auto *box = new QDialogButtonBox(this);
    QPushButton *accept = box->addButton(tr("Accept"), QDialogButtonBox::AcceptRole);
    box->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    accept->setDefault(true);
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &ListSelectDialog::acceptCurrent);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { acceptCurrent(); });
    connect(m_list, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *) { acceptCurrent(); });
}

void ListSelectDialog::acceptCurrent()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;   /* sin seleccion no se acepta: mismo efecto que Cancel */
    m_chosen = row;
    accept();
}

int ListSelectDialog::pick(QWidget *parent, const QString &title,
                           const QStringList &labels, int current)
{
    ListSelectDialog dlg(title, labels, current, parent);
    return dlg.exec() == QDialog::Accepted ? dlg.chosenIndex() : -1;
}

} /* namespace ewguiqt */
