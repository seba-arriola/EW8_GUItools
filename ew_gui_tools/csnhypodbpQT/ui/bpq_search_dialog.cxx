/*
 * bpq_search_dialog.cxx - Ver bpq_search_dialog.h.
 */
#include "bpq_search_dialog.h"

#include <QCalendarWidget>
#include <QDate>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

/* "YYYY-MM-DD HH:MM:SS" -> "HH:MM:SS"; si no lo trae, medianoche. */
static QString time_part(const QString &text)
{
    const QString s = text.trimmed();
    int i = s.size();

    while (i > 0 && s.at(i - 1) != QLatin1Char(' ') && s.at(i - 1) != QLatin1Char('T'))
        i--;
    {
        const QString tok = s.mid(i);
        if (tok.count(QLatin1Char(':')) == 2) return tok;
    }
    return QStringLiteral("00:00:00");
}

static QString with_date(const QString &text, const QDate &d)
{
    return d.toString(QStringLiteral("yyyy-MM-dd")) + QLatin1Char(' ') + time_part(text);
}

/* Calendario en ventana MODAL propia: los popups de Qt quedan detras de la
 * ventana bajo WSLg/Wayland (bug #1299), mismo motivo que el resto del prototipo. */
static bool pick_date(QWidget *parent, const QString &cur, QDate *out)
{
    QDialog dlg(parent);
    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    QCalendarWidget *cal = new QCalendarWidget(&dlg);
    const QDate curDate = QDate::fromString(cur.left(10), QStringLiteral("yyyy-MM-dd"));

    dlg.setObjectName("calendar-dialog");
    dlg.setWindowTitle(QStringLiteral("Pick date"));
    dlg.setModal(true);
    cal->setGridVisible(true);
    if (curDate.isValid()) cal->setSelectedDate(curDate);
    lay->addWidget(cal);

    QDialogButtonBox *box =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);

    if (dlg.exec() != QDialog::Accepted) return false;
    *out = cal->selectedDate();
    return true;
}

BpqSearchDialog::BpqSearchDialog(const QString &from, const QString &to,
                                 int maxEvents, QWidget *parent)
    : QDialog(parent)
{
    setObjectName("search-dialog");
    setWindowTitle(QStringLiteral("Search events"));
    setModal(true);

    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_from = new QLineEdit(from, this);
    m_from->setObjectName("search-from");
    m_from->setMaxLength(32);
    m_from->setPlaceholderText(QStringLiteral("YYYY-MM-DD HH:MM:SS"));
    m_to = new QLineEdit(to, this);
    m_to->setObjectName("search-to");
    m_to->setMaxLength(32);
    m_to->setPlaceholderText(QStringLiteral("YYYY-MM-DD HH:MM:SS"));

    m_max = new QSpinBox(this);
    m_max->setObjectName("search-max");
    m_max->setRange(1, 100000);
    m_max->setValue(maxEvents);
    /* Sin los +/- gigantes del tema: ancho acotado al texto. */
    m_max->setMaximumWidth(120);

    /* Cada campo con su boton de calendario: escribe la fecha conservando la
     * hora que ya tuviera el texto (o medianoche si no traia). */
    auto *fromRow = new QWidget(this);
    auto *fromLay = new QHBoxLayout(fromRow);
    fromLay->setContentsMargins(0, 0, 0, 0);
    fromLay->addWidget(m_from, 1);
    auto *fromBtn = new QPushButton(QStringLiteral("..."), fromRow);
    fromBtn->setObjectName("search-from-cal");
    fromBtn->setFixedWidth(32);
    fromLay->addWidget(fromBtn, 0);
    connect(fromBtn, &QPushButton::clicked, this, [this]() {
        QDate d;
        if (pick_date(this, m_from->text(), &d))
            m_from->setText(with_date(m_from->text(), d));
    });

    auto *toRow = new QWidget(this);
    auto *toLay = new QHBoxLayout(toRow);
    toLay->setContentsMargins(0, 0, 0, 0);
    toLay->addWidget(m_to, 1);
    auto *toBtn = new QPushButton(QStringLiteral("..."), toRow);
    toBtn->setObjectName("search-to-cal");
    toBtn->setFixedWidth(32);
    toLay->addWidget(toBtn, 0);
    connect(toBtn, &QPushButton::clicked, this, [this]() {
        QDate d;
        if (pick_date(this, m_to->text(), &d))
            m_to->setText(with_date(m_to->text(), d));
    });

    form->addRow(QStringLiteral("Origin time from"), fromRow);
    form->addRow(QStringLiteral("Origin time to"), toRow);
    form->addRow(QStringLiteral("Max events"), m_max);
    lay->addLayout(form);

    auto *hint = new QLabel(QStringLiteral(
        "Tiempos en UTC. El rango es obligatorio; con Max events se acota la "
        "consulta a los mas recientes del rango."), this);
    hint->setObjectName("search-hint");
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(box);
}

QString BpqSearchDialog::fromText() const { return m_from->text().trimmed(); }
QString BpqSearchDialog::toText() const   { return m_to->text().trimmed(); }
int     BpqSearchDialog::maxEvents() const { return m_max->value(); }
