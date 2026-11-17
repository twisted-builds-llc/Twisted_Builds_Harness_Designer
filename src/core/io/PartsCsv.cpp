// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/PartsCsv.h"

#include "model/Library.h"
#include "model/PinLabels.h"
#include "model/Queries.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>

namespace tbh::io {

namespace {
// Translation context for this file (picked up by lupdate).
class Tr
{
    Q_DECLARE_TR_FUNCTIONS(tbh::io::PartsCsv)
};

struct Record
{
    int line = 0; // where the record starts, 1-based
    QStringList cells;
};

// Splits CSV text into records: quoted cells may hold separators, quotes ("") and newlines.
std::vector<Record> parseRecords(const QString &text, QChar separator)
{
    std::vector<Record> records;
    Record record{1, {}};
    QString cell;
    bool quoted = false;
    int line = 1;
    auto endRecord = [&] {
        record.cells << cell;
        cell.clear();
        const bool blank = record.cells.size() == 1 && record.cells.first().trimmed().isEmpty();
        if (!blank)
            records.push_back(record);
        record = Record{line, {}};
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == u'"' && i + 1 < text.size() && text[i + 1] == u'"') {
                cell += u'"';
                ++i;
            } else if (c == u'"') {
                quoted = false;
            } else {
                if (c == u'\n')
                    ++line;
                cell += c;
            }
        } else if (c == u'"') {
            quoted = true;
        } else if (c == separator) {
            record.cells << cell;
            cell.clear();
        } else if (c == u'\n') {
            ++line;
            endRecord();
        } else if (c != u'\r') {
            cell += c;
        }
    }
    if (!cell.isEmpty() || !record.cells.isEmpty())
        endRecord();
    return records;
}

// The separator the header line uses most: ',' or ';'.
QChar detectSeparator(const QString &text)
{
    int commas = 0, semicolons = 0;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == u'"')
            quoted = !quoted;
        else if (!quoted && c == u'\n')
            break;
        else if (!quoted && c == u',')
            ++commas;
        else if (!quoted && c == u';')
            ++semicolons;
    }
    return semicolons > commas ? u';' : u',';
}

// Header text to a column key: "Insulation min (mm)" -> "insulationmin", "P/N" -> "pn".
QString columnKey(QString header)
{
    static const QRegularExpression parenthesized(QStringLiteral("\\(.*\\)"));
    static const QRegularExpression notAlnum(QStringLiteral("[^a-z0-9]"));
    header = header.toLower().remove(parenthesized).remove(notAlnum);
    if (header == u"pn" || header == u"partno")
        return QStringLiteral("partnumber");
    return header;
}

QStringList splitList(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[;,]"));
    QStringList list;
    for (const QString &item : text.split(separators, Qt::SkipEmptyParts)) {
        if (!item.trimmed().isEmpty())
            list << item.trimmed();
    }
    return list;
}

// Empty: nullopt and true. A positive number: it and true. Otherwise false.
bool readDiameter(const QString &text, std::optional<double> *value)
{
    if (text.trimmed().isEmpty()) {
        value->reset();
        return true;
    }
    bool ok = false;
    double v = QLocale::c().toDouble(text.trimmed(), &ok);
    if (!ok)
        v = QLocale().toDouble(text.trimmed(), &ok);
    if (!ok || v <= 0)
        return false;
    *value = v;
    return true;
}

template<typename T, typename Same>
void putLast(std::vector<T> &list, T item, Same same)
{
    const auto it = std::find_if(list.begin(), list.end(), [&](const T &existing) { return same(existing, item); });
    if (it != list.end())
        *it = std::move(item);
    else
        list.push_back(std::move(item));
}

} // namespace

PartsCsv readPartsCsv(const QByteArray &data)
{
    PartsCsv result;
    QString text = QString::fromUtf8(data);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    const std::vector<Record> records = parseRecords(text, detectSeparator(text));
    if (records.empty()) {
        result.problems << Tr::tr("The file is empty.");
        return result;
    }
    QHash<QString, qsizetype> columns;
    for (qsizetype i = 0; i < records[0].cells.size(); ++i)
        columns.insert(columnKey(records[0].cells[i]), i);
    if (!columns.contains(QStringLiteral("type")) || !columns.contains(QStringLiteral("partnumber"))) {
        result.problems << Tr::tr("The first line must name the columns, including \"Type\" and \"Part number\".");
        return result;
    }

    for (size_t r = 1; r < records.size(); ++r) {
        const Record &record = records[r];
        auto cell = [&](const char *key) {
            const qsizetype column = columns.value(QLatin1StringView(key), -1);
            return column >= 0 && column < record.cells.size() ? record.cells[column].trimmed() : QString();
        };
        auto problem = [&](const QString &message) {
            result.problems << Tr::tr("Line %1: %2").arg(record.line).arg(message);
        };
        const QString type = cell("type").toLower();
        const QString partNumber = cell("partnumber");
        if (partNumber.isEmpty()) {
            problem(Tr::tr("no part number."));
            continue;
        }
        if (type == u"housing" || type == u"connector") {
            ConnectorPart part;
            part.partNumber = partNumber;
            part.manufacturer = cell("manufacturer");
            part.family = cell("family");
            part.description = cell("description");
            const QString contacts = cell("contacts").toLower();
            if (contacts == u"male" || contacts == u"pins" || contacts == u"pin")
                part.gender = Gender::Male;
            else if (contacts == u"female" || contacts == u"sockets" || contacts == u"socket")
                part.gender = Gender::Female;
            else if (!contacts.isEmpty() && contacts != u"none") {
                problem(Tr::tr("%1: contacts must be male, female or none.").arg(partNumber));
                continue;
            }
            QString error;
            const auto labels = expandPinPattern(cell("cavities"), 1, false, &error);
            if (!labels) {
                problem(Tr::tr("%1: cavities: %2").arg(partNumber, error));
                continue;
            }
            part.cavityLabels = *labels;
            part.terminals = splitList(cell("terminals"));
            part.seals = splitList(cell("seals"));
            part.plugs = splitList(cell("plugs"));
            putLast(result.connectorParts, std::move(part),
                    [](const ConnectorPart &a, const ConnectorPart &b) { return a.partNumber == b.partNumber; });
            continue;
        }
        PinPart part;
        if (type == u"terminal" || type == u"contact")
            part.kind = PinPartKind::Terminal;
        else if (type == u"seal")
            part.kind = PinPartKind::Seal;
        else if (type == u"plug" || type == u"cavity plug" || type == u"blind plug")
            part.kind = PinPartKind::Plug;
        else {
            problem(Tr::tr("%1: type \"%2\" is not housing, terminal, seal or plug.").arg(partNumber, cell("type")));
            continue;
        }
        part.partNumber = partNumber;
        part.manufacturer = cell("manufacturer");
        part.description = cell("description");
        bool ok = true;
        for (const auto &[key, gauge] : {std::pair{"wirefrom", &part.wireMin}, std::pair{"wireto", &part.wireMax}}) {
            if (cell(key).isEmpty())
                continue;
            *gauge = parseGauge(cell(key));
            if (!*gauge) {
                problem(Tr::tr("%1: \"%2\" is not a wire size such as 18 AWG or 0.75 mm2.").arg(partNumber, cell(key)));
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;
        if (!readDiameter(cell("insulationmin"), &part.insulationMinMm)
            || !readDiameter(cell("insulationmax"), &part.insulationMaxMm)) {
            problem(Tr::tr("%1: insulation diameters must be positive numbers in millimeters.").arg(partNumber));
            continue;
        }
        putLast(result.pinParts, std::move(part), [](const PinPart &a, const PinPart &b) {
            return a.kind == b.kind && a.partNumber == b.partNumber;
        });
    }
    return result;
}

QByteArray partsCsvTemplate()
{
    return QByteArrayLiteral(
        "Type,Part number,Manufacturer,Description,Family,Contacts,Cavities,Terminals,Seals,Plugs,"
        "Wire from,Wire to,Insulation min (mm),Insulation max (mm)\r\n"
        "housing,EXAMPLE-H4,Example Co,4-way plug,Example series,female,1-4,EXAMPLE-T1;EXAMPLE-T2,"
        "EXAMPLE-S1,EXAMPLE-P1,,,,\r\n"
        "terminal,EXAMPLE-T1,Example Co,Socket,,,,,,,20 AWG,16 AWG,1.4,3.0\r\n"
        "terminal,EXAMPLE-T2,Example Co,Socket,,,,,,,0.75 mm2,1.5 mm2,,\r\n"
        "seal,EXAMPLE-S1,Example Co,Wire seal,,,,,,,,,1.4,2.1\r\n"
        "plug,EXAMPLE-P1,Example Co,Cavity plug,,,,,,,,,,\r\n");
}

void mergeImportedParts(Library &library, const PartsCsv &imported, int *added, int *replaced)
{
    *added = 0;
    *replaced = 0;
    for (const ConnectorPart &part : imported.connectorParts) {
        const bool exists = library.connectorPart(part.partNumber);
        ++(exists ? *replaced : *added);
        putLast(library.connectorParts, part,
                [](const ConnectorPart &a, const ConnectorPart &b) { return a.partNumber == b.partNumber; });
    }
    for (const PinPart &part : imported.pinParts) {
        const bool exists = library.pinPart(part.kind, part.partNumber);
        ++(exists ? *replaced : *added);
        putLast(library.pinParts, part, [](const PinPart &a, const PinPart &b) {
            return a.kind == b.kind && a.partNumber == b.partNumber;
        });
    }
}

} // namespace tbh::io
