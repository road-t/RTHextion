#include "DisassemblyDockWidget.h"
#include "disassembler.h"
#include "hexeditor/hexeditor.h"
#include "DockTitleBar.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFrame>
#include <QFont>

static const int kMaxInstructions = 200;
static const int kMaxBytes = 2048;

DisassemblyDockWidget::DisassemblyDockWidget(QWidget *parent)
    : BaseDockWidget(tr("Disassembly"), parent)
    , m_romType(static_cast<RomType>(0))
{
    setWindowTitle(tr("Disassembly"));
    setObjectName(QStringLiteral("DisassemblyDockWidget"));

    m_disasm = new Disassembler();

    // Content widget
    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Toolbar row
    auto *toolRow = new QHBoxLayout();
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(4);

    // Follow cursor toggle
    m_followBtn = new QToolButton(this);
    m_followBtn->setCheckable(true);
    m_followBtn->setChecked(true);
    m_followBtn->setAutoRaise(true);
    m_followBtn->setToolTip(tr("Follow cursor"));
    m_followBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
    m_followBtn->setIconSize(QSize(16, 16));
    toolRow->addWidget(m_followBtn);

    connect(m_followBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_followCursor = checked;
        if (checked)
            refresh();
    });

    toolRow->addSpacing(8);

    // Status label (shows platform or "Not supported")
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    toolRow->addWidget(m_statusLabel);

    toolRow->addStretch();
    layout->addLayout(toolRow);

    // Disassembly table: Offset | Hex | Instruction | Operands
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({tr("Offset"), tr("Hex"), tr("Instruction"), tr("Operands")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setDefaultSectionSize(20);
    m_table->setShowGrid(false);

    // Monospace font for the table
    QFont monoFont(QStringLiteral("Menlo"), 11);
    monoFont.setStyleHint(QFont::Monospace);
    m_table->setFont(monoFont);

    QHeaderView *hdr = m_table->horizontalHeader();
    hdr->setMinimumSectionSize(30);
    hdr->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->setColumnWidth(0, 80);
    hdr->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->setColumnWidth(1, 120);
    hdr->setSectionResizeMode(2, QHeaderView::Interactive);
    m_table->setColumnWidth(2, 80);
    hdr->setSectionResizeMode(3, QHeaderView::Stretch);

    layout->addWidget(m_table);

    setWidget(container);
    initTitleBar();

    connect(m_table, &QTableWidget::cellClicked, this, &DisassemblyDockWidget::onCellClicked);

    updateSupportLabel();
}

DisassemblyDockWidget::~DisassemblyDockWidget()
{
    delete m_disasm;
}

void DisassemblyDockWidget::setHexEdit(HexEditor *hexEdit)
{
    if (m_hexEdit) {
        disconnect(m_hexEdit, &HexEditor::currentAddressChanged,
                   this, &DisassemblyDockWidget::onCursorPositionChanged);
    }

    m_hexEdit = hexEdit;
    m_lastOffset = -1;

    if (m_hexEdit) {
        connect(m_hexEdit, &HexEditor::currentAddressChanged,
                this, &DisassemblyDockWidget::onCursorPositionChanged);
    }

    refresh();
}

void DisassemblyDockWidget::setRomType(RomType type)
{
    if (m_romType == type)
        return;
    m_romType = type;

    if (Disassembler::isSupported(type)) {
        m_disasm->setRomType(type);
    }

    m_lastOffset = -1;
    updateSupportLabel();
    refresh();
}

void DisassemblyDockWidget::refresh()
{
    if (!m_hexEdit || !isVisible()) {
        return;
    }

    // Get cursor position in bytes (cursorPosition is in nibbles)
    const qint64 cursorNibble = m_hexEdit->cursorPosition();
    const qint64 fileOffset = cursorNibble / 2;

    disassembleAt(fileOffset);
}

void DisassemblyDockWidget::clear()
{
    m_table->setRowCount(0);
    m_lastOffset = -1;
}

void DisassemblyDockWidget::disassembleAt(qint64 fileOffset)
{
    if (!Disassembler::isSupported(m_romType)) {
        m_table->setRowCount(0);
        return;
    }

    // Avoid redundant disassembly of the same region
    if (fileOffset == m_lastOffset)
        return;
    m_lastOffset = fileOffset;

    const QByteArray data = m_hexEdit->data();
    if (data.isEmpty() || fileOffset >= data.size()) {
        m_table->setRowCount(0);
        return;
    }

    const auto instructions = m_disasm->disassemble(data, fileOffset, kMaxBytes, kMaxInstructions);

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(0);
    m_table->setRowCount(instructions.size());

    const QColor linkColor = palette().color(QPalette::Link);
    const QColor branchBg = palette().color(QPalette::AlternateBase);

    for (int i = 0; i < instructions.size(); ++i) {
        const auto &instr = instructions[i];

        // Column 0: File offset
        auto *offsetItem = new QTableWidgetItem(
            QString::number(instr.fileOffset, 16).toUpper().rightJustified(8, QLatin1Char('0')));
        offsetItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(i, 0, offsetItem);

        // Column 1: Raw bytes
        auto *bytesItem = new QTableWidgetItem(instr.bytes);
        bytesItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_table->setItem(i, 1, bytesItem);

        // Column 2: Mnemonic
        auto *mnemonicItem = new QTableWidgetItem(instr.mnemonic);
        mnemonicItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (instr.isBranch) {
            QFont boldFont = m_table->font();
            boldFont.setBold(true);
            mnemonicItem->setFont(boldFont);
        }
        m_table->setItem(i, 2, mnemonicItem);

        // Column 3: Operands (with clickable link for branches)
        auto *operandsItem = new QTableWidgetItem(instr.operands);
        operandsItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        if (instr.isBranch && instr.branchTarget >= 0 && instr.branchTarget < data.size()) {
            // Make it look like a link
            operandsItem->setForeground(linkColor);
            QFont linkFont = m_table->font();
            linkFont.setUnderline(true);
            operandsItem->setFont(linkFont);
            // Store target offset in UserRole for click handling
            operandsItem->setData(Qt::UserRole, instr.branchTarget);
        }

        m_table->setItem(i, 3, operandsItem);
    }

    m_table->setUpdatesEnabled(true);

    // Update title with instruction count
    const int count = instructions.size();
    const QString title = tr("Disassembly") +
        (count > 0 ? QStringLiteral(" \u2013 %1").arg(count) : QString());
    setWindowTitle(title);
}

void DisassemblyDockWidget::onCursorPositionChanged(qint64 /*offset*/)
{
    if (m_followCursor)
        refresh();
}

void DisassemblyDockWidget::onCellClicked(int row, int column)
{
    // Column 0: click on offset → jump hex editor to that byte
    if (column == 0) {
        QTableWidgetItem *item = m_table->item(row, 0);
        if (!item) return;
        bool ok = false;
        qint64 offset = item->text().toLongLong(&ok, 16);
        if (ok)
            emit jumpToOffset(offset);
        return;
    }

    // Column 3 (operands): if it's a branch link, jump to target
    if (column == 3) {
        QTableWidgetItem *item = m_table->item(row, 3);
        if (!item) return;
        QVariant targetVar = item->data(Qt::UserRole);
        if (targetVar.isValid()) {
            qint64 target = targetVar.toLongLong();
            if (target >= 0) {
                // Jump hex editor to target and re-disassemble from there
                emit jumpToOffset(target);
            }
        }
    }
}

void DisassemblyDockWidget::updateSupportLabel()
{
    if (Disassembler::isSupported(m_romType)) {
        m_statusLabel->setText(romTypeName(m_romType));
        m_statusLabel->setStyleSheet(QString());
    } else {
        if (m_romType == static_cast<RomType>(0))
            m_statusLabel->setText(tr("No ROM type detected"));
        else
            m_statusLabel->setText(tr("Disassembly not supported for this ROM type"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    }
}

QByteArray DisassemblyDockWidget::saveColumnsState() const
{
    return m_table->horizontalHeader()->saveState();
}

void DisassemblyDockWidget::restoreColumnsState(const QByteArray &state)
{
    if (!state.isEmpty())
        m_table->horizontalHeader()->restoreState(state);
}

void DisassemblyDockWidget::retranslateUi()
{
    setWindowTitle(tr("Disassembly"));
    if (m_followBtn)
        m_followBtn->setToolTip(tr("Follow cursor"));
    m_table->setHorizontalHeaderLabels({tr("Offset"), tr("Hex"), tr("Instruction"), tr("Operands")});
    updateSupportLabel();
}

void DisassemblyDockWidget::onPaletteChanged()
{
    if (m_followBtn)
        m_followBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
    // Re-render to update link colors
    m_lastOffset = -1;
    refresh();
}
