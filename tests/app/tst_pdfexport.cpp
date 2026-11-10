// SPDX-License-Identifier: GPL-3.0-or-later
#include "../core/SampleHarness.h"
#include "model/HarnessModel.h"
#include "io/TbhFile.h"
#include "print/PdfExport.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace tbh;
using namespace tbh::app;
using namespace tbh::testing;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Page objects in the file ("/Type /Page", not "/Type /Pages").
int pageObjects(const QByteArray &pdf)
{
    static const QRegularExpression page(QStringLiteral("/Type\\s*/Page(?![s\\w])"));
    return int(QString::fromLatin1(pdf).count(page));
}

} // namespace

class TestPdfExport : public QObject
{
    Q_OBJECT

private slots:
    void writesEverySheet()
    {
        QTemporaryDir dir;
        HarnessModel model;
        Harness h = bomSample();
        h.info.partNumber = u"TH-100"_s;
        model.reset(h);
        const QString path = dir.filePath(u"package.pdf"_s);
        QString error;
        // Layout, schematic, cut list (5 wires: one sheet), pinouts (4 small tables: one sheet).
        const int sheets = exportBuildPackage(model, path, PdfExportOptions{}, &error);
        QCOMPARE(sheets, 4);
        QVERIFY(error.isEmpty());
        const QByteArray pdf = readFile(path);
        QVERIFY(pdf.startsWith("%PDF"));
        QCOMPARE(pageObjects(pdf), 4);
    }

    void longCutListContinues()
    {
        // 60 wires between flying leads: no connectors or splices, so no pinout sheet.
        // Letter landscape is 8.5 in = 612 pt tall; minus 10 mm margins (2 x 28.35 pt) = 555.3.
        // Table room: 555.3 - frame inset 2 x 10 - heading 26 - title block (26 + 3 x 20 = 86)
        // - gap 8 = 415.3 pt. Header row 14 pt leaves 401.3 pt = 28 rows of 14 pt per sheet
        // -> 28 + 28 + 4 = 3 sheets.
        // Total: layout + schematic + 3 = 5.
        Harness h;
        for (int i = 0; i < 60; ++i) {
            const QString a = u"a%1"_s.arg(i);
            const QString b = u"b%1"_s.arg(i);
            h.freeEnds.push_back(FreeEnd{a, FreeEndKind::FlyingLead, {}, {}, {}});
            h.freeEnds.push_back(FreeEnd{b, FreeEndKind::FlyingLead, {}, {}, {}});
            h.wires.push_back(wire(u"w%1"_s.arg(i), FreeEndRef{a}, FreeEndRef{b}));
        }
        HarnessModel model;
        model.reset(h);
        QTemporaryDir dir;
        const QString path = dir.filePath(u"long.pdf"_s);
        QCOMPARE(exportBuildPackage(model, path, PdfExportOptions{}, nullptr), 5);
        QCOMPARE(pageObjects(readFile(path)), 5);
    }

    void emptyHarnessStillPrints()
    {
        // Layout and schematic sheets with a "nothing placed" note, and an empty cut list.
        HarnessModel model;
        QTemporaryDir dir;
        QCOMPARE(exportBuildPackage(model, dir.filePath(u"empty.pdf"_s), PdfExportOptions{}, nullptr), 3);
    }

    // Visual check, skipped unless TBH_SCREENSHOT_DIR is set. TBH_PDF_INPUT may name a .tbh
    // file to export instead of the sample harness.
    void screenshots()
    {
        const QString out = qEnvironmentVariable("TBH_SCREENSHOT_DIR");
        if (out.isEmpty())
            QSKIP("TBH_SCREENSHOT_DIR not set");
        HarnessModel model;
        Harness h = bomSample();
        if (const QString input = qEnvironmentVariable("TBH_PDF_INPUT"); !input.isEmpty()) {
            QString error;
            const auto read = io::readTbhFile(input, &error);
            QVERIFY2(read, qPrintable(error));
            h = *read;
        }
        model.reset(h);
        QVERIFY(exportBuildPackage(model, out + u"/m7-build-package.pdf"_s, PdfExportOptions{}, nullptr) > 0);
    }

    void unwritablePathFails()
    {
        HarnessModel model;
        QTemporaryDir dir;
        QString error;
        QCOMPARE(exportBuildPackage(model, dir.filePath(u"missing/dir/out.pdf"_s), PdfExportOptions{}, &error), 0);
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestPdfExport)
#include "tst_pdfexport.moc"
