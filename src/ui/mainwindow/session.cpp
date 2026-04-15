// Auto-extracted from mainwindow.cpp
#include "mainwindow.h"
#include "internal.h"
#include <QPainter>
#include <QFontMetrics>
#include <QStatusBar>
using namespace MainWindowInternal;
#include <QTabWidget>
#include <QTabBar>
#include <QScrollBar>
#include <QMessageBox>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QApplication>
#include "SectionListModel.h"

void MainWindow::saveCurrentSession()
{
    if (!m_currentSession)
        return;

    m_currentSession->editor = hexEdit;
    m_currentSession->document = m_document;
    m_currentSession->curFile = curFile;
    m_currentSession->tableFilePath = tableFilePath;
    m_currentSession->isUntitled = isUntitled;
    m_currentSession->curOffset = (hexEdit ? hexEdit->getCurrentOffset() : curOffset);
    m_currentSession->table = tb;
    m_currentSession->changeTrackingSnapshot = m_changeTrackingSnapshot;
    m_currentSession->detectedRomType = m_detectedRomType;
    m_currentSession->pointerOffset = m_pointerOffset;
    m_currentSession->pointerSize = m_pointerSize;
    m_currentSession->currentEncoding = m_currentEncoding;
    m_currentSession->showPointers = (showPointersAct && showPointersAct->isChecked());
    m_currentSession->showChanges = (showChangesAct && showChangesAct->isChecked());
    m_currentSession->changesHexMode = (m_changesDock && m_changesDock->hexMode());
    m_currentSession->navigationHistory = navigationHistory;
    m_currentSession->navigationHistoryIndex = navigationHistoryIndex;
    m_currentSession->navigationJumpInProgress = navigationJumpInProgress;
    m_currentSession->tableSnapshot = m_tablesDock->takeSnapshot();
    m_currentSession->tableActiveIndex = m_tablesDock->currentIndex();
    m_currentSession->liveTableState = m_tablesDock->detachTabs();
    m_currentSession->dockTablesVisible = m_tablesDock && m_tablesDock->isVisible();
    m_currentSession->dockPointersVisible = m_pointersDock && m_pointersDock->isVisible();
    m_currentSession->dockChangesVisible = m_changesDock && m_changesDock->isVisible();
    m_currentSession->dockSectionsVisible = m_sectionsDock && m_sectionsDock->isVisible();
    m_currentSession->dockVisibilityInitialized = true;
    if (m_document)
        m_document->useTable = (useTableAct && useTableAct->isChecked());

    // Snapshot sections for this tab
    if (m_document && m_sectionModel) {
        m_document->snapshotSections(m_sectionModel);
        m_document->showSections = hexEdit->showSections();
    }

    // Save per-tab dock collapse/size state from live dock widgets
    if (m_tablesDock) {
        m_currentSession->dockTablesCollapsed      = m_tablesDock->isCollapsed();
        m_currentSession->dockTablesExpandedWidth  = m_tablesDock->expandedWidth();
        m_currentSession->dockTablesExpandedHeight = m_tablesDock->expandedHeight();
    }
    if (m_pointersDock) {
        m_currentSession->dockPointersCollapsed      = m_pointersDock->isCollapsed();
        m_currentSession->dockPointersExpandedWidth  = m_pointersDock->expandedWidth();
        m_currentSession->dockPointersExpandedHeight = m_pointersDock->expandedHeight();
    }
    if (m_changesDock) {
        m_currentSession->dockChangesCollapsed      = m_changesDock->isCollapsed();
        m_currentSession->dockChangesExpandedWidth  = m_changesDock->expandedWidth();
        m_currentSession->dockChangesExpandedHeight = m_changesDock->expandedHeight();
    }
    if (m_sectionsDock) {
        m_currentSession->dockSectionsCollapsed      = m_sectionsDock->isCollapsed();
        m_currentSession->dockSectionsExpandedWidth  = m_sectionsDock->expandedWidth();
        m_currentSession->dockSectionsExpandedHeight = m_sectionsDock->expandedHeight();
    }
    m_currentSession->dockStateInitialized = true;

    // Save Find/Replace dialog state for this tab
    if (searchDialog) {
        const SearchDialog::State s = searchDialog->dialogState();
        m_currentSession->searchFindText    = s.findText;
        m_currentSession->searchFindFormat  = s.findFormat;
        m_currentSession->searchReplaceText = s.replaceText;
        m_currentSession->searchReplaceFormat = s.replaceFormat;
        m_currentSession->searchRelative    = s.relative;
    }

    // Save Find Pointers dialog state for this tab
    if (pointersDialog) {
        const PointersDialog::State ps = pointersDialog->dialogState();
        m_currentSession->ptrSearchDir        = ps.searchDir;
        m_currentSession->ptrExcludeSelection = ps.excludeSelection;
        m_currentSession->ptrAlignedOnly      = ps.alignedOnly;
        m_currentSession->ptrOptimize         = ps.optimize;
    }
}

void MainWindow::restoreSession(EditorSession *session)
{
    m_restoringProjectUi = true;

    m_currentSession = session;
    hexEdit = session->editor;
    m_document = session->document;
    curFile = session->curFile;
    tableFilePath = session->tableFilePath;
    isUntitled = session->isUntitled;
    curOffset = session->curOffset;
    m_changeTrackingSnapshot = session->changeTrackingSnapshot;
    m_detectedRomType = session->detectedRomType;
    m_pointerOffset = session->pointerOffset;
    m_pointerSize = session->pointerSize;
    m_currentEncoding = session->currentEncoding;
    navigationHistory = session->navigationHistory;
    navigationHistoryIndex = session->navigationHistoryIndex;
    navigationJumpInProgress = session->navigationJumpInProgress;

    m_pointersDock->setHexEdit(hexEdit);

    hexEdit->setDisasmRomType(m_detectedRomType);

    // Restore sections for this tab
    if (m_document && m_sectionModel)
    {
        // Suppress automatic rebuildTree during section restore.
        if (m_sectionsDock)
            m_sectionsDock->setSuppressRebuild(true);

        // Block sectionsChanged signal during data swap to prevent
        // the OLD editor from doing a pointless rebuildSectionAwareLayout
        // with the new tab's section data.
        {
            const QSignalBlocker blocker(m_sectionModel);
            m_document->restoreSections(m_sectionModel);
        }

        // Now connect the new editor.  setSectionModel disconnects old,
        // connects new, and does a SINGLE rebuildSectionAwareLayout on the
        // correct editor with the correct section data.
        m_sectionModel->setUndoStack(hexEdit ? hexEdit->undoStack() : nullptr);
        hexEdit->setSectionModel(m_sectionModel);
        hexEdit->setShowSections(m_document->showSections);

        if (m_sectionsDock)
        {
            m_sectionsDock->setShowSectionsChecked(m_document->showSections);
            // Suppress all intermediate rebuilds, do exactly one at the end.
            m_sectionsDock->setSuppressRebuild(true);
            m_sectionsDock->setRomTypeName(QString::fromLatin1(romTypeName(m_detectedRomType)));
            m_sectionsDock->setCurrentRomType(m_detectedRomType);
            m_sectionsDock->setSuppressRebuild(false);
            m_sectionsDock->refresh();
        }
    }

    // Restore the per-session table dock content. applySnapshot rebuilds the
    // dock's tab list and emits activeTableChanged/tableContentChanged.
    // Suppress "project modified" side effects while this is a pure restore.

    m_restoringTableDockState = true;

    // Block signals from the tables dock during restore so that
    // applySnapshot/attachTabs don't trigger expensive handlers
    // (onDockTableChanged, onDockTableContentChanged, syncTableNames)
    // that call setTranslationTable, refreshView, refreshChangesView etc.
    m_tablesDock->blockSignals(true);

    if (!session->liveTableState.wrappers.isEmpty()) {
        // Fast path — reattach previously detached live widgets (no
        // widget destruction / creation, no deep copy, no delegate setup).
        m_tablesDock->attachTabs(std::move(session->liveTableState));
    } else {
        // Slow path — first restore from a project file or brand-new session.
        // Null out translation table pointers on ALL editors before
        // applySnapshot clears m_tables to avoid dangling _tb pointers.
        for (EditorSession *s : m_sessions) {
            if (s->editor)
                s->editor->clearTranslationTableQuiet();
        }
        m_tablesDock->applySnapshot(session->tableSnapshot, session->tableActiveIndex);
    }

    m_tablesDock->blockSignals(false);

    m_restoringTableDockState = false;
    tb = m_tablesDock->currentTable();
    session->table = tb;  // keep in sync after pointer recreation
    const bool hasTables = m_tablesDock->count() > 0;
    
    // Restore per-tab dock collapse/size state, or fall back to uncollapsed.
    if (session->dockStateInitialized) {
        auto applyDockState = [](BaseDockWidget *d, bool collapsed, int expW, int expH) {
            if (!d) return;
            if (expW > 0 || expH > 0)
                d->setExpandedSize(expW, expH);
            if (collapsed != d->isCollapsed())
                d->setCollapsed(collapsed);
        };
        applyDockState(m_tablesDock,
                       session->dockTablesCollapsed,
                       session->dockTablesExpandedWidth,
                       session->dockTablesExpandedHeight);
        applyDockState(m_pointersDock,
                       session->dockPointersCollapsed,
                       session->dockPointersExpandedWidth,
                       session->dockPointersExpandedHeight);
        applyDockState(m_changesDock,
                       session->dockChangesCollapsed,
                       session->dockChangesExpandedWidth,
                       session->dockChangesExpandedHeight);
        applyDockState(m_sectionsDock,
                       session->dockSectionsCollapsed,
                       session->dockSectionsExpandedWidth,
                       session->dockSectionsExpandedHeight);
    } else {
        m_tablesDock->setCollapsed(false);
        m_pointersDock->setCollapsed(false);
        m_changesDock->setCollapsed(false);
        m_sectionsDock->setCollapsed(false);
    }

    if (session->dockVisibilityInitialized) {
        m_tablesDock->setVisible(session->dockTablesVisible);
        m_pointersDock->setVisible(session->dockPointersVisible);
        m_changesDock->setVisible(session->dockChangesVisible);
        m_sectionsDock->setVisible(session->dockSectionsVisible);
    } else {
        m_tablesDock->show();
        m_pointersDock->show();
        m_changesDock->show();
        m_sectionsDock->show();
    }

    useTableAct->setEnabled(hasTables);
    editTableAct->setEnabled(hasTables);
    saveTableAct->setEnabled(hasTables);
    saveTableAsAct->setEnabled(hasTables);
    useTableAct->setChecked(hasTables && m_document && m_document->useTable);

    if (m_pointersDock && m_pointersDock->isVisible()) {
        m_pointersDock->setSuppressResize(true);
        m_pointersDock->refreshView();
        m_pointersDock->setSuppressResize(false);
    }

    m_restoringProjectUi = true;

    // Sync show-pointers action and dock button with the restored editor's state
    if (showPointersAct) {
        const bool hasPointers = hexEdit && !hexEdit->pointers()->empty();
        const bool showPointers = session ? session->showPointers : true;
        showPointersAct->setEnabled(hasPointers);
        showPointersAct->setChecked(showPointers);
        if (hexEdit)
            hexEdit->setShowPointers(showPointers);
        // QAction::toggled connection auto-updates m_pointersDock button
    }

    // Sync changes-dock supplementary buttons with restored session
    if (m_changesDock) {
        const bool showChanges = session ? session->showChanges : false;
        const bool changesHexMode = session ? session->changesHexMode : false;
        if (showChangesAct)
            showChangesAct->setChecked(showChanges);
        m_changesDock->setShowChangesChecked(showChangesAct && showChangesAct->isChecked());
        m_changesDock->setHexMode(changesHexMode);
        m_changesDock->setShowOriginalChecked(hexEdit && hexEdit->showOriginal());
    }

    // Sync UI with restored session
    setWindowModified(hexEdit ? hexEdit->isModified() : false);
    updateWindowTitle();
    updateActionStates();
    updateNavigationActions();

    {
        const QSignalBlocker blocker(cbRomType);
        cbRomType->setCurrentIndex(static_cast<int>(m_detectedRomType));
        syncRomTypeMenu(static_cast<int>(m_detectedRomType));
    }
    updateEndiannesLabel();

    if (lbEncoding)
        lbEncoding->setText(m_currentEncoding);
    syncEncodingMenu();

    applySelectedTable();

    toggleShowChanges();

    m_restoringProjectUi = false;

    if (m_changesDock && m_changesDock->isVisible() && showChangesAct && showChangesAct->isChecked())
        refreshChangesView();

    // Sync status bar with restored session
    if (hexEdit) {
        setAddress(hexEdit->getCurrentOffset());
        setSize(hexEdit->dataSize());
        setSelection(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
        setOverwriteMode(hexEdit->overwriteMode());
        updateValuePanels();

        // After a session restore from settings the editor viewport has no size
        // yet (window not shown). Applying the scroll right now would be a no-op.
        // Leave scrollPending = true; showEvent will apply it after first show.
        // For user-triggered tab switches the viewport already has a real size,
        // so apply immediately.
        if (session->scrollPending) {
            if (session->curOffset > 0) {
                hexEdit->setCursorPosition(session->curOffset * 2);
                if (hexEdit->viewport()->height() > 0) {
                    session->scrollPending = false;
                    hexEdit->ensureVisibleCentered();
                }
                // else: leave scrollPending = true for showEvent
            } else {
                session->scrollPending = false;
            }
        }
    }

    updateDockAreaActions();
}

void MainWindow::updateTabTitle(int index)
{
    if (index < 0 || index >= m_sessions.size())
        return;
    const EditorSession *s = m_sessions[index];
    QString title;
    if (s->document && !s->document->projectName.isEmpty())
        title = s->document->projectName;
    else if (s->curFile.isEmpty())
        title = tr("New file");
    else
        title = QFileInfo(s->curFile).fileName();
    if ((s->editor && s->editor->isModified()) || (s->document && s->document->isDirty()))
        title += QStringLiteral(" *");
    m_tabWidget->setTabText(index, title);
    m_tabWidget->setTabToolTip(index, s->curFile);
}

void MainWindow::onTabChanged(int index)
{
    if (index < 0 || index >= m_sessions.size())
        return;

    if (m_currentSession && m_currentSession->editor)
        disconnectEditorSignals(m_currentSession->editor);

    saveCurrentSession();
    restoreSession(m_sessions[index]);

    connectEditorSignals(hexEdit);
}

void MainWindow::onTabCloseRequested(int index)
{
    if (index < 0 || index >= m_sessions.size())
        return;

    // Switch to the tab being closed so maybeSave works on it
    if (m_tabWidget->currentIndex() != index)
        m_tabWidget->setCurrentIndex(index);

    const bool wasFileModified = isWindowModified();
    if (!maybeSave())
        return;
    // Only prompt about the project when the file had no unsaved hex edits.
    // If the user saved, save() already saved the project too.  If the user
    // discarded file changes, honour their intent to close without saving.
    if (!wasFileModified) {
        if (!maybeSaveProject())
            return;
    }

    EditorSession *session = m_sessions[index];
    disconnectEditorSignals(session->editor);

    // Release the QFile reference held by Chunks before destroying the session.
    // EditorSession::file is used as Chunks::_ioDevice; deleting the session
    // would free the QFile while Chunks still holds a pointer to it, causing
    // a use-after-free crash (chunks.cpp line 72: _ioDevice->open(...)).
    if (session->editor)
        session->editor->setData(QByteArray());

    m_sessions.remove(index);
    m_tabWidget->removeTab(index);
    delete session;

    if (m_sessions.isEmpty()) {
        // All tabs closed — clear working state
        m_currentSession = nullptr;
        hexEdit = nullptr;
        m_document = nullptr;
        m_changesUiUpdateTimer->stop();

        // Clear dock widgets: their content belongs to the closed project
        m_tablesDock->clearAll();
        m_tablesDock->show();
        m_tablesDock->setCollapsed(false);
        m_changesDock->clear();
        m_changesDock->hide();
        m_pointersDock->setHexEdit(nullptr);
        m_pointersDock->hide();

        // Keep m_tabWidget visible so dock widgets don't expand to fill its area.
        // An empty QTabWidget shows just a blank area — acceptable as a placeholder.

        updateWindowTitle();
        updateActionStates();
        updateDockAreaActions();
    }
    // else: removeTab triggered onTabChanged → restoreSession handles the rest
}

void MainWindow::newTab()
{
    // createSession() → setCurrentIndex() → onTabChanged() handles save/restore
    createSession();
    setCurrentFile(QString());
}

void MainWindow::closeTab()
{
    onTabCloseRequested(m_tabWidget->currentIndex());
}

void MainWindow::nextTab()
{
    int next = m_tabWidget->currentIndex() + 1;
    if (next >= m_tabWidget->count())
        next = 0;
    m_tabWidget->setCurrentIndex(next);
}

void MainWindow::previousTab()
{
    int prev = m_tabWidget->currentIndex() - 1;
    if (prev < 0)
        prev = m_tabWidget->count() - 1;
    m_tabWidget->setCurrentIndex(prev);
}

void MainWindow::loadFileInNewTab(const QString &fileName)
{
    createSession();  // onTabChanged handles save/restore
    loadFile(fileName);
}

void MainWindow::loadFileInNewTab(const QString &fileName, RomType suggestedRomType)
{
    createSession();  // onTabChanged handles save/restore
    loadFile(fileName, suggestedRomType);
}

// ---------- End session / tab management ----------

// ---------------------------------------------------------------------------
// Tab-picker overlay — a frameless top-level translucent window that dims the
// whole main window except for the tab bar, letting the user click a tab to
// select the copy destination.
//
// On macOS (and generally with WA_TranslucentBackground) transparent pixels of
// a top-level window are click-through at the OS level, so mouse events in the
// hole go directly to the real tab bar widget. We install an event filter on
// that tab bar to intercept the click BEFORE it switches the tab, then invoke
// our callback which explicitly switches to the target tab and copies the table.
// ---------------------------------------------------------------------------
namespace {

class TabPickerOverlay : public QWidget
{
public:
    TabPickerOverlay(QWidget *mainWindow,
                     QTabWidget *tabs,
                     const QString &instructionText,
                     std::function<void(int)> onTabSelected,
                     std::function<void()>    onCanceled)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
        , m_tabs(tabs)
        , m_text(instructionText)
        , m_onTabSelected(std::move(onTabSelected))
        , m_onCanceled(std::move(onCanceled))
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setGeometry(mainWindow->geometry());
        // Intercept clicks that pass through the transparent hole to the real tab bar
        if (m_tabs && m_tabs->tabBar())
            m_tabs->tabBar()->installEventFilter(this);
        show();
    }

    ~TabPickerOverlay() override
    {
        if (m_tabs && m_tabs->tabBar())
            m_tabs->tabBar()->removeEventFilter(this);
    }

protected:
    // Tab bar rect expressed in this overlay's local coordinates
    QRect tabBarRectInOverlay() const
    {
        if (!m_tabs || !m_tabs->tabBar()) return {};
        QTabBar *tb = m_tabs->tabBar();
        return QRect(mapFromGlobal(tb->mapToGlobal(QPoint(0, 0))), tb->size());
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Dim the whole overlay
        p.fillRect(rect(), QColor(0, 0, 0, 160));

        const QRect tbr = tabBarRectInOverlay();
        if (tbr.isEmpty()) return;

        // Punch a transparent hole so the real tab bar shows through
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(tbr, Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        // Blue highlight border around the tab bar
        p.setPen(QPen(QColor(80, 160, 255, 230), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(tbr.adjusted(-2, -2, 2, 2));

        // Instruction label drawn below the tab bar
        QFont f = font();
        f.setPointSize(11);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(m_text);
        const int th = fm.height();
        const int px = qBound(4, tbr.left(), rect().right() - tw - 28);
        const int py = qMin(tbr.bottom() + 12, rect().bottom() - th - 16);
        const QRect bg(px, py, tw + 24, th + 12);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 30, 30, 220));
        p.drawRoundedRect(bg, 6, 6);
        p.setPen(Qt::white);
        p.drawText(bg, Qt::AlignCenter, m_text);
    }

    // Clicks that stayed on the dark area (not in the transparent hole)
    void mousePressEvent(QMouseEvent *) override
    {
        if (m_done) return;
        auto cb = m_onCanceled;
        finish();
        cb();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            auto cb = m_onCanceled;
            finish();
            cb();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

    // Intercept mouse presses on the tab bar before the tab bar switches naturally.
    // Return true to consume the event so the tab bar does NOT switch on its own;
    // the callback calls setCurrentIndex() explicitly.
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (!m_done && m_tabs && obj == m_tabs->tabBar()
                && event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            const int idx = m_tabs->tabBar()->tabAt(me->pos());
            if (idx >= 0) {
                auto cb = m_onTabSelected;
                finish();
                cb(idx);
                return true;  // consume — tab bar must NOT switch by itself
            }
            // Clicked gap / empty area in the tab bar → cancel
            auto cb = m_onCanceled;
            finish();
            cb();
            return false;
        }
        return QWidget::eventFilter(obj, event);
    }

private:
    void finish()
    {
        if (m_done) return;
        m_done = true;
        close();  // WA_DeleteOnClose: deferred deletion, safe to use members after this
    }

    QPointer<QTabWidget>      m_tabs;
    QString                   m_text;
    std::function<void(int)>  m_onTabSelected;
    std::function<void()>     m_onCanceled;
    bool                      m_done = false;
};

} // anonymous namespace

void MainWindow::startCopyTableToTab()
{
    if (!m_tablesDock || m_tablesDock->count() == 0)
        return;
    if (m_tabWidget->count() < 2) {
        statusBar()->showMessage(tr("Need at least 2 tabs to copy table"), 3000);
        return;
    }

    const int srcTableIdx = m_tablesDock->currentIndex();
    const QVector<TableTab> allTables = m_tablesDock->allTables();
    if (srcTableIdx < 0 || srcTableIdx >= allTables.size())
        return;
    const TableTab tableCopy = allTables[srcTableIdx];
    const int sourceTabIdx = m_tabWidget->currentIndex();

    // Close any previous overlay
    if (m_tabPickerOverlay) {
        m_tabPickerOverlay->close();
        m_tabPickerOverlay = nullptr;
    }

    const QString hint = tr("Select tab to copy table, press Esc to cancel");
    auto *overlay = new TabPickerOverlay(
        this, m_tabWidget, hint,
        [this, tableCopy, sourceTabIdx](int targetIdx) {
            if (targetIdx == sourceTabIdx) {
                statusBar()->showMessage(tr("Cannot copy table to the same tab"), 3000);
                return;
            }
            m_tabWidget->setCurrentIndex(targetIdx);
            m_tablesDock->addTable(tableCopy.name, &tableCopy.table);
            useTableAct->setDisabled(false);
            useTableAct->setChecked(true);
            switchUseTable();
            statusBar()->showMessage(tr("Table copied to tab"), 2000);
        },
        []() {} // cancel: nothing to do
    );
    m_tabPickerOverlay = overlay;
    connect(overlay, &QObject::destroyed, this, [this]() {
        m_tabPickerOverlay = nullptr;
    });
}

