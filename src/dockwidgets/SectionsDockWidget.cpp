#include "SectionsDockWidget.h"
#include "SectionListModel.h"
#include "DockTitleBar.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QColorDialog>
#include <QMenu>
#include <QPainter>
#include <QDropEvent>
#include <QSignalBlocker>
#include <functional>

// ---------------------------------------------------------------------------
// Helper: depth of a QTreeWidgetItem (1 = direct child of the invisible root)
// ---------------------------------------------------------------------------
static int treeItemDepth(const QTreeWidgetItem *item)
{
    int d = 0;
    for (const QTreeWidgetItem *p = item; p; p = p->parent())
        ++d;
    return d;
}

// ---------------------------------------------------------------------------
// Internal drag-aware tree.  No Q_OBJECT — uses dropCallback instead.
// ---------------------------------------------------------------------------
class SectionTree : public QTreeWidget
{
public:
    explicit SectionTree(QWidget *parent = nullptr) : QTreeWidget(parent) {}

    std::function<void()> dropCallback;

protected:
    void dropEvent(QDropEvent *event) override
    {
        QTreeWidgetItem *dragItem = currentItem();

        // Never drag the ROM-root node (UserRole+1 == -1)
        if (!dragItem || dragItem->data(0, Qt::UserRole + 1).toInt() < 0) {
            event->ignore();
            return;
        }

        QTreeWidget::dropEvent(event);
        if (dropCallback)
            dropCallback();
    }
};

SectionsDockWidget::SectionsDockWidget(QWidget *parent)
    : BaseDockWidget(tr("Sections"), parent)
{
    setWindowTitle(tr("Sections"));
    setObjectName(QStringLiteral("SectionsDockWidget"));

    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Toolbar
    auto *toolRow = new QHBoxLayout();
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(4);

    m_showSectionsBtn = new QToolButton(this);
    m_showSectionsBtn->setCheckable(true);
    m_showSectionsBtn->setChecked(true);
    m_showSectionsBtn->setAutoRaise(true);
    m_showSectionsBtn->setToolTip(tr("Show section colors"));
    m_showSectionsBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
    m_showSectionsBtn->setIconSize(QSize(16, 16));
    toolRow->addWidget(m_showSectionsBtn);

    m_addBtn = new QToolButton(this);
    m_addBtn->setAutoRaise(true);
    m_addBtn->setToolTip(tr("Add section"));
    m_addBtn->setIcon(makeAddIcon(palette().color(QPalette::WindowText)));
    m_addBtn->setIconSize(QSize(16, 16));
    toolRow->addWidget(m_addBtn);

    toolRow->addStretch();
    layout->addLayout(toolRow);

    // Tree widget
    m_tree = new SectionTree(this);
    m_tree->setHeaderLabels({tr("Name"), tr("Offset")});
    m_tree->setColumnCount(2);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDropIndicatorShown(true);
    m_tree->dropCallback = [this]() { onDropped(); };
    layout->addWidget(m_tree);

    setWidget(container);
    initTitleBar();

    connect(m_showSectionsBtn, &QToolButton::toggled,
            this, &SectionsDockWidget::showSectionsToggled);
    // When the add button is clicked, let mainwindow determine the parent by
    // offset — just emit -1 so mainwindow auto-detects via cursor position.
    connect(m_addBtn, &QToolButton::clicked, this, [this]() {
        emit addSectionRequested(-1);
    });
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &SectionsDockWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &SectionsDockWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &SectionsDockWidget::onTreeContextMenu);
}

void SectionsDockWidget::setModel(SectionListModel *model)
{
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);

    m_model = model;

    if (m_model)
        connect(m_model, &SectionListModel::sectionsChanged,
                this, &SectionsDockWidget::rebuildTree);

    rebuildTree();
}

void SectionsDockWidget::setRomTypeName(const QString &name)
{
    if (m_romTypeName != name) {
        m_romTypeName = name;
        rebuildTree();
    }
}

void SectionsDockWidget::setTableNames(const QStringList &names)
{
    m_tableNames = names;
}

void SectionsDockWidget::refresh()
{
    rebuildTree();
}

void SectionsDockWidget::setShowSectionsChecked(bool checked)
{
    const QSignalBlocker blocker(m_showSectionsBtn);
    m_showSectionsBtn->setChecked(checked);
}

void SectionsDockWidget::retranslateUi()
{
    setWindowTitle(tr("Sections"));
    m_showSectionsBtn->setToolTip(tr("Show section colors"));
    m_addBtn->setToolTip(tr("Add section"));
    m_tree->setHeaderLabels({tr("Name"), tr("Offset")});
}

void SectionsDockWidget::onPaletteChanged()
{
    const QColor fg = palette().color(QPalette::WindowText);
    m_showSectionsBtn->setIcon(makeEyeIcon(fg));
    m_addBtn->setIcon(makeAddIcon(fg));
    rebuildTree();
}

void SectionsDockWidget::onItemClicked(QTreeWidgetItem *item, int /*column*/)
{
    bool ok = false;
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong(&ok);
    if (ok && offset >= 0)
        emit jumpToOffset(offset);
}

void SectionsDockWidget::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (!item || !m_model)
        return;

    const int sectionIdx = item->data(0, Qt::UserRole + 1).toInt();
    if (sectionIdx < 0 || sectionIdx >= m_model->count())
        return;

    const Section &s = m_model->at(sectionIdx);
    if (s.endOffset > s.startOffset)
        emit selectRangeRequested(s.startOffset, s.endOffset);
}

void SectionsDockWidget::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item || !m_model)
        return;

    const int sectionIdx = item->data(0, Qt::UserRole + 1).toInt();
    if (sectionIdx < 0 || sectionIdx >= m_model->count())
        return;

    const int depth = treeItemDepth(item);  // 2 = root section, 3+ = subsection

    const Section &section = m_model->at(sectionIdx);

    QMenu menu(this);
    QAction *renameAct  = menu.addAction(tr("Rename"));
    QAction *recolorAct = menu.addAction(tr("Change color"));
    // Any section can have subsections (unlimited nesting)
    QAction *addSubAct  = menu.addAction(tr("Add subsection"));
    QAction *vfFormatAct = menu.addAction(tr("Virtually format") + "...");
    QAction *vfRemoveAct = menu.addAction(tr("Remove virtual formatting"));
    menu.addSeparator();

    // ── Display mode submenu ──
    QMenu *displayMenu = menu.addMenu(tr("Display mode"));
    const int curMode = section.displayMode;

    QAction *actDefault = displayMenu->addAction(tr("Default"));
    actDefault->setCheckable(true);
    actDefault->setChecked(curMode == SectionDisplay_Default);
    actDefault->setData(SectionDisplay_Default);

    QAction *actRaw = displayMenu->addAction(tr("Raw"));
    actRaw->setCheckable(true);
    actRaw->setChecked(curMode == SectionDisplay_Raw);
    actRaw->setData(SectionDisplay_Raw);

    if (!m_tableNames.isEmpty()) {
        displayMenu->addSeparator();
        for (int ti = 0; ti < m_tableNames.size(); ++ti) {
            const int tableMode = ti + 1; // 1-based
            QAction *actTbl = displayMenu->addAction(m_tableNames[ti]);
            actTbl->setCheckable(true);
            actTbl->setChecked(curMode == tableMode);
            actTbl->setData(tableMode);
        }
    }

    displayMenu->addSeparator();
    QAction *actDisasm = displayMenu->addAction(tr("Disassembly"));
    actDisasm->setCheckable(true);
    actDisasm->setChecked(curMode == SectionDisplay_Disasm);
    actDisasm->setData(SectionDisplay_Disasm);

    menu.addSeparator();
    QAction *deleteAct  = menu.addAction(tr("Delete"));
    Q_UNUSED(depth)

    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == renameAct) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Rename section"),
            tr("Name") + ':', QLineEdit::Normal,
            m_model->at(sectionIdx).name, &ok);
        if (ok && !name.isEmpty())
            m_model->renameSection(sectionIdx, name);
    } else if (chosen == recolorAct) {
        const QColor color = QColorDialog::getColor(
            m_model->at(sectionIdx).color, this, tr("Section color"));
        if (color.isValid())
            m_model->recolorSection(sectionIdx, color);
    } else if (addSubAct && chosen == addSubAct) {
        emit addSectionRequested(sectionIdx);
    } else if (chosen == vfFormatAct) {
        emit virtualFormattingRequested(section.startOffset, section.endOffset);
    } else if (chosen == vfRemoveAct) {
        emit removeVirtualFormattingRequested(section.startOffset, section.endOffset);
    } else if (chosen == deleteAct) {
        m_model->removeSection(sectionIdx);
    } else if (displayMenu->actions().contains(chosen)) {
        // Display mode action from the submenu
        const int newMode = chosen->data().toInt();
        Section s = m_model->at(sectionIdx);
        if (s.displayMode != newMode) {
            s.displayMode = newMode;
            m_model->updateSection(sectionIdx, s);
        }
    }
}

void SectionsDockWidget::rebuildTree()
{
    if (m_suppressRebuild)
        return;

    m_tree->clear();

    const QString rootLabel = m_romTypeName.isEmpty()
                                  ? QStringLiteral("ROM")
                                  : QStringLiteral("ROM (%1)").arg(m_romTypeName);

    auto *romRoot = new QTreeWidgetItem(m_tree, {rootLabel});
    romRoot->setData(0, Qt::UserRole,     qint64(-1));
    romRoot->setData(0, Qt::UserRole + 1, -1);
    romRoot->setFlags(romRoot->flags() & ~Qt::ItemIsDragEnabled);

    if (m_model) {
        // Build a flat index → item map; parents always have lower flat indices
        QVector<QTreeWidgetItem *> itemByIdx(m_model->count(), nullptr);

        for (int i = 0; i < m_model->count(); ++i) {
            const Section &s = m_model->at(i);
            QTreeWidgetItem *parentItem =
                (s.parentIndex >= 0 && s.parentIndex < i)
                    ? itemByIdx[s.parentIndex]
                    : romRoot;
            if (!parentItem) parentItem = romRoot;

            const QString offsetStr = QStringLiteral("0x%1")
                .arg(s.startOffset, 0, 16, QLatin1Char('0'));
            auto *child = new QTreeWidgetItem(parentItem, {s.name, offsetStr});
            child->setData(0, Qt::UserRole,     s.startOffset);
            child->setData(0, Qt::UserRole + 1, i);
            child->setIcon(0, colorSwatchIcon(s.color));

            // All section items are draggable and can accept drops (unlimited nesting)
            child->setFlags(child->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);

            itemByIdx[i] = child;
        }
    }

    m_tree->expandAll();
    m_tree->resizeColumnToContents(0);
}

QIcon SectionsDockWidget::colorSwatchIcon(const QColor &color) const
{
    QPixmap pix(16, 16);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(2, 2, 12, 12, 2, 2);
    return QIcon(pix);
}

void SectionsDockWidget::onDropped()
{
    syncModelFromTree();
}

// Reads the tree hierarchy after a drag-drop move, rebuilds the flat Section
// array with correct parentIndex values, and updates the model.
void SectionsDockWidget::syncModelFromTree()
{
    if (!m_model) return;

    QTreeWidgetItem *romRoot = m_tree->topLevelItem(0);
    if (!romRoot) return;

    QVector<Section> newSections;
    int flatIdx = 0;

    // DFS pre-order: each parent is appended before its children so that
    // parentIndex always refers to a lower flat index.
    std::function<void(QTreeWidgetItem *, int)> processItem =
        [&](QTreeWidgetItem *item, int parentFlatIdx) {
            const int oldIdx = item->data(0, Qt::UserRole + 1).toInt();
            if (oldIdx < 0 || oldIdx >= m_model->count())
                return;  // skip the ROM root pseudo-item

            Section s = m_model->at(oldIdx);
            s.parentIndex = parentFlatIdx;
            newSections.append(s);
            const int myIdx = flatIdx++;

            for (int j = 0; j < item->childCount(); ++j)
                processItem(item->child(j), myIdx);
        };

    for (int i = 0; i < romRoot->childCount(); ++i)
        processItem(romRoot->child(i), -1);

    // If a subsection is promoted to a higher level, sibling ranges on that new
    // level must not overlap. Clamp each earlier sibling to the next sibling's
    // start offset.
    for (int i = 0; i < newSections.size(); ++i) {
        for (int j = 0; j < newSections.size(); ++j) {
            if (i == j || newSections[i].parentIndex != newSections[j].parentIndex)
                continue;
            if (newSections[i].startOffset < newSections[j].startOffset
                && newSections[i].endOffset > newSections[j].startOffset) {
                newSections[i].endOffset = qMax(newSections[i].startOffset,
                                                newSections[j].startOffset);
            }
        }
    }

    // Update model: suppress the rebuildTree triggered by sectionsChanged so
    // that we rebuild once explicitly below.
    m_suppressRebuild = true;
    m_model->applySections(newSections, tr("Move section"));
    m_suppressRebuild = false;
    rebuildTree();
}

// ---------------------------------------------------------------------------
// Auto-highlight the deepest section that contains 'offset'.
// Uses programmatic setCurrentItem so itemClicked is NOT emitted (no jump).
// ---------------------------------------------------------------------------
void SectionsDockWidget::highlightOffset(qint64 offset)
{
    if (!m_model) return;
    QTreeWidgetItem *romRoot = m_tree->topLevelItem(0);
    if (!romRoot) return;

    // Find the deepest (most nested) section containing the offset.
    auto sectionDepth = [&](int idx) -> int {
        int d = 0;
        int pi = m_model->at(idx).parentIndex;
        while (pi >= 0 && pi < m_model->count()) {
            ++d;
            pi = m_model->at(pi).parentIndex;
        }
        return d;
    };

    int bestIdx = -1;
    int bestDepth = -1;
    for (int i = 0; i < m_model->count(); ++i) {
        const Section &s = m_model->at(i);
        if (offset >= s.startOffset && offset < s.endOffset) {
            const int d = sectionDepth(i);
            if (d > bestDepth) {
                bestDepth = d;
                bestIdx = i;
            }
        }
    }

    if (bestIdx < 0) {
        // No section at this offset — deselect silently
        m_tree->blockSignals(true);
        m_tree->clearSelection();
        m_tree->setCurrentItem(nullptr);
        m_tree->blockSignals(false);
        return;
    }

    // Walk the tree to find the item with matching section index
    std::function<QTreeWidgetItem *(QTreeWidgetItem *)> findItem =
        [&](QTreeWidgetItem *parent) -> QTreeWidgetItem * {
            for (int i = 0; i < parent->childCount(); ++i) {
                auto *ch = parent->child(i);
                if (ch->data(0, Qt::UserRole + 1).toInt() == bestIdx)
                    return ch;
                if (auto *found = findItem(ch))
                    return found;
            }
            return nullptr;
        };

    QTreeWidgetItem *target = findItem(romRoot);
    if (target && target != m_tree->currentItem()) {
        m_tree->blockSignals(true);
        m_tree->setCurrentItem(target);
        m_tree->blockSignals(false);
    }
}
