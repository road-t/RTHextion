#include "TablesDockWidget.h"
#include "translationtable.h"
#include "DockTitleBar.h"
#include "BaseDockWidget.h"

#include <QPainter>
#include <QPainterPath>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QFile>
#include <QRegularExpressionValidator>
#include <QLineEdit>
#include <QTabBar>
#include <QSet>
#include <QPushButton>
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QMenu>
#include <QUndoCommand>
#include <QDataStream>
#include <QSignalBlocker>
#include <QMainWindow>
#include <QClipboard>
#include <QMimeData>
#include <QRegularExpression>
#include <QEvent>
#include <algorithm>

#include <QStyledItemDelegate>

// ---------------------------------------------------------------------------
// Hex-only input delegate for column 0 of the table grid
// ---------------------------------------------------------------------------

class HexColumnDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (index.column() == 0) {
            if (auto *le = qobject_cast<QLineEdit *>(editor)) {
                static const QRegularExpression hexRe(QStringLiteral("^([0-9A-Fa-f]{2})*$"));
                le->setValidator(new QRegularExpressionValidator(hexRe, le));
            }
        }
        return editor;
    }
};

// ---------------------------------------------------------------------------
// QTableWidgetItem subclass that sorts hex keys numerically
// ---------------------------------------------------------------------------

class HexSortItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem &other) const override
    {
        const QString a = text().trimmed().toUpper();
        const QString b = other.text().trimmed().toUpper();
        // Empty keys (placeholder row) sort last
        if (a.isEmpty()) return false;
        if (b.isEmpty()) return true;
        // Compare as unsigned hex integers (up to 64-bit keys)
        bool okA = false, okB = false;
        const quint64 va = a.toULongLong(&okA, 16);
        const quint64 vb = b.toULongLong(&okB, 16);
        if (okA && okB) return va < vb;
        // Fallback to string compare for very long keys
        return a < b;
    }
};

// ---------------------------------------------------------------------------
// Undo command: stores before/after snapshots of all tables
// ---------------------------------------------------------------------------

static TableTab cloneTab(const TableTab &src)
{
    return src;
}

static void configureTableGridColumns(QTableWidget *grid)
{
    if (!grid || !grid->horizontalHeader())
        return;

    auto *header = grid->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Interactive);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    header->setStretchLastSection(false);

    const int hexWidth = qMax(80, grid->fontMetrics().horizontalAdvance(QStringLiteral("00000000")) + 20);
    const int valueWidth = qMax(80, hexWidth / 2);
    grid->setColumnWidth(0, hexWidth);
    grid->setColumnWidth(1, valueWidth);
}

static constexpr const char *kTableRowsMimeType = "application/x-rthextion-table-rows";

class TableSnapshotCommand : public QUndoCommand
{
public:
    TableSnapshotCommand(TablesDockWidget *dock,
                         QVector<TableTab> before,
                         int beforeActive,
                         QVector<TableTab> after,
                         int afterActive,
                         const QString &description)
        : QUndoCommand(description)
        , m_dock(dock)
        , m_before(std::move(before))
        , m_beforeActive(beforeActive)
        , m_after(std::move(after))
        , m_afterActive(afterActive)
    {}

    ~TableSnapshotCommand() override = default;

    void undo() override { m_dock->applySnapshot(m_before, m_beforeActive); }
    void redo() override { m_dock->applySnapshot(m_after,  m_afterActive); }

private:
    TablesDockWidget *m_dock;
    QVector<TableTab> m_before;
    int m_beforeActive;
    QVector<TableTab> m_after;
    int m_afterActive;
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TablesDockWidget::TablesDockWidget(QWidget *parent)
    : BaseDockWidget(tr("Tables"), parent)
{
    setObjectName(QStringLiteral("TablesDockWidget"));
    m_defaultExpandedWidth = 360;

    m_undoStack = new QUndoStack(this);

    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));
    m_undoAct      = m_undoStack->createUndoAction(this);
    m_redoAct      = m_undoStack->createRedoAction(this);
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_redoAct->setShortcut(QKeySequence::Redo);
    m_undoAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_redoAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);

    // Eye (use-table) toggle button — inserted before "+"
    m_useTableBtn = new QToolButton(this);
    m_useTableBtn->setCheckable(true);
    m_useTableBtn->setAutoRaise(true);
    m_useTableBtn->setToolTip(tr("Use table"));
    m_useTableBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
    m_useTableBtn->setIconSize(QSize(16, 16));
    m_toolbar->insertWidget(m_toolbar->actions().isEmpty() ? nullptr : m_toolbar->actions().first(),
                            m_useTableBtn);
    connect(m_useTableBtn, &QToolButton::clicked, this, [this](bool checked) {
        emit useTableToggled(checked);
    });

    m_addAct = new QAction(this);
    connect(m_addAct, &QAction::triggered, this, [this] {
        pushUndoSnapshot(tr("Add table"));
        addTable();
    });

    m_duplicateAct = new QAction(this);
    connect(m_duplicateAct, &QAction::triggered, this, [this] {
        pushUndoSnapshot(tr("Duplicate table"));
        duplicateCurrentTable();
    });

    m_generateAct = new QAction(this);
    connect(m_generateAct, &QAction::triggered, this, [this] {
        emit generateTableRequested();
    });

    m_importAct = new QAction(this);
    connect(m_importAct, &QAction::triggered, this, [this] {
        importTable();
    });

    m_addMenu = new QMenu(this);
    m_addMenu->addAction(m_importAct);
    m_addMenu->addSeparator();
    m_addMenu->addAction(m_duplicateAct);
    m_addMenu->addAction(m_generateAct);
    m_addMenu->addAction(m_addAct);

    m_addBtn = new QToolButton(this);
    m_addBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_addBtn->setMenu(m_addMenu);
    m_addBtn->setIcon(makeAddIcon(palette().color(QPalette::WindowText)));
    m_addBtn->setIconSize(QSize(16, 16));
    m_addBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    connect(m_addBtn, &QToolButton::clicked, this, [this] {
        if (m_addAct)
            m_addAct->trigger();
    });
    m_toolbar->addWidget(m_addBtn);

    m_removeAct = m_toolbar->addAction(QString{}, this, [this]{ removeCurrentTable(); });
    m_removeAct->setIcon(makeRemoveIcon(palette().color(QPalette::WindowText)));

    m_toolbar->addSeparator();
    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    m_copyToAct = m_toolbar->addAction(QString{}, this, [this]{ emit copyToTabRequested(); });
    m_copyToAct->setIcon(makeCopyToIcon(palette().color(QPalette::WindowText)));
    m_exportAct = m_toolbar->addAction(QString{}, this, [this]{ exportCurrentTable(); });
    m_exportAct->setIcon(makeExportIcon(palette().color(QPalette::WindowText)));
    layout->addWidget(m_toolbar);

    // Tab widget
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(false);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::currentChanged, this, &TablesDockWidget::onTabChanged);
    connect(m_tabs->tabBar(), &QTabBar::tabMoved, this, &TablesDockWidget::onTabMoved);
    // Double-click on a tab label → rename
    connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked,
            this, &TablesDockWidget::onTabDoubleClicked);
    // Right-click context menu on tab bar
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QWidget::customContextMenuRequested,
            this, &TablesDockWidget::onTabContextMenu);
    layout->addWidget(m_tabs);

    container->setLayout(layout);
    setWidget(container);

    initTitleBar();

    updateButtonStates();
    retranslateUi();

}

TablesDockWidget::~TablesDockWidget()
{
    // Remove tabs first to disconnect signals.
    while (m_tabs->count() > 0)
        m_tabs->removeTab(0);

    m_tables.clear();

    // Clear commands after live state is gone.
    m_undoStack->clear();
}

// ---------------------------------------------------------------------------
// Table management
// ---------------------------------------------------------------------------

int TablesDockWidget::addTable(const QString &name, const TranslationTable *table)
{
    setCollapsed(false);

    TableTab tab;
    tab.name = name.isEmpty() ? defaultTabName(0) : name;
    if (table)
        tab.table = *table;
    // First table added to an empty dock becomes the original encoding table
    tab.isOriginal = m_tables.isEmpty();

    // Create grid widget
    auto *grid = new QTableWidget(0, 2, this);
    grid->setHorizontalHeaderLabels(QStringList() << QStringLiteral("HEX") << tr("Value"));
    grid->setItemDelegateForColumn(0, new HexColumnDelegate(grid));
    configureTableGridColumns(grid);
    grid->setAlternatingRowColors(true);
    grid->setSelectionBehavior(QAbstractItemView::SelectRows);
    grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    grid->setContextMenuPolicy(Qt::CustomContextMenu);
    grid->verticalHeader()->setDefaultSectionSize(22);
    grid->setSortingEnabled(true);
    grid->sortByColumn(0, Qt::AscendingOrder);
    connect(grid, &QTableWidget::cellChanged, this, &TablesDockWidget::onCellChanged);
    connect(grid, &QTableWidget::cellClicked, this, [this, grid](int row, int /*col*/) {
        if (isPlaceholderRow(grid, row))
            activatePlaceholderRow(grid, row);
    });
    connect(grid, &QTableWidget::customContextMenuRequested, this, [this, grid](const QPoint &pos) {
        onGridContextMenu(grid, pos);
    });

    m_ignoreChanges = true;
    populateGrid(grid, &tab.table);
    m_ignoreChanges = false;

    m_tables.append(tab);

    // Wrap the grid in a container widget; add a centered type toggle row above the grid
    auto *wrapper = new QWidget(this);
    auto *wLayout = new QVBoxLayout(wrapper);
    wLayout->setContentsMargins(0, 2, 0, 0);
    wLayout->setSpacing(2);

    auto *typeRow = new QWidget(wrapper);
    typeRow->setObjectName(QStringLiteral("tableTypeRow"));
    auto *typeLayout = new QHBoxLayout(typeRow);
    typeLayout->setContentsMargins(4, 0, 4, 0);
    typeLayout->setSpacing(0);

    auto *origBtn = new QToolButton(typeRow);
    origBtn->setObjectName(QStringLiteral("origBtn"));
    origBtn->setText(tr("Original"));
    origBtn->setCheckable(true);
    origBtn->setChecked(tab.isOriginal);

    auto *transBtn = new QToolButton(typeRow);
    transBtn->setObjectName(QStringLiteral("transBtn"));
    transBtn->setText(tr("Translation"));
    transBtn->setCheckable(true);
    transBtn->setChecked(!tab.isOriginal);

    auto *bg = new QButtonGroup(wrapper);
    bg->setExclusive(true);
    bg->addButton(origBtn, 0);
    bg->addButton(transBtn, 1);

    connect(bg, &QButtonGroup::idClicked, this, [this, wrapper](int id) {
        const int tabIdx = m_tabs->indexOf(wrapper);
        if (tabIdx < 0 || tabIdx >= m_tables.size()) return;
        setTableOriginal(tabIdx, id == 0);
        emit tableContentChanged();
    });

    typeLayout->addStretch();
    typeLayout->addWidget(origBtn);
    typeLayout->addWidget(transBtn);
    typeLayout->addStretch();

    wLayout->addWidget(typeRow);
    wLayout->addWidget(grid);

    const int idx = m_tabs->addTab(wrapper, tab.name);
    m_tabs->setCurrentIndex(idx);
    updateButtonStates();
    return idx;
}

void TablesDockWidget::duplicateCurrentTable()
{
    const int idx = m_tabs->currentIndex();
    if (idx < 0 || idx >= m_tables.size())
        return;

    syncTableFromGrid(idx);

    const TableTab &src = m_tables[idx];
    addTable(src.name + " " + tr("(copy)"), &src.table);
}

void TablesDockWidget::removeCurrentTable()
{
    const int idx = m_tabs->currentIndex();
    if (idx < 0 || idx >= m_tables.size())
        return;

    // Confirmation dialog
    const auto res = QMessageBox::question(
        this,
        tr("Remove table"),
        tr("Remove table \"%1\"?").arg(m_tables[idx].name),
        QMessageBox::Yes | QMessageBox::Cancel);
    if (res != QMessageBox::Yes)
        return;

    pushUndoSnapshot(tr("Remove table"));

    if (m_tables.size() == 1) {
        // Last table — just clear it, don't remove the tab
        m_tables[0].table.clearItems();
        m_ignoreChanges = true;
        if (auto *g = gridAt(0)) {
            g->setRowCount(0);
            ensurePlaceholderRow(g);
        }
        m_ignoreChanges = false;
    } else {
        m_tables.removeAt(idx);
        m_tabs->removeTab(idx);
    }

    updateButtonStates();
    emit tableContentChanged();
    emit activeTableChanged(currentTable());
}

void TablesDockWidget::exportCurrentTable()
{
    const int idx = m_tabs->currentIndex();
    if (idx < 0 || idx >= m_tables.size())
        return;

    syncTableFromGrid(idx);

    const TranslationTable *t = &m_tables[idx].table;
    if (t->size() == 0) return;

    // Build suggested filename: "ProjectName - TableName" or just "TableName"
    QString suggestedName = m_tables[idx].name;
    if (!m_projectName.isEmpty())
        suggestedName = m_projectName + QStringLiteral(" - ") + suggestedName;

    const QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export table"), suggestedName + QStringLiteral(".tbl"),
        QStringLiteral("Tables (*.tbl);;Text files (*.txt)"));
    if (fileName.isEmpty()) return;

    if (!t->save(fileName))
        QMessageBox::warning(this, tr("Error"), tr("Could not save the table file"));
}

void TablesDockWidget::importTable()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, tr("Import table"), QString(),
        QStringLiteral("Tables (*.tbl *.tab *.table);;Text files (*.txt)"));
    if (fileName.isEmpty()) return;

    QString importEncoding;
    QFile rawFile(fileName);
    if (rawFile.open(QIODevice::ReadOnly)) {
        const QByteArray raw = rawFile.readAll();
        rawFile.close();
        if (TranslationTable::hasNonAsciiValueBytes(raw)) {
            const QStringList encodings = TranslationTable::supportedImportEncodings();
            const QString guessed = TranslationTable::guessImportEncoding(raw);
            const int idx = qMax(0, encodings.indexOf(guessed));
            bool ok = false;
            const QString picked = QInputDialog::getItem(
                this,
                tr("Table encoding"),
                tr("Select encoding for imported table:"),
                encodings,
                idx,
                false,
                &ok);
            if (!ok)
                return;
            importEncoding = picked;
        }
    }

    pushUndoSnapshot(tr("Import table"));

    const TranslationTable importedTable(fileName, importEncoding);
    addTable(QFileInfo(fileName).completeBaseName(), &importedTable);
    emit tableContentChanged();
}

void TablesDockWidget::clearAll()
{
    m_ignoreChanges = true;
    while (m_tabs->count() > 0)
        m_tabs->removeTab(0);
    m_tables.clear();
    m_nextTableNumber = 1;
    m_undoStack->clear();
    m_ignoreChanges = false;
    updateButtonStates();
}

int TablesDockWidget::count() const
{
    return m_tables.size();
}

TranslationTable *TablesDockWidget::currentTable() const
{
    const int idx = m_tabs->currentIndex();
    if (idx >= 0 && idx < m_tables.size())
        return const_cast<TranslationTable *>(&m_tables[idx].table);
    return nullptr;
}

int TablesDockWidget::currentIndex() const
{
    return m_tabs->currentIndex();
}

void TablesDockWidget::setCurrentIndex(int index)
{
    if (index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
}

const QVector<TableTab> &TablesDockWidget::allTables() const
{
    // Sync all grids before returning
    for (int i = 0; i < m_tables.size(); ++i)
        const_cast<TablesDockWidget *>(this)->syncTableFromGrid(i);
    return m_tables;
}

void TablesDockWidget::retranslateUi()
{
    setWindowTitle(tr("Tables"));
    if (m_useTableBtn)
        m_useTableBtn->setToolTip(tr("Use table"));
    if (m_addBtn)
        m_addBtn->setToolTip(tr("Add"));
    m_addAct->setText(tr("Blank"));
    m_addAct->setToolTip(tr("Add empty table"));
    m_duplicateAct->setText(tr("Copy"));
    m_duplicateAct->setToolTip(tr("Duplicate current table"));
    if (m_generateAct) {
        m_generateAct->setText(tr("Generate"));
        m_generateAct->setToolTip(tr("Generate table by searching for a known text"));
    }
    if (m_importAct) {
        m_importAct->setText(tr("Import"));
        m_importAct->setToolTip(tr("Import table from file"));
    }
    m_removeAct->setToolTip(tr("Remove current table"));
    if (m_copyToAct)
        m_copyToAct->setToolTip(tr("Copy table to another tab"));
    m_exportAct->setToolTip(tr("Export table to file"));
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *w = m_tabs->widget(i);
        if (!w) continue;
        if (auto *btn = w->findChild<QToolButton *>(QStringLiteral("origBtn")))
            btn->setText(tr("Original"));
        if (auto *btn = w->findChild<QToolButton *>(QStringLiteral("transBtn")))
            btn->setText(tr("Translation"));
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void TablesDockWidget::onTabChanged(int index)
{
    if (m_ignoreChanges) return;
    updateButtonStates();
    if (index >= 0 && index < m_tables.size())
        emit activeTableChanged(&m_tables[index].table);
    else
        emit activeTableChanged(nullptr);
}

void TablesDockWidget::onTabMoved(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_tables.size() || to >= m_tables.size())
        return;

    m_tables.move(from, to);
    updateButtonStates();
    emit tableContentChanged();
    emit activeTableChanged(currentTable());
}

void TablesDockWidget::onTabDoubleClicked(int index)
{
    if (index < 0 || index >= m_tables.size())
        return;

    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename table"),
        tr("Table name") + ":",
        QLineEdit::Normal,
        m_tables[index].name,
        &ok);
    if (!ok || newName.trimmed().isEmpty()) {
        // Deferred focus restoration: after the modal dialog's close animation
        // completes, Qt may move focus elsewhere, so we restore it via queued call.
        QMetaObject::invokeMethod(this, [this, index]() {
            m_tabs->setCurrentIndex(index);
            if (auto *bar = m_tabs->tabBar()) {
                bar->setCurrentIndex(index);
                bar->setFocus(Qt::OtherFocusReason);
                bar->update();
            }
            m_tabs->update();
        }, Qt::QueuedConnection);
        return;
    }

    pushUndoSnapshot(tr("Rename table"));
    m_tables[index].name = newName.trimmed();
    m_tabs->setTabText(index, m_tables[index].name);
    emit tableContentChanged();
}

void TablesDockWidget::onCellChanged(int row, int col)
{
    if (m_ignoreChanges) return;

    const int idx = m_tabs->currentIndex();
    if (idx < 0 || idx >= m_tables.size()) return;

    auto *grid = gridAt(idx);
    if (!grid)
        return;

    if (row >= 0 && row < grid->rowCount() && !isPlaceholderRow(grid, row)) {
        auto *hexItem = grid->item(row, 0);
        auto *valItem = grid->item(row, 1);
        if (hexItem && valItem) {
            const QString normalizedHex = hexItem->text().trimmed().toUpper();
            const bool validHex = isValidHexKeyText(normalizedHex);

            if (col == 0) {
                m_ignoreChanges = true;
                hexItem->setText(normalizedHex);
                if (validHex) {
                    hexItem->setBackground(QBrush());
                    valItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
                    valItem->setToolTip(QString());
                } else {
                    hexItem->setBackground(QColor(255, 220, 220));
                    valItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                    valItem->setToolTip(tr("HEX must contain only hexadecimal digits and even length (e.g. 0A or 0A1B)."));
                }
                m_ignoreChanges = false;
            } else if (col == 1 && !validHex) {
                m_ignoreChanges = true;
                valItem->setText(QString());
                valItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                valItem->setToolTip(tr("Enter HEX key first"));
                m_ignoreChanges = false;
                QMessageBox::warning(this, tr("Invalid HEX"),
                                     tr("Allowed format: hexadecimal digits with even length."));
            }
        }
    }

    // Push undo snapshot for content edits
    // We don't push per-keystroke; instead we rely on tab losing focus / explicit commit.
    // For now push per-change (may create many entries but is functionally correct).
    syncTableFromGrid(idx);
    updateButtonStates();
    emit tableContentChanged();
    emit activeTableChanged(&m_tables[idx].table);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void TablesDockWidget::populateGrid(QTableWidget *grid, TranslationTable *table)
{
    grid->setSortingEnabled(false);
    grid->setRowCount(0);
    if (!table) {
        ensurePlaceholderRow(grid);
        grid->setSortingEnabled(true);
        return;
    }

    auto *items = table->getItems();
    for (auto it = items->cbegin(); it != items->cend(); ++it) {
        const int row = grid->rowCount();
        grid->insertRow(row);
        auto *hexItem = new HexSortItem(
            QString::number(static_cast<uint8_t>(it.key()), 16).toUpper().rightJustified(2, '0'));
        hexItem->setTextAlignment(Qt::AlignCenter);
        grid->setItem(row, 0, hexItem);
        grid->setItem(row, 1, new QTableWidgetItem(it.value()));
    }

    const auto &mbItems = table->getMultiByteItems();
    for (auto it = mbItems.cbegin(); it != mbItems.cend(); ++it) {
        const int row = grid->rowCount();
        grid->insertRow(row);
        QString hexKey;
        for (int i = 0; i < it.key().size(); ++i)
            hexKey += QString::number(static_cast<uint8_t>(it.key()[i]), 16).toUpper().rightJustified(2, '0');
        auto *hexItem = new HexSortItem(hexKey);
        hexItem->setTextAlignment(Qt::AlignCenter);
        grid->setItem(row, 0, hexItem);
        grid->setItem(row, 1, new QTableWidgetItem(it.value()));
    }

    ensurePlaceholderRow(grid);
    grid->setSortingEnabled(true);
}

void TablesDockWidget::syncTableFromGrid(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= m_tables.size()) return;

    auto *grid = gridAt(tabIndex);
    if (!grid) return;

    TranslationTable *table = &m_tables[tabIndex].table;
    table->clearItems();

    for (int r = 0; r < grid->rowCount(); ++r) {
        auto *hexItem = grid->item(r, 0);
        auto *valItem = grid->item(r, 1);
        if (!hexItem || !valItem) continue;

        const QString hexText = hexItem->text().trimmed();
        if (hexText.isEmpty() || hexText.size() % 2 != 0) continue;

        QByteArray key;
        bool ok = true;
        for (int i = 0; i < hexText.size(); i += 2) {
            bool partOk = false;
            const int v = hexText.mid(i, 2).toInt(&partOk, 16);
            if (!partOk) { ok = false; break; }
            key.append(static_cast<char>(v));
        }
        if (!ok || key.isEmpty()) continue;

        const QString value = valItem->text();
        if (value.isEmpty()) continue;

        if (key.size() == 1)
            table->setItem(static_cast<uint8_t>(key[0]), value);
        else
            table->setMultiByteItem(key, value);
    }

    m_tabs->setTabText(tabIndex, m_tables[tabIndex].name);
}

void TablesDockWidget::ensurePlaceholderRow(QTableWidget *grid)
{
    if (!grid)
        return;

    const int row = grid->rowCount();
    grid->insertRow(row);

    auto *hexItem = new HexSortItem(QString());
    hexItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    hexItem->setTextAlignment(Qt::AlignCenter);

    auto *valueItem = new QTableWidgetItem(tr("Add value"));
    valueItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    valueItem->setForeground(QBrush(Qt::gray));
    valueItem->setData(Qt::UserRole, true);

    grid->setItem(row, 0, hexItem);
    grid->setItem(row, 1, valueItem);
}

bool TablesDockWidget::isPlaceholderRow(QTableWidget *grid, int row) const
{
    if (!grid || row < 0 || row >= grid->rowCount())
        return false;
    auto *valueItem = grid->item(row, 1);
    return valueItem && valueItem->data(Qt::UserRole).toBool();
}

void TablesDockWidget::activatePlaceholderRow(QTableWidget *grid, int row)
{
    if (!isPlaceholderRow(grid, row))
        return;

    const QSignalBlocker blocker(grid);

    auto *hexItem = grid->item(row, 0);
    if (!hexItem) {
        hexItem = new HexSortItem;
        grid->setItem(row, 0, hexItem);
    }
    hexItem->setText(QString());
    hexItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    hexItem->setTextAlignment(Qt::AlignCenter);

    auto *valueItem = grid->item(row, 1);
    if (!valueItem) {
        valueItem = new QTableWidgetItem;
        grid->setItem(row, 1, valueItem);
    }
    valueItem->setText(QString());
    valueItem->setData(Qt::UserRole, false);
    valueItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    valueItem->setToolTip(tr("Enter HEX key first"));
    valueItem->setForeground(QBrush());

    ensurePlaceholderRow(grid);

    grid->setCurrentCell(row, 0);
    grid->editItem(hexItem);
}

bool TablesDockWidget::isValidHexKeyText(const QString &hexText) const
{
    static const QRegularExpression kHexRe(QStringLiteral("^[0-9A-Fa-f]+$"));
    if (hexText.isEmpty())
        return false;
    if (hexText.size() % 2 != 0)
        return false;
    return kHexRe.match(hexText).hasMatch();
}

QVector<int> TablesDockWidget::selectedEditableRows(QTableWidget *grid) const
{
    QVector<int> rows;
    if (!grid)
        return rows;

    QModelIndexList indices = grid->selectionModel() ? grid->selectionModel()->selectedRows() : QModelIndexList();
    rows.reserve(indices.size());
    for (const QModelIndex &idx : indices) {
        if (!idx.isValid())
            continue;
        const int row = idx.row();
        if (row >= 0 && row < grid->rowCount() && !isPlaceholderRow(grid, row))
            rows.append(row);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void TablesDockWidget::copyRowsToClipboard(QTableWidget *grid, const QVector<int> &rows) const
{
    if (!grid || rows.isEmpty())
        return;

    QStringList lines;
    lines.reserve(rows.size());
    for (int row : rows) {
        auto *hexItem = grid->item(row, 0);
        auto *valItem = grid->item(row, 1);
        if (!hexItem || !valItem)
            continue;
        const QString hex = hexItem->text().trimmed().toUpper();
        const QString value = valItem->text();
        if (!isValidHexKeyText(hex))
            continue;
        lines.append(hex + QLatin1Char('\t') + value);
    }

    if (lines.isEmpty())
        return;

    auto *mime = new QMimeData;
    const QString text = lines.join(QLatin1Char('\n'));
    mime->setData(kTableRowsMimeType, text.toUtf8());
    mime->setText(text);
    QApplication::clipboard()->setMimeData(mime);
}

void TablesDockWidget::deleteRows(QTableWidget *grid, const QVector<int> &rows)
{
    if (!grid || rows.isEmpty())
        return;

    m_ignoreChanges = true;
    for (int i = rows.size() - 1; i >= 0; --i)
        grid->removeRow(rows[i]);
    m_ignoreChanges = false;

    ensurePlaceholderRow(grid);
}

void TablesDockWidget::pasteRowsFromClipboard(QTableWidget *grid)
{
    if (!grid)
        return;

    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime)
        return;

    QString text;
    if (mime->hasFormat(kTableRowsMimeType))
        text = QString::fromUtf8(mime->data(kTableRowsMimeType));
    else
        text = mime->text();

    if (text.trimmed().isEmpty())
        return;

    struct RowData {
        QString hex;
        QString value;
    };
    QVector<RowData> incoming;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
    incoming.reserve(lines.size());

    for (const QString &line : lines) {
        QString hex;
        QString value;
        const int tabPos = line.indexOf(QLatin1Char('\t'));
        if (tabPos >= 0) {
            hex = line.left(tabPos).trimmed().toUpper();
            value = line.mid(tabPos + 1);
        } else {
            const int eqPos = line.indexOf(QLatin1Char('='));
            if (eqPos <= 0)
                continue;
            hex = line.left(eqPos).trimmed().toUpper();
            value = line.mid(eqPos + 1);
        }

        if (!isValidHexKeyText(hex))
            continue;

        incoming.append({hex, value});
    }

    if (incoming.isEmpty())
        return;

    QHash<QString, int> existingByHex;
    for (int r = 0; r < grid->rowCount(); ++r) {
        if (isPlaceholderRow(grid, r))
            continue;
        auto *hexItem = grid->item(r, 0);
        if (!hexItem)
            continue;
        const QString hex = hexItem->text().trimmed().toUpper();
        if (isValidHexKeyText(hex))
            existingByHex.insert(hex, r);
    }

    enum class ConflictDecision { Ask, YesAll, NoAll };
    ConflictDecision decision = ConflictDecision::Ask;

    m_ignoreChanges = true;
    for (const RowData &rowData : incoming) {
        if (existingByHex.contains(rowData.hex)) {
            const int row = existingByHex.value(rowData.hex);
            auto *valItem = grid->item(row, 1);
            if (!valItem)
                continue;
            const QString currentValue = valItem->text();
            if (currentValue == rowData.value)
                continue;

            bool overwrite = false;
            if (decision == ConflictDecision::YesAll) {
                overwrite = true;
            } else if (decision == ConflictDecision::NoAll) {
                overwrite = false;
            } else {
                QMessageBox box(QMessageBox::Question,
                                tr("Overwrite value"),
                                tr("Key %1 already exists with different value. Overwrite?").arg(rowData.hex),
                                QMessageBox::NoButton,
                                this);
                QPushButton *yesBtn = box.addButton(tr("Yes"), QMessageBox::YesRole);
                QPushButton *noBtn = box.addButton(tr("No"), QMessageBox::NoRole);
                QPushButton *yesAllBtn = box.addButton(tr("Yes for all"), QMessageBox::AcceptRole);
                QPushButton *noAllBtn = box.addButton(tr("No for all"), QMessageBox::RejectRole);
                box.exec();

                if (box.clickedButton() == yesAllBtn) {
                    decision = ConflictDecision::YesAll;
                    overwrite = true;
                } else if (box.clickedButton() == noAllBtn) {
                    decision = ConflictDecision::NoAll;
                    overwrite = false;
                } else if (box.clickedButton() == yesBtn) {
                    overwrite = true;
                } else if (box.clickedButton() == noBtn) {
                    overwrite = false;
                }
            }

            if (overwrite)
                valItem->setText(rowData.value);
            continue;
        }

        int insertRow = grid->rowCount();
        for (int r = 0; r < grid->rowCount(); ++r) {
            if (isPlaceholderRow(grid, r)) {
                insertRow = r;
                break;
            }
        }

        grid->insertRow(insertRow);
        auto *hexItem = new HexSortItem(rowData.hex);
        hexItem->setTextAlignment(Qt::AlignCenter);
        auto *valItem = new QTableWidgetItem(rowData.value);
        valItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        grid->setItem(insertRow, 0, hexItem);
        grid->setItem(insertRow, 1, valItem);

        existingByHex.insert(rowData.hex, insertRow);
    }
    m_ignoreChanges = false;

    syncTableFromGrid(m_tabs->currentIndex());
    emit tableContentChanged();
    emit activeTableChanged(currentTable());
}

void TablesDockWidget::onGridContextMenu(QTableWidget *grid, const QPoint &pos)
{
    if (!grid)
        return;

    const QVector<int> rows = selectedEditableRows(grid);
    const bool hasRows = !rows.isEmpty();

    QMenu menu(this);
    QAction *cutAct = menu.addAction(tr("Cut"));
    QAction *copyAct = menu.addAction(tr("Copy"));
    QAction *deleteAct = menu.addAction(tr("Delete"));
    menu.addSeparator();
    QAction *pasteAct = menu.addAction(tr("Paste"));

    cutAct->setEnabled(hasRows);
    copyAct->setEnabled(hasRows);
    deleteAct->setEnabled(hasRows);
    pasteAct->setEnabled(QApplication::clipboard() && QApplication::clipboard()->mimeData() &&
                        (QApplication::clipboard()->mimeData()->hasFormat(kTableRowsMimeType)
                         || !QApplication::clipboard()->text().trimmed().isEmpty()));

    QAction *chosen = menu.exec(grid->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == copyAct) {
        copyRowsToClipboard(grid, rows);
        return;
    }

    if (chosen == cutAct) {
        pushUndoSnapshot(tr("Cut rows"));
        copyRowsToClipboard(grid, rows);
        deleteRows(grid, rows);
        syncTableFromGrid(m_tabs->currentIndex());
        updateButtonStates();
        emit tableContentChanged();
        emit activeTableChanged(currentTable());
        return;
    }

    if (chosen == deleteAct) {
        pushUndoSnapshot(tr("Delete rows"));
        deleteRows(grid, rows);
        syncTableFromGrid(m_tabs->currentIndex());
        updateButtonStates();
        emit tableContentChanged();
        emit activeTableChanged(currentTable());
        return;
    }

    if (chosen == pasteAct) {
        pushUndoSnapshot(tr("Paste rows"));
        pasteRowsFromClipboard(grid);
        updateButtonStates();
        return;
    }
}

void TablesDockWidget::updateButtonStates()
{
    const int idx = m_tabs->currentIndex();
    const bool hasTab = idx >= 0 && idx < m_tables.size();
    const bool hasEntries = hasTab && m_tables[idx].table.size() > 0;

    m_removeAct->setEnabled(hasTab);
    m_copyToAct->setEnabled(hasTab);
    m_exportAct->setEnabled(hasEntries);
    m_duplicateAct->setEnabled(hasTab);

    // Update per-tab toggle buttons
    auto *wrapper = (idx >= 0 && idx < m_tabs->count()) ? m_tabs->widget(idx) : nullptr;
    if (wrapper) {
        auto *origBtn  = wrapper->findChild<QToolButton *>(QStringLiteral("origBtn"));
        auto *transBtn = wrapper->findChild<QToolButton *>(QStringLiteral("transBtn"));
        if (origBtn && transBtn) {
            const bool orig = hasTab && m_tables[idx].isOriginal;
            origBtn->setEnabled(hasTab);
            transBtn->setEnabled(hasTab);
            origBtn->setChecked(orig);
            transBtn->setChecked(hasTab && !orig);
        }
    }
}

QTableWidget *TablesDockWidget::currentGrid() const
{
    return gridAt(m_tabs->currentIndex());
}

QTableWidget *TablesDockWidget::gridAt(int index) const
{
    if (index < 0 || index >= m_tabs->count()) return nullptr;
    auto *wrapper = m_tabs->widget(index);
    if (!wrapper) return nullptr;
    return wrapper->findChild<QTableWidget *>();
}

QString TablesDockWidget::defaultTabName(int /*number*/) const
{
    // Find the highest "Table N" number already used and suggest N+1
    const QString prefix = tr("Table") + QStringLiteral(" ");
    int maxN = 0;
    for (const auto &tab : m_tables) {
        if (tab.name.startsWith(prefix)) {
            bool ok = false;
            const int n = tab.name.mid(prefix.size()).toInt(&ok);
            if (ok && n > maxN)
                maxN = n;
        }
    }
    return prefix + QString::number(maxN + 1);
}

// ---------------------------------------------------------------------------
// Undo/redo snapshot helpers
// ---------------------------------------------------------------------------

QVector<TableTab> TablesDockWidget::takeSnapshot() const
{
    // Sync all grids first
    for (int i = 0; i < m_tables.size(); ++i)
        const_cast<TablesDockWidget *>(this)->syncTableFromGrid(i);

    QVector<TableTab> snap;
    snap.reserve(m_tables.size());
    for (const auto &t : m_tables)
        snap.append(cloneTab(t));
    return snap;
}

void TablesDockWidget::pushUndoSnapshot(const QString &description)
{
    const QVector<TableTab> before = takeSnapshot();
    const int beforeActive = m_tabs->currentIndex();

    // The "after" state will be captured after the operation completes.
    // We store the before snapshot in m_pendingSnapshot and create the command lazily.
    // For simplicity, push command immediately with current state as both before & after,
    // and store it so it can be  OR, use a two-step approach.updated 
    //
    // Simplest correct approach: store before in member, let the caller do the mutation,
    // then finalise. But since lambdas call pushUndoSnapshot before the action, we push
    // a command with before==current. The redo() of this command will re-apply after state.
    // We use a deferred finalise via QTimer::singleShot(0).

    // Store for deferred finalization
    m_pendingSnapshot = before;

    // Deferred: capture "after" state and push command
    QMetaObject::invokeMethod(this, [this, description, before, beforeActive]() {
        const QVector<TableTab> after = takeSnapshot();
        const int afterActive = m_tabs->currentIndex();
        // Only push if something actually changed
        bool changed = (after.size() != before.size());
        if (!changed) {
            for (int i = 0; i < after.size() && !changed; ++i) {
                changed = (after[i].name != before[i].name)
                       || (after[i].table.size() != before[i].table.size());
            }
        }
        if (changed) {
            m_undoStack->push(new TableSnapshotCommand(
                this, before, beforeActive, after, afterActive, description));
        }
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Fast tab-switching: detach / attach live widgets
// ---------------------------------------------------------------------------

TablesDockWidget::LiveTabState TablesDockWidget::detachTabs()
{
    LiveTabState state;
    state.activeIndex = m_tabs->currentIndex();

    // Sync table data from grids before detaching
    for (int i = 0; i < m_tables.size(); ++i)
        syncTableFromGrid(i);

    state.tables = std::move(m_tables);
    m_tables.clear(); // ensure predictable empty state after move

    // Remove tab widgets from QTabWidget without deleting them
    state.wrappers.reserve(m_tabs->count());
    while (m_tabs->count() > 0) {
        QWidget *w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        w->hide();
        w->setParent(nullptr);          // un-parent so Qt won't auto-delete
        state.wrappers.append(w);
    }

    updateButtonStates();
    return state;
}

void TablesDockWidget::attachTabs(LiveTabState &&state)
{
    m_ignoreChanges = true;

    // Drop any residual tabs (should be empty after a prior detach)
    while (m_tabs->count() > 0)
        m_tabs->removeTab(0);

    m_tables = std::move(state.tables);

    for (int i = 0; i < state.wrappers.size(); ++i) {
        QWidget *w = state.wrappers[i];
        w->setParent(this);
        m_tabs->addTab(w, i < m_tables.size() ? m_tables[i].name : QString());
    }
    state.wrappers.clear();

    m_ignoreChanges = false;

    if (state.activeIndex >= 0 && state.activeIndex < m_tabs->count())
        m_tabs->setCurrentIndex(state.activeIndex);

    updateButtonStates();
    emit tableContentChanged();
    emit activeTableChanged(currentTable());
}

void TablesDockWidget::applySnapshot(const QVector<TableTab> &snapshot, int activeIndex)
{
    m_ignoreChanges = true;

    // Remove all current tabs
    while (m_tabs->count() > 0)
        m_tabs->removeTab(0);
    m_tables.clear();

    // Re-create tabs from snapshot
    for (const auto &snap : snapshot) {
        TableTab copy = cloneTab(snap);
        auto *grid = new QTableWidget(0, 2, this);
        grid->setHorizontalHeaderLabels(QStringList() << QStringLiteral("HEX") << tr("Value"));
        grid->setItemDelegateForColumn(0, new HexColumnDelegate(grid));
        configureTableGridColumns(grid);
        grid->setAlternatingRowColors(true);
        grid->setSelectionBehavior(QAbstractItemView::SelectRows);
        grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
        grid->setContextMenuPolicy(Qt::CustomContextMenu);
        grid->verticalHeader()->setDefaultSectionSize(22);
        grid->setSortingEnabled(true);
        grid->sortByColumn(0, Qt::AscendingOrder);
        connect(grid, &QTableWidget::cellChanged, this, &TablesDockWidget::onCellChanged);
        connect(grid, &QTableWidget::cellClicked, this, [this, grid](int row, int /*col*/) {
            if (isPlaceholderRow(grid, row))
                activatePlaceholderRow(grid, row);
        });
        connect(grid, &QTableWidget::customContextMenuRequested, this, [this, grid](const QPoint &pos) {
            onGridContextMenu(grid, pos);
        });

        populateGrid(grid, &copy.table);

        // Wrap in container with centered per-tab type toggle row
        auto *wrapper = new QWidget(this);
        auto *wLayout = new QVBoxLayout(wrapper);
        wLayout->setContentsMargins(0, 2, 0, 0);
        wLayout->setSpacing(2);

        auto *typeRow = new QWidget(wrapper);
        typeRow->setObjectName(QStringLiteral("tableTypeRow"));
        auto *typeLayout = new QHBoxLayout(typeRow);
        typeLayout->setContentsMargins(4, 0, 4, 0);
        typeLayout->setSpacing(0);

        auto *origBtn = new QToolButton(typeRow);
        origBtn->setObjectName(QStringLiteral("origBtn"));
        origBtn->setText(tr("Original"));
        origBtn->setCheckable(true);
        origBtn->setChecked(copy.isOriginal);

        auto *transBtn = new QToolButton(typeRow);
        transBtn->setObjectName(QStringLiteral("transBtn"));
        transBtn->setText(tr("Translation"));
        transBtn->setCheckable(true);
        transBtn->setChecked(!copy.isOriginal);

        auto *bg = new QButtonGroup(wrapper);
        bg->setExclusive(true);
        bg->addButton(origBtn, 0);
        bg->addButton(transBtn, 1);

        connect(bg, &QButtonGroup::idClicked, this, [this, wrapper](int id) {
            const int tabIdx = m_tabs->indexOf(wrapper);
            if (tabIdx < 0 || tabIdx >= m_tables.size()) return;
            setTableOriginal(tabIdx, id == 0);
            emit tableContentChanged();
        });

        typeLayout->addStretch();
        typeLayout->addWidget(origBtn);
        typeLayout->addWidget(transBtn);
        typeLayout->addStretch();

        wLayout->addWidget(typeRow);
        wLayout->addWidget(grid);

        m_tables.append(copy);
        m_tabs->addTab(wrapper, copy.name);
    }

    m_ignoreChanges = false;

    if (activeIndex >= 0 && activeIndex < m_tabs->count())
        m_tabs->setCurrentIndex(activeIndex);

    updateButtonStates();
    emit tableContentChanged();
    emit activeTableChanged(currentTable());
}

void TablesDockWidget::setUseTableChecked(bool checked)
{
    if (m_useTableBtn)
        m_useTableBtn->setChecked(checked);
}

void TablesDockWidget::setUseTableEnabled(bool enabled)
{
    if (m_useTableBtn)
        m_useTableBtn->setEnabled(enabled);
}

QByteArray TablesDockWidget::saveColumnsState() const
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);

    // For each table tab, save the grid's header state
    stream << static_cast<int>(m_tables.size());
    for (int i = 0; i < m_tabs->count(); ++i) {
        QTableWidget *grid = gridAt(i);
        if (grid && grid->horizontalHeader()) {
            QByteArray headerState = grid->horizontalHeader()->saveState();
            stream << headerState;
        } else {
            stream << QByteArray();
        }
    }

    return state;
}

void TablesDockWidget::restoreColumnsState(const QByteArray &state)
{
    if (state.isEmpty())
        return;

    QDataStream stream(state);
    stream.setVersion(QDataStream::Qt_5_0);

    int tabCount = 0;
    stream >> tabCount;

    for (int i = 0; i < tabCount && i < m_tabs->count(); ++i) {
        QByteArray headerState;
        stream >> headerState;

        if (!headerState.isEmpty()) {
            QTableWidget *grid = gridAt(i);
            if (grid && grid->horizontalHeader()) {
                grid->horizontalHeader()->restoreState(headerState);
            }
        }
    }
}

bool TablesDockWidget::isTableOriginal(int index) const
{
    if (index < 0 || index >= m_tables.size())
        return false;
    return m_tables[index].isOriginal;
}

void TablesDockWidget::setTableOriginal(int index, bool original)
{
    if (index < 0 || index >= m_tables.size())
        return;

    m_tables[index].isOriginal = original;

    updateButtonStates();
}

void TablesDockWidget::onTabContextMenu(const QPoint &pos)
{
    const int tabIndex = m_tabs->tabBar()->tabAt(pos);
    if (tabIndex < 0 || tabIndex >= m_tables.size())
        return;

    QMenu menu(this);
    QAction *markOriginalAct = menu.addAction(tr("Original encoding table"));
    markOriginalAct->setCheckable(true);
    markOriginalAct->setChecked(m_tables[tabIndex].isOriginal);

    if (menu.exec(m_tabs->tabBar()->mapToGlobal(pos)) == markOriginalAct) {
        pushUndoSnapshot(tr("Set original table"));
        setTableOriginal(tabIndex, markOriginalAct->isChecked());
    }
}

void TablesDockWidget::onPaletteChanged()
{
    const QColor col = palette().color(QPalette::WindowText);
    if (m_useTableBtn)
        m_useTableBtn->setIcon(makeEyeIcon(col));
    if (m_addBtn)
        m_addBtn->setIcon(makeAddIcon(col));
    if (m_removeAct)
        m_removeAct->setIcon(makeRemoveIcon(col));
    if (m_copyToAct)
        m_copyToAct->setIcon(makeCopyToIcon(col));
    if (m_exportAct)
        m_exportAct->setIcon(makeExportIcon(col));
}
