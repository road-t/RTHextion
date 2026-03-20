#include "ChangesDockWidget.h"
#include "translationtable.h"

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QStringDecoder>
#include <QSignalBlocker>
#include <QButtonGroup>

ChangesDockWidget::ChangesDockWidget(QWidget *parent)
    : QDockWidget(parent)
{
    setWindowTitle(tr("Changes"));
    setObjectName(QStringLiteral("ChangesDockWidget"));
    setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    // Compact title bar (same pattern as PointersDockWidget)
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("dockTitleBar"));
    titleBar->setFixedHeight(16);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(4, 0, 2, 0);
    titleLayout->setSpacing(1);

    m_titleLabel = new QLabel(tr("Changes"), titleBar);
    QFont smallFont = m_titleLabel->font();
    smallFont.setPointSizeF(smallFont.pointSizeF() * 0.8);
    m_titleLabel->setFont(smallFont);
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();

    m_collapseBtn = new QToolButton(titleBar);
    m_collapseBtn->setArrowType(Qt::DownArrow);
    m_collapseBtn->setAutoRaise(true);
    m_collapseBtn->setFixedSize(14, 14);
    m_collapseBtn->setToolTip(tr("Collapse / Expand"));
    titleLayout->addWidget(m_collapseBtn);
    setTitleBarWidget(titleBar);

    // Content widget
    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Toolbar row: just the eye button
    auto *toolRow = new QHBoxLayout();
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(2);

    // Eye (show-changes) toggle button
    m_showChangesBtn = new QToolButton(this);
    m_showChangesBtn->setCheckable(true);
    m_showChangesBtn->setChecked(false);
    m_showChangesBtn->setAutoRaise(true);
    m_showChangesBtn->setToolTip(tr("Show changes"));
    {
        auto paint = [](bool filled) -> QPixmap {
            const int sz = 16;
            QPixmap pm(sz, sz);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const QColor col(70, 70, 70);
            p.setPen(QPen(col, 1.3));
            const float cx = sz * 0.5f, cy = sz * 0.5f;
            const float rx = sz * 0.44f, ry = sz * 0.27f;
            QPainterPath lens;
            lens.moveTo(cx - rx, cy);
            lens.cubicTo(cx - rx*0.4f, cy - ry*2.0f, cx + rx*0.4f, cy - ry*2.0f, cx + rx, cy);
            lens.cubicTo(cx + rx*0.4f, cy + ry*2.0f, cx - rx*0.4f, cy + ry*2.0f, cx - rx, cy);
            p.drawPath(lens);
            const float pr = ry * 0.72f;
            if (filled) { p.setBrush(col); p.setPen(Qt::NoPen); }
            p.drawEllipse(QPointF(cx, cy), pr, pr);
            return pm;
        };
        QIcon icon;
        icon.addPixmap(paint(true),  QIcon::Normal,  QIcon::On);
        icon.addPixmap(paint(false), QIcon::Normal,  QIcon::Off);
        icon.addPixmap(paint(false), QIcon::Disabled, QIcon::Off);
        m_showChangesBtn->setIcon(icon);
    }
    m_showChangesBtn->setIconSize(QSize(16, 16));
    toolRow->addWidget(m_showChangesBtn);

    // Text / Hex display mode — two exclusive toggle buttons
    m_textBtn = new QToolButton(this);
    m_textBtn->setCheckable(true);
    m_textBtn->setChecked(true);   // default: text mode
    m_textBtn->setAutoRaise(true);
    m_textBtn->setText(QStringLiteral("TEXT"));
    m_textBtn->setToolTip(tr("Show values as text"));

    m_hexBtn = new QToolButton(this);
    m_hexBtn->setCheckable(true);
    m_hexBtn->setChecked(false);
    m_hexBtn->setAutoRaise(true);
    m_hexBtn->setText(QStringLiteral("HEX"));
    m_hexBtn->setToolTip(tr("Show values as hexadecimal"));

    m_displayModeGroup = new QButtonGroup(this);
    m_displayModeGroup->setExclusive(true);
    m_displayModeGroup->addButton(m_textBtn, 0);  // id 0 = text
    m_displayModeGroup->addButton(m_hexBtn,  1);  // id 1 = hex

    toolRow->addWidget(m_textBtn);
    toolRow->addWidget(m_hexBtn);

    toolRow->addStretch();
    layout->addLayout(toolRow);

    connect(m_showChangesBtn, &QToolButton::clicked, this, [this](bool checked) {
        emit showChangesToggled(checked);
    });

    connect(m_displayModeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_hexMode = (id == 1);
        refresh(m_lastOriginals, m_lastCurrentData,
                m_lastOrigTable, m_lastActiveTable,
                m_lastUseTable, m_lastEncoding);
    });

    // Changes table: # | Offset | Original | Current
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({tr("#"), tr("Offset"), tr("Original"), tr("Current")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setDefaultSectionSize(20);

    QHeaderView *hdr = m_table->horizontalHeader();
    hdr->setMinimumSectionSize(30);
    hdr->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->setColumnWidth(0, 30);
    hdr->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->setColumnWidth(1, 80);
    hdr->setSectionResizeMode(2, QHeaderView::Interactive);
    m_table->setColumnWidth(2, 120);
    hdr->setSectionResizeMode(3, QHeaderView::Interactive);
    hdr->setStretchLastSection(true);

    layout->addWidget(m_table);

    setWidget(container);

    connect(m_table, &QTableWidget::doubleClicked, this, &ChangesDockWidget::onRowDoubleClicked);
    connect(m_collapseBtn, &QToolButton::clicked, this, [this]() {
        const bool visible = m_contentWidget->isVisible();
        m_contentWidget->setVisible(!visible);
        m_collapseBtn->setArrowType(visible ? Qt::RightArrow : Qt::DownArrow);
    });
}

void ChangesDockWidget::refresh(const QVector<QPair<qint64, QByteArray>> &originals,
                                const QByteArray &currentData,
                                TranslationTable *originalTable,
                                TranslationTable *activeTable,
                                bool useTable,
                                const QString &encoding)
{
    // Store parameters for re-render when display mode is toggled
    m_lastOriginals   = originals;
    m_lastCurrentData = currentData;
    m_lastOrigTable   = originalTable;
    m_lastActiveTable = activeTable;
    m_lastUseTable    = useTable;
    m_lastEncoding    = encoding;

    // Helper: encode bytes according to current display mode
    auto encodeBytes = [this, useTable, &encoding](const QByteArray &bytes, TranslationTable *table) -> QString {
        auto hexFallback = [](const QByteArray &b) -> QString {
            QString hex;
            for (int i = 0; i < b.size(); ++i) {
                if (i > 0) hex += QLatin1Char(' ');
                hex += QString::number(static_cast<uint8_t>(b[i]), 16).toUpper()
                             .rightJustified(2, QLatin1Char('0'));
            }
            return hex;
        };

        if (m_hexMode)
            return hexFallback(bytes);

        if (useTable && table && table->size() > 0)
            return table->encode(bytes);

        if (!useTable) {
            // Decode using the current text encoding (like the ASCII area)
            if (encoding.isEmpty() || encoding == QLatin1String("ASCII"))
                return QString::fromLatin1(bytes);
            QStringDecoder dec(encoding.toUtf8().constData());
            if (dec.isValid()) {
                const QString result = dec(bytes);
                if (!result.isEmpty() || bytes.isEmpty())
                    return result;
            }
            return QString::fromLatin1(bytes);
        }

        // useTable ON but no table — hex fallback
        return hexFallback(bytes);
    };

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(0);

    for (const auto &entry : originals)
    {
        const qint64 baseOffset = entry.first;
        const QByteArray &origBytes = entry.second;

        // Get the current bytes at the same position
        const QByteArray curBytes = currentData.mid(static_cast<int>(baseOffset), origBytes.size());

        // Skip if nothing changed in this region
        bool anyChanged = false;
        for (int i = 0; i < origBytes.size(); ++i) {
            const uint8_t orig = static_cast<uint8_t>(origBytes.at(i));
            const uint8_t cur  = (i < curBytes.size()) ? static_cast<uint8_t>(curBytes.at(i)) : orig;
            if (cur != orig) { anyChanged = true; break; }
        }
        if (!anyChanged)
            continue;

        const int row = m_table->rowCount();
        m_table->insertRow(row);

        // Column 0: row number
        auto *numItem = new QTableWidgetItem(QString::number(row + 1));
        numItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Column 1: base offset (hex)
        auto *offsetItem = new QTableWidgetItem(
            QString::number(baseOffset, 16).toUpper().rightJustified(8, QLatin1Char('0')));
        offsetItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Column 2: original bytes encoded
        auto *origItem = new QTableWidgetItem(encodeBytes(origBytes, originalTable));
        origItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        // Column 3: current bytes encoded
        auto *curItem = new QTableWidgetItem(encodeBytes(curBytes, activeTable));
        curItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        m_table->setItem(row, 0, numItem);
        m_table->setItem(row, 1, offsetItem);
        m_table->setItem(row, 2, origItem);
        m_table->setItem(row, 3, curItem);
    }

    m_table->setUpdatesEnabled(true);

    // Update title with count
    if (m_titleLabel) {
        const int count = m_table->rowCount();
        const QString title = tr("Changes") +
            (count > 0 ? QStringLiteral(" \u2013 %1").arg(count) : QString());
        m_titleLabel->setText(title);
    }
}

void ChangesDockWidget::clear()
{
    m_table->setRowCount(0);
}

void ChangesDockWidget::setShowChangesChecked(bool checked)
{
    m_showChangesBtn->setChecked(checked);
}

void ChangesDockWidget::setShowChangesEnabled(bool enabled)
{
    m_showChangesBtn->setEnabled(enabled);
}

void ChangesDockWidget::setHexMode(bool hexMode)
{
    m_hexMode = hexMode;

    if (m_textBtn && m_hexBtn) {
        const QSignalBlocker b1(m_textBtn);
        const QSignalBlocker b2(m_hexBtn);
        m_textBtn->setChecked(!hexMode);
        m_hexBtn->setChecked(hexMode);
    }

    refresh(m_lastOriginals, m_lastCurrentData,
            m_lastOrigTable, m_lastActiveTable,
            m_lastUseTable, m_lastEncoding);
}

QByteArray ChangesDockWidget::saveColumnsState() const
{
    return m_table->horizontalHeader()->saveState();
}

void ChangesDockWidget::restoreColumnsState(const QByteArray &state)
{
    if (!state.isEmpty())
        m_table->horizontalHeader()->restoreState(state);
}

void ChangesDockWidget::retranslateUi()
{
    setWindowTitle(tr("Changes"));
    if (m_titleLabel)
        m_titleLabel->setText(tr("Changes"));
    if (m_showChangesBtn)
        m_showChangesBtn->setToolTip(tr("Show changes"));
    if (m_textBtn)
        m_textBtn->setToolTip(tr("Show values as text"));
    if (m_hexBtn)
        m_hexBtn->setToolTip(tr("Show values as hexadecimal"));
    m_table->setHorizontalHeaderLabels({tr("#"), tr("Offset"), tr("Original"), tr("Current")});
}

void ChangesDockWidget::onRowDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_table->rowCount())
        return;

    // Offset is now in column 1
    QTableWidgetItem *offsetItem = m_table->item(index.row(), 1);
    if (!offsetItem)
        return;

    bool ok = false;
    const QString offsetText = offsetItem->text();
    qint64 offset = offsetText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                    ? offsetText.mid(2).toLongLong(&ok, 16)
                    : offsetText.toLongLong(&ok, 16);
    if (ok)
        emit jumpToOffset(offset);
}
