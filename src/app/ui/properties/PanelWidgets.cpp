// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/PanelWidgets.h"

#include "ui/WireColors.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>

namespace tbh::app {

using namespace Qt::StringLiterals;

CommitTextEdit::CommitTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setTabChangesFocus(true);
    setMaximumHeight(80);
}

void CommitTextEdit::setTextIfDifferent(const QString &text)
{
    if (toPlainText() != text)
        setPlainText(text);
}

void CommitTextEdit::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    if (onCommit)
        onCommit();
}

namespace {

QIcon swatch(const QColor &color)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(Qt::gray);
    painter.setBrush(color.isValid() ? QBrush(color) : QBrush(Qt::NoBrush));
    painter.drawRect(QRectF(0.5, 0.5, 13, 13));
    return QIcon(pixmap);
}

} // namespace

WireColorCombo::WireColorCombo(bool allowNone, QWidget *parent)
    : QComboBox(parent)
    , m_allowNone(allowNone)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    if (allowNone)
        addItem(tr("None"), QString());
    for (const QString &code : standardWireColorCodes())
        addItem(swatch(wireColor(code)), QStringLiteral("%1 - %2").arg(code, wireColorName(code)), code);
}

std::optional<QString> WireColorCombo::value() const
{
    const QString text = currentText().trimmed();
    const int index = findText(text);
    if (index >= 0)
        return itemData(index).toString();
    if (text.isEmpty())
        return m_allowNone ? std::optional(QString()) : std::nullopt;
    for (const QString &code : standardWireColorCodes()) {
        if (text.compare(code, Qt::CaseInsensitive) == 0)
            return code;
    }
    if (text.startsWith(QLatin1Char('#')) && QColor::fromString(text).isValid())
        return QColor::fromString(text).name();
    return std::nullopt;
}

void WireColorCombo::setValue(const QString &value)
{
    lineEdit()->setPlaceholderText({});
    const int index = findData(value);
    if (index >= 0)
        setCurrentIndex(index);
    else
        setEditText(value);
}

void WireColorCombo::setMixed()
{
    setEditText({});
    lineEdit()->setPlaceholderText(tr("(multiple)"));
}

QString gaugeText(const Gauge &gauge)
{
    const QString value = QString::number(gauge.value);
    return gauge.system == GaugeSystem::Awg ? QStringLiteral("%1 AWG").arg(value)
                                            : QStringLiteral("%1 mm²").arg(value);
}

GaugeEditor::GaugeEditor(QWidget *parent)
    : QWidget(parent)
    , m_value(new QComboBox(this))
    , m_system(new QComboBox(this))
{
    m_value->setEditable(true);
    m_value->setInsertPolicy(QComboBox::NoInsert);
    m_system->addItem(QStringLiteral("AWG"), int(GaugeSystem::Awg));
    m_system->addItem(QStringLiteral("mm²"), int(GaugeSystem::Mm2));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_value, 1);
    layout->addWidget(m_system);
    fillValues();
    connect(m_value->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        if (onCommit)
            onCommit();
    });
    connect(m_value, &QComboBox::activated, this, [this] {
        if (onCommit)
            onCommit();
    });
    connect(m_system, &QComboBox::activated, this, [this] {
        fillValues();
        if (onCommit)
            onCommit();
    });
}

void GaugeEditor::fillValues()
{
    const QString text = m_value->currentText();
    m_value->clear();
    const QStringList values = GaugeSystem(m_system->currentData().toInt()) == GaugeSystem::Awg
                                   ? QStringList{u"30"_s, u"28"_s, u"26"_s, u"24"_s, u"22"_s, u"20"_s,
                                                 u"18"_s, u"16"_s, u"14"_s, u"12"_s, u"10"_s, u"8"_s,
                                                 u"6"_s, u"4"_s, u"2"_s, u"1"_s, u"0"_s}
                                   : QStringList{u"0.13"_s, u"0.22"_s, u"0.35"_s, u"0.5"_s, u"0.75"_s,
                                                 u"1"_s, u"1.5"_s, u"2.5"_s, u"4"_s, u"6"_s, u"10"_s,
                                                 u"16"_s, u"25"_s, u"35"_s, u"50"_s};
    m_value->addItems(values);
    m_value->setEditText(text);
}

std::optional<Gauge> GaugeEditor::value() const
{
    bool ok = false;
    const double v = m_value->currentText().trimmed().toDouble(&ok);
    if (!ok || v < 0)
        return std::nullopt;
    return Gauge{v, GaugeSystem(m_system->currentData().toInt())};
}

void GaugeEditor::setValue(const Gauge &gauge)
{
    m_value->lineEdit()->setPlaceholderText({});
    m_system->setCurrentIndex(m_system->findData(int(gauge.system)));
    fillValues();
    m_value->setEditText(QString::number(gauge.value));
}

void GaugeEditor::setMixed()
{
    m_value->setEditText({});
    m_value->lineEdit()->setPlaceholderText(tr("(multiple)"));
}

} // namespace tbh::app
