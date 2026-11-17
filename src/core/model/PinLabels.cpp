// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/PinLabels.h"

#include "model/Harness.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

namespace tbh {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::PinLabels)
};
} // namespace

namespace {

QString formatNumber(qint64 value, qsizetype width)
{
    return QStringLiteral("%1").arg(value, int(width), 10, QLatin1Char('0'));
}

} // namespace

QString nextPinLabel(const QString &previous)
{
    static const QRegularExpression trailing(QStringLiteral("^(.*?)(\\d+)$"));
    const auto match = trailing.match(previous);
    if (!match.hasMatch())
        return {};
    const QString digits = match.captured(2);
    if (digits.size() > 15)
        return {};
    const qint64 next = digits.toLongLong() + 1;
    return match.captured(1) + formatNumber(next, digits.size());
}

QString compressPinLabels(const QStringList &labels)
{
    static const QRegularExpression numbered(QStringLiteral("^(.*?)(\\d{1,6})$"));
    QStringList tokens;
    for (qsizetype i = 0; i < labels.size();) {
        const auto match = numbered.match(labels[i]);
        qsizetype end = i + 1; // one past the run
        if (match.hasMatch()) {
            const QString prefix = match.captured(1);
            const QString digits = match.captured(2);
            const qsizetype width = digits.startsWith(QLatin1Char('0')) ? digits.size() : 0;
            const qint64 start = digits.toLongLong();
            while (end < labels.size()
                   && labels[end] == prefix + formatNumber(start + (end - i), width))
                ++end;
            if (end - i >= 3) {
                tokens << labels[i] + QLatin1Char('-') + labels[end - 1];
                i = end;
                continue;
            }
            end = i + 1;
        }
        tokens << labels[i];
        i = end;
    }
    const QString pattern = tokens.join(QStringLiteral(", "));
    if (expandPinPattern(pattern, 1, false, nullptr) == std::optional<QStringList>(labels))
        return pattern;
    return labels.join(QStringLiteral(", "));
}

std::optional<QStringList> expandPinPattern(const QString &pattern, int step, bool reverse,
                                            QString *error)
{
    auto failWith = [error](const QString &message) -> std::optional<QStringList> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (step < 1)
        return failWith(Tr::tr("Step must be 1 or more."));

    static const QRegularExpression range(
        QStringLiteral("^(.*?)(\\d{1,6})\\s*-\\s*(.*?)(\\d{1,6})$"));
    QStringList labels;
    for (const QString &rawToken : pattern.split(QLatin1Char(','))) {
        const QString token = rawToken.trimmed();
        if (token.isEmpty())
            continue;
        const auto match = range.match(token);
        if (!match.hasMatch()) {
            labels.append(token);
        } else {
            const QString prefix = match.captured(1);
            const QString endPrefix = match.captured(3).trimmed();
            if (!endPrefix.isEmpty() && endPrefix != prefix)
                return failWith(Tr::tr("\"%1\": both ends of a range need the same prefix.").arg(token));
            const QString startDigits = match.captured(2);
            const qsizetype width = startDigits.startsWith(QLatin1Char('0')) ? startDigits.size() : 0;
            const int start = startDigits.toInt();
            const int end = match.captured(4).toInt();
            const int direction = end >= start ? 1 : -1;
            const int count = (std::abs(end - start)) / step + 1;
            if (labels.size() + count > kMaxPins)
                return failWith(Tr::tr("The pattern gives more than %1 labels.").arg(kMaxPins));
            for (int i = 0; i < count; ++i)
                labels.append(prefix + formatNumber(start + direction * i * step, width));
        }
        if (labels.size() > kMaxPins)
            return failWith(Tr::tr("The pattern gives more than %1 labels.").arg(kMaxPins));
    }
    if (labels.isEmpty())
        return failWith(Tr::tr("The pattern gives no labels."));
    if (reverse)
        std::reverse(labels.begin(), labels.end());
    return labels;
}

} // namespace tbh
