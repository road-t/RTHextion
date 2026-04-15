// Auto-extracted from mainwindow.cpp
#include "mainwindow.h"
#include "internal.h"
#include "appinfo.h"
#include <QStatusBar>
#include <QComboBox>
#include <QTabWidget>
using namespace MainWindowInternal;
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QApplication>
#include <QProgressDialog>
#include <QLabel>
#include <QPainter>
#include <QFontMetrics>
#include <QScrollBar>
#include "SectionListModel.h"
#include "PointerListModel.h"
#include "romdetect.h"
#include "romchecksum.h"
#include "encodingdetect.h"

void MainWindow::openProject()
{
    const QString dir = lastDirectory(QStringLiteral("kLastProjectDirKey"));
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), dir,
        tr("RTHextion Project (*.rthp);;All Files (*)"));
    if (path.isEmpty())
        return;

    // Open in new tab if current has content
    if (m_sessions.isEmpty() || !isUntitled || (hexEdit && hexEdit->isModified()))
        createSession();

    openProjectFile(path);
}

void MainWindow::openProjectFile(const QString &path)
{
    // If this exact project is already open, close everything cleanly first
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    const QString alreadyOpen = m_document ? QFileInfo(m_document->projectFilePath).canonicalFilePath() : QString();

    if (!canonicalPath.isEmpty() && canonicalPath == alreadyOpen) {
        // Close current file/project without prompting (we're about to reload)
        m_document->projectFilePath.clear();
        m_document->projectName.clear();
        m_document->clearDirty();
        m_document->originalBytes.clear();
        m_document->originalFileSize = -1;
        hexEdit->setData(QByteArray());
        m_changeTrackingSnapshot = QByteArray();
        hexEdit->clearPointers();
        hexEdit->removeTranslationTable();
        tb = nullptr;
        m_tablesDock->clearAll();
        useTableAct->setChecked(false);
        useTableAct->setEnabled(false);
        editTableAct->setEnabled(false);
        saveTableAct->setEnabled(false);
        saveTableAsAct->setEnabled(false);
        resetNavigationHistory();
        setCurrentFile(QString());
        setWindowModified(false);
    }

    HexDocument doc;
    if (!doc.loadProject(path)) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Cannot read project file from %1").arg(path));
        return;
    }

    const bool wasRestoringProjectUi = m_restoringProjectUi;
    m_restoringProjectUi = true;

    // 1. Load the data file
    if (!doc.filePath.isEmpty() && QFile::exists(doc.filePath)) {
        loadFile(doc.filePath);
    }

    // 2. Load translation tables into dock widget
    tb = nullptr;
    m_restoringTableDockState = true;
    m_tablesDock->clearAll();
    bool hasTables = false;

    if (!doc.tables.isEmpty()) {
        // Multi-table: add all tables to the dock
        for (auto &entry : doc.tables) {
            m_tablesDock->addTable(entry.name, entry.table);
        }
        // Restore isOriginal flags from the project (override the first-table default)
        for (int i = 0; i < doc.tables.size(); ++i)
            m_tablesDock->setTableOriginal(i, doc.tables[i].isOriginal);
        doc.tables.clear();

        hasTables = m_tablesDock->count() > 0;
        if (doc.activeTableIndex >= 0 && doc.activeTableIndex < m_tablesDock->count())
            m_tablesDock->setCurrentIndex(doc.activeTableIndex);
        else if (hasTables)
            m_tablesDock->setCurrentIndex(0);

        tb = m_tablesDock->currentTable();
        useTableAct->setEnabled(hasTables);
        useTableAct->setChecked(hasTables && doc.useTable);

        if (useTableAct->isChecked())
            applySelectedTable();
        else
            hexEdit->removeTranslationTable();

        editTableAct->setEnabled(m_tablesDock->count() > 0);
        saveTableAct->setEnabled(m_tablesDock->count() > 0);
        saveTableAsAct->setEnabled(m_tablesDock->count() > 0);
    } else if (doc.translationTable) {
        // Legacy single table from project
        m_tablesDock->addTable(QStringLiteral("Table 1"), doc.translationTable);
        hasTables = m_tablesDock->count() > 0;
        tb = m_tablesDock->currentTable();
        tableFilePath = doc.tableFilePath;
        if (doc.useTable)
            hexEdit->setTranslationTable(tb);
        useTableAct->setEnabled(true);
        useTableAct->setChecked(doc.useTable);
        editTableAct->setEnabled(true);
        saveTableAct->setEnabled(true);
        saveTableAsAct->setEnabled(true);
        if (!doc.useTable)
            hexEdit->removeTranslationTable();
    } else if (!doc.tableFilePath.isEmpty() && QFile::exists(doc.tableFilePath)) {
        // Fall back to loading table from file path
        const TranslationTable fileTable(doc.tableFilePath);
        m_tablesDock->addTable(QFileInfo(doc.tableFilePath).completeBaseName(), &fileTable);
        hasTables = m_tablesDock->count() > 0;
        tb = m_tablesDock->currentTable();
        tableFilePath = doc.tableFilePath;
        if (doc.useTable)
            hexEdit->setTranslationTable(tb);
        useTableAct->setEnabled(true);
        useTableAct->setChecked(doc.useTable);
        editTableAct->setEnabled(true);
        saveTableAct->setEnabled(true);
        saveTableAsAct->setEnabled(true);
        if (!doc.useTable)
            hexEdit->removeTranslationTable();
    }

    m_restoringTableDockState = false;

    // 3. Encoding
    m_currentEncoding = doc.currentEncoding();
    hexEdit->setCurrentEncoding(doc.currentEncoding());
    if (lbEncoding)
        lbEncoding->setText(doc.currentEncoding());
    syncEncodingMenu();

    // 4. ROM type + byte order
    m_detectedRomType = doc.romType();
    m_pointerOffset = doc.pointerOffset();
    m_pointerSize = doc.pointerSize();
    {
        const QSignalBlocker blocker(cbRomType);
        cbRomType->setCurrentIndex(static_cast<int>(doc.romType()));
        syncRomTypeMenu(static_cast<int>(doc.romType()));
    }
    hexEdit->byteOrder = doc.byteOrder();
    updateEndiannesLabel();

    // 5. Pointers
    doc.restorePointers(hexEdit->pointers());
    // Update action state only — don't call pointersUpdated() here because it
    // calls markDirty() which would fire the tab-title callback before the
    // document copy (*m_document = doc) resets m_dirty to false.
    showPointersAct->setEnabled(!hexEdit->pointers()->empty());

    // 5b. Sections
    doc.restoreSections(m_sectionModel);
    hexEdit->setShowSections(doc.showSections);
    if (m_sectionsDock) {
        m_sectionsDock->setShowSectionsChecked(doc.showSections);
        m_sectionsDock->setRomTypeName(QString::fromLatin1(romTypeName(doc.romType())));
        m_sectionsDock->setCurrentRomType(doc.romType());
    }

    // 6. Alignment (virtual line breaks) — block signal to avoid marking project modified on load
    {
        const QSignalBlocker blocker(hexEdit);
        if (!doc.alignmentOffsets().isEmpty())
            hexEdit->setLineBreaks(doc.alignmentOffsets());
        else
            hexEdit->clearLineBreaks();
    }

    // 7. Cursor position
    if (doc.cursorPosition > 0) {
        hexEdit->setCursorPosition(doc.cursorPosition);
        hexEdit->ensureVisible();
    }

    // 7. Store project association
    *m_document = doc;
    m_document->translationTable = nullptr; // MainWindow owns tb
    // Re-install the dirty-change callback: copy-assignment wiped it.
    if (m_currentSession) {
        EditorSession *session = m_currentSession;
        m_document->setDirtyChangedCallback([this, session] {
            const int idx = m_sessions.indexOf(session);
            if (idx >= 0)
                updateTabTitle(idx);
        });
    }

    // 8. Remember project as last opened
    QSettings settings;
    settings.setValue(QStringLiteral("LastProjectFile"), path);
    addToRecentProjects(path);
    rememberDirectory(QStringLiteral("kLastProjectDirKey"), path);
    m_tablesDock->setProjectName(m_document->projectName);
    statusBar()->showMessage(tr("Project loaded"), 2000);
    m_document->clearDirty();
    updateWindowTitle();
    updateActionStates();

    // Restore per-tab display settings from the session/app state.
    m_restoringProjectUi = true;
    const bool showPointers = m_currentSession ? m_currentSession->showPointers : true;
    const bool showChanges = m_currentSession ? m_currentSession->showChanges : false;
    const bool changesHexMode = m_currentSession ? m_currentSession->changesHexMode : false;
    showPointersAct->setChecked(showPointers);
    m_pointersDock->setShowPointersChecked(showPointers);
    switchShowPointers();
    showChangesAct->setChecked(showChanges);
    m_changesDock->setShowChangesChecked(showChanges);
    m_changesDock->setHexMode(changesHexMode);
    toggleShowChanges();

    // Restore dock layout (positions/sizes) from project, then re-apply the
    // per-project closed/open state stored in app settings.
    if (!doc.dockLayoutState.isEmpty())
        restoreState(doc.dockLayoutState);
    restoreProjectDockVisibilityState(path);
    if (!doc.tablesColumnsState.isEmpty())
        m_tablesDock->restoreColumnsState(doc.tablesColumnsState);
    updateDockAreaActions();

    m_restoringProjectUi = wasRestoringProjectUi;
    m_document->clearDirty();
    if (hexEdit)
        hexEdit->setModified(false);
    setWindowModified(false);
    // Ensure the tab title reflects the now-clean document state (anything that
    // ran between session creation and here may have left a stale '*' via the
    // markDirty callback or from loadFile's pointersUpdated call).
    updateTabTitle(m_tabWidget->currentIndex());
    if (!m_document->originalBytes.isEmpty()) {
        refreshChangesView();
        enforceBottomDockEqualWidth();
    }

    // Sync session state from the just-loaded project without detaching live tab
    // widgets from the dock.  Calling saveCurrentSession() here would invoke
    // detachTabs(), which strips every tab widget from the QTabWidget and makes
    // the dock appear empty.  Instead we only update the fields that could be
    // stale from the default-constructed session created at project open time.
    if (m_currentSession)
    {
        m_currentSession->tableSnapshot    = m_tablesDock->takeSnapshot();
        m_currentSession->tableActiveIndex = m_tablesDock->currentIndex();
        m_currentSession->dockTablesVisible    = m_tablesDock   && m_tablesDock->isVisible();
        m_currentSession->dockPointersVisible  = m_pointersDock && m_pointersDock->isVisible();
        m_currentSession->dockChangesVisible   = m_changesDock  && m_changesDock->isVisible();
        m_currentSession->dockSectionsVisible  = m_sectionsDock && m_sectionsDock->isVisible();
        m_currentSession->dockVisibilityInitialized = true;
    }
}

bool MainWindow::saveProject()
{
    if (m_document->projectFilePath.isEmpty())
        return saveProjectAs();

    return saveProjectImpl(m_document->projectFilePath);
}

bool MainWindow::saveProjectAs()
{
    // Suggest a name: take current file's base name, replace underscores with spaces
    QString suggestedName;
    if (!curFile.isEmpty()) {
        suggestedName = QFileInfo(curFile).completeBaseName().replace(QLatin1Char('_'), QLatin1Char(' '));
    } else if (!m_document->projectName.isEmpty()) {
        suggestedName = m_document->projectName;
    }

    const QString dir = lastDirectory(QStringLiteral("kLastProjectDirKey"));
    const QString suggested = dir.isEmpty() ? suggestedName
                                             : QDir(dir).filePath(suggestedName);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project"), suggested,
        tr("RTHextion Project (*.rthp);;All Files (*)"));
    if (path.isEmpty())
        return false;

    if (saveProjectImpl(path)) {
        rememberDirectory(QStringLiteral("kLastProjectDirKey"), path);
        return true;
    }
    return false;
}

bool MainWindow::saveProjectImpl(const QString &path)
{
    // Derive project name from file name if not set yet
    if (m_document->projectName.isEmpty())
        m_document->projectName = QFileInfo(path).completeBaseName();

    m_tablesDock->setProjectName(m_document->projectName);

    // Populate document from current state
    m_document->filePath = curFile;
    m_document->tableFilePath = tableFilePath;
    m_document->useTable = (useTableAct && useTableAct->isChecked());
    m_document->setCurrentEncoding(m_currentEncoding);
    m_document->setRomType(m_detectedRomType);
    m_document->setPointerOffset(m_pointerOffset);
    m_document->setPointerSize(m_pointerSize);
    m_document->setByteOrder(hexEdit->byteOrder);
    m_document->snapshotPointers(hexEdit->pointers());
    m_document->snapshotSections(m_sectionModel);
    m_document->showSections = hexEdit->showSections();
    m_document->dockLayoutState = QByteArray(); // now stored in per-project app settings
    m_document->tablesColumnsState = m_tablesDock ? m_tablesDock->saveColumnsState() : QByteArray();
    m_document->cursorPosition = 0; // now stored in per-project app settings
    m_document->setAlignmentOffsets(hexEdit->lineBreaks());

    // Recompute tracked diffs byte-by-byte.
    // If project already has an original baseline (e.g. loaded via "Load original"),
    // keep that baseline authoritative and only keep bytes that are still changed.
    // Otherwise, derive baseline from the current file on disk.
    const QVector<QPair<qint64, QByteArray>> previousOriginalBytes = m_document->originalBytes;
    const QByteArray currentData = hexEdit->data();
    m_document->originalBytes.clear();
    m_document->originalFileSize = -1;

    auto appendGroupedDiffs = [this](const QVector<QPair<qint64, QByteArray>> &flatDiffs) {
        if (flatDiffs.isEmpty())
            return;

        qint64 runStart = -1;
        QByteArray runBytes;
        for (int i = 0; i < flatDiffs.size(); ++i) {
            const qint64 ofs = flatDiffs[i].first;
            const char origByte = flatDiffs[i].second.isEmpty() ? char(0) : flatDiffs[i].second.at(0);

            if (runStart < 0) {
                runStart = ofs;
                runBytes.clear();
                runBytes.append(origByte);
                continue;
            }

            const qint64 prevOfs = flatDiffs[i - 1].first;
            if (ofs == prevOfs + 1) {
                runBytes.append(origByte);
            } else {
                m_document->originalBytes.append({runStart, runBytes});
                runStart = ofs;
                runBytes.clear();
                runBytes.append(origByte);
            }
        }

        if (runStart >= 0 && !runBytes.isEmpty())
            m_document->originalBytes.append({runStart, runBytes});
    };

    if (!previousOriginalBytes.isEmpty()) {
        // Flatten previous grouped baseline and keep only currently changed bytes.
        QVector<QPair<qint64, QByteArray>> filtered;
        for (const auto &entry : previousOriginalBytes) {
            const qint64 base = entry.first;
            const QByteArray &origBytes = entry.second;
            for (int i = 0; i < origBytes.size(); ++i) {
                const qint64 pos = base + i;
                const char origByte = origBytes.at(i);

                bool changedNow = false;
                if (pos >= 0 && pos < currentData.size())
                    changedNow = currentData.at(pos) != origByte;
                else
                    changedNow = true;

                if (changedNow)
                    filtered.append({pos, QByteArray(1, origByte)});
            }
        }

        std::sort(filtered.begin(), filtered.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });

        appendGroupedDiffs(filtered);
    } else if (!curFile.isEmpty()) {
        // No external baseline tracked yet: compare against bytes from file on disk.
        QFile diskFile(curFile);
        if (diskFile.open(QIODevice::ReadOnly)) {
            const QByteArray originalFileData = diskFile.readAll();
            QVector<QPair<qint64, QByteArray>> flatDiffs;

            const qint64 commonSize = qMin<qint64>(originalFileData.size(), currentData.size());
            for (qint64 i = 0; i < commonSize; ++i) {
                if (originalFileData.at(i) != currentData.at(i))
                    flatDiffs.append({i, QByteArray(1, originalFileData.at(i))});
            }

            // If current file is shorter than original, keep truncated tail bytes too.
            if (originalFileData.size() > currentData.size()) {
                for (qint64 i = currentData.size(); i < originalFileData.size(); ++i)
                    flatDiffs.append({i, QByteArray(1, originalFileData.at(i))});
            }

            appendGroupedDiffs(flatDiffs);
        }
    }

    // Build table list from dock widget for multi-table serialization
    QVector<DocTableEntry> docTables;
    const auto &dockTables = m_tablesDock->allTables();
    for (const auto &tt : dockTables) {
        DocTableEntry dte;
        dte.name = tt.name;
        dte.isOriginal = tt.isOriginal;
        dte.table = const_cast<TranslationTable *>(&tt.table);  // not owned — just a reference for serialization
        docTables.append(dte);
    }
    const int activeIdx = (m_tablesDock && m_tablesDock->count() > 0)
                              ? m_tablesDock->currentIndex() : -1;

    if (!m_document->saveProject(path, docTables, activeIdx)) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Cannot write project file to %1").arg(path));
        return false;
    }

    {
        QSettings settings;
        settings.setValue(QStringLiteral("LastProjectFile"), path);
    }
    addToRecentProjects(path);
    m_document->clearDirty();
    updateWindowTitle();
    updateActionStates();
    updateTabTitle(m_tabWidget->currentIndex());

    statusBar()->showMessage(tr("Project saved"), 2000);
    return true;
}


void MainWindow::loadFile(const QString &fileName)
{
    if (!maybeSave())
        return;
    if (!maybeSaveProject())
        return;

    // Capture project state before clearing it
    const bool hadSavedProject = m_document && !m_document->projectFilePath.isEmpty();

    // Clear project state when loading a new file directly
    m_document->projectFilePath.clear();
    m_document->projectName.clear();
    m_document->clearDirty();

    const QString canonicalIncoming = QFileInfo(fileName).canonicalFilePath();
    const QString incomingPath = canonicalIncoming.isEmpty() ? fileName : canonicalIncoming;
    const bool loadingAnotherFile = !curFile.isEmpty() && !incomingPath.isEmpty() && curFile != incomingPath;

    // If a saved project was open, silently clear pointers/table without asking
    if (loadingAnotherFile && !hadSavedProject && !hexEdit->pointers()->empty())
    {
        QMessageBox confirm(QMessageBox::Warning,
                            QString::fromLatin1(AppInfo::Name),
                            tr("You are about to load another file. Clear pointer list?"),
                            QMessageBox::Yes | QMessageBox::Cancel,
                            this);
        if (confirm.exec() != QMessageBox::Yes)
            return;
    }
    if (loadingAnotherFile || hadSavedProject)
    {
        hexEdit->clearPointers();
        pointersUpdated();
    }

    m_currentSession->file.setFileName(fileName);

    if (!hexEdit->setData(m_currentSession->file))
    {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Cannot read file %1:\n%2.")
                                 .arg(fileName)
                                 .arg(m_currentSession->file.errorString()));
        return;
    }

    // For files up to ~256MB, keep full snapshot for incremental change tracking.
    // For larger files, skip the snapshot to avoid doubling RAM usage.
    static const qint64 kSnapshotSizeLimit = 256 * 1024 * 1024;
    if (hexEdit->dataSize() <= kSnapshotSizeLimit)
        m_changeTrackingSnapshot = hexEdit->data();
    else
        m_changeTrackingSnapshot = QByteArray(""); // non-null empty → change tracking disabled for large files
    hexEdit->setShowOriginal(false);
    if (m_changesDock)
        m_changesDock->setShowOriginalChecked(false);

    if (isTableLikeFilePath(fileName) && useTableAct) {
        useTableAct->setChecked(false);
        hexEdit->removeTranslationTable();
    } else {
        applyTranslationTableForViewMode();
    }

    resetNavigationHistory();
    setCurrentFile(fileName);
    statusBar()->showMessage(tr("File loaded"), 2000);
    rememberDirectory(kLastFileDirKey, fileName);

    // Encoding detection order on open:
    // 1. Detect ROM type (if enabled). If detected → force ASCII, done.
    // 2. If ROM unknown, detect text encoding (if enabled). If non-ASCII found → apply it, done.
    // 3. Otherwise: if ResetEncodingOnClose is set → apply DefaultEncoding; else keep current.
    QSettings settings;
    const bool detectRomTypeEnabled = settings.value("DetectEndianness", true).toBool();
    const bool detectEncodingEnabled = settings.value("DetectEncoding", true).toBool();
    const QString defaultEncoding = settings.value("DefaultEncoding", QStringLiteral("ASCII")).toString();
    const bool resetEncoding = settings.value("ResetEncodingOnClose", false).toBool();
    const bool resetTable   = settings.value("ResetTableOnClose",    false).toBool();

    // Apply "reset on file close" preferences, but only when no saved project is being replaced
    if (resetTable && !hadSavedProject) {
        hexEdit->removeTranslationTable();
        tb = nullptr;
        tableFilePath.clear();
        m_tablesDock->clearAll();
        useTableAct->setChecked(false);
        useTableAct->setEnabled(false);
        editTableAct->setEnabled(false);
        saveTableAct->setEnabled(false);
        saveTableAsAct->setEnabled(false);
    } else if (hadSavedProject) {
        // Silently clear table when switching file within a project context
        hexEdit->removeTranslationTable();
        tb = nullptr;
        tableFilePath.clear();
        m_tablesDock->clearAll();
        useTableAct->setChecked(false);
        useTableAct->setEnabled(false);
        editTableAct->setEnabled(false);
        saveTableAct->setEnabled(false);
        saveTableAsAct->setEnabled(false);
    }

    RomType rom = RomType::Unknown;
    if (detectRomTypeEnabled)
    {
        const QByteArray header = hexEdit->dataAt(0, 512);
        rom = detectRomType(fileName, header);
    }
    m_detectedRomType = rom;
    m_pointerOffset = defaultPointerOffset(rom);
    if (m_currentSession) {
        m_currentSession->detectedRomType = m_detectedRomType;
        m_currentSession->pointerOffset = m_pointerOffset;
    }

    {
        const QSignalBlocker blocker(cbRomType);
        cbRomType->setCurrentIndex(static_cast<int>(rom));
        syncRomTypeMenu(static_cast<int>(rom));
    }

    if (rom != RomType::Unknown)
    {
        hexEdit->byteOrder = defaultByteOrder(rom);
        updateEndiannesLabel();
    }

    hexEdit->setDisasmRomType(rom);

    auto applyEncoding = [this](const QString &enc) {
        m_currentEncoding = enc;
        hexEdit->setCurrentEncoding(enc);
        if (lbEncoding)
            lbEncoding->setText(enc);
        syncEncodingMenu();
    };

    if (rom != RomType::Unknown)
    {
        applyEncoding(QStringLiteral("ASCII")); // known ROM type always uses ASCII
    }
    else if (detectEncodingEnabled)
    {
        const QByteArray dataChunk = hexEdit->dataAt(0, 4096);
        const QString detectedEncoding = detectEncoding(dataChunk);
        if (detectedEncoding != QStringLiteral("ASCII"))
        {
            applyEncoding(detectedEncoding);
            statusBar()->showMessage(tr("File loaded. Encoding: %1").arg(detectedEncoding), 3000);
        }
        else
        {
            // Detection found nothing — step 3: reset if flag set, else keep current.
            if (resetEncoding)
                applyEncoding(defaultEncoding);
        }
    }
    else
    {
        // Detection disabled — step 3: reset if flag set, else keep current.
        if (resetEncoding)
            applyEncoding(defaultEncoding);
    }

    settings.setValue("RecentFile0", fileName);
    settings.setValue("RecentFile0RomType", static_cast<int>(rom));

    if (m_sectionsDock) {
        m_sectionsDock->setRomTypeName(QString::fromLatin1(romTypeName(rom)));
        m_sectionsDock->setCurrentRomType(rom);
    }

    // Do NOT auto-scan sections on file load: sections are now created only on explicit
    // "Parse" action via SectionsDockWidget context menu to avoid unexpected behavior.
    // Removed: if (m_sectionModel && m_sectionModel->count() == 0 && rom != RomType::Unknown) {
    //             parseHeaderSectionsImpl(false);
    //         }

    if (m_document)
        m_document->clearDirty();
    if (hexEdit)
        hexEdit->setModified(false);

    // New regular files should start with sections dock visible.
    if (!m_restoringSession && m_sectionsDock) {
        m_sectionsDock->show();
        if (m_currentSession) {
            m_currentSession->dockSectionsVisible = true;
            m_currentSession->dockVisibilityInitialized = true;
        }
    }

    setWindowModified(false);
    updateTabTitle(m_tabWidget->currentIndex());
}

/// Overloaded version that restores a previously-saved ROM type when auto-detection returns Unknown.
/// This is used when loading the last session to preserve user's manual ROM type selection.
void MainWindow::loadFile(const QString &fileName, RomType suggestedRomType)
{
    // First, try auto-detection
    loadFile(fileName);

    // If detection failed but we have a suggestion from previous session, apply it
    if (m_detectedRomType == RomType::Unknown && suggestedRomType != RomType::Unknown)
    {
        m_detectedRomType = suggestedRomType;
        m_pointerOffset = defaultPointerOffset(suggestedRomType);
        if (m_currentSession) {
            m_currentSession->detectedRomType = m_detectedRomType;
            m_currentSession->pointerOffset = m_pointerOffset;
        }

        {
            const QSignalBlocker blocker(cbRomType);
            cbRomType->setCurrentIndex(static_cast<int>(suggestedRomType));
            syncRomTypeMenu(static_cast<int>(suggestedRomType));
        }

        if (suggestedRomType != RomType::Unknown)
        {
            hexEdit->byteOrder = defaultByteOrder(suggestedRomType);
            updateEndiannesLabel();
        }

        hexEdit->setDisasmRomType(suggestedRomType);

        QSettings settings;
        settings.setValue("RecentFile0RomType", static_cast<int>(suggestedRomType));

        if (m_sectionsDock) {
            m_sectionsDock->setRomTypeName(QString::fromLatin1(romTypeName(suggestedRomType)));
            m_sectionsDock->setCurrentRomType(suggestedRomType);
        }
    }
}

void MainWindow::createIpsPatch()
{
    if (curFile.isEmpty()) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("No file is currently open."));
        return;
    }

    if (!m_document || m_document->originalBytes.isEmpty()) {
        QMessageBox::information(this, QString::fromLatin1(AppInfo::Name),
                                 tr("No changes detected."));
        return;
    }

    // Build IPS records from project originalBytes:
    // for each tracked region, compare original vs. current file data
    const QByteArray currentData = hexEdit->data();
    QVector<QPair<qint64, QByteArray>> records;

    for (const auto &entry : m_document->originalBytes) {
        const qint64 baseOffset = entry.first;
        const QByteArray &origBytes = entry.second;

        qint64 i = 0;
        while (i < origBytes.size()) {
            const qint64 absPos = baseOffset + i;
            if (absPos >= currentData.size() || origBytes.at(i) == currentData.at(absPos)) {
                ++i;
                continue;
            }
            // Start of a differing run
            const qint64 start = absPos;
            QByteArray patchBytes;
            while (i < origBytes.size()) {
                const qint64 pos = baseOffset + i;
                if (pos >= currentData.size())
                    break;
                if (origBytes.at(i) == currentData.at(pos))
                    break;
                patchBytes.append(currentData.at(pos));
                ++i;
                if (patchBytes.size() >= 0xFFFF) break;
            }
            if (!patchBytes.isEmpty())
                records.append({start, patchBytes});
        }
    }

    if (records.isEmpty()) {
        QMessageBox::information(this, QString::fromLatin1(AppInfo::Name),
                                 tr("No changes detected."));
        return;
    }

    // Suggest project name as IPS filename
    QString suggestedIpsPath;
    {
        const QString dir = lastDirectory(kLastFileDirKey);
        const QString baseName = (m_document && !m_document->projectName.isEmpty())
                                     ? m_document->projectName
                                     : QFileInfo(curFile).completeBaseName();
        suggestedIpsPath = dir.isEmpty() ? baseName : QDir(dir).filePath(baseName);
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Create IPS patch"), suggestedIpsPath,
        tr("IPS Patch (*.ips);;All Files (*)"));
    if (path.isEmpty())
        return;

    QFile ipsFile(path);
    if (!ipsFile.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Cannot write file %1.").arg(path));
        return;
    }

    // IPS header
    ipsFile.write("PATCH", 5);

    for (const auto &rec : records) {
        // 3-byte offset (big-endian)
        quint32 ofs = static_cast<quint32>(rec.first);
        char ofsBytes[3] = {
            static_cast<char>((ofs >> 16) & 0xFF),
            static_cast<char>((ofs >> 8) & 0xFF),
            static_cast<char>(ofs & 0xFF)
        };
        ipsFile.write(ofsBytes, 3);

        // 2-byte size (big-endian)
        quint16 sz = static_cast<quint16>(rec.second.size());
        char szBytes[2] = {
            static_cast<char>((sz >> 8) & 0xFF),
            static_cast<char>(sz & 0xFF)
        };
        ipsFile.write(szBytes, 2);

        // Data
        ipsFile.write(rec.second);
    }

    // IPS footer
    ipsFile.write("EOF", 3);
    ipsFile.close();

    rememberDirectory(kLastFileDirKey, path);
    statusBar()->showMessage(tr("IPS patch saved"), 2000);
}


void MainWindow::loadIpsPatch()
{
    if (curFile.isEmpty()) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("No file is currently open."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load IPS patch"), lastDirectory(kLastFileDirKey),
        tr("IPS Patch (*.ips);;All Files (*)"));
    if (path.isEmpty())
        return;

    QFile ipsFile(path);
    if (!ipsFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Cannot read file %1:\n%2.").arg(path, ipsFile.errorString()));
        return;
    }

    const QByteArray ipsData = ipsFile.readAll();
    ipsFile.close();

    // Validate header
    if (ipsData.size() < 8 || ipsData.left(5) != QByteArray("PATCH")) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Invalid IPS patch file."));
        return;
    }

    // Work entirely in memory to avoid per-byte undo stack overhead.
    QByteArray currentData = hexEdit->data();
    const qint64 originalSize = currentData.size();
    QVector<QPair<qint64, QByteArray>> origGroups;

    // Single pass: parse IPS records, collect original bytes, and apply
    // patches directly to currentData.
    int pos = 5; // after "PATCH"
    while (pos + 3 <= ipsData.size()) {
        if (ipsData.mid(pos, 3) == QByteArray("EOF"))
            break;
        if (pos + 5 > ipsData.size())
            break;

        quint32 ofs = (static_cast<quint8>(ipsData.at(pos)) << 16)
                    | (static_cast<quint8>(ipsData.at(pos + 1)) << 8)
                    | static_cast<quint8>(ipsData.at(pos + 2));
        pos += 3;

        quint16 sz = (static_cast<quint8>(ipsData.at(pos)) << 8)
                   | static_cast<quint8>(ipsData.at(pos + 1));
        pos += 2;

        QByteArray patchData;
        if (sz == 0) {
            // RLE record
            if (pos + 3 > ipsData.size()) break;
            quint16 runLen = (static_cast<quint8>(ipsData.at(pos)) << 8)
                           | static_cast<quint8>(ipsData.at(pos + 1));
            char runVal = ipsData.at(pos + 2);
            pos += 3;
            patchData = QByteArray(runLen, runVal);
        } else {
            if (pos + sz > ipsData.size()) break;
            patchData = ipsData.mid(pos, sz);
            pos += sz;
        }

        const qint64 patchEnd = ofs + patchData.size();

        // Expand if the record extends beyond the current size
        if (patchEnd > currentData.size())
            currentData.resize(static_cast<int>(patchEnd));

        // Store original bytes (only from the original-size portion)
        if (ofs < originalSize) {
            qint64 origLen = qMin<qint64>(patchData.size(), originalSize - ofs);
            origGroups.append({ofs, currentData.mid(static_cast<int>(ofs),
                                                    static_cast<int>(origLen))});
        }

        // Apply patch directly to the byte array
        memcpy(currentData.data() + ofs, patchData.constData(), patchData.size());
    }

    // Record original file size before expansion
    if (currentData.size() > originalSize) {
        if (m_document->originalFileSize < 0)
            m_document->originalFileSize = originalSize;
    }

    // Invalidate the change tracking snapshot so that onHexDataChangedAt
    // (triggered by setData) just re-initialises it without processing diffs.
    m_changeTrackingSnapshot = QByteArray();

    // Replace editor contents in one shot (resets undo stack).
    hexEdit->setData(currentData);

    // Mark as modified so the user is prompted to save.
    hexEdit->setModified(true);
    setWindowModified(true);

    // Re-initialise the tracking snapshot with the new data.
    m_changeTrackingSnapshot = currentData;

    // Merge origGroups into project's originalBytes
    for (const auto &g : origGroups) {
        bool merged = false;
        for (auto &existing : m_document->originalBytes) {
            if (existing.first == g.first) {
                merged = true;
                break;
            }
        }
        if (!merged)
            m_document->originalBytes.append(g);
    }

    rememberDirectory(kLastFileDirKey, path);
    statusBar()->showMessage(tr("IPS patch applied"), 2000);

    // Enable changes highlighting and set hex mode
    if (showChangesAct && !showChangesAct->isChecked())
        showChangesAct->setChecked(true);

    if (m_changesDock)
        m_changesDock->setHexMode(true);

    m_changesDock->show();
    enforceBottomDockEqualWidth();
    updateChangedBytesHighlight();
    refreshChangesView();
}


void MainWindow::loadOriginal()
{
    if (curFile.isEmpty()) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("No file is currently open."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load original file"), lastDirectory(kLastFileDirKey),
        tr("All Files (*)"));
    if (path.isEmpty())
        return;

    QFile origFile(path);
    if (!origFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Cannot read file %1:\n%2.").arg(path, origFile.errorString()));
        return;
    }

    const QByteArray origData = origFile.readAll();
    origFile.close();

    const QByteArray currentData = hexEdit->data();

    if (origData.size() != currentData.size()) {
        const auto reply = QMessageBox::warning(
            this, QString::fromLatin1(AppInfo::Name),
            tr("Warning: the selected file has a different size from the currently open file "
               "(%1 vs %2 bytes).\n\n"
               "Size mismatch may lead to undefined behavior when comparing changes, "
               "applying IPS patches, or recalculating checksums.\n\n"
               "Continue anyway? Only overlapping bytes will be compared.")
                .arg(origData.size())
                .arg(currentData.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
    }

    // Compare byte-by-byte and collect contiguous changed runs.
    // Store original (from origData) values for every changed position.
    const qint64 compareLen = qMin<qint64>(origData.size(), currentData.size());
    QVector<QPair<qint64, QByteArray>> newOriginalBytes;

    qint64 runStart = -1;
    QByteArray runBytes;
    for (qint64 i = 0; i <= compareLen; ++i) {
        const bool changed = (i < compareLen) && (currentData.at(i) != origData.at(i));
        if (changed) {
            if (runStart < 0) {
                runStart = i;
                runBytes.clear();
            }
            runBytes.append(origData.at(i));
        } else {
            if (runStart >= 0) {
                newOriginalBytes.append({runStart, runBytes});
                runStart = -1;
                runBytes.clear();
            }
        }
    }

    if (newOriginalBytes.isEmpty()) {
        QMessageBox::information(this, QString::fromLatin1(AppInfo::Name),
                                 tr("Two files are identical, no changes detected"));
        return;
    }

    m_document->originalBytes = newOriginalBytes;
    m_document->originalFileSize = origData.size();
    updateActionStates();
    m_changesDock->show();
    enforceBottomDockEqualWidth();
    refreshChangesView();
    if (showChangesAct->isChecked())
        updateChangedBytesHighlight();

    rememberDirectory(kLastFileDirKey, path);
    statusBar()->showMessage(
        tr("Original loaded: %n changed byte(s) tracked", nullptr, static_cast<int>(newOriginalBytes.size())),
        3000);
}

