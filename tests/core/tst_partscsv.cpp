// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/PartsCsv.h"
#include "model/Library.h"

#include <QTest>

using namespace tbh;
using namespace tbh::io;
using namespace Qt::StringLiterals;

class TestPartsCsv : public QObject
{
    Q_OBJECT

private slots:
    void readsAllTypes()
    {
        // Columns in another order, an extra column, a quoted list with commas, a quoted
        // description with a comma and a doubled quote, CRLF line ends and a byte order mark.
        const QByteArray csv = "\xEF\xBB\xBF"
                               "Part Number,Type,Notes,Cavities,Contacts,Terminals,Plugs,Wire from,Wire to,"
                               "Insulation min (mm),Insulation max (mm),Description\r\n"
                               "DT06-2S,Housing,ignored,1-2,sockets,\"T16, T14\",114017,,,,,\r\n"
                               "T16,terminal,,,,,,20 AWG,16,1.35,3.05,\"Socket, size 16 \"\"solid\"\"\"\r\n"
                               "\r\n"
                               "114017,plug,,,,,,,,,,\r\n";
        const PartsCsv parts = readPartsCsv(csv);
        QVERIFY2(parts.problems.isEmpty(), qPrintable(parts.problems.join(u'\n')));
        QCOMPARE(parts.connectorParts.size(), size_t(1));
        const ConnectorPart &housing = parts.connectorParts[0];
        QCOMPARE(housing.partNumber, u"DT06-2S"_s);
        QCOMPARE(housing.gender, Gender::Female);
        QCOMPARE(housing.cavityLabels, (QStringList{u"1"_s, u"2"_s}));
        QCOMPARE(housing.terminals, (QStringList{u"T16"_s, u"T14"_s}));
        QCOMPARE(housing.plugs, QStringList{u"114017"_s});
        QVERIFY(!housing.approximate);
        QCOMPARE(parts.pinParts.size(), size_t(2));
        const PinPart &terminal = parts.pinParts[0];
        QCOMPARE(terminal.kind, PinPartKind::Terminal);
        QCOMPARE(terminal.description, u"Socket, size 16 \"solid\""_s);
        QCOMPARE(terminal.wireMin, std::optional<Gauge>(Gauge{20, GaugeSystem::Awg}));
        QCOMPARE(terminal.wireMax, std::optional<Gauge>(Gauge{16, GaugeSystem::Awg})); // no unit: AWG
        QCOMPARE(terminal.insulationMinMm, std::optional<double>(1.35));
        QCOMPARE(terminal.insulationMaxMm, std::optional<double>(3.05));
        QCOMPARE(parts.pinParts[1].kind, PinPartKind::Plug);
    }

    void semicolonsAndProblems()
    {
        // Semicolon separated (European Excel); bad rows are reported with their line and
        // skipped; a repeated part number keeps its last row.
        const QByteArray csv = "Type;Part number;Cavities;Wire from;Insulation min\n"
                               "housing;H1;A1-B3;;\n"       // line 2: mixed prefix
                               "terminal;T1;;thick;\n"      // line 3: bad wire size
                               "seal;S1;;;-2\n"             // line 4: bad diameter
                               "relay;R1;;;\n"              // line 5: unknown type
                               ";;1-2;;\n"                  // line 6: no part number
                               "seal;S2;;;1.5\n"
                               "seal;S2;;;1.7\n";
        const PartsCsv parts = readPartsCsv(csv);
        QCOMPARE(parts.problems.size(), 5);
        for (int i = 0; i < 5; ++i)
            QVERIFY2(parts.problems[i].startsWith(u"Line %1:"_s.arg(i + 2)), qPrintable(parts.problems[i]));
        QVERIFY(parts.connectorParts.empty());
        QCOMPARE(parts.pinParts.size(), size_t(1));
        QCOMPARE(parts.pinParts[0].insulationMinMm, std::optional<double>(1.7));
    }

    void missingHeader()
    {
        QCOMPARE(readPartsCsv("").problems.size(), 1);
        const PartsCsv parts = readPartsCsv("Part,Kind\nX,seal\n");
        QCOMPARE(parts.problems.size(), 1);
        QVERIFY(parts.pinParts.empty());
    }

    void templateReadsBack()
    {
        const PartsCsv parts = readPartsCsv(partsCsvTemplate());
        QVERIFY2(parts.problems.isEmpty(), qPrintable(parts.problems.join(u'\n')));
        QCOMPARE(parts.connectorParts.size(), size_t(1));
        QCOMPARE(parts.connectorParts[0].cavityLabels.size(), 4);
        QCOMPARE(parts.connectorParts[0].terminals, (QStringList{u"EXAMPLE-T1"_s, u"EXAMPLE-T2"_s}));
        QCOMPARE(parts.pinParts.size(), size_t(4));
        QCOMPARE(parts.pinParts[1].wireMax, std::optional<Gauge>(Gauge{1.5, GaugeSystem::Mm2}));
    }

    void mergeReplacesByPartNumber()
    {
        Library library;
        ConnectorPart old;
        old.partNumber = u"H1"_s;
        old.description = u"old"_s;
        old.approximate = true;
        library.connectorParts = {old};
        PinPart seal;
        seal.kind = PinPartKind::Seal;
        seal.partNumber = u"X1"_s;
        library.pinParts = {seal};

        PartsCsv imported;
        ConnectorPart updated = old;
        updated.description = u"new"_s;
        updated.approximate = false;
        imported.connectorParts = {updated};
        PinPart terminal;
        terminal.partNumber = u"X1"_s; // same number, other kind: added
        imported.pinParts = {terminal};
        int added = 0, replaced = 0;
        mergeImportedParts(library, imported, &added, &replaced);
        QCOMPARE(added, 1);
        QCOMPARE(replaced, 1);
        QCOMPARE(library.connectorParts.size(), size_t(1));
        QCOMPARE(library.connectorPart(u"H1"_s)->description, u"new"_s);
        QVERIFY(!library.connectorPart(u"H1"_s)->approximate);
        QCOMPARE(library.pinParts.size(), size_t(2));
    }
};

QTEST_GUILESS_MAIN(TestPartsCsv)
#include "tst_partscsv.moc"
