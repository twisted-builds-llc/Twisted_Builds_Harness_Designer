// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/Harness.h"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QTableWidget;

namespace tbh::app {

// Edits the user's parts library: wire types with outer diameter per gauge, and the
// sleeving catalog. Edited entries lose their "approximate" flag.
class LibraryDialog : public QDialog
{
    Q_OBJECT

public:
    LibraryDialog(const Library &library, QWidget *parent = nullptr);

    // Valid after accept().
    const Library &library() const { return m_library; }

    void accept() override;

private:
    void loadTypes();
    void showType(int index);
    bool storeType(int index, QString *error);
    void loadSleevings();
    bool storeSleevings(QString *error);
    void addOdRow(const WireOuterDiameter &od);
    void addSleevingRow(const Sleeving &sleeving);

    Library m_original;
    Library m_library;
    int m_currentType = -1;
    QListWidget *m_types;
    QLineEdit *m_typeName;
    QTableWidget *m_ods;
    QTableWidget *m_sleevings;
};

} // namespace tbh::app
