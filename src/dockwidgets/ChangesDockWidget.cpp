#include "ChangesDockWidget.h"
#include "translationtable.h"
#include "DockTitleBar.h"
#include "BaseDockWidget.h"

#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QStringDecoder>
#include <QSignalBlocker>
#include <QButtonGroup>
#include <QMainWindow>

ChangesDockWidget::ChangesDockWidget(QWidget *parent)
    : BaseDockWidget(tr("Changes"), parent)
{
    setWindowTitle(tr("Changes"));
    setObjectName(QStringLiteral("ChangesDockWidget"));

    // Content widget
    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Toolbar row: just the eye button
    auto *toolRow = new QHBoxLayout();
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(4);

    // Eye (show-changes) toggle button
    m_showChangesBtn = new QToolButton(this);
    m_showChangesBtn->setCheckable(true);
    m_showChangesBtn->setChecked(false);
    m_showChangesBtn->setAutoRaise(true);
    m_showChangesBtn->setToolTip(tr("Show changes"));
    {
        m_showChangesBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
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

    auto makeSep = [this]() -> QWidget * {
        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        return sep;
    };

    toolRow->addSpacing(4);
    toolRow->addWidget(makeSep());
    toolRow->addSpacing(4);
    toolRow->addWidget(m_textBtn);
    toolRow->addWidget(m_hexBtn);

    // Current / Original two-button exclusive switcher (mirrors TEXT/HEX pair)
    m_currentBtn = new QToolButton(this);
    m_currentBtn->setCheckable(true);
    m_currentBtn->setChecked(true);   // default: current (edited) view
    m_currentBtn->setAutoRaise(true);
    m_currentBtn->setText(tr("Current"));
    m_currentBtn->setToolTip(tr("Show current (edited) file content"));

    m_originalBtn = new QToolButton(this);
    m_originalBtn->setCheckable(true);
    m_originalBtn->setChecked(false);
    m_originalBtn->setAutoRaise(true);
    m_originalBtn->setText(tr("Original"));
    m_originalBtn->setToolTip(tr("Show original file content (read-only)"));

    m_viewModeGroup = new QButtonGroup(this);
    m_viewModeGroup->setExclusive(true);
    m_viewModeGroup->addButton(m_currentBtn,  0);  // id 0 = current
    m_viewModeGroup->addButton(m_originalBtn, 1);  // id 1 = original

    toolRow->addSpacing(4);
    toolRow->addWidget(makeSep());
    toolRow->addSpacing(4);
    toolRow->addWidget(m_currentBtn);
    toolRow->addWidget(m_originalBtn);

    toolRow->addStretch();
    layout->addLayout(toolRow);

    connect(m_showChangesBtn, &QToolButton::clicked, this, [this](bool checked) {
        emit showChangesToggled(checked);
    });

    connect(m_viewModeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        emit showOriginalToggled(id == 1);
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

    initTitleBar();

    connect(m_table, &QTableWidget::doubleClicked, this, &ChangesDockWidget::onRowDoubleClicked);
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
    // Truncates results longer than 255 characters and adds "..."
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

        QString result;
        if (m_hexMode)
            result = hexFallback(bytes);
        else if (useTable && table && table->size() > 0)
            result = table->encode(bytes);
        else if (!useTable) {
            // Decode using the current text encoding (like the ASCII area)
            if (encoding.isEmpty() || encoding == QLatin1String("ASCII"))
                result = QString::fromLatin1(bytes);
            else {
                QStringDecoder dec(encoding.toUtf8().constData());
                if (dec.isValid()) {
                    result = dec(bytes);
                    if (result.isEmpty() && !bytes.isEmpty())
                        result = QString::fromLatin1(bytes);
                } else {
                    result = QString::fromLatin1(bytes);
                }
            }
        } else {
            // useTable ON but no table — hex fallback
            result = hexFallback(bytes);
        }
        
        // Truncate if longer than 255 characters
        if (result.length() > 255) {
            result.truncate(255);
            result += QStringLiteral("...");
        }
        return result;
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
    {
        const int count = m_table->rowCount();
        const QString title = tr("Changes") +
            (count > 0 ? QStringLiteral(" \u2013 %1").arg(count) : QString());
        setWindowTitle(title);
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

void ChangesDockWidget::setShowOriginalChecked(bool checked)
{
    if (m_currentBtn && m_originalBtn) {
        const QSignalBlocker b1(m_currentBtn);
        const QSignalBlocker b2(m_originalBtn);
        m_currentBtn->setChecked(!checked);
        m_originalBtn->setChecked(checked);
    }
}

void ChangesDockWidget::setShowOriginalEnabled(bool enabled)
{
    if (m_currentBtn)
        m_currentBtn->setEnabled(enabled);
    if (m_originalBtn)
        m_originalBtn->setEnabled(enabled);
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
    if (m_showChangesBtn)
        m_showChangesBtn->setToolTip(tr("Show changes"));
    if (m_currentBtn)
        m_currentBtn->setToolTip(tr("Show current (edited) file content"));
    if (m_originalBtn)
        m_originalBtn->setToolTip(tr("Show original file content (read-only)"));
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

void ChangesDockWidget::onPaletteChanged()
{
    if (m_showChangesBtn)
        m_showChangesBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
}
