// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Harness.h"
#include "model/PinLabels.h"

#include <QTest>

using namespace tbh;
using namespace Qt::StringLiterals;

class TestPinLabels : public QObject
{
    Q_OBJECT

private slots:
    void nextLabel_data()
    {
        QTest::addColumn<QString>("previous");
        QTest::addColumn<QString>("next");
        QTest::newRow("letter+digit") << u"A8"_s << u"A9"_s;
        QTest::newRow("carry") << u"A9"_s << u"A10"_s;
        QTest::newRow("plain") << u"9"_s << u"10"_s;
        QTest::newRow("padded") << u"07"_s << u"08"_s;
        QTest::newRow("padded carry") << u"09"_s << u"10"_s;
        QTest::newRow("padding grows") << u"99"_s << u"100"_s;
        QTest::newRow("multi-letter") << u"GND2"_s << u"GND3"_s;
        QTest::newRow("no number") << u"GND"_s << QString();
        QTest::newRow("symbol") << u"+"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }

    void nextLabel()
    {
        QFETCH(QString, previous);
        QFETCH(QString, next);
        QCOMPARE(nextPinLabel(previous), next);
    }

    void expand_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<int>("step");
        QTest::addColumn<bool>("reverse");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("simple range") << u"1-4"_s << 1 << false << QStringList{u"1"_s, u"2"_s, u"3"_s, u"4"_s};
        QTest::newRow("prefixed ranges")
            << u"A1-A3, B1-B2"_s << 1 << false
            << QStringList{u"A1"_s, u"A2"_s, u"A3"_s, u"B1"_s, u"B2"_s};
        QTest::newRow("short end") << u"C1-3"_s << 1 << false << QStringList{u"C1"_s, u"C2"_s, u"C3"_s};
        QTest::newRow("descending") << u"3-1"_s << 1 << false << QStringList{u"3"_s, u"2"_s, u"1"_s};
        QTest::newRow("padding") << u"08-11"_s << 1 << false
                                 << QStringList{u"08"_s, u"09"_s, u"10"_s, u"11"_s};
        // 1, 3, 5 (7 is past the end)
        QTest::newRow("step 2") << u"1-6"_s << 2 << false << QStringList{u"1"_s, u"3"_s, u"5"_s};
        QTest::newRow("reverse") << u"A1-A2, GND"_s << 1 << true
                                 << QStringList{u"GND"_s, u"A2"_s, u"A1"_s};
        QTest::newRow("literals") << u"+, -, GND-1"_s << 1 << false
                                  << QStringList{u"+"_s, u"-"_s, u"GND-1"_s};
        QTest::newRow("spaces and empty tokens") << u" 1 - 2 ,, "_s << 1 << false
                                                 << QStringList{u"1"_s, u"2"_s};
    }

    void expand()
    {
        QFETCH(QString, pattern);
        QFETCH(int, step);
        QFETCH(bool, reverse);
        QFETCH(QStringList, expected);
        QString error;
        const auto labels = expandPinPattern(pattern, step, reverse, &error);
        QVERIFY2(labels, qPrintable(error));
        QCOMPARE(*labels, expected);
    }

    void plainRangeOf120()
    {
        const auto labels = expandPinPattern(u"1-120"_s, 1, false, nullptr);
        QVERIFY(labels);
        QCOMPARE(labels->size(), 120);
        QCOMPARE(labels->last(), u"120"_s);
    }

    void errors()
    {
        QString error;
        QVERIFY(!expandPinPattern(u"A1-B8"_s, 1, false, &error)); // mixed prefix
        QVERIFY(!error.isEmpty());
        QVERIFY(!expandPinPattern(u" , "_s, 1, false, &error)); // empty
        QVERIFY(!expandPinPattern(u"1-4"_s, 0, false, &error)); // bad step
        QVERIFY(expandPinPattern(u"1-999"_s, 1, false, &error)); // max
        QVERIFY(!expandPinPattern(u"1-1000"_s, 1, false, &error)); // over max
        QVERIFY(!expandPinPattern(u"1-999, X"_s, 1, false, &error)); // literal over max
    }
};

QTEST_GUILESS_MAIN(TestPinLabels)
#include "tst_pinlabels.moc"
