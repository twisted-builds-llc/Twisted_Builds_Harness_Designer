// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/HarnessModel.h"

#include <QSignalSpy>
#include <QTest>

using namespace tbh;
using namespace Qt::StringLiterals;

class TestHarnessModel : public QObject
{
    Q_OBJECT

private slots:
    void resetReplacesHarness()
    {
        HarnessModel model;
        QSignalSpy spy(&model, &HarnessModel::harnessReset);
        Harness h;
        h.info.name = u"A"_s;
        model.reset(h);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.harness().info.name, u"A"_s);
    }

    void setInfoEmitsOnlyOnChange()
    {
        HarnessModel model;
        QSignalSpy spy(&model, &HarnessModel::infoChanged);
        HarnessInfo info;
        info.name = u"B"_s;
        model.setInfo(info);
        QCOMPARE(spy.count(), 1);
        model.setInfo(info);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.harness().info.name, u"B"_s);
    }

    void newIdIsUniqueUuid()
    {
        const QString a = newId();
        QCOMPARE(a.size(), 36);
        QVERIFY(a != newId());
    }
};

QTEST_GUILESS_MAIN(TestHarnessModel)
#include "tst_harnessmodel.moc"
