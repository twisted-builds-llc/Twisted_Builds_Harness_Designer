// SPDX-License-Identifier: GPL-3.0-or-later
#include "bom/BomCsv.h"

#include <QCoreApplication>
#include <QLocale>

namespace tbh::bom {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("tbh::bom::BomCsv", text);
}

QString field(const QString &value)
{
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('\r'))) {
        QString escaped = value;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return value;
}

QString row(const QStringList &fields)
{
    QStringList escaped;
    for (const QString &f : fields)
        escaped.append(field(f));
    return escaped.join(QLatin1Char(',')) + QStringLiteral("\r\n");
}

QString number(double value, int decimals)
{
    QString text = QLocale::c().toString(value, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    return text;
}

QString length(double mm, Units units)
{
    return number(mmToUnits(mm, units), units == Units::Inches ? 2 : 1);
}

QString gauge(const Gauge &g)
{
    return number(g.value, 3) + (g.system == GaugeSystem::Awg ? QStringLiteral(" AWG") : QStringLiteral(" mm2"));
}

QString partNumberOrNone(const QString &pn)
{
    return pn.isEmpty() ? tr("(no part number)") : pn;
}

QString categoryName(PartCategory c)
{
    switch (c) {
    case PartCategory::Connector:
        return tr("Connector");
    case PartCategory::Terminal:
        return tr("Terminal");
    case PartCategory::Seal:
        return tr("Seal");
    case PartCategory::CavityPlug:
        return tr("Cavity plug");
    case PartCategory::Splice:
        return tr("Splice");
    }
    return {};
}

QString sectionTitle(CsvSection section)
{
    switch (section) {
    case CsvSection::Wires:
        return tr("Wire");
    case CsvSection::CutList:
        return tr("Cut list");
    case CsvSection::Sleeving:
        return tr("Sleeving");
    case CsvSection::Parts:
        return tr("Parts");
    case CsvSection::Extras:
        return tr("Other items");
    }
    return {};
}

} // namespace

QString sectionFileName(CsvSection section)
{
    switch (section) {
    case CsvSection::Wires:
        return QStringLiteral("wires");
    case CsvSection::CutList:
        return QStringLiteral("cut-list");
    case CsvSection::Sleeving:
        return QStringLiteral("sleeving");
    case CsvSection::Parts:
        return QStringLiteral("parts");
    case CsvSection::Extras:
        return QStringLiteral("other-items");
    }
    return {};
}

QString sectionToCsv(const Bom &bom, CsvSection section, Units units)
{
    const QString u = unitsSymbol(units);
    QString csv;
    switch (section) {
    case CsvSection::Wires:
        csv += row({tr("Color"), tr("Stripe"), tr("Gauge"), tr("Wire type"), tr("Pieces"),
                    tr("Length (%1)").arg(u), tr("Length with waste (%1)").arg(u), tr("Pieces without length")});
        for (const WireLine &w : bom.wires)
            csv += row({w.baseColor, w.stripeColor, gauge(w.gauge), w.wireTypeName.isEmpty() ? w.wireTypeId : w.wireTypeName,
                        QString::number(w.pieces), length(w.lengthMm, units), length(w.lengthWithWasteMm, units),
                        QString::number(w.piecesWithoutLength)});
        break;
    case CsvSection::CutList:
        csv += row({tr("Wire"), tr("End A"), tr("End B"), tr("Color"), tr("Stripe"), tr("Gauge"), tr("Wire type"),
                    tr("Cut length (%1)").arg(u), tr("Note")});
        for (const CutLine &c : bom.cutList)
            csv += row({c.name, c.endA, c.endB, c.baseColor, c.stripeColor, gauge(c.gauge), c.wireTypeName,
                        c.cutMm ? length(*c.cutMm, units) : QString(),
                        !c.cutMm ? tr("not routed") : (c.overridden ? tr("manual length") : QString())});
        break;
    case CsvSection::Sleeving:
        csv += row({tr("Type"), tr("Size"), tr("Segments"), tr("Length (%1)").arg(u),
                    tr("Length with waste (%1)").arg(u)});
        for (const SleevingLine &s : bom.sleeving)
            csv += row({s.family, s.known ? s.sizeLabel : tr("(not in library)"), QString::number(s.segments),
                        length(s.lengthMm, units), length(s.lengthWithWasteMm, units)});
        break;
    case CsvSection::Parts:
        csv += row({tr("Category"), tr("Part number"), tr("Description"), tr("Quantity"), tr("Used at")});
        for (const PartLine &p : bom.parts)
            csv += row({categoryName(p.category), partNumberOrNone(p.partNumber), p.description,
                        QString::number(p.quantity), p.references.join(QStringLiteral("; "))});
        break;
    case CsvSection::Extras:
        csv += row({tr("Part number"), tr("Description"), tr("Quantity"), tr("Unit"), tr("Notes")});
        for (const BomExtra &e : bom.extras)
            csv += row({e.partNumber, e.description, number(e.quantity, 3), e.unit, e.notes});
        break;
    }
    return csv;
}

QString bomToCsv(const Bom &bom, Units units)
{
    QString csv;
    for (CsvSection section : {CsvSection::Wires, CsvSection::CutList, CsvSection::Sleeving, CsvSection::Parts,
                               CsvSection::Extras}) {
        if (!csv.isEmpty())
            csv += QStringLiteral("\r\n");
        csv += row({sectionTitle(section)});
        csv += sectionToCsv(bom, section, units);
    }
    return csv;
}

} // namespace tbh::bom
