#include "TablesDockWidget.h"
#include "translationtable.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QRegularExpressionValidator>
#include <QLineEdit>
#include <QTabBar>
#include <QSet>
#include <QApplication>
#include <QUndoCommand>
#include <algorithm>

// ---------------------------------------------------------------------------
// Undo command: stores before/after snapshots of all tables
// ---------------------------------------------------------------------------

static TableTab cloneTab(const TableTab &src)
{
    return src;
}

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
    : QDockWidget(tr("Tables"), parent)
{
    setObjectName(QStringLiteral("TablesDockWidget"));
    setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    // Custom title bar with collapse button
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("dockTitleBar"));
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(6, 2, 2, 2);
    titleLayout->setSpacing(2);
    m_titleLabel = new QLabel(tr("Tables"), titleBar);
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();
    m_collapseBtn = new QToolButton(titleBar);
    m_collapseBtn->setArrowType(Qt::DownArrow);
    m_collapseBtn->setAutoRaise(true);
    m_collapseBtn->setToolTip(tr("Collapse / Expand"));
    titleLayout->addWidget(m_collapseBtn);
    setTitleBarWidget(titleBar);

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

    m_addAct       = m_toolbar->addAction(QStringLiteral("+"),   this, [this]{ pushUndoSnapshot(tr("Add table")); addTable(); });
    m_duplicateAct = m_toolbar->addAction(QStringLiteral("Copy"), this, [this]{ pushUndoSnapshot(tr("Duplicate table")); duplicateCurrentTable(); });
    m_importAct    = m_toolbar->addAction(QStringLiteral("Import"), this, [this]{ importTable(); });
    m_exportAct    = m_toolbar->addAction(QStringLiteral("Export"), this, [this]{ exportCurrentTable(); });
    m_toolbar->addSeparator();
    m_removeAct    = m_toolbar->addAction(QStringLiteral("X"),  this, [this]{ removeCurrentTable(); });
    layout->addWidget(m_toolbar);

    // Tab widget
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(false);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::currentChanged, this, &TablesDockWidget::onTabChanged);
    // Double-click on a tab label → rename
    connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked,
            this, &TablesDockWidget::onTabDoubleClicked);
    layout->addWidget(m_tabs);

    container->setLayout(layout);
    setWidget(container);

    updateButtonStates();
    retranslateUi();

    connect(m_collapseBtn, &QToolButton::clicked, this, [this]() {
        const bool visible = m_contentWidget->isVisible();
        m_contentWidget->setVisible(!visible);
        m_collapseBtn->setArrowType(visible ? Qt::RightArrow : Qt::DownArrow);
    });
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
    TableTab tab;
    tab.name = name.isEmpty() ? defaultTabName(0) : name;
    if (table)
        tab.table = *table;

    // Create grid widget
    auto *grid = new QTableWidget(0, 2, this);
    grid->setHorizontalHeaderLabels(QStringList() << QStringLiteral("HEX") << tr("Value"));
    grid->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    grid->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    grid->setAlternatingRowColors(true);
    grid->setSelectionBehavior(QAbstractItemView::SelectRows);
    grid->verticalHeader()->setDefaultSectionSize(22);
    connect(grid, &QTableWidget::cellChanged, this, &TablesDockWidget::onCellChanged);

    m_ignoreChanges = true;
    populateGrid(grid, &tab.table);
    m_ignoreChanges = false;

    m_tables.append(tab);
    const int idx = m_tabs->addTab(grid, tab.name);
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
        if (auto *g = gridAt(0))
            g->setRowCount(0);
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

    pushUndoSnapshot(tr("Import table"));

    const TranslationTable importedTable(fileName);
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
    if (m_titleLabel)
        m_titleLabel->setText(tr("Tables"));
    if (m_collapseBtn)
        m_collapseBtn->setToolTip(tr("Collapse / Expand"));
    m_addAct->setToolTip(tr("Add empty table"));
    m_duplicateAct->setToolTip(tr("Duplicate current table"));
    m_importAct->setToolTip(tr("Import table from file"));
    m_exportAct->setToolTip(tr("Export table to file"));
    m_removeAct->setToolTip(tr("Remove current table"));
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
        m_tabs->setCurrentIndex(index);
        m_tabs->tabBar()->setFocus();
        return;
    }

    pushUndoSnapshot(tr("Rename table"));
    m_tables[index].name = newName.trimmed();
    m_tabs->setTabText(index, m_tables[index].name);
    emit tableContentChanged();
}

void TablesDockWidget::onCellChanged(int /*row*/, int /*col*/)
{
    if (m_ignoreChanges) return;

    const int idx = m_tabs->currentIndex();
    if (idx < 0 || idx >= m_tables.size()) return;

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
    grid->setRowCount(0);
    if (!table) return;

    auto *items = table->getItems();
    for (auto it = items->cbegin(); it != items->cend(); ++it) {
        const int row = grid->rowCount();
        grid->insertRow(row);
        auto *hexItem = new QTableWidgetItem(
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
        auto *hexItem = new QTableWidgetItem(hexKey);
        hexItem->setTextAlignment(Qt::AlignCenter);
        grid->setItem(row, 0, hexItem);
        grid->setItem(row, 1, new QTableWidgetItem(it.value()));
    }
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

void TablesDockWidget::updateButtonStates()
{
    const int idx = m_tabs->currentIndex();
    const bool hasTab = idx >= 0 && idx < m_tables.size();
    const bool hasEntries = hasTab && m_tables[idx].table.size() > 0;

    m_removeAct->setEnabled(hasTab);
    m_exportAct->setEnabled(hasEntries);
    m_duplicateAct->setEnabled(hasTab);
}

QTableWidget *TablesDockWidget::currentGrid() const
{
    return gridAt(m_tabs->currentIndex());
}

QTableWidget *TablesDockWidget::gridAt(int index) const
{
    if (index < 0 || index >= m_tabs->count()) return nullptr;
    return qobject_cast<QTableWidget *>(m_tabs->widget(index));
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
        grid->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        grid->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        grid->setAlternatingRowColors(true);
        grid->setSelectionBehavior(QAbstractItemView::SelectRows);
        grid->verticalHeader()->setDefaultSectionSize(22);
        connect(grid, &QTableWidget::cellChanged, this, &TablesDockWidget::onCellChanged);

        populateGrid(grid, &copy.table);
        m_tables.append(copy);
        m_tabs->addTab(grid, copy.name);
    }

    m_ignoreChanges = false;

    if (activeIndex >= 0 && activeIndex < m_tabs->count())
        m_tabs->setCurrentIndex(activeIndex);

    updateButtonStates();
    emit tableContentChanged();
    emit activeTableChanged(currentTable());
}
