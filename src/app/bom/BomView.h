// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "bom/Bom.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace tbh::app {

class Document;

// The BOM tab: generated live from the model, one table per section, editable "other
// items" rows, and CSV export.
class BomView : public QWidget
{
    Q_OBJECT

public:
    explicit BomView(Document *document, QWidget *parent = nullptr);

    const bom::Bom &currentBom() const { return m_bom; }
    void exportCsv();
    // Writes the export without dialogs. `perSection`: `path` is a directory and files are
    // named "<base>-<section>.csv"; otherwise `path` is the file.
    bool writeCsv(const QString &path, bool perSection, const QString &baseName, QString *error) const;

signals:
    void wireActivated(const QString &wireId);

private:
    void scheduleRefresh();
    void refresh();
    void commitExtra(int row);
    void addExtra();
    void removeExtra();

    Document *m_document;
    bom::Bom m_bom;
    bool m_pending = false;
    bool m_filling = false;
    QLabel *m_summary;
    QTableWidget *m_wires;
    QTableWidget *m_cutList;
    QTableWidget *m_sleeving;
    QTableWidget *m_parts;
    QTableWidget *m_extras;
};

} // namespace tbh::app
