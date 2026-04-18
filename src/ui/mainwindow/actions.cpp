// Auto-extracted from mainwindow.cpp
#include "mainwindow.h"
#include "internal.h"
#include "appinfo.h"
#include <QClipboard>
#include <QStatusBar>
using namespace MainWindowInternal;
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QShortcut>
#include <QMouseEvent>
#include <QChildEvent>
#include <QSplitter>
#include <QDockWidget>
#include <QApplication>
#include "appsettings.h"
#include "DockTitleBar.h"
#include "SectionListModel.h"

void MainWindow::retranslateUi()
{
    // Actions - File
    newAct->setText(tr("New"));
    newAct->setStatusTip(tr("Create a new file"));
    openAct->setText(tr("Open file..."));
    openAct->setStatusTip(tr("Open an existing file"));
    saveAct->setText(tr("Save"));
    saveAct->setStatusTip(tr("Save the file to disk"));
    closeAct->setText(tr("Close"));
    closeAct->setStatusTip(tr("Close the current file"));
    saveAsAct->setText(tr("Save As..."));
    saveAsAct->setStatusTip(tr("Save the file under a new name"));
    saveReadable->setText(tr("Save dump..."));
    saveReadable->setStatusTip(tr("Save the file as dump"));
    revertAct->setText(tr("Revert"));
    revertAct->setStatusTip(tr("Revert file to last saved version"));
    exitAct->setText(tr("Exit"));
    exitAct->setStatusTip(tr("Exit the application"));

    openProjectAct->setText(tr("Open Project..."));
    openProjectAct->setStatusTip(tr("Open an RTHextion project file"));
    saveProjectAct->setText(tr("Save Project"));
    saveProjectAct->setStatusTip(tr("Save the current project"));
    saveProjectAsAct->setText(tr("Save Project As..."));
    saveProjectAsAct->setStatusTip(tr("Save the project under a new name"));

    // Actions - Changes
    showChangesAct->setText(tr("Show changes"));
    showChangesAct->setStatusTip(tr("Highlight bytes changed compared to project originals"));
    createIpsPatchAct->setText(tr("Create IPS patch..."));
    createIpsPatchAct->setStatusTip(tr("Save current changes as an IPS patch file"));
    loadIpsPatchAct->setText(tr("Load IPS patch..."));
    loadIpsPatchAct->setStatusTip(tr("Load an IPS patch and apply it as changes"));
    loadOriginalAct->setText(tr("Load original..."));
    loadOriginalAct->setStatusTip(tr("Load the original (unmodified) file to compute changes"));

    // Actions - Edit
    undoAct->setText(tr("Undo"));
    undoAct->setStatusTip(tr("Undo last action"));
    redoAct->setText(tr("Redo"));
    redoAct->setStatusTip(tr("Redo last undone action"));
    copyAddressAct->setText(tr("Copy address"));
    cutAct->setText(tr("Cut"));
    copyAct->setText(tr("Copy"));
    pasteAct->setText(tr("Paste"));
    saveSelectionReadable->setText(tr("Save selection as dump"));
    saveSelectionReadable->setStatusTip(tr("Save selection as dump"));

    // Actions - Table
    loadTableAct->setText(tr("Import"));
    loadTableAct->setStatusTip(tr("Import table"));
    useTableAct->setText(tr("Use table"));
    useTableAct->setStatusTip(tr("Use translation table"));
    editTableAct->setText(tr("Edit table"));
    editTableAct->setStatusTip(tr("Edit translation table"));
    createEmptyTableAct->setText(tr("Empty table"));
    createEmptyTableAct->setStatusTip(tr("Create a new empty translation table"));
    semiAutoTableAct->setText(tr("Semi-auto generated"));
    semiAutoTableAct->setStatusTip(tr("Generate table by searching for a known text"));
    saveTableAct->setText(tr("Save table"));
    saveTableAct->setStatusTip(tr("Save translation table"));
    saveTableAsAct->setText(tr("Save table as..."));
    saveTableAsAct->setStatusTip(tr("Save translation table to a new file"));
    createTableMenu->setTitle(tr("Create table"));

    // Actions - Script
    dumpScriptAct->setText(tr("Edit script"));
    dumpScriptAct->setStatusTip(tr("Edit text script"));
    insertScriptAct->setText(tr("Import script"));
    insertScriptAct->setStatusTip(tr("Import text script"));

    // Actions - Pointers
    findPointersAct->setText(tr("Find pointers"));
    findPointersAct->setStatusTip(tr("Find pointers for selected text"));
    showPointersAct->setText(tr("Show pointers"));
    showPointersAct->setStatusTip(tr("Show the pointers search dialog"));

    // Actions - Search
    findAct->setText(tr("Find/Replace"));
    findAct->setStatusTip(tr("Show the dialog for finding and replacing"));
    findNextAct->setText(tr("Find next"));
    findNextAct->setStatusTip(tr("Find next occurrence of the searched pattern"));
    virtualFormatAct->setText(tr("Virtually format") + QString("..."));
    removeVirtualFormattingAct->setText(tr("Remove virtual formatting"));
    gotoAct->setText(tr("Jump to offset") + QString("..."));
    gotoAct->setStatusTip(tr("Go to specified offset"));
    previousPositionAct->setText(tr("Previous position"));
    previousPositionAct->setStatusTip(tr("Go to previous cursor position"));
    nextPositionAct->setText(tr("Next position"));
    nextPositionAct->setStatusTip(tr("Go to next cursor position"));
    firstPositionAct->setText(tr("First position"));
    firstPositionAct->setStatusTip(tr("Go to first cursor position in history"));
    lastPositionAct->setText(tr("Last position"));
    lastPositionAct->setStatusTip(tr("Go to last cursor position in history"));
    toFileBeginningAct->setText(tr("To file beginning"));
    toFileBeginningAct->setStatusTip(tr("Go to beginning of file"));
    toFileEndAct->setText(tr("To file end"));
    toFileEndAct->setStatusTip(tr("Go to end of file"));
    if (restoreDockLayoutAct) {
        restoreDockLayoutAct->setText(tr("Restore"));
        restoreDockLayoutAct->setStatusTip(tr("Restore default dock layout"));
    }
    if (collapseLeftDockAreaAct) {
        collapseLeftDockAreaAct->setText(tr("Collapse left panel"));
        collapseLeftDockAreaAct->setStatusTip(tr("Collapse or expand all docks in the left panel"));
    }
    if (collapseRightDockAreaAct) {
        collapseRightDockAreaAct->setText(tr("Collapse right panel"));
        collapseRightDockAreaAct->setStatusTip(tr("Collapse or expand all docks in the right panel"));
    }
    if (collapseBottomDockAreaAct) {
        collapseBottomDockAreaAct->setText(tr("Collapse bottom panel"));
        collapseBottomDockAreaAct->setStatusTip(tr("Collapse or expand all docks in the bottom panel"));
    }

    // Actions - Help/Options
    aboutAct->setText(tr("About %1").arg(AppInfo::Name));
    aboutAct->setStatusTip(tr("Show the application's About box"));
    checkUpdatesAct->setText(tr("Check for Updates..."));
    optionsAct->setText(tr("Preferences"));
    optionsAct->setStatusTip(tr("Show the application options dialog"));

    // Menu titles
    fileMenu->setTitle(tr("File"));
    recentProjectMenu->setTitle(tr("Recent projects"));
    recentFileMenu->setTitle(tr("Recent files"));
    romTypeMenu->setTitle(tr("ROM type"));
    encodingMenu->setTitle(tr("Encoding"));
    editMenu->setTitle(tr("Edit"));
    changesMenu->setTitle(tr("Changes"));
    tableMenu->setTitle(tr("Table"));
    recentTableMenu->setTitle(tr("Recent"));
    scriptMenu->setTitle(tr("Script"));
    pointersMenu->setTitle(tr("Pointers"));
    goMenu->setTitle(tr("Go"));
    viewMenu->setTitle(tr("View"));
    toolbarMenu->setTitle(tr("Toolbar"));
    statusBarMenu->setTitle(tr("Status bar"));
    panelsMenu->setTitle(tr("Panels"));
    languageMenu->setTitle(tr("Language"));
    if (dockMenu)
        dockMenu->setTitle(tr("Dock"));
    helpMenu->setTitle(tr("Help"));

    // Retranslate dock widget
    if (m_tablesDock)
        m_tablesDock->retranslateUi();
    if (m_pointersDock)
        m_pointersDock->retranslateUi();
    if (m_changesDock)
        m_changesDock->retranslateUi();

    if (m_sectionsDock)
        m_sectionsDock->retranslateUi();

    showStatusEndianAct->setText(tr("Endianness"));
    showStatusByteAct->setText(tr("Byte"));
    showStatusWordAct->setText(tr("Word"));
    showStatusDwordAct->setText(tr("DWord"));
    showStatusSelectionAct->setText(tr("Selection"));
    showStatusAddressAct->setText(tr("Address"));
    showStatusSizeAct->setText(tr("Size"));
    showStatusModeAct->setText(tr("Mode"));
    if (showStatusEncodingAct)
        showStatusEncodingAct->setText(tr("Encoding"));
    showSignedValuesAct->setText(tr("Show signed values"));
    showAddressAreaAct->setText(tr("Address area"));
    if (asciiAreaMenu)
        asciiAreaMenu->setTitle(tr("ASCII area"));
    if (panelModeTextAct)
        panelModeTextAct->setText(tr("Text"));
    if (panelModeGraphicsAct)
        panelModeGraphicsAct->setText(tr("Graphics"));
    if (panelModeSoundAct)
        panelModeSoundAct->setText(tr("Sound"));
    if (panelModeDisasmAct)
        panelModeDisasmAct->setText(tr("Disassembly"));
    showAddressGridAct->setText(tr("Show grid"));
    if (showDarkThemeAct)
        showDarkThemeAct->setText(tr("Dark theme"));
    if (showStatusBarAct)
        showStatusBarAct->setText(tr("Status bar"));

    showMapPointersAct->setText(tr("Changes"));
    showMapPointersAct->setStatusTip(tr("Show changed byte locations on the side map"));
    showMapTargetsAct->setText(tr("Pointers/Data"));
    showMapTargetsAct->setStatusTip(tr("Show pointer and target locations on the side map"));

    mapsMenu->setTitle(tr("Maps"));

    // Toolbar names and their View menu entries
    fileToolBar->setWindowTitle(tr("File"));
    editToolBar->setWindowTitle(tr("Edit"));
    searchToolBar->setWindowTitle(tr("Search"));
    navigationToolBar->setWindowTitle(tr("Navigation"));
    scriptToolBar->setWindowTitle(tr("Script"));
    profileToolBar->setWindowTitle(tr("Profile"));
    fileToolBar->toggleViewAction()->setText(tr("File"));
    editToolBar->toggleViewAction()->setText(tr("Actions"));
    searchToolBar->toggleViewAction()->setText(tr("Search"));
    navigationToolBar->toggleViewAction()->setText(tr("Navigation"));
    scriptToolBar->toggleViewAction()->setText(tr("Script"));
    profileToolBar->toggleViewAction()->setText(tr("Profile"));
    resetToolbarsAct->setText(tr("Reset"));

    // Repopulate ROM type combo to translate "Unknown" entry
    repopulateRomTypeCombo();

    // Retranslate ROM type menu "Unknown" entry
    if (romTypeMenuGroup) {
        const auto actions = romTypeMenuGroup->actions();
        if (!actions.isEmpty())
            actions[0]->setText(tr("Unknown"));
    }

    // Refresh "Ready" message if status bar is currently in idle state
    const bool wasIdle = statusBar()->currentMessage() == m_readyText;
    m_readyText = tr("Ready");
    if (wasIdle)
        statusBar()->showMessage(m_readyText);

    // Toolbar navigation actions
    toolbarFirstPositionAct->setToolTip(tr("First position"));
    toolbarFirstPositionAct->setStatusTip(tr("Go to first cursor position in history"));
    toolbarPreviousPositionAct->setToolTip(tr("Previous position"));
    toolbarPreviousPositionAct->setStatusTip(tr("Go to previous cursor position"));
    toolbarNextPositionAct->setToolTip(tr("Next position"));
    toolbarNextPositionAct->setStatusTip(tr("Go to next cursor position"));
    toolbarLastPositionAct->setToolTip(tr("Last position"));
    toolbarLastPositionAct->setStatusTip(tr("Go to last cursor position in history"));

    // Toolbar script actions
    toolbarDumpScriptAct->setText(tr("Edit script"));
    toolbarDumpScriptAct->setToolTip(tr("Edit script"));
    toolbarDumpScriptAct->setStatusTip(tr("Edit text script"));
    toolbarInsertScriptAct->setText(tr("Import script"));
    toolbarInsertScriptAct->setToolTip(tr("Import script"));
    toolbarInsertScriptAct->setStatusTip(tr("Import text script"));

    // Status bar labels
    lbSizeName->setText(tr("Size") + QString(":"));
    updateEndiannesLabel();
    setOverwriteMode(hexEdit->overwriteMode());
    updateValuePanels();
    setSelection(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
}


void MainWindow::createActions()
{
    newAct = new QAction(QIcon(":/images/new.png"), tr("New"), this);
    newAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    newAct->setStatusTip(tr("Create a new file"));
    connect(newAct, &QAction::triggered, this, &MainWindow::newFile);
    openAct = new QAction(QIcon(":/images/open.png"), tr("Open file..."), this);
    openAct->setShortcuts(QKeySequence::Open);
    openAct->setStatusTip(tr("Open an existing file"));
    connect(openAct, SIGNAL(triggered()), this, SLOT(open()));

    saveAct = new QAction(QIcon(":/images/save.png"), tr("Save"), this);
    saveAct->setShortcuts(QKeySequence::Save);
    saveAct->setStatusTip(tr("Save file to disk"));
    connect(saveAct, SIGNAL(triggered()), this, SLOT(save()));

    closeAct = new QAction(tr("Close"), this);
    closeAct->setShortcuts(QKeySequence::Close);
    closeAct->setStatusTip(tr("Close the current file"));
    connect(closeAct, &QAction::triggered, this, &MainWindow::closeTab);
    addAction(closeAct); // register shortcut even without being in a menu

    saveAsAct = new QAction(tr("Save As..."), this);
    saveAsAct->setShortcuts(QKeySequence::SaveAs);
    saveAsAct->setStatusTip(tr("Save file under a new name"));
    connect(saveAsAct, SIGNAL(triggered()), this, SLOT(saveAs()));

    saveReadable = new QAction(tr("Save dump..."), this);
    saveReadable->setStatusTip(tr("Save file as dump"));
    connect(saveReadable, SIGNAL(triggered()), this, SLOT(saveToReadableFile()));

    revertAct = new QAction(tr("Revert"), this);
    revertAct->setStatusTip(tr("Revert file to last saved version"));
    connect(revertAct, SIGNAL(triggered()), this, SLOT(revert()));

    exitAct = new QAction(tr("Exit"), this);
    exitAct->setShortcuts(QKeySequence::Quit);
    exitAct->setStatusTip(tr("Exit the application"));
    connect(exitAct, SIGNAL(triggered()), qApp, SLOT(closeAllWindows()));

    newTabAct = new QAction(tr("New Tab"), this);
    newTabAct->setStatusTip(tr("Open a new empty tab"));
    connect(newTabAct, &QAction::triggered, this, &MainWindow::newTab);

    closeTabAct = new QAction(tr("Close Tab"), this);
    closeTabAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    closeTabAct->setStatusTip(tr("Close the current tab"));
    connect(closeTabAct, &QAction::triggered, this, &MainWindow::closeTab);

    nextTabAct = new QAction(tr("Next Tab"), this);
    nextTabAct->setShortcut(QKeySequence::NextChild);
    connect(nextTabAct, &QAction::triggered, this, &MainWindow::nextTab);

    prevTabAct = new QAction(tr("Previous Tab"), this);
    prevTabAct->setShortcut(QKeySequence::PreviousChild);
    connect(prevTabAct, &QAction::triggered, this, &MainWindow::previousTab);

    openProjectAct = new QAction(tr("Open Project"), this);
    openProjectAct->setStatusTip(tr("Open an RTHextion project file"));
    connect(openProjectAct, &QAction::triggered, this, &MainWindow::openProject);

    saveProjectAct = new QAction(tr("Save Project"), this);
    saveProjectAct->setStatusTip(tr("Save the current project"));
    connect(saveProjectAct, &QAction::triggered, this, &MainWindow::saveProject);

    saveProjectAsAct = new QAction(tr("Save Project As..."), this);
    saveProjectAsAct->setStatusTip(tr("Save the project under a new name"));
    connect(saveProjectAsAct, &QAction::triggered, this, &MainWindow::saveProjectAs);

    showChangesAct = new QAction(tr("Show changes"), this);
    showChangesAct->setCheckable(true);
    showChangesAct->setStatusTip(tr("Highlight bytes changed compared to project originals"));
    connect(showChangesAct, &QAction::triggered, this, &MainWindow::toggleShowChanges);

    createIpsPatchAct = new QAction(tr("Create IPS patch..."), this);
    createIpsPatchAct->setStatusTip(tr("Save current changes as an IPS patch file"));
    createIpsPatchAct->setEnabled(false);
    connect(createIpsPatchAct, &QAction::triggered, this, &MainWindow::createIpsPatch);

    loadIpsPatchAct = new QAction(tr("Load IPS patch..."), this);
    loadIpsPatchAct->setStatusTip(tr("Load an IPS patch and apply it as changes"));
    connect(loadIpsPatchAct, &QAction::triggered, this, &MainWindow::loadIpsPatch);

    loadOriginalAct = new QAction(tr("Load original..."), this);
    loadOriginalAct->setStatusTip(tr("Load the original (unmodified) file to compute changes"));
    loadOriginalAct->setEnabled(false);
    connect(loadOriginalAct, &QAction::triggered, this, &MainWindow::loadOriginal);

    undoAct = new QAction(QIcon(":/images/undo.png"), tr("Undo"), this);
    undoAct->setShortcuts(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, this, [this]{ if (hexEdit) hexEdit->undo(); });

    redoAct = new QAction(QIcon(":/images/redo.png"), tr("Redo"), this);
    redoAct->setShortcuts(QKeySequence::Redo);
    connect(redoAct, &QAction::triggered, this, [this]{ if (hexEdit) hexEdit->redo(); });

    copyAddressAct = new QAction(tr("Copy address"), this);
    connect(copyAddressAct, &QAction::triggered, this, [this]()
    {
        qint64 bytePos = hexEdit->cursorPosition() / 2;
        const QString addrText = QStringLiteral("0x") + QString("%1").arg(bytePos, 8, 16, QChar('0')).toUpper();
        QApplication::clipboard()->setText(addrText);
    });

    cutAct = new QAction(tr("Cut"), this);
    cutAct->setShortcut(QKeySequence::Cut);
    cutAct->setEnabled(false);
    connect(cutAct, &QAction::triggered, this, [this]()
    {
        const qint64 selBegin = hexEdit->getSelectionBegin();
        const qint64 selEnd = hexEdit->getSelectionEnd();
        if (selBegin >= selEnd) return;
        const QByteArray raw = hexEdit->dataAt(selBegin, selEnd - selBegin);
        
        if (hexEdit->editAreaIsAscii())
            QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
        else
            QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
        
        if (!hexEdit->overwriteMode())
            hexEdit->remove(selBegin, selEnd - selBegin);
        hexEdit->setCursorPosition(2 * selBegin);
    });

    copyAct = new QAction(tr("Copy"), this);
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setEnabled(false);
    connect(copyAct, &QAction::triggered, this, [this]()
    {
        const qint64 selBegin = hexEdit->getSelectionBegin();
        const qint64 selEnd = hexEdit->getSelectionEnd();
        if (selBegin >= selEnd) return;
        const QByteArray raw = hexEdit->dataAt(selBegin, selEnd - selBegin);
        const qint64 selLast = qMax<qint64>(selBegin, selEnd - 1);

        const bool copyDisasm = hexEdit->showDisasm()
            || (m_sectionModel && (m_sectionModel->displayModeAtOffset(selBegin) == SectionDisplay_Disasm
                || m_sectionModel->displayModeAtOffset(selLast) == SectionDisplay_Disasm));

        if (copyDisasm)
        {
            const QString disasmText = hexEdit->selectedDisasmText();
            if (!disasmText.isEmpty()) {
                QApplication::clipboard()->setText(disasmText);
                return;
            }
            QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
        }
        else if (hexEdit->editAreaIsAscii())
            QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
        else
            QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
    });

    pasteAct = new QAction(tr("Paste"), this);
    pasteAct->setShortcut(QKeySequence::Paste);
    pasteAct->setEnabled(false);
    connect(pasteAct, &QAction::triggered, this, [this]()
    {
        if (!hexEdit || hexEdit->isReadOnly() || hexEdit->showOriginal())
            return;

        const QString clipText = QApplication::clipboard()->text();
        if (clipText.isEmpty()) return;

        QByteArray ba;
        if (hexEdit->editAreaIsAscii())
            ba = hexEdit->encodeTextForCurrentEncoding(clipText);
        else
        {
            QString stripped = clipText;
            stripped.remove(' ').remove('\t').remove('\n').remove('\r');
            ba = QByteArray::fromHex(stripped.toLatin1());
        }

        if (ba.isEmpty()) return;

        const qint64 selBegin = hexEdit->getSelectionBegin();
        const qint64 selEnd   = hexEdit->getSelectionEnd();
        const bool hasSelection = (selEnd - selBegin > 1);

        if (hexEdit->overwriteMode())
        {
            if (hasSelection)
            {
                // REPLACE with selection: truncate paste to selection size, paste at selection beginning
                const qint64 selLen = selEnd - selBegin;
                ba = ba.left(static_cast<int>(selLen));
                hexEdit->replace(selBegin, ba.size(), ba);
                hexEdit->setCursorPosition(2 * (selBegin + ba.size()));
            }
            else
            {
                const qint64 pos = hexEdit->cursorPosition() / 2;
                ba = ba.left(static_cast<int>(std::min<qint64>(ba.size(), hexEdit->dataSize() - pos)));
                hexEdit->replace(pos, ba.size(), ba);
                hexEdit->setCursorPosition(2 * (pos + ba.size()));
            }
        }
        else
        {
            if (hasSelection)
            {
                // INSERT with selection: delete entire selection, then insert paste at selection beginning
                const qint64 selLen = selEnd - selBegin;
                hexEdit->remove(selBegin, static_cast<int>(selLen));
                hexEdit->insert(selBegin, ba);
                hexEdit->setCursorPosition(2 * (selBegin + ba.size()));
            }
            else
            {
                const qint64 pos = hexEdit->cursorPosition() / 2;
                hexEdit->insert(pos, ba);
                hexEdit->setCursorPosition(2 * (pos + ba.size()));
            }
        }
    });

    saveSelectionReadable = new QAction(tr("Save Selection Dump..."), this);
    saveSelectionReadable->setStatusTip(tr("Save selection as dump"));
    saveSelectionReadable->setEnabled(false);
    connect(saveSelectionReadable, SIGNAL(triggered()), this, SLOT(saveSelectionToReadableFile()));

    loadTableAct = new QAction(tr("Import"), this);
    loadTableAct->setStatusTip(tr("Import table"));
    connect(loadTableAct, SIGNAL(triggered()), this, SLOT(loadTable()));

    useTableAct = new QAction(tr("Use table"), this);
    useTableAct->setCheckable(true);
    useTableAct->setChecked(true);
    useTableAct->setDisabled(true);
    useTableAct->setStatusTip(tr("Use translation table"));
    connect(useTableAct, SIGNAL(triggered()), this, SLOT(switchUseTable()));
    connect(useTableAct, &QAction::toggled, this, [this](bool checked) {
        if (m_tablesDock) m_tablesDock->setUseTableChecked(checked);
    });
    connect(useTableAct, &QAction::changed, this, [this]() {
        if (m_tablesDock) m_tablesDock->setUseTableEnabled(useTableAct->isEnabled());
    });

    editTableAct = new QAction(tr("Edit table"), this);
    editTableAct->setDisabled(true);
    editTableAct->setStatusTip(tr("Edit translation table"));
    connect(editTableAct, SIGNAL(triggered()), this, SLOT(editTable()));

    createEmptyTableAct = new QAction(tr("Empty table"), this);
    createEmptyTableAct->setStatusTip(tr("Create a new empty translation table"));
    connect(createEmptyTableAct, &QAction::triggered, this, &MainWindow::createEmptyTable);

    semiAutoTableAct = new QAction(tr("Semi-auto generated"), this);
    semiAutoTableAct->setStatusTip(tr("Generate table by searching for a known text"));
    connect(semiAutoTableAct, &QAction::triggered, this, &MainWindow::showSemiAutoTableDialog);

    saveTableAct = new QAction(tr("Save table"), this);
    saveTableAct->setDisabled(true);
    saveTableAct->setStatusTip(tr("Save translation table"));
    connect(saveTableAct, &QAction::triggered, this, &MainWindow::saveTable);

    saveTableAsAct = new QAction(tr("Save table as..."), this);
    saveTableAsAct->setDisabled(true);
    saveTableAsAct->setStatusTip(tr("Save translation table to a new file"));
    connect(saveTableAsAct, &QAction::triggered, this, &MainWindow::saveTableAs);

    dumpScriptAct = new QAction(QIcon(":/images/dump.png"), tr("Edit script"), this);
    dumpScriptAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    dumpScriptAct->setStatusTip(tr("Edit text script"));
    dumpScriptAct->setEnabled(false);
    connect(dumpScriptAct, SIGNAL(triggered()), this, SLOT(dumpScript()));

    insertScriptAct = new QAction(QIcon(":/images/insert_script.png"), tr("Import script"), this);
    insertScriptAct->setStatusTip(tr("Import text script"));
    insertScriptAct->setEnabled(true);
    connect(insertScriptAct, SIGNAL(triggered()), this, SLOT(insertScript()));

    findPointersAct = new QAction(QIcon(":/images/find_ptr.png"), tr("Find pointers"), this);
    findPointersAct->setShortcuts(QKeySequence::New);
    findPointersAct->setStatusTip(tr("Find pointers for selected text"));
    connect(findPointersAct, SIGNAL(triggered()), this, SLOT(showPointersDialog()));

    showPointersAct = new QAction(tr("Show pointers"), this);
    showPointersAct->setEnabled(false);
    showPointersAct->setCheckable(true);
    showPointersAct->setChecked(true);
    connect(showPointersAct, SIGNAL(triggered()), this, SLOT(switchShowPointers()));

    aboutAct = new QAction(tr("About %1").arg(AppInfo::Name), this);
    aboutAct->setStatusTip(tr("Show the application's About box"));
    aboutAct->setMenuRole(QAction::NoRole); // prevent macOS from auto-moving to Apple menu
    connect(aboutAct, SIGNAL(triggered()), this, SLOT(about()));

    checkUpdatesAct = new QAction(tr("Check for Updates..."), this);
    checkUpdatesAct->setMenuRole(QAction::NoRole);
    connect(checkUpdatesAct, &QAction::triggered, this, &MainWindow::checkForUpdates);

    findAct = new QAction(QIcon(":/images/find.png"), tr("Find/Replace"), this);
    findAct->setShortcuts(QKeySequence::Find);
    findAct->setStatusTip(tr("Show the dialog for finding and replacing"));
    connect(findAct, SIGNAL(triggered()), this, SLOT(showSearchDialog()));

    findNextAct = new QAction(tr("Find next"), this);
    findNextAct->setStatusTip(tr("Find next occurrence of the searched pattern"));
    connect(findNextAct, SIGNAL(triggered()), this, SLOT(findNext()));

    virtualFormatAct = new QAction(tr("Virtually format") + "...", this);
    connect(virtualFormatAct, &QAction::triggered, this, [this]() { showVirtualFormatDialog(); });

    removeVirtualFormattingAct = new QAction(tr("Remove virtual formatting"), this);
    removeVirtualFormattingAct->setEnabled(false);
    connect(removeVirtualFormattingAct, &QAction::triggered, this, [this]() { removeVirtualFormatting(); });

    gotoAct = new QAction(tr("Jump to offset") + QString("..."), this);
    gotoAct->setShortcut(QKeySequence::FindNext); // ctrl/cmd + G
    gotoAct->setStatusTip(tr("Go to specified offset"));
    connect(gotoAct, SIGNAL(triggered()), this, SLOT(showJumpToDialog()));

    previousPositionAct = new QAction(tr("Previous position"), this);
    previousPositionAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_BracketLeft),
                                       QKeySequence(Qt::META | Qt::Key_BracketLeft)});
    previousPositionAct->setStatusTip(tr("Go to previous cursor position"));
    connect(previousPositionAct, &QAction::triggered, this, &MainWindow::goToPreviousPosition);

    nextPositionAct = new QAction(tr("Next position"), this);
    nextPositionAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_BracketRight),
                                   QKeySequence(Qt::META | Qt::Key_BracketRight)});
    nextPositionAct->setStatusTip(tr("Go to next cursor position"));
    connect(nextPositionAct, &QAction::triggered, this, &MainWindow::goToNextPosition);

    firstPositionAct = new QAction(tr("First position"), this);
    firstPositionAct->setStatusTip(tr("Go to first cursor position in history"));
    connect(firstPositionAct, &QAction::triggered, this, &MainWindow::goToFirstPosition);

    lastPositionAct = new QAction(tr("Last position"), this);
    firstPositionAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Home),
                                    QKeySequence(Qt::META | Qt::Key_Home)});
    lastPositionAct->setStatusTip(tr("Go to last cursor position in history"));
    connect(lastPositionAct, &QAction::triggered, this, &MainWindow::goToLastPosition);

    toFileBeginningAct = new QAction(tr("To file beginning"), this);
    lastPositionAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_End),
                                   QKeySequence(Qt::META | Qt::Key_End)});
    toFileBeginningAct->setStatusTip(tr("Go to beginning of file"));
    connect(toFileBeginningAct, &QAction::triggered, this, &MainWindow::goToFileBeginning);

    toFileEndAct = new QAction(tr("To file end"), this);
    toFileBeginningAct->setShortcut(QKeySequence(Qt::Key_Home));
    toFileEndAct->setStatusTip(tr("Go to end of file"));
    connect(toFileEndAct, &QAction::triggered, this, &MainWindow::goToFileEnd);

    optionsAct = new QAction(tr("Preferences"), this);
    toFileEndAct->setShortcut(QKeySequence(Qt::Key_End));
    optionsAct->setStatusTip(tr("Show the application options dialog"));
    optionsAct->setMenuRole(QAction::PreferencesRole); // macOS: moves to app menu automatically
    optionsAct->setShortcut(QKeySequence::Preferences);
    connect(optionsAct, SIGNAL(triggered()), this, SLOT(showOptionsDialog()));

    languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);

    langRussianAct = new QAction(QStringLiteral("Русский"), this);
    langRussianAct->setCheckable(true);
    langRussianAct->setData("ru");
    connect(langRussianAct, SIGNAL(triggered()), this, SLOT(setLanguage()));

    langEnglishAct = new QAction(QStringLiteral("English"), this);
    langEnglishAct->setCheckable(true);
    langEnglishAct->setData("en");
    connect(langEnglishAct, SIGNAL(triggered()), this, SLOT(setLanguage()));

    langFrenchAct = new QAction(QStringLiteral("Français"), this);
    langFrenchAct->setCheckable(true);
    langFrenchAct->setData("fr");
    connect(langFrenchAct, SIGNAL(triggered()), this, SLOT(setLanguage()));

    langGermanAct = new QAction(QStringLiteral("Deutsch"), this);
    langGermanAct->setCheckable(true);
    langGermanAct->setData("de");
    connect(langGermanAct, SIGNAL(triggered()), this, SLOT(setLanguage()));

    langSpanishAct = new QAction(QStringLiteral("Español"), this);
    langSpanishAct->setCheckable(true);
    langSpanishAct->setData("es");
    connect(langSpanishAct, SIGNAL(triggered()), this, SLOT(setLanguage()));

    langPortugueseAct = new QAction(QStringLiteral("Português"), this);
    langPortugueseAct->setCheckable(true);
    langPortugueseAct->setData("pt");
    connect(langPortugueseAct, SIGNAL(triggered()), this, SLOT(setLanguage()));

    langJapaneseAct = new QAction(QStringLiteral("日本語"), this);
    langJapaneseAct->setCheckable(true);
    langJapaneseAct->setData("ja");
    connect(langJapaneseAct, SIGNAL(triggered()), this, SLOT(setLanguage()));

    langChineseSimplifiedAct = new QAction(QStringLiteral("简体中文"), this);
    langChineseSimplifiedAct->setCheckable(true);
    langChineseSimplifiedAct->setData("zh_CN");
    connect(langChineseSimplifiedAct, SIGNAL(triggered()), this, SLOT(setLanguage()));

    languageGroup->addAction(langRussianAct);
    languageGroup->addAction(langEnglishAct);
    languageGroup->addAction(langFrenchAct);
    languageGroup->addAction(langGermanAct);
    languageGroup->addAction(langSpanishAct);
    languageGroup->addAction(langPortugueseAct);
    languageGroup->addAction(langJapaneseAct);
    languageGroup->addAction(langChineseSimplifiedAct);

    auto &settings = AppSettings::instance();
    
    // Detect system language on first run if Language setting doesn't exist
    QString language;
    if (!settings.contains("Language"))
    {
        // First-time launch: detect system language
        language = detectSystemLanguage();
        settings.setValue("Language", language);
    }
    else
    {
        // Use saved language preference
        language = settings.value("Language", QStringLiteral("en")).toString();
    }
    
    const QString languageShort = language.left(2);

    if (language == "de" || languageShort == "de")
        langGermanAct->setChecked(true);
    else if (language == "ru" || languageShort == "ru")
        langRussianAct->setChecked(true);
    else if (language == "fr" || languageShort == "fr")
        langFrenchAct->setChecked(true);
    else if (language == "es" || languageShort == "es")
        langSpanishAct->setChecked(true);
    else if (language == "pt" || languageShort == "pt")
        langPortugueseAct->setChecked(true);
    else if (language == "ja" || languageShort == "ja")
        langJapaneseAct->setChecked(true);
    else if (language == "zh_CN" || language.startsWith("zh_") || languageShort == "zh")
        langChineseSimplifiedAct->setChecked(true);
    else
        langEnglishAct->setChecked(true);

    showStatusEndianAct = new QAction(tr("Endianness"), this);
    showStatusEndianAct->setCheckable(true);
    showStatusEndianAct->setChecked(true);

    showStatusByteAct = new QAction(tr("Byte"), this);
    showStatusByteAct->setCheckable(true);
    showStatusByteAct->setChecked(true);

    showStatusWordAct = new QAction(tr("Word"), this);
    showStatusWordAct->setCheckable(true);
    showStatusWordAct->setChecked(true);

    showStatusDwordAct = new QAction(tr("DWord"), this);
    showStatusDwordAct->setCheckable(true);
    showStatusDwordAct->setChecked(true);

    showStatusSelectionAct = new QAction(tr("Selection"), this);
    showStatusSelectionAct->setCheckable(true);
    showStatusSelectionAct->setChecked(true);

    showStatusAddressAct = new QAction(tr("Address"), this);
    showStatusAddressAct->setCheckable(true);
    showStatusAddressAct->setChecked(true);

    showStatusSizeAct = new QAction(tr("Size"), this);
    showStatusSizeAct->setCheckable(true);
    showStatusSizeAct->setChecked(true);

    showStatusModeAct = new QAction(tr("Mode"), this);
    showStatusModeAct->setCheckable(true);
    showStatusModeAct->setChecked(true);

    showStatusEncodingAct = new QAction(tr("Encoding"), this);
    showStatusEncodingAct->setCheckable(true);
    showStatusEncodingAct->setChecked(true);

    showSignedValuesAct = new QAction(tr("Show signed values"), this);
    showSignedValuesAct->setCheckable(true);
    showSignedValuesAct->setChecked(false);

    showAddressAreaAct = new QAction(tr("Address area"), this);
    showAddressAreaAct->setCheckable(true);
    showAddressAreaAct->setChecked(true);

    // ASCII area panel mode submenu actions
    asciiAreaMenu = new QMenu(tr("Data area"), this);
    asciiAreaGroup = new QActionGroup(this);
    asciiAreaGroup->setExclusive(true);

    panelModeTextAct = new QAction(tr("Text"), this);
    panelModeTextAct->setCheckable(true);
    panelModeTextAct->setChecked(true);
    asciiAreaGroup->addAction(panelModeTextAct);
    asciiAreaMenu->addAction(panelModeTextAct);

    panelModeGraphicsAct = new QAction(tr("Graphics"), this);
    panelModeGraphicsAct->setCheckable(true);
    asciiAreaGroup->addAction(panelModeGraphicsAct);
    asciiAreaMenu->addAction(panelModeGraphicsAct);

    panelModeDisasmAct = new QAction(tr("Disassembly"), this);
    panelModeDisasmAct->setCheckable(true);
    asciiAreaGroup->addAction(panelModeDisasmAct);
    asciiAreaMenu->addAction(panelModeDisasmAct);

    showAddressGridAct = new QAction(tr("Show grid"), this);
    showAddressGridAct->setCheckable(true);
    showAddressGridAct->setChecked(true);

    showDarkThemeAct = new QAction(tr("Dark theme"), this);
    showDarkThemeAct->setCheckable(true);
    showDarkThemeAct->setChecked(false);
    connect(showDarkThemeAct, &QAction::toggled, this, &MainWindow::toggleDarkTheme);

    connect(showStatusEndianAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showStatusByteAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showStatusWordAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showStatusDwordAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showStatusSelectionAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showStatusAddressAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showStatusSizeAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showStatusModeAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showStatusEncodingAct, &QAction::toggled, this, [this](bool)
            { updateStatusBarVisibility(); });
    connect(showSignedValuesAct, &QAction::toggled, this, [this](bool)
            { updateValuePanels(); });
    connect(showAddressAreaAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setAddressArea(checked); });
    connect(asciiAreaGroup, &QActionGroup::triggered, this, [this](QAction *act)
            {
                auto &s = AppSettings::instance();
                if (act == panelModeTextAct) {
                    hexEdit->setShowDisasm(false);
                    hexEdit->setShowGraphicsPanel(false);
                    hexEdit->setAsciiArea(true);
                    s.setValue("PanelMode", QStringLiteral("text"));
                } else if (act == panelModeGraphicsAct) {
                    hexEdit->setShowDisasm(false);
                    hexEdit->setShowGraphicsPanel(true);
                    hexEdit->setAsciiArea(true);
                    s.setValue("PanelMode", QStringLiteral("graphics"));
                    if (m_graphicsDock)
                        m_graphicsDock->show();
                } else if (act == panelModeDisasmAct) {
                    hexEdit->setShowGraphicsPanel(false);
                    hexEdit->setAsciiArea(true);
                    hexEdit->setShowDisasm(true);
                    s.setValue("PanelMode", QStringLiteral("disasm"));
                }
            });
    connect(showAddressGridAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setShowHexGrid(checked); });

    showMapPointersAct = new QAction(tr("Changes"), this);
    showMapPointersAct->setCheckable(true);
    showMapPointersAct->setChecked(true);
    showMapPointersAct->setStatusTip(tr("Show changed byte locations on the side map"));
    connect(showMapPointersAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setScrollMapChangesVisible(checked); });

    showMapTargetsAct = new QAction(tr("Pointers/Data"), this);
    showMapTargetsAct->setCheckable(true);
    showMapTargetsAct->setChecked(true);
    showMapTargetsAct->setStatusTip(tr("Show pointer and target locations on the side map"));
    connect(showMapTargetsAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setScrollMapTargetVisible(checked); });

    restoreDockLayoutAct = new QAction(tr("Restore"), this);
    restoreDockLayoutAct->setStatusTip(tr("Restore default dock layout"));
    connect(restoreDockLayoutAct, &QAction::triggered, this, &MainWindow::restoreDockLayout);

    collapseLeftDockAreaAct = new QAction(tr("Collapse left panel"), this);
    collapseLeftDockAreaAct->setCheckable(true);
    connect(collapseLeftDockAreaAct, &QAction::toggled, this, [this](bool checked) {
        setDockAreaCollapsed(Qt::LeftDockWidgetArea, checked);
    });

    collapseRightDockAreaAct = new QAction(tr("Collapse right panel"), this);
    collapseRightDockAreaAct->setCheckable(true);
    connect(collapseRightDockAreaAct, &QAction::toggled, this, [this](bool checked) {
        setDockAreaCollapsed(Qt::RightDockWidgetArea, checked);
    });

    collapseBottomDockAreaAct = new QAction(tr("Collapse bottom panel"), this);
    collapseBottomDockAreaAct->setCheckable(true);
    connect(collapseBottomDockAreaAct, &QAction::toggled, this, [this](bool checked) {
        setDockAreaCollapsed(Qt::BottomDockWidgetArea, checked);
    });

    updateNavigationActions();

}


void MainWindow::createMenus()
{

    fileMenu = menuBar()->addMenu(tr("File"));

    fileMenu->addAction(newAct);
    fileMenu->addAction(openAct);
    fileMenu->addAction(openProjectAct);
    fileMenu->addSeparator();
    recentProjectMenu = fileMenu->addMenu(tr("Recent projects"));
    recentProjectMenu->setEnabled(false);
    recentFileMenu = fileMenu->addMenu(tr("Recent files"));
    recentFileMenu->setEnabled(false);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAct);
    fileMenu->addAction(saveAsAct);
    fileMenu->addAction(saveReadable);
    fileMenu->addSeparator();
    fileMenu->addAction(saveProjectAct);
    fileMenu->addAction(saveProjectAsAct);
    fileMenu->addSeparator();
    // ROM type submenu
    romTypeMenu = fileMenu->addMenu(tr("ROM type"));
    romTypeMenuGroup = new QActionGroup(this);
    romTypeMenuGroup->setExclusive(true);
    {
        QAction *act = romTypeMenu->addAction(tr("Unknown"));
        act->setCheckable(true);
        act->setChecked(true);
        act->setData(0);
        romTypeMenuGroup->addAction(act);
        for (int i = 1; i < kRomTypeCount; ++i) {
            act = romTypeMenu->addAction(tr(romTypeName(static_cast<RomType>(i))));
            act->setCheckable(true);
            act->setData(i);
            romTypeMenuGroup->addAction(act);
        }
    }
    connect(romTypeMenuGroup, &QActionGroup::triggered, this, &MainWindow::onMenuRomTypeTriggered);

    // Encoding submenu
    encodingMenu = fileMenu->addMenu(tr("Encoding"));
    encodingGroup = new QActionGroup(this);
    encodingGroup->setExclusive(true);
    {
        auto addEnc = [&](const char *name, bool isFirst = false) {
            QAction *act = encodingMenu->addAction(QString::fromLatin1(name));
            act->setCheckable(true);
            act->setData(QString::fromLatin1(name));
            if (isFirst) act->setChecked(true);
            encodingGroup->addAction(act);
        };
        // Unicode
        addEnc("ASCII", true);
        addEnc("UTF-8");
        addEnc("UTF-16 LE");
        addEnc("UTF-16 BE");
        addEnc("UTF-32 LE");
        addEnc("UTF-32 BE");
        // Asian
        encodingMenu->addSeparator();
        addEnc("Shift-JIS");
        addEnc("EUC-JP");
        addEnc("ISO-2022-JP");
        addEnc("GB2312");
        addEnc("GBK");
        addEnc("GB18030");
        addEnc("EUC-KR");
        // Cyrillic
        encodingMenu->addSeparator();
        addEnc("Windows-1251");
        addEnc("KOI8-R");
        addEnc("KOI8-U");
        addEnc("CP-866");
        addEnc("Mac Cyrillic");
        addEnc("ISO-8859-5");
        // West/Central European
        encodingMenu->addSeparator();
        addEnc("ISO-8859-1");
        addEnc("ISO-8859-2");
        addEnc("ISO-8859-3");
        addEnc("ISO-8859-4");
        addEnc("ISO-8859-7");
        addEnc("ISO-8859-9");
        addEnc("ISO-8859-10");
        addEnc("ISO-8859-13");
        addEnc("ISO-8859-14");
        addEnc("ISO-8859-15");
        addEnc("ISO-8859-16");
        addEnc("Windows-1252");
        // Middle East / Other
        encodingMenu->addSeparator();
        addEnc("ISO-8859-6");
        addEnc("ISO-8859-8");
        addEnc("ISO-8859-11");
    }
    connect(encodingGroup, &QActionGroup::triggered, this, &MainWindow::onEncodingTriggered);

    fileMenu->addSeparator();
    fileMenu->addAction(revertAct);
    fileMenu->addSeparator();
    fileMenu->addAction(optionsAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAct);

    editMenu = menuBar()->addMenu(tr("Edit"));
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);
    editMenu->addSeparator();
    editMenu->addAction(copyAddressAct);
    editMenu->addAction(cutAct);
    editMenu->addAction(copyAct);
    editMenu->addAction(pasteAct);
    editMenu->addSeparator();
    editMenu->addAction(saveSelectionReadable);
    editMenu->addSeparator();
    editMenu->addAction(findAct);
    editMenu->addAction(findNextAct);
    editMenu->addSeparator();
    editMenu->addAction(virtualFormatAct);
    editMenu->addAction(removeVirtualFormattingAct);

    changesMenu = menuBar()->addMenu(tr("Changes"));
    changesMenu->addAction(showChangesAct);
    changesMenu->addSeparator();
    changesMenu->addAction(loadOriginalAct);
    changesMenu->addSeparator();
    changesMenu->addAction(createIpsPatchAct);
    changesMenu->addAction(loadIpsPatchAct);

    goMenu = menuBar()->addMenu(tr("Go"));
    goMenu->addAction(gotoAct);
    goMenu->addSeparator();
    goMenu->addAction(previousPositionAct);
    goMenu->addAction(nextPositionAct);
    goMenu->addAction(firstPositionAct);
    goMenu->addAction(lastPositionAct);
    goMenu->addSeparator();
    goMenu->addAction(toFileBeginningAct);
    goMenu->addAction(toFileEndAct);
    goMenu->addSeparator();
    goMenu->addAction(nextTabAct);
    goMenu->addAction(prevTabAct);

    tableMenu = menuBar()->addMenu(tr("Table"));
    tableMenu->addAction(loadTableAct);
    recentTableMenu = tableMenu->addMenu(tr("Recent"));
    recentTableMenu->setEnabled(false);
    createTableMenu = tableMenu->addMenu(tr("Create table"));
    createTableMenu->addAction(createEmptyTableAct);
    createTableMenu->addAction(semiAutoTableAct);
    tableMenu->addSeparator();
    tableMenu->addAction(editTableAct);
    tableMenu->addAction(saveTableAct);
    tableMenu->addAction(saveTableAsAct);
    tableMenu->addSeparator();
    tableMenu->addAction(useTableAct);

    scriptMenu = menuBar()->addMenu(tr("Script"));
    scriptMenu->setEnabled(true);
    scriptMenu->addAction(dumpScriptAct);
    scriptMenu->addAction(insertScriptAct);

    pointersMenu = menuBar()->addMenu(tr("Pointers"));
    pointersMenu->addAction(findPointersAct);
    pointersMenu->addAction(showPointersAct);

    viewMenu = menuBar()->addMenu(tr("View"));

    panelsMenu = viewMenu->addMenu(tr("Panels"));
    panelsMenu->addAction(showAddressAreaAct);
    panelsMenu->addMenu(asciiAreaMenu);
    panelsMenu->addSeparator();
    showStatusBarAct = panelsMenu->addAction(tr("Status bar"));
    showStatusBarAct->setCheckable(true);
    showStatusBarAct->setChecked(true);
    connect(showStatusBarAct, &QAction::toggled, statusBar(), &QStatusBar::setVisible);

    mapsMenu = viewMenu->addMenu(tr("Maps"));
    mapsMenu->addAction(showMapPointersAct);
    mapsMenu->addAction(showMapTargetsAct);

    toolbarMenu = viewMenu->addMenu(tr("Toolbar"));

    QAction *fileToolbarAct = fileToolBar->toggleViewAction();
    QAction *actionsToolbarAct = editToolBar->toggleViewAction();
    QAction *searchToolbarAct = searchToolBar->toggleViewAction();
    QAction *navigationToolbarAct = navigationToolBar->toggleViewAction();
    QAction *scriptToolbarAct = scriptToolBar->toggleViewAction();
    QAction *profileToolbarAct = profileToolBar->toggleViewAction();
    fileToolbarAct->setText(tr("File"));
    actionsToolbarAct->setText(tr("Actions"));
    searchToolbarAct->setText(tr("Search"));
    navigationToolbarAct->setText(tr("Navigation"));
    scriptToolbarAct->setText(tr("Script"));
    profileToolbarAct->setText(tr("Profile"));
    toolbarMenu->addAction(fileToolbarAct);
    toolbarMenu->addAction(actionsToolbarAct);
    toolbarMenu->addAction(searchToolbarAct);
    toolbarMenu->addAction(navigationToolbarAct);
    toolbarMenu->addAction(scriptToolbarAct);
    toolbarMenu->addAction(profileToolbarAct);
    toolbarMenu->addSeparator();
    resetToolbarsAct = toolbarMenu->addAction(tr("Reset"));

    connect(resetToolbarsAct, &QAction::triggered, this, [this]()
            {
        if (!defaultWindowState.isEmpty())
            restoreState(defaultWindowState);
        fileToolBar->show();
        editToolBar->show();
        searchToolBar->show();
        navigationToolBar->show();
        scriptToolBar->show();
        profileToolBar->show(); });

    statusBarMenu = viewMenu->addMenu(tr("Status bar"));
    statusBarMenu->addAction(showStatusByteAct);
    statusBarMenu->addAction(showStatusWordAct);
    statusBarMenu->addAction(showStatusDwordAct);
    statusBarMenu->addAction(showStatusSelectionAct);
    statusBarMenu->addAction(showStatusAddressAct);
    statusBarMenu->addAction(showStatusSizeAct);
    statusBarMenu->addAction(showStatusModeAct);
    statusBarMenu->addAction(showStatusEncodingAct);
    statusBarMenu->addSeparator();
    statusBarMenu->addAction(showSignedValuesAct);

    languageMenu = viewMenu->addMenu(tr("Language"));
    languageMenu->addAction(langRussianAct);
    languageMenu->addAction(langEnglishAct);
    languageMenu->addAction(langFrenchAct);
    languageMenu->addAction(langGermanAct);
    languageMenu->addAction(langSpanishAct);
    languageMenu->addAction(langPortugueseAct);
    languageMenu->addAction(langJapaneseAct);
    languageMenu->addAction(langChineseSimplifiedAct);

    // Dock submenu
    dockMenu = viewMenu->addMenu(tr("Dock"));
    
    // Store references to dock toggle actions.
    // Set fixed text and override QDockWidget's auto-sync with windowTitle
    // (which may include counts like "Pointers – 42").
    sectionsDockToggleAct = m_sectionsDock->toggleViewAction();
    sectionsDockToggleAct->setText(tr("Sections"));
    dockMenu->addAction(sectionsDockToggleAct);
    connect(m_sectionsDock, &QDockWidget::windowTitleChanged, this, [this]() {
        if (sectionsDockToggleAct) sectionsDockToggleAct->setText(tr("Sections"));
    });

    tablesDockToggleAct = m_tablesDock->toggleViewAction();
    tablesDockToggleAct->setText(tr("Tables"));
    dockMenu->addAction(tablesDockToggleAct);
    connect(m_tablesDock, &QDockWidget::windowTitleChanged, this, [this]() {
        if (tablesDockToggleAct) tablesDockToggleAct->setText(tr("Tables"));
    });
    
    pointersDockToggleAct = m_pointersDock->toggleViewAction();
    pointersDockToggleAct->setText(tr("Pointers"));
    dockMenu->addAction(pointersDockToggleAct);
    connect(m_pointersDock, &QDockWidget::windowTitleChanged, this, [this]() {
        if (pointersDockToggleAct) pointersDockToggleAct->setText(tr("Pointers"));
    });
    
    changesDockToggleAct = m_changesDock->toggleViewAction();
    changesDockToggleAct->setText(tr("Changes"));
    dockMenu->addAction(changesDockToggleAct);
    connect(m_changesDock, &QDockWidget::windowTitleChanged, this, [this]() {
        if (changesDockToggleAct) changesDockToggleAct->setText(tr("Changes"));
    });

    audioDockToggleAct = m_audioDock->toggleViewAction();
    audioDockToggleAct->setText(tr("Audio"));
    dockMenu->addAction(audioDockToggleAct);
    connect(m_audioDock, &QDockWidget::windowTitleChanged, this, [this]() {
        if (audioDockToggleAct) audioDockToggleAct->setText(tr("Audio"));
    });

    graphicsDockToggleAct = m_graphicsDock->toggleViewAction();
    graphicsDockToggleAct->setText(tr("Graphics"));
    dockMenu->addAction(graphicsDockToggleAct);
    connect(m_graphicsDock, &QDockWidget::windowTitleChanged, this, [this]() {
        if (graphicsDockToggleAct) graphicsDockToggleAct->setText(tr("Graphics"));
    });

    dockMenu->addSeparator();

    QAction *collapseAllDocksAct = dockMenu->addAction(tr("Collapse all"));
    connect(collapseAllDocksAct, &QAction::triggered, this, [this]() {
        setDockAreaCollapsed(Qt::LeftDockWidgetArea, true);
        setDockAreaCollapsed(Qt::RightDockWidgetArea, true);
        setDockAreaCollapsed(Qt::BottomDockWidgetArea, true);
    });

    QAction *expandAllDocksAct = dockMenu->addAction(tr("Expand all"));
    connect(expandAllDocksAct, &QAction::triggered, this, [this]() {
        setDockAreaCollapsed(Qt::LeftDockWidgetArea, false);
        setDockAreaCollapsed(Qt::RightDockWidgetArea, false);
        setDockAreaCollapsed(Qt::BottomDockWidgetArea, false);
    });

    dockMenu->addSeparator();
    dockMenu->addAction(restoreDockLayoutAct);
    updateDockAreaActions();

    viewMenu->addSeparator();

    helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(checkUpdatesAct);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAct);
}


void MainWindow::createStatusBar()
{
    // Encoding label (leftmost permanent widget, left of value panels)
    lbEncoding = new QLabel();
    lbEncoding->setFrameShape(QFrame::Panel);
    lbEncoding->setFrameShadow(QFrame::Sunken);
    lbEncoding->setMinimumWidth(90);
    lbEncoding->setAlignment(Qt::AlignCenter);
    lbEncoding->setText(m_currentEncoding);
    lbEncoding->setCursor(Qt::PointingHandCursor);
    lbEncoding->setToolTip(tr("Select current encoding:"));
    lbEncoding->installEventFilter(this);
    statusBar()->addPermanentWidget(lbEncoding);

    // Value labels (separate panels)
    lbValueByte = new QLabel();
    lbValueByte->setFrameShape(QFrame::Panel);
    lbValueByte->setFrameShadow(QFrame::Sunken);
    lbValueByte->setMinimumWidth(45);
    statusBar()->addPermanentWidget(lbValueByte);

    lbValueWord = new QLabel();
    lbValueWord->setFrameShape(QFrame::Panel);
    lbValueWord->setFrameShadow(QFrame::Sunken);
    lbValueWord->setMinimumWidth(60);
    statusBar()->addPermanentWidget(lbValueWord);

    lbValueDword = new QLabel();
    lbValueDword->setFrameShape(QFrame::Panel);
    lbValueDword->setFrameShadow(QFrame::Sunken);
    lbValueDword->setMinimumWidth(98);
    statusBar()->addPermanentWidget(lbValueDword);

    // Selection label
    lbSelection = new QLabel();
    lbSelection->setFrameShape(QFrame::Panel);
    lbSelection->setFrameShadow(QFrame::Sunken);
    lbSelection->setMinimumWidth(200);
    lbSelection->setAlignment(Qt::AlignCenter);
    statusBar()->addPermanentWidget(lbSelection);

    // Address Label
    lbAddress = new QLabel();
    lbAddress->setFrameShape(QFrame::Panel);
    lbAddress->setFrameShadow(QFrame::Sunken);
    lbAddress->setAlignment(Qt::AlignCenter);
    lbAddress->setMinimumWidth(120);
    statusBar()->addPermanentWidget(lbAddress);
    setAddress(hexEdit->getCurrentOffset());

    // Size Label
    lbSizeName = new QLabel();
    lbSizeName->setText(tr("Size") + QString(":"));
    statusBar()->addPermanentWidget(lbSizeName);
    lbSize = new QLabel();
    lbSize->setFrameShape(QFrame::Panel);
    lbSize->setFrameShadow(QFrame::Sunken);
    lbSize->setMinimumWidth(70);
    statusBar()->addPermanentWidget(lbSize);
    if (hexEdit)
        setSize(hexEdit->dataSize());

    // Overwrite Mode Label
    lbOverwriteMode = new QPushButton();
    lbOverwriteMode->setFlat(true);
    lbOverwriteMode->setMinimumWidth(80);
    lbOverwriteMode->setMaximumSize(QSize(100, 24));
    statusBar()->addPermanentWidget(lbOverwriteMode);
    connect(lbOverwriteMode, SIGNAL(clicked()), this, SLOT(toggleOverwriteMode()));
    setOverwriteMode(hexEdit->overwriteMode());

    updateValuePanels();
    updateStatusBarVisibility();

    m_readyText = tr("Ready");
    statusBar()->showMessage(m_readyText);
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString &msg) {
        if (msg.isEmpty())
            statusBar()->showMessage(m_readyText);
    });
}

void MainWindow::restoreDockLayout()
{
    if (!defaultWindowState.isEmpty())
        restoreState(defaultWindowState);

    const auto defaultFeatures = QDockWidget::DockWidgetClosable
                               | QDockWidget::DockWidgetFloatable
                               | QDockWidget::DockWidgetMovable;

    if (m_tablesDock) {
        m_tablesDock->setFeatures(defaultFeatures);
        m_tablesDock->show();
        m_tablesDock->setCollapsed(false);
        m_tablesDock->raise();
        resizeDocks({m_tablesDock}, {360}, Qt::Horizontal);
    }

    if (m_pointersDock) {
        m_pointersDock->setFeatures(defaultFeatures);
        m_pointersDock->show();
        m_pointersDock->setCollapsed(false);
    }

    if (m_changesDock) {
        m_changesDock->setFeatures(defaultFeatures);
        m_changesDock->show();
        m_changesDock->setCollapsed(false);
    }

    if (m_sectionsDock) {
        m_sectionsDock->setFeatures(defaultFeatures);
        m_sectionsDock->show();
        m_sectionsDock->setCollapsed(false);
    }

    setDockAreaCollapsed(Qt::LeftDockWidgetArea, false);
    setDockAreaCollapsed(Qt::RightDockWidgetArea, false);
    setDockAreaCollapsed(Qt::BottomDockWidgetArea, false);

    if (m_pointersDock && m_changesDock)
        resizeDocks({m_pointersDock, m_changesDock}, {220, 220}, Qt::Vertical);

    if (m_currentSession) {
        m_currentSession->dockTablesVisible = true;
        m_currentSession->dockPointersVisible = true;
        m_currentSession->dockChangesVisible = true;
        m_currentSession->dockSectionsVisible = true;
        m_currentSession->dockVisibilityInitialized = true;
    }

    saveProjectDockVisibilityState();
    enforceBottomDockEqualWidth();

    if (m_document)
        m_document->dockLayoutState = saveState();

    updateDockAreaActions();
}


void MainWindow::setDockAreaCollapsed(Qt::DockWidgetArea area, bool collapsed)
{
    // For bottom area: capture horizontal widths BEFORE any collapse/expand
    // so the splitter proportions can be restored afterwards.
    QList<QDockWidget*> bottomDocks;
    QList<int> savedWidths;
    if (area == Qt::BottomDockWidgetArea) {
        auto capture = [&](QDockWidget *dock) {
            if (dock && !dock->isFloating() &&
                dockWidgetArea(dock) == Qt::BottomDockWidgetArea) {
                bottomDocks << dock;
                savedWidths << dock->width();
            }
        };
        capture(m_pointersDock);
        capture(m_changesDock);
    }

    // Collapse all known dock widgets in the given area.
    // Each dock's setCollapsed() handles resizing to title-bar-only extent.
    auto collapseIfInArea = [this, area, collapsed](QDockWidget *dock) {
        if (!dock || dock->isFloating())
            return;
        if (dockWidgetArea(dock) != area)
            return;
        if (dock == m_tablesDock)
            m_tablesDock->setCollapsed(collapsed);
        else if (dock == m_pointersDock)
            m_pointersDock->setCollapsed(collapsed);
        else if (dock == m_changesDock)
            m_changesDock->setCollapsed(collapsed);
        else {
            if (collapsed)
                dock->hide();
            else
                dock->show();
        }
    };

    collapseIfInArea(m_tablesDock);
    collapseIfInArea(m_pointersDock);
    collapseIfInArea(m_changesDock);

    // Restore horizontal widths for bottom docks (preserves the splitter ratio
    // the user had before collapse, instead of forcing 50/50).
    if (!bottomDocks.isEmpty())
        resizeDocks(bottomDocks, savedWidths, Qt::Horizontal);

    updateDockAreaActions();
}

bool MainWindow::isDockAreaCollapsed(Qt::DockWidgetArea area) const
{
    QList<QDockWidget *> docksInArea;
    const auto allDocks = findChildren<QDockWidget *>();
    for (QDockWidget *dock : allDocks) {
        if (!dock)
            continue;
        if (dockWidgetArea(dock) == area)
            docksInArea.append(dock);
    }
    if (docksInArea.isEmpty())
        return false;

    bool hasTrackedDock = false;
    for (QDockWidget *dock : docksInArea) {
        if (!dock || dock->isFloating())
            continue;

        hasTrackedDock = true;

        if (dock == m_tablesDock) {
            if (!m_tablesDock->isCollapsed())
                return false;
        } else if (dock == m_pointersDock) {
            if (!m_pointersDock->isCollapsed())
                return false;
        } else if (dock == m_changesDock) {
            if (!m_changesDock->isCollapsed())
                return false;
        } else if (dock->isVisible()) {
            return false;
        }
    }

    return hasTrackedDock;
}

void MainWindow::updateDockAreaActions()
{
    auto hasAreaDocks = [this](Qt::DockWidgetArea area) {
        const auto allDocks = findChildren<QDockWidget *>();
        for (QDockWidget *dock : allDocks) {
            if (dock && dockWidgetArea(dock) == area)
                return true;
        }
        return false;
    };

    const auto syncAction = [this, &hasAreaDocks](QAction *act, Qt::DockWidgetArea area) {
        if (!act)
            return;
        const bool hasDocks = hasAreaDocks(area);
        act->setEnabled(hasDocks);
        const QSignalBlocker blocker(act);
        act->setChecked(hasDocks && isDockAreaCollapsed(area));
    };

    syncAction(collapseLeftDockAreaAct, Qt::LeftDockWidgetArea);
    syncAction(collapseRightDockAreaAct, Qt::RightDockWidgetArea);
    syncAction(collapseBottomDockAreaAct, Qt::BottomDockWidgetArea);
}

Qt::DockWidgetArea MainWindow::separatorDockArea(QWidget *separator) const
{
    if (!separator || !centralWidget())
        return Qt::NoDockWidgetArea;

    // Map the separator's center to MainWindow coordinates
    const QPoint sepCenter = separator->mapTo(const_cast<MainWindow *>(this),
                                              QPoint(separator->width() / 2, separator->height() / 2));
    const QRect cr = centralWidget()->geometry();

    // Thin horizontal separator below central widget → bottom area
    if (separator->height() < separator->width()
        && sepCenter.y() >= cr.bottom() - 2) {
        return Qt::BottomDockWidgetArea;
    }
    // Thin vertical separator to the left → left area
    if (separator->width() < separator->height()
        && sepCenter.x() <= cr.left() + 2) {
        return Qt::LeftDockWidgetArea;
    }
    // Thin vertical separator to the right → right area
    if (separator->width() < separator->height()
        && sepCenter.x() >= cr.right() - 2) {
        return Qt::RightDockWidgetArea;
    }

    return Qt::NoDockWidgetArea;
}

void MainWindow::installSeparatorEventFilters()
{
    // Install event filters on direct child widgets that are likely dock separators
    const auto children = this->children();
    for (QObject *child : children) {
        if (!child->isWidgetType())
            continue;
        QWidget *w = static_cast<QWidget *>(child);
        if (w == centralWidget() || w == m_tabWidget
            || qobject_cast<QDockWidget *>(w)
            || qobject_cast<QToolBar *>(w)
            || qobject_cast<QMenuBar *>(w)
            || qobject_cast<QStatusBar *>(w))
            continue;
        w->installEventFilter(this);
    }
}

void MainWindow::setupDockTitleBarCallbacks()
{
    // Helper: set up collapse callback and double-click filter for a dock's title bar.
    // installDoubleClickFilter() must be called AFTER setTitleBarWidget() so our
    // event filter is installed last and therefore runs first (LIFO), intercepting
    // double-clicks before QDockWidget's internal filter can toggle floating.
    auto setupCallback = [this](QDockWidget *dock) {
        if (!dock)
            return;
        auto *titleBar = static_cast<DockTitleBar *>(dock->titleBarWidget());
        if (!titleBar)
            return;
        titleBar->setCollapseAreaCallback([this, dock] {
            auto *baseDock = qobject_cast<BaseDockWidget *>(dock);
            if (baseDock)
                baseDock->setCollapsed(!baseDock->isCollapsed());
        });
        titleBar->installDoubleClickFilter();
    };

    setupCallback(m_tablesDock);
    setupCallback(m_pointersDock);
    setupCallback(m_changesDock);
}


void MainWindow::createToolBars()
{
    fileToolBar = addToolBar(tr("File"));
    fileToolBar->setObjectName("fileToolBar");
    fileToolBar->addAction(newAct);
    fileToolBar->addAction(openAct);
    fileToolBar->addAction(saveAct);

    editToolBar = addToolBar(tr("Edit"));
    editToolBar->setObjectName("editToolBar");
    editToolBar->addAction(undoAct);
    editToolBar->addAction(redoAct);

    searchToolBar = addToolBar(tr("Search"));
    searchToolBar->setObjectName("searchToolBar");
    searchToolBar->addAction(findAct);
    searchToolBar->addAction(findPointersAct);

    navigationToolBar = addToolBar(tr("Navigation"));
    navigationToolBar->setObjectName("navigationToolBar");

    toolbarFirstPositionAct = navigationToolBar->addAction(QIcon(QStringLiteral(":/images/rewind.png")), QString());
    toolbarFirstPositionAct->setToolTip(tr("First position"));
    toolbarFirstPositionAct->setStatusTip(tr("Go to first cursor position in history"));
    connect(toolbarFirstPositionAct, &QAction::triggered, this, &MainWindow::goToFirstPosition);

    toolbarPreviousPositionAct = navigationToolBar->addAction(QIcon(QStringLiteral(":/images/prev.png")), QString());
    toolbarPreviousPositionAct->setToolTip(tr("Previous position"));
    toolbarPreviousPositionAct->setStatusTip(tr("Go to previous cursor position"));
    connect(toolbarPreviousPositionAct, &QAction::triggered, this, &MainWindow::goToPreviousPosition);

    toolbarNextPositionAct = navigationToolBar->addAction(QIcon(QStringLiteral(":/images/next.png")), QString());
    toolbarNextPositionAct->setToolTip(tr("Next position"));
    toolbarNextPositionAct->setStatusTip(tr("Go to next cursor position"));
    connect(toolbarNextPositionAct, &QAction::triggered, this, &MainWindow::goToNextPosition);

    toolbarLastPositionAct = navigationToolBar->addAction(QIcon(QStringLiteral(":/images/fast-forward.png")), QString());
    toolbarLastPositionAct->setToolTip(tr("Last position"));
    toolbarLastPositionAct->setStatusTip(tr("Go to last cursor position in history"));
    connect(toolbarLastPositionAct, &QAction::triggered, this, &MainWindow::goToLastPosition);

    scriptToolBar = addToolBar(tr("Script"));
    scriptToolBar->setObjectName("scriptToolBar");

    toolbarDumpScriptAct = scriptToolBar->addAction(QIcon(QStringLiteral(":/images/dump.png")), tr("Dump script"));
    toolbarDumpScriptAct->setToolTip(tr("Dump script"));
    toolbarDumpScriptAct->setStatusTip(tr("Dump text script"));
    toolbarDumpScriptAct->setEnabled(false);
    connect(toolbarDumpScriptAct, &QAction::triggered, this, &MainWindow::dumpScript);

    toolbarInsertScriptAct = scriptToolBar->addAction(QIcon(QStringLiteral(":/images/insert_script.png")), tr("Insert script"));
    toolbarInsertScriptAct->setToolTip(tr("Insert script"));
    toolbarInsertScriptAct->setStatusTip(tr("Insert text script"));
    toolbarInsertScriptAct->setEnabled(true);
    connect(toolbarInsertScriptAct, &QAction::triggered, this, &MainWindow::insertScript);

    // Profile toolbar: byte order + ROM type
    profileToolBar = addToolBar(tr("Profile"));
    profileToolBar->setObjectName("profileToolBar");

    lbEndiannes = new QPushButton();
    lbEndiannes->setFlat(true);
    lbEndiannes->setMaximumSize(QSize(120, 24));
    connect(lbEndiannes, SIGNAL(clicked()), this, SLOT(updateEndiannes()));
    profileToolBar->addWidget(lbEndiannes);

    profileToolBar->addSeparator();

    auto *lblRom = new QLabel(tr("Type:"));
    lblRom->setMargin(4);
    profileToolBar->addWidget(lblRom);

    cbRomType = new QComboBox();
    repopulateRomTypeCombo();
    cbRomType->setCurrentIndex(0);
    connect(cbRomType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRomTypeChanged);
    profileToolBar->addWidget(cbRomType);

    updateEndiannesLabel();
}

// ---------------------------------------------------------------------------
// Project open / save
// ---------------------------------------------------------------------------

