#include "TablesDockWidget.h"
#include "translationtable.h"

#include <QPainter>
#include <QPainterPath>

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
#include <QButtonGroup>
#include <QMenu>
#include <QUndoCommand>
#include <QDataStream>
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
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    // Custom compact title bar
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("dockTitleBar"));
    titleBar->setFixedHeight(16);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(4, 0, 2, 0);
    titleLayout->setSpacing(1);
    m_titleLabel = new QLabel(tr("Tables"), titleBar);
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
    // Paint a simple 16x16 eye icon
    auto makeEyeIcon = [](bool /*unused*/) -> QIcon {
        auto paint = [](bool filled) -> QPixmap {
            const int sz = 16;
            QPixmap pm(sz, sz);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const QColor col(70, 70, 70);
            p.setPen(QPen(col, 1.3));
            // Lens (almond) outline
            QPainterPath lens;
            const float cx = sz * 0.5f, cy = sz * 0.5f;
            const float rx = sz * 0.44f, ry = sz * 0.27f;
            lens.moveTo(cx - rx, cy);
            lens.cubicTo(cx - rx * 0.4f, cy - ry * 2.0f, cx + rx * 0.4f, cy - ry * 2.0f, cx + rx, cy);
            lens.cubicTo(cx + rx * 0.4f, cy + ry * 2.0f, cx - rx * 0.4f, cy + ry * 2.0f, cx - rx, cy);
            p.drawPath(lens);
            // Pupil
            const float pr = ry * 0.72f;
            if (filled) {
                p.setBrush(col);
                p.setPen(Qt::NoPen);
            }
            p.drawEllipse(QPointF(cx, cy), pr, pr);
            return pm;
        };
        QIcon icon;
        icon.addPixmap(paint(true),  QIcon::Normal,  QIcon::On);
        icon.addPixmap(paint(false), QIcon::Normal,  QIcon::Off);
        icon.addPixmap(paint(false), QIcon::Disabled, QIcon::Off);
        return icon;
    };
    m_useTableBtn->setIcon(makeEyeIcon(false));
    m_useTableBtn->setIconSize(QSize(16, 16));
    m_toolbar->insertWidget(m_toolbar->actions().isEmpty() ? nullptr : m_toolbar->actions().first(),
                            m_useTableBtn);
    connect(m_useTableBtn, &QToolButton::clicked, this, [this](bool checked) {
        emit useTableToggled(checked);
    });

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
    // Right-click context menu on tab bar
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QWidget::customContextMenuRequested,
            this, &TablesDockWidget::onTabContextMenu);
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
    // First table added to an empty dock becomes the original encoding table
    tab.isOriginal = m_tables.isEmpty();

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
            m_tabs->tabBar()->setFocus(Qt::OtherFocusReason);
        }, Qt::QueuedConnection);
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

    if (original) {
        // Mark exactly one table as original.
        for (int i = 0; i < m_tables.size(); ++i)
            m_tables[i].isOriginal = (i == index);
    } else {
        // Allow clearing the flag for this table without touching others.
        m_tables[index].isOriginal = false;
    }

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
