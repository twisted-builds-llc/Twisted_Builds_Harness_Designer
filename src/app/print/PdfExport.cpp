// SPDX-License-Identifier: GPL-3.0-or-later
#include "print/PdfExport.h"

#include "SelectionModel.h"
#include "bom/BuildSheets.h"
#include "layout/LayoutScene.h"
#include "model/Queries.h"
#include "schematic/SchematicScene.h"
#include "ui/Format.h"
#include "ui/WireColors.h"
#include "ui/properties/PanelWidgets.h"
#include "ui/theme/Theme.h"

#include <QCoreApplication>
#include <QDate>
#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPdfWriter>

#include <functional>

namespace tbh::app {

namespace {

class Tr
{
    Q_DECLARE_TR_FUNCTIONS(PdfExport)
};

// Device units are 1/1200 inch; sizes below are given in points (1/72 inch) through pt().
constexpr int kResolution = 1200;
constexpr qreal pt(qreal points) { return points * kResolution / 72.0; }

const QColor kInk(0x1d, 0x20, 0x24);
const QColor kGrid(0xa0, 0xa4, 0xaa);
const QColor kCaption(0x5f, 0x64, 0x6b);
const QColor kHeaderFill(0xe4, 0xe6, 0xe9);
const QColor kStripeFill(0xf4, 0xf5, 0xf6);

QFont font(qreal points, bool bold = false)
{
    QFont f;
    f.setPointSizeF(points);
    f.setBold(bold);
    return f;
}

// Scenes drawn on paper: no grid, plain white.
class PrintSchematicScene : public SchematicScene
{
public:
    using SchematicScene::SchematicScene;

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override { painter->fillRect(rect, Qt::white); }
};

class PrintLayoutScene : public LayoutScene
{
public:
    using LayoutScene::LayoutScene;

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override { painter->fillRect(rect, Qt::white); }
};

// ---- Tables -------------------------------------------------------------------------

struct Column
{
    QString header;
    qreal weight; // share of the table width
    Qt::Alignment align = Qt::AlignLeft;
};

struct Cell
{
    QString text;
    QString wireColor;   // base color: draws a swatch before the text
    QString wireStripe;
    bool checkbox = false;
};

Cell textCell(const QString &text)
{
    Cell cell;
    cell.text = text;
    return cell;
}

Cell checkboxCell()
{
    Cell cell;
    cell.checkbox = true;
    return cell;
}

QString colorText(const QString &base, const QString &stripe)
{
    return stripe.isEmpty() ? base : QStringLiteral("%1/%2").arg(base, stripe);
}

// Color code text with a swatch of the wire color in front.
Cell colorCell(const QString &base, const QString &stripe)
{
    Cell cell;
    cell.text = colorText(base, stripe);
    cell.wireColor = base;
    cell.wireStripe = stripe;
    return cell;
}

struct TableBlock
{
    QString heading; // drawn above the table; empty = none
    std::vector<Column> columns;
    std::vector<std::vector<Cell>> rows;
};

// Part of a block on one sheet.
struct BlockSlice
{
    const TableBlock *block;
    size_t firstRow;
    size_t rowCount;
    bool continued; // the block started on an earlier sheet
};

// Title block: a name row and three rows of two fields.
constexpr qreal kTitleNameHeight = pt(26);
constexpr qreal kTitleRowHeight = pt(20);
constexpr qreal kTitleBlockHeight = kTitleNameHeight + 3 * kTitleRowHeight;

constexpr qreal kRowHeight = pt(14);
constexpr qreal kHeadingHeight = pt(20);
constexpr qreal kBlockGap = pt(12);

qreal fixedHeight(const TableBlock &block)
{
    return (block.heading.isEmpty() ? 0 : kHeadingHeight) + kRowHeight;
}

// Flows blocks down sheets of `height`. A block that does not fit in what is left of a
// sheet moves to the next one if it fits there whole; otherwise it is split, and each part
// repeats its heading and column headers.
std::vector<std::vector<BlockSlice>> paginate(const std::vector<TableBlock> &blocks, qreal height)
{
    std::vector<std::vector<BlockSlice>> sheets(1);
    qreal y = 0;
    auto newSheet = [&] {
        sheets.emplace_back();
        y = 0;
    };
    for (const TableBlock &block : blocks) {
        const qreal fixed = fixedHeight(block);
        size_t row = 0;
        bool continued = false;
        while (true) {
            const size_t left = block.rows.size() - row;
            const qreal gap = sheets.back().empty() ? 0 : kBlockGap;
            const qreal room = height - y - gap - fixed;
            size_t fit = room > 0 ? size_t(room / kRowHeight) : 0;
            if (!sheets.back().empty() && fit < left && (fixed + qreal(left) * kRowHeight <= height || fit == 0)) {
                newSheet();
                continue;
            }
            fit = std::max<size_t>(fit, 1); // a sheet too small for one row still makes progress
            const size_t take = std::min(fit, left);
            sheets.back().push_back({&block, row, take, continued});
            y += gap + fixed + qreal(take) * kRowHeight;
            row += take;
            if (row >= block.rows.size())
                break;
            newSheet();
            continued = true;
        }
    }
    return sheets;
}

void drawSwatch(QPainter &p, const QRectF &box, const QString &base, const QString &stripe)
{
    const QColor fill = wireColor(base);
    if (!fill.isValid())
        return;
    p.setPen(QPen(kInk, pt(0.5)));
    p.setBrush(fill);
    p.drawRect(box);
    const QColor band = wireColor(stripe);
    if (band.isValid()) {
        p.setPen(Qt::NoPen);
        p.setBrush(band);
        p.drawRect(QRectF(box.left(), box.center().y() - box.height() / 6, box.width(), box.height() / 3));
    }
}

// Returns the height used.
qreal drawSlice(QPainter &p, const BlockSlice &slice, const QRectF &area)
{
    const TableBlock &block = *slice.block;
    qreal y = area.top();
    if (!block.heading.isEmpty()) {
        p.setFont(font(10, true));
        p.setPen(kInk);
        const QString heading = slice.continued ? Tr::tr("%1 (continued)").arg(block.heading) : block.heading;
        p.drawText(QRectF(area.left(), y, area.width(), kHeadingHeight), Qt::AlignLeft | Qt::AlignVCenter, heading);
        y += kHeadingHeight;
    }

    qreal totalWeight = 0;
    for (const Column &c : block.columns)
        totalWeight += c.weight;
    std::vector<qreal> xs{area.left()};
    for (const Column &c : block.columns)
        xs.push_back(xs.back() + area.width() * c.weight / totalWeight);

    const qreal pad = pt(3);
    auto drawRow = [&](qreal top, const std::vector<Cell> &cells, bool header, bool shaded) {
        const QRectF rowRect(area.left(), top, area.width(), kRowHeight);
        if (header || shaded)
            p.fillRect(rowRect, header ? kHeaderFill : kStripeFill);
        p.setFont(font(8, header));
        const QFontMetricsF metrics(p.font(), p.device());
        for (size_t c = 0; c < block.columns.size(); ++c) {
            QRectF box(xs[c] + pad, top, xs[c + 1] - xs[c] - 2 * pad, kRowHeight);
            const Cell &cell = cells[c];
            if (cell.checkbox) {
                const qreal side = pt(7);
                p.setPen(QPen(kInk, pt(0.6)));
                p.setBrush(Qt::NoBrush);
                p.drawRect(QRectF(box.center().x() - side / 2, box.center().y() - side / 2, side, side));
                continue;
            }
            if (!cell.wireColor.isEmpty() && wireColor(cell.wireColor).isValid()) {
                const QRectF swatch(box.left(), box.center().y() - pt(3.5), pt(14), pt(7));
                drawSwatch(p, swatch, cell.wireColor, cell.wireStripe);
                box.setLeft(swatch.right() + pt(4));
            }
            p.setPen(kInk);
            const Qt::Alignment align = header ? Qt::AlignLeft : block.columns[c].align;
            p.drawText(box, int(align | Qt::AlignVCenter), metrics.elidedText(cell.text, Qt::ElideRight, box.width()));
        }
        p.setPen(QPen(kGrid, pt(0.4)));
        p.drawLine(rowRect.bottomLeft(), rowRect.bottomRight());
    };

    std::vector<Cell> header;
    for (const Column &c : block.columns)
        header.push_back(textCell(c.header));
    const qreal tableTop = y;
    drawRow(y, header, true, false);
    y += kRowHeight;
    for (size_t r = 0; r < slice.rowCount; ++r) {
        const size_t index = slice.firstRow + r;
        drawRow(y, block.rows[index], false, index % 2 == 1);
        y += kRowHeight;
    }
    p.setPen(QPen(kGrid, pt(0.4)));
    p.setBrush(Qt::NoBrush);
    for (const qreal x : xs)
        p.drawLine(QPointF(x, tableTop), QPointF(x, y));
    p.drawLine(QPointF(area.left(), tableTop), QPointF(area.right(), tableTop));
    return y - area.top();
}

// ---- Sheet content ------------------------------------------------------------------

TableBlock cutListBlock(const Harness &harness, const Library &library)
{
    const Units units = harness.info.units;
    TableBlock block;
    block.columns = {{Tr::tr("Cut"), 0.5, Qt::AlignCenter},  {Tr::tr("#"), 0.5, Qt::AlignRight},
                     {Tr::tr("Wire"), 2.0},                  {Tr::tr("Gauge"), 1.0},
                     {Tr::tr("Color"), 1.7},                 {Tr::tr("Type"), 1.0},
                     {Tr::tr("Cut length"), 1.2, Qt::AlignRight}, {Tr::tr("From"), 2.4},
                     {Tr::tr("To"), 2.4},                    {Tr::tr("Note"), 1.3}};
    const auto lines = bom::cutListInCuttingOrder(bom::buildBom(harness, library).cutList);
    int number = 0;
    for (const bom::CutLine &c : lines) {
        const QString note = !c.cutMm ? Tr::tr("Not routed") : (c.overridden ? Tr::tr("Manual length") : QString());
        block.rows.push_back({checkboxCell(),
                              textCell(QString::number(++number)),
                              textCell(c.name),
                              textCell(gaugeText(c.gauge)),
                              colorCell(c.baseColor, c.stripeColor),
                              textCell(c.wireTypeName),
                              textCell(c.cutMm ? formatLength(*c.cutMm, units) : QStringLiteral("—")),
                              textCell(c.endA),
                              textCell(c.endB),
                              textCell(note)});
    }
    return block;
}

std::vector<TableBlock> pinoutBlocks(const Harness &harness)
{
    const std::vector<Column> columns = {{Tr::tr("Pin"), 0.7},    {Tr::tr("Circuit"), 2.0}, {Tr::tr("Wire"), 2.0},
                                         {Tr::tr("Color"), 1.7},  {Tr::tr("Gauge"), 1.0},   {Tr::tr("Goes to"), 2.6}};
    std::vector<TableBlock> blocks;
    for (const bom::PinoutTable &table : bom::pinoutTables(harness)) {
        TableBlock block;
        QStringList heading{table.title};
        if (!table.partNumber.isEmpty())
            heading << table.partNumber;
        if (!table.description.isEmpty())
            heading << table.description;
        block.heading = heading.join(QStringLiteral(" — "));
        block.columns = columns;
        for (const bom::PinoutRow &r : table.rows) {
            if (r.wireId.isEmpty()) {
                block.rows.push_back({textCell(r.pin), textCell(r.circuit), textCell(QStringLiteral("—")), Cell(), Cell(),
                                      textCell(r.cavityPlug ? Tr::tr("Cavity plug") : Tr::tr("Empty"))});
                continue;
            }
            block.rows.push_back({textCell(r.pin), textCell(r.circuit), textCell(r.wireName),
                                  colorCell(r.baseColor, r.stripeColor),
                                  textCell(gaugeText(r.gauge)), textCell(r.otherEnd)});
        }
        blocks.push_back(std::move(block));
    }
    return blocks;
}

QString unitsName(Units units)
{
    switch (units) {
    case Units::Inches:
        return Tr::tr("Inches");
    case Units::Millimeters:
        return Tr::tr("Millimeters");
    case Units::Centimeters:
        return Tr::tr("Centimeters");
    }
    return {};
}

struct Sheet
{
    QString title;
    QString subtitle;
    std::function<void(QPainter &, const QRectF &)> paint;
};

// Bounds of the shown items (itemsBoundingRect() counts hidden ones, e.g. other sheets).
QRectF visibleItemsRect(const QGraphicsScene &scene)
{
    QRectF rect;
    for (const QGraphicsItem *item : scene.items()) {
        if (item->isVisible())
            rect |= item->sceneBoundingRect();
    }
    return rect;
}

void drawScene(QPainter &p, QGraphicsScene &scene, const QRectF &area, const QString &emptyText)
{
    const QRectF items = visibleItemsRect(scene);
    if (items.isEmpty()) {
        p.setFont(font(10));
        p.setPen(kCaption);
        p.drawText(area, Qt::AlignCenter, emptyText);
        return;
    }
    scene.render(&p, area, items.adjusted(-10, -10, 10, 10), Qt::KeepAspectRatio);
}

// Every schematic sheet in a row, at one scale, each under its name and in a thin frame.
void drawSheetsSideBySide(QPainter &p, SchematicScene &scene, const QRectF &area)
{
    struct Part
    {
        QString id;
        QString name;
        QRectF rect; // scene rect of the sheet's items
    };
    const Schematic &schematic = scene.model()->harness().schematic;
    std::vector<Part> parts;
    for (int i = 0; i < int(schematic.sheets.size()); ++i) {
        scene.setCurrentSheet(schematic.sheets[size_t(i)].id);
        const QRectF rect = visibleItemsRect(scene);
        if (!rect.isEmpty())
            parts.push_back({schematic.sheets[size_t(i)].id, sheetDisplayName(schematic, i), rect.adjusted(-10, -10, 10, 10)});
    }
    if (parts.empty()) {
        p.setFont(font(10));
        p.setPen(kCaption);
        p.drawText(area, Qt::AlignCenter, Tr::tr("Nothing is placed on the schematic yet."));
        return;
    }
    constexpr qreal kGap = 120; // scene units between sheets
    const qreal labelHeight = pt(14);
    qreal width = kGap * qreal(parts.size() - 1);
    qreal height = 0;
    for (const Part &part : parts) {
        width += part.rect.width();
        height = std::max(height, part.rect.height());
    }
    const qreal scale = std::min(area.width() / width, (area.height() - labelHeight) / height);
    qreal x = area.left() + (area.width() - width * scale) / 2;
    const qreal y = area.top() + labelHeight;
    for (const Part &part : parts) {
        const QRectF target(x, y, part.rect.width() * scale, part.rect.height() * scale);
        scene.setCurrentSheet(part.id);
        scene.render(&p, target, part.rect, Qt::KeepAspectRatio);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kCaption, pt(0.5)));
        p.drawRect(target);
        p.setFont(font(8, true));
        p.setPen(kInk);
        p.drawText(QRectF(target.left(), area.top(), std::max(target.width(), pt(120)), labelHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, part.name);
        x += (part.rect.width() + kGap) * scale;
    }
}

// Bottom-right title block. Returns its rectangle.
QRectF drawTitleBlock(QPainter &p, const QRectF &frame, const HarnessInfo &info, int sheet, int sheets)
{
    const qreal width = std::min(pt(320), frame.width() * 0.6);
    const qreal nameHeight = kTitleNameHeight;
    const qreal rowHeight = kTitleRowHeight;
    const QRectF box(frame.right() - width, frame.bottom() - nameHeight - 3 * rowHeight, width,
                     nameHeight + 3 * rowHeight);
    p.setBrush(Qt::white);
    p.setPen(QPen(kInk, pt(0.8)));
    p.drawRect(box);

    auto field = [&](const QRectF &cell, const QString &caption, const QString &value, qreal size, bool bold) {
        p.setPen(QPen(kInk, pt(0.5)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cell);
        const QRectF inner = cell.adjusted(pt(3), pt(1.5), -pt(3), -pt(2));
        p.setFont(font(5.5));
        p.setPen(kCaption);
        p.drawText(inner, Qt::AlignLeft | Qt::AlignTop, caption);
        p.setFont(font(size, bold));
        p.setPen(kInk);
        const QFontMetricsF metrics(p.font(), p.device());
        p.drawText(inner, Qt::AlignLeft | Qt::AlignBottom, metrics.elidedText(value, Qt::ElideRight, inner.width()));
    };
    const qreal half = width / 2;
    field(QRectF(box.left(), box.top(), width, nameHeight), Tr::tr("Harness"), info.name, 11, true);
    const QString rows[3][4] = {
        {Tr::tr("Part number"), info.partNumber, Tr::tr("Revision"), info.revision},
        {Tr::tr("Drawn by"), info.author, Tr::tr("Date"), QLocale().toString(QDate::currentDate(), QLocale::ShortFormat)},
        {Tr::tr("Units"), unitsName(info.units), Tr::tr("Sheet"), Tr::tr("%1 of %2").arg(sheet).arg(sheets)},
    };
    for (int r = 0; r < 3; ++r) {
        const qreal top = box.top() + nameHeight + r * rowHeight;
        field(QRectF(box.left(), top, half, rowHeight), rows[r][0], rows[r][1], 8, false);
        field(QRectF(box.left() + half, top, half, rowHeight), rows[r][2], rows[r][3], 8, false);
    }
    return box;
}

} // namespace

int exportBuildPackage(const HarnessModel &source, const QString &path, const PdfExportOptions &options,
                       QString *error)
{
    // Fresh scenes on a copy of the model: nothing selected or dimmed, light colors.
    HarnessModel model;
    model.setUserLibrary(source.userLibrary());
    model.reset(source.harness());
    const Harness &harness = model.harness();
    SelectionModel selection(&model);
    PrintLayoutScene layout(&model, &selection);
    PrintSchematicScene schematic(&model, &selection);
    for (QGraphicsScene *scene : {static_cast<QGraphicsScene *>(&layout), static_cast<QGraphicsScene *>(&schematic)})
        scene->setPalette(lightThemePalette());
    layout.setShowBundleDiameters(options.showBundleDiameters);
    layout.refreshCaptions();

    QPdfWriter writer(path);
    writer.setResolution(kResolution);
    writer.setTitle(harness.info.name);
    writer.setCreator(QCoreApplication::applicationName());
    writer.setPageLayout(QPageLayout(options.pageSize, QPageLayout::Landscape, QMarginsF(10, 10, 10, 10),
                                     QPageLayout::Millimeter));

    QPainter painter;
    if (!painter.begin(&writer)) {
        if (error)
            *error = Tr::tr("Could not write “%1”.").arg(path);
        return 0;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF frame(QPointF(0, 0), QSizeF(writer.width(), writer.height()));
    // Content sits inside the frame, below the sheet heading and above the title block.
    const qreal inset = pt(10);
    const qreal headingHeight = pt(26);
    const QRectF content(frame.left() + inset, frame.top() + inset + headingHeight, frame.width() - 2 * inset,
                         frame.height() - 2 * inset - headingHeight - kTitleBlockHeight - pt(8));

    std::vector<Sheet> sheets;
    sheets.push_back({Tr::tr("Layout"), Tr::tr("Not to scale: build to the lengths shown."),
                      [&](QPainter &p, const QRectF &area) {
                          drawScene(p, layout, area, Tr::tr("Nothing is placed in the layout yet."));
                      }});
    // One schematic sheet prints as before. Several print side by side on one page first,
    // then one page each.
    const auto &schematicSheets = harness.schematic.sheets;
    if (schematicSheets.size() == 1) {
        sheets.push_back({Tr::tr("Schematic"), QString(), [&](QPainter &p, const QRectF &area) {
                              drawScene(p, schematic, area, Tr::tr("Nothing is placed on the schematic yet."));
                          }});
    } else {
        sheets.push_back({Tr::tr("Schematic - All Sheets"), QString(),
                          [&](QPainter &p, const QRectF &area) { drawSheetsSideBySide(p, schematic, area); }});
        for (int i = 0; i < int(schematicSheets.size()); ++i) {
            const QString id = schematicSheets[size_t(i)].id;
            sheets.push_back({Tr::tr("Schematic - %1").arg(sheetDisplayName(harness.schematic, i)),
                              Tr::tr("Sheet %1 of %2").arg(i + 1).arg(schematicSheets.size()),
                              [&, id](QPainter &p, const QRectF &area) {
                                  schematic.setCurrentSheet(id);
                                  drawScene(p, schematic, area, Tr::tr("Nothing is placed on this sheet yet."));
                              }});
        }
    }

    const std::vector<TableBlock> cutList{cutListBlock(harness, model.library())};
    const std::vector<TableBlock> pinouts = pinoutBlocks(harness);
    auto addTableSheets = [&](const QString &title, const QString &subtitle, const std::vector<TableBlock> &blocks) {
        for (const std::vector<BlockSlice> &slices : paginate(blocks, content.height())) {
            sheets.push_back({title, subtitle, [slices](QPainter &p, const QRectF &area) {
                                  qreal y = area.top();
                                  for (const BlockSlice &slice : slices) {
                                      if (y > area.top())
                                          y += kBlockGap;
                                      y += drawSlice(p, slice, QRectF(area.left(), y, area.width(), area.bottom() - y));
                                  }
                              }});
        }
    };
    addTableSheets(Tr::tr("Wire Cut List"),
                   Tr::tr("Sorted for cutting: by gauge, then color. Lengths include end allowances and service loop."),
                   cutList);
    if (!pinouts.empty())
        addTableSheets(Tr::tr("Connector Pinouts"), QString(), pinouts);

    const int count = int(sheets.size());
    for (int i = 0; i < count; ++i) {
        if (i > 0)
            writer.newPage();
        const Sheet &sheet = sheets[size_t(i)];
        painter.setPen(QPen(kInk, pt(1.2)));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frame.adjusted(pt(0.6), pt(0.6), -pt(0.6), -pt(0.6)));

        const QRectF heading(content.left(), frame.top() + inset, content.width(), headingHeight);
        painter.setFont(font(14, true));
        painter.setPen(kInk);
        painter.drawText(heading, Qt::AlignLeft | Qt::AlignTop, sheet.title);
        if (!sheet.subtitle.isEmpty()) {
            painter.setFont(font(8));
            painter.setPen(kCaption);
            painter.drawText(heading, Qt::AlignRight | Qt::AlignTop, sheet.subtitle);
        }

        painter.save();
        painter.setClipRect(content);
        sheet.paint(painter, content);
        painter.restore();
        drawTitleBlock(painter, frame, harness.info, i + 1, count);
    }
    painter.end();
    return count;
}

} // namespace tbh::app
