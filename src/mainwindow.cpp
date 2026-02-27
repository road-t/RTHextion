#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QStatusBar>
#include <QLabel>
#include <QPixmap>
#include <QAction>
#include <QActionGroup>
#include <QMenuBar>
#include <QToolBar>
#include <QColorDialog>
#include <QFontDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QSettings>
#include <QDir>

#include "QtWidgets/qpushbutton.h"
#include "appinfo.h"
#include "langtranslator.h"
#include "mainwindow.h"

namespace
{
    const char *kLastFileDirKey = "Paths/LastFileDir";
    const char *kLastTableDirKey = "Paths/LastTableDir";
    const char *kLastDumpDirKey = "Paths/LastDumpDir";
    const char *kMainWindowStateKey = "MainWindow/State";
}

/*****************************************************************************/
/* Public methods */
/*****************************************************************************/
MainWindow::MainWindow()
{
    setAcceptDrops( true );
    init();
}

/*****************************************************************************/
/* Protected methods */
/*****************************************************************************/
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave())
    {
        event->ignore();
        return;
    }

    writeSettings();
    event->accept();
}


void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->accept();
}


void MainWindow::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        QString filePath = urls.at(0).toLocalFile();
        loadFile(filePath);
        event->accept();
    }
}

/*****************************************************************************/
/* Private Slots */
/*****************************************************************************/
void MainWindow::about()
{
    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle(tr("About"));
    aboutBox.setIconPixmap(QPixmap(":/images/tj.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Build translated body parts separately to avoid huge translation strings
    QString versionLine = QStringLiteral("%1 v%2").arg(AppInfo::Name).arg(AppInfo::Version);
    QString descriptionLine = tr("This is a hex editor for retro game translation/ROM hacking.\nA tribute to Translhextion editor made by Januschan in early 00's.");
    QString copyrightLine = tr("Ilya 'Road Tripper' Annikov © 2021-2026. All rights reserved.");
    QString githubLine = QStringLiteral("GitHub: https://github.com/road-t/RTHextion");

    QString rawText = versionLine + "\n\n" + descriptionLine + "\n\n" + copyrightLine + "\n\n" + githubLine;

    static const QString urlPlain = QStringLiteral("https://github.com/road-t/RTHextion");
    static const QString urlLink = QStringLiteral("<a href='https://github.com/road-t/RTHextion'>https://github.com/road-t/RTHextion</a>");
    QString htmlText = rawText.toHtmlEscaped()
                           .replace(QLatin1Char('\n'), QLatin1String("<br>"))
                           .replace(urlPlain, urlLink);

    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setText(htmlText);
    aboutBox.setTextInteractionFlags(Qt::TextBrowserInteraction);

    const auto labels = aboutBox.findChildren<QLabel *>();
    for (QLabel *label : labels)
    {
        if (label->text().contains(QStringLiteral("https://github.com/road-t/RTHextion")))
        {
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(true);
            break;
        }
    }

    // Widen dialog ~30% via a horizontal spacer in the internal grid layout
    if (auto *layout = qobject_cast<QGridLayout *>(aboutBox.layout()))
    {
        auto *spacer = new QSpacerItem(560, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        layout->addItem(spacer, layout->rowCount(), 0, 1, layout->columnCount());
    }

    aboutBox.exec();
}

void MainWindow::dataChanged()
{
    setWindowModified(hexEdit->isModified());
    updateActionStates();
}

void MainWindow::closeFile()
{
    if (!maybeSave())
        return;

    hexEdit->setData(QByteArray());
    hexEdit->clearPointers();
    showPointersAct->setEnabled(false);
    setCurrentFile("");
    statusBar()->showMessage(tr("File closed"), 2000);
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;

    hexEdit->setData(QByteArray());
    hexEdit->clearPointers();
    showPointersAct->setEnabled(false);
    setCurrentFile("");
    statusBar()->showMessage(tr("New file created"), 2000);
}

void MainWindow::open()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Open file"), lastDirectory(kLastFileDirKey));

    if (!fileName.isEmpty()) {
        loadFile(fileName);
    }
}

void MainWindow::revert()
{
    if (isUntitled)
    {
        // For new files, just clear the data
        QMessageBox msg(QMessageBox::Warning, nullptr, tr("Clear all changes?"), QMessageBox::Yes | QMessageBox::Cancel, this);
        auto result = msg.exec();

        if (result == QMessageBox::Yes)
        {
            hexEdit->setData(QByteArray());
            hexEdit->clearPointers();
            showPointersAct->setEnabled(false);
            statusBar()->showMessage(tr("Data cleared"), 2000);
        }
    }
    else
    {
        // For existing files, reload from disk
        QMessageBox msg(QMessageBox::Warning, nullptr, tr("Are you sure want to load last saved version and lose all the changes?"), QMessageBox::Yes | QMessageBox::Cancel, this);
        auto result = msg.exec();

        if (result == QMessageBox::Yes)
        {
            loadFile(curFile);
            statusBar()->showMessage(tr("File reverted"), 2000);
        }
    }
}

void MainWindow::optionsAccepted()
{
    readSettings();
}

void MainWindow::pointersUpdated()
{
    showPointersAct->setEnabled(!hexEdit->pointers()->empty());
}

void MainWindow::findNext()
{
    searchDialog->findNext();
}

void MainWindow::showJumpToDialog()
{
    jumpToDialog->show();
}

bool MainWindow::save()
{
    if (isUntitled)
    {
        return saveAs();
    }
    else
    {
        return saveFile(curFile);
    }
}

bool MainWindow::saveAs()
{
    QString initialPath = curFile;
    if (initialPath.isEmpty())
        initialPath = lastDirectory(kLastFileDirKey);

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As"), initialPath);

    if (fileName.isEmpty())
        return false;

    return saveFile(fileName);
}

void MainWindow::saveSelectionToReadableFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save dump to file"), lastDirectory(kLastDumpDirKey));

    if (!fileName.isEmpty())
    {
        QFile file(fileName);

        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            QMessageBox::warning(this, tr("RTHextion"),
                                 tr("Cannot write file %1:\n%2.")
                                 .arg(fileName)
                                 .arg(file.errorString()));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);

        auto data = tb ? tb->encode(hexEdit->getRawSelection(), true) : hexEdit->data();

        file.write(data.toUtf8());

         //file.write(hexEdit->selectionToReadableString().toLatin1());

        QApplication::restoreOverrideCursor();

        statusBar()->showMessage(tr("File saved"), 2000);
        rememberDirectory(kLastDumpDirKey, fileName);
    }
}

void MainWindow::saveToReadableFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save dump to file"), lastDirectory(kLastDumpDirKey));
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            QMessageBox::warning(this, tr("RTHextion"),
                                 tr("Cannot write file %1:\n%2.")
                                 .arg(fileName)
                                 .arg(file.errorString()));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        file.write(hexEdit->toReadableString().toLatin1());
        QApplication::restoreOverrideCursor();

        statusBar()->showMessage(tr("File saved"), 2000);
        rememberDirectory(kLastDumpDirKey, fileName);
    }
}

void MainWindow::setAddress(qint64 address)
{
    lbAddress->setText(QString("%1").arg(address, 1, 16));
    curOffset = address;

    auto value = hexEdit->getValue(address);

    QString bValue;

    bValue = hexEdit->bigEndian ?
                QString("byte: %1 | word: %2 | dword: %3").arg(value.uByte).arg(value.beWord).arg(value.beDword) :
                QString("byte: %1 | word: %2 | dword: %3").arg(value.uByte).arg(value.leWord).arg(value.leDword);

    lbValue->setText(bValue);
}

void MainWindow::setSelection(qint64 start, qint64 end)
{
    auto len = end - start;
    auto text = len ? QString("0x%1-0x%2: %3").arg(start, 2, 16, QChar('0')).arg(end, 2, 16, QChar('0')).arg(end - start) : tr("No selection");

    lbSelection->setText(text);
    saveSelectionReadable->setEnabled(len > 0);
}

void MainWindow::setOverwriteMode(bool mode)
{
    lbOverwriteMode->setText(mode ? tr("REPLACE") : tr("INSERT"));
}

void MainWindow::setSize(qint64 size)
{
    lbSize->setText(QString("%1").arg(size));
}

void MainWindow::showOptionsDialog()
{
    optionsDialog->show();
}

void MainWindow::showSearchDialog()
{
    searchDialog->show();
}

void MainWindow::showPointersDialog()
{
    pointersDialog->show();
}

bool MainWindow::loadTable()
{
    auto fileName = QFileDialog::getOpenFileName(this, tr("Load translation table"), lastDirectory(kLastTableDirKey), "Tables (*.tbl *.tab *.table);;Text files (*.txt)");

    if (!fileName.isEmpty())
    {
        if (tb)
            delete tb;

        // todo catch exceptions
        tb = new TranslationTable(fileName);
        hexEdit->setTranslationTable(tb);
        useTableAct->setDisabled(false);
        editTableAct->setDisabled(false);
        updateScriptMenuState();
        rememberDirectory(kLastTableDirKey, fileName);
    }

    return true;
}

void MainWindow::switchUseTable()
{
    useTableAct->isChecked() ? hexEdit->setTranslationTable(tb) : hexEdit->removeTranslationTable();
    updateScriptMenuState();
}

void MainWindow::updateScriptMenuState()
{
    bool tableActive = tb && useTableAct->isChecked();
    scriptMenu->setEnabled(tableActive);
}

void MainWindow::toggleOverwriteMode()
{
    hexEdit->setOverwriteMode(!hexEdit->overwriteMode());
}

void MainWindow::setLanguage()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString language = action->data().toString();
    const QString languageShort = language.left(2);

    // Remove previous custom translator (if any)
    const auto translators = qApp->findChildren<LangTranslator *>();
    for (auto *t : translators)
    {
        qApp->removeTranslator(t);
        t->deleteLater();
    }

    // Load and install new translator
    LangTranslator *translator = new LangTranslator(qApp);
    if (translator->load(QStringLiteral(":/translations/") + language + QStringLiteral(".lang")) ||
        translator->load(QStringLiteral(":/translations/") + languageShort + QStringLiteral(".lang")))
    {
        qApp->installTranslator(translator);
    }
    else
    {
        delete translator;
    }

    LangTranslator::setCurrentLanguage(language);

    // Save language preference
    QSettings settings;
    settings.setValue("Language", language);

    // Retranslate everything immediately
    retranslateUi();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    // Actions - File
    newAct->setText(tr("New"));
    newAct->setStatusTip(tr("Create a new file"));
    openAct->setText(tr("Open..."));
    openAct->setStatusTip(tr("Open an existing file"));
    saveAct->setText(tr("Save"));
    saveAct->setStatusTip(tr("Save the document to disk"));
    closeAct->setText(tr("Close"));
    closeAct->setStatusTip(tr("Close the current file"));
    saveAsAct->setText(tr("Save As..."));
    saveAsAct->setStatusTip(tr("Save the document under a new name"));
    saveReadable->setText(tr("Save Dump..."));
    saveReadable->setStatusTip(tr("Save document as dump"));
    revertAct->setText(tr("Revert"));
    revertAct->setStatusTip(tr("Revert file to last saved version"));
    exitAct->setText(tr("Exit"));
    exitAct->setStatusTip(tr("Exit the application"));

    // Actions - Edit
    undoAct->setText(tr("Undo"));
    redoAct->setText(tr("Redo"));
    saveSelectionReadable->setText(tr("Save selection as dump"));
    saveSelectionReadable->setStatusTip(tr("Save selection as dump"));

    // Actions - Table
    loadTableAct->setText(tr("Load table"));
    loadTableAct->setStatusTip(tr("Load translation table"));
    useTableAct->setText(tr("Use table"));
    useTableAct->setStatusTip(tr("Use translation table"));
    editTableAct->setText(tr("Edit table"));
    editTableAct->setStatusTip(tr("Edit translation table"));

    // Actions - Script
    dumpScriptAct->setText(tr("Dump script"));
    dumpScriptAct->setStatusTip(tr("Dump text script"));
    insertScriptAct->setText(tr("Insert script"));
    insertScriptAct->setStatusTip(tr("Insert text script"));

    // Actions - Pointers
    findPointersAct->setText(tr("Find pointers"));
    findPointersAct->setStatusTip(tr("Find pointers for selected text"));
    showPointersAct->setText(tr("Show pointers"));

    // Actions - Search
    findAct->setText(tr("Find/Replace"));
    findAct->setStatusTip(tr("Show the Dialog for finding and replacing"));
    findNextAct->setText(tr("Find next"));
    findNextAct->setStatusTip(tr("Find next occurrence of the searched pattern"));
    gotoAct->setText(tr("Jump to offset..."));
    gotoAct->setStatusTip(tr("Go to specified offset"));

    // Actions - Help/Options
    aboutAct->setText(tr("About %1").arg(AppInfo::Name));
    aboutAct->setStatusTip(tr("Show the application's About box"));
    optionsAct->setText(tr("Preferences"));
    optionsAct->setStatusTip(tr("Show the Dialog to select applications options"));

    // Menu titles
    fileMenu->setTitle(tr("File"));
    editMenu->setTitle(tr("Edit"));
    tableMenu->setTitle(tr("Table"));
    scriptMenu->setTitle(tr("Script"));
    pointersMenu->setTitle(tr("Pointers"));
    viewMenu->setTitle(tr("View"));
    toolbarMenu->setTitle(tr("Toolbar"));
    languageMenu->setTitle(tr("Language"));
    helpMenu->setTitle(tr("Help"));

    // Toolbar names and their View menu entries
    fileToolBar->setWindowTitle(tr("File"));
    editToolBar->setWindowTitle(tr("Edit"));
    searchToolBar->setWindowTitle(tr("Search"));
    fileToolBar->toggleViewAction()->setText(tr("File"));
    editToolBar->toggleViewAction()->setText(tr("Actions"));
    searchToolBar->toggleViewAction()->setText(tr("Search"));
    resetToolbarsAct->setText(tr("Reset"));

    // Status bar labels
    lbAddressName->setText(tr("Address:"));
    lbSizeName->setText(tr("Size:"));
    lbOverwriteModeName->setText(tr("Mode:"));
    lbEndiannes->setText(hexEdit->bigEndian ? tr("Big-endian") : tr("Little-endian"));
    setOverwriteMode(hexEdit->overwriteMode());
}

void MainWindow::editTable()
{
    tableEditDialog->show();
}

void MainWindow::onTranslationTableChanged()
{
    if (useTableAct->isChecked())
        hexEdit->setTranslationTable(tb);

    hexEdit->viewport()->update();
    hexEdit->update();

    if (pointersDialog)
        pointersDialog->refreshFromTable();
}

void MainWindow::dumpScript()
{
    dumpScriptDialog->show();
}

void MainWindow::insertScript()
{
    insertScriptDialog->show();
}

/*****************************************************************************/
/* Private Methods */
/*****************************************************************************/
void MainWindow::init()
{
    setAttribute(Qt::WA_DeleteOnClose);

    optionsDialog = new OptionsDialog(this);
    connect(optionsDialog, SIGNAL(accepted()), this, SLOT(optionsAccepted()));

    isUntitled = true;

    hexEdit = new QHexEdit;
    setCentralWidget(hexEdit);

    connect(hexEdit, SIGNAL(overwriteModeChanged(bool)), this, SLOT(setOverwriteMode(bool)));
    connect(hexEdit, SIGNAL(dataChanged()), this, SLOT(dataChanged()));
    connect(hexEdit, SIGNAL(undoAvailable(bool)), undoAct, SLOT(setEnabled(bool)));
    connect(hexEdit, SIGNAL(redoAvailable(bool)), redoAct, SLOT(setEnabled(bool)));
    searchDialog = new SearchDialog(hexEdit, this);

    jumpToDialog = new JumpToDialog(hexEdit, this);

    pointersDialog = new PointersDialog(hexEdit, this);
    connect(pointersDialog, SIGNAL(accepted()), this, SLOT(pointersUpdated()));

    tableEditDialog = new TableEditDialog(&tb, this);
    connect(tableEditDialog, SIGNAL(tableChanged()), this, SLOT(onTranslationTableChanged()));

    dumpScriptDialog = new DumpScriptDialog(hexEdit, this);
    insertScriptDialog = new InsertScriptDialog(hexEdit, this);

    createActions();
    createToolBars();
    createMenus();
    createStatusBar();
    defaultWindowState = saveState();

    setCurrentFile("");
    readSettings();
    updateActionStates();

    // Set grayscale filter for disabled toolbar buttons
    QString styleSheet =
        "QToolButton:disabled { "
        "    filter: grayscale(100%); "
        "    opacity: 0.6; "
        "}";
    this->setStyleSheet(styleSheet);

    setUnifiedTitleAndToolBarOnMac(true);
}

void MainWindow::createActions()
{
    newAct = new QAction(tr("New"), this);
    newAct->setShortcuts(QKeySequence::New);
    newAct->setStatusTip(tr("Create a new file"));
    connect(newAct, SIGNAL(triggered()), this, SLOT(newFile()));

    openAct = new QAction(QIcon(":/images/open.png"), tr("Open..."), this);
    openAct->setShortcuts(QKeySequence::Open);
    openAct->setStatusTip(tr("Open an existing file"));
    connect(openAct, SIGNAL(triggered()), this, SLOT(open()));

    saveAct = new QAction(QIcon(":/images/save.png"), tr("Save"), this);
    saveAct->setShortcuts(QKeySequence::Save);
    saveAct->setStatusTip(tr("Save the document to disk"));
    connect(saveAct, SIGNAL(triggered()), this, SLOT(save()));

    closeAct = new QAction(tr("Close"), this);
    closeAct->setShortcuts(QKeySequence::Close);
    closeAct->setStatusTip(tr("Close the current file"));
    connect(closeAct, SIGNAL(triggered()), this, SLOT(closeFile()));

    saveAsAct = new QAction(tr("Save As..."), this);
    saveAsAct->setShortcuts(QKeySequence::SaveAs);
    saveAsAct->setStatusTip(tr("Save the document under a new name"));
    connect(saveAsAct, SIGNAL(triggered()), this, SLOT(saveAs()));

    saveReadable = new QAction(tr("Save Dump..."), this);
    saveReadable->setStatusTip(tr("Save document as dump"));
    connect(saveReadable, SIGNAL(triggered()), this, SLOT(saveToReadableFile()));

    revertAct = new QAction(tr("Revert"), this);
    revertAct->setStatusTip(tr("Revert file to last saved version"));
    connect(revertAct, SIGNAL(triggered()), this, SLOT(revert()));

    exitAct = new QAction(tr("Exit"), this);
    exitAct->setShortcuts(QKeySequence::Quit);
    exitAct->setStatusTip(tr("Exit the application"));
    connect(exitAct, SIGNAL(triggered()), qApp, SLOT(closeAllWindows()));

    undoAct = new QAction(QIcon(":/images/undo.png"), tr("Undo"), this);
    undoAct->setShortcuts(QKeySequence::Undo);
    connect(undoAct, SIGNAL(triggered()), hexEdit, SLOT(undo()));

    redoAct = new QAction(QIcon(":/images/redo.png"), tr("Redo"), this);
    redoAct->setShortcuts(QKeySequence::Redo);
    connect(redoAct, SIGNAL(triggered()), hexEdit, SLOT(redo()));

    saveSelectionReadable = new QAction(tr("Save selection as dump"), this);
    saveSelectionReadable->setStatusTip(tr("Save selection as dump"));
    saveSelectionReadable->setEnabled(false);
    connect(saveSelectionReadable, SIGNAL(triggered()), this, SLOT(saveSelectionToReadableFile()));

    loadTableAct = new QAction(tr("Load table"), this);
    loadTableAct->setStatusTip(tr("Load translation table"));
    connect(loadTableAct, SIGNAL(triggered()), this, SLOT(loadTable()));

    useTableAct = new QAction(tr("Use table"), this);
    useTableAct->setShortcuts(QKeySequence::AddTab);
    useTableAct->setCheckable(true);
    useTableAct->setChecked(true);
    useTableAct->setDisabled(true);
    useTableAct->setStatusTip(tr("Use translation table"));
    connect(useTableAct, SIGNAL(triggered()), this, SLOT(switchUseTable()));

    editTableAct = new QAction(tr("Edit table"), this);
    editTableAct->setDisabled(true);
    editTableAct->setStatusTip(tr("Edit translation table"));
    connect(editTableAct, SIGNAL(triggered()), this, SLOT(editTable()));

    dumpScriptAct = new QAction(tr("Dump script"), this);
    dumpScriptAct->setStatusTip(tr("Dump text script"));
    connect(dumpScriptAct, SIGNAL(triggered()), this, SLOT(dumpScript()));

    insertScriptAct = new QAction(tr("Insert script"), this);
    insertScriptAct->setStatusTip(tr("Insert text script"));
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

    findAct = new QAction(QIcon(":/images/find.png"), tr("Find/Replace"), this);
    findAct->setShortcuts(QKeySequence::Find);
    findAct->setStatusTip(tr("Show the Dialog for finding and replacing"));
    connect(findAct, SIGNAL(triggered()), this, SLOT(showSearchDialog()));

    findNextAct = new QAction(tr("Find next"), this);
    findNextAct->setStatusTip(tr("Find next occurrence of the searched pattern"));
    connect(findNextAct, SIGNAL(triggered()), this, SLOT(findNext()));

    gotoAct = new QAction(tr("Jump to offset..."), this);
    gotoAct->setShortcut(QKeySequence::FindNext); // ctrl/cmd + G
    gotoAct->setStatusTip(tr("Go to specified offset"));
    connect(gotoAct, SIGNAL(triggered()), this, SLOT(showJumpToDialog()));

    optionsAct = new QAction(tr("Preferences"), this);
    optionsAct->setStatusTip(tr("Show the Dialog to select applications options"));
    optionsAct->setMenuRole(QAction::NoRole); // prevent macOS from auto-moving to Apple menu
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

    QSettings settings;
    const QString language = settings.value("Language", QLocale::system().name()).toString();
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
}

void MainWindow::createMenus()
{
    fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->addAction(newAct);
    fileMenu->addAction(openAct);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAct);
    fileMenu->addAction(saveAsAct);
    fileMenu->addAction(saveReadable);
    fileMenu->addSeparator();
    fileMenu->addAction(revertAct);
    fileMenu->addAction(closeAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAct);

    editMenu = menuBar()->addMenu(tr("Edit"));
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);
    editMenu->addAction(saveSelectionReadable);
    editMenu->addSeparator();
    editMenu->addAction(findAct);
    editMenu->addAction(findNextAct);
    editMenu->addSeparator();
    editMenu->addAction(gotoAct);

    tableMenu = menuBar()->addMenu(tr("Table"));
    tableMenu->addAction(loadTableAct);
    tableMenu->addAction(useTableAct);
    tableMenu->addAction(editTableAct);

    scriptMenu = menuBar()->addMenu(tr("Script"));
    scriptMenu->setEnabled(false);
    scriptMenu->addAction(dumpScriptAct);
    scriptMenu->addAction(insertScriptAct);

    pointersMenu = menuBar()->addMenu(tr("Pointers"));
    pointersMenu->addAction(findPointersAct);
    pointersMenu->addAction(showPointersAct);

    viewMenu = menuBar()->addMenu(tr("View"));
    toolbarMenu = viewMenu->addMenu(tr("Toolbar"));
    QAction *fileToolbarAct = fileToolBar->toggleViewAction();
    QAction *actionsToolbarAct = editToolBar->toggleViewAction();
    QAction *searchToolbarAct = searchToolBar->toggleViewAction();
    fileToolbarAct->setText(tr("File"));
    actionsToolbarAct->setText(tr("Actions"));
    searchToolbarAct->setText(tr("Search"));
    toolbarMenu->addAction(fileToolbarAct);
    toolbarMenu->addAction(actionsToolbarAct);
    toolbarMenu->addAction(searchToolbarAct);
    toolbarMenu->addSeparator();
    resetToolbarsAct = toolbarMenu->addAction(tr("Reset"));
    connect(resetToolbarsAct, &QAction::triggered, this, [this]()
            {
        if (!defaultWindowState.isEmpty())
            restoreState(defaultWindowState);
        fileToolBar->show();
        editToolBar->show();
        searchToolBar->show(); });

    languageMenu = viewMenu->addMenu(tr("Language"));
    languageMenu->addAction(langRussianAct);
    languageMenu->addAction(langEnglishAct);
    languageMenu->addAction(langFrenchAct);
    languageMenu->addAction(langGermanAct);
    languageMenu->addAction(langSpanishAct);
    languageMenu->addAction(langPortugueseAct);
    languageMenu->addAction(langJapaneseAct);
    languageMenu->addAction(langChineseSimplifiedAct);
    viewMenu->addSeparator();
    viewMenu->addAction(optionsAct);

    helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(aboutAct);

    connect(useTableAct, SIGNAL(triggered()), this, SLOT(updateScriptMenuState()));
}

void MainWindow::createStatusBar()
{
 //   QGridLayout *myGridLayout = new QGridLayout();
 //   statusBar()->setLayout(myGridLayout);

    // Endiannes label
    lbEndiannes = new QPushButton();
    lbEndiannes->setFlat(true);
    lbEndiannes->setText(tr("Little-endian"));
    lbEndiannes->setMaximumSize(QSize(103, 24));
    connect(lbEndiannes, SIGNAL(clicked()), this, SLOT(updateEndiannes()));
    statusBar()->addWidget(lbEndiannes);

    // Value label
    lbValue = new QLabel();
    lbValue->setFrameShape(QFrame::Panel);
    lbValue->setFrameShadow(QFrame::Sunken);
    lbValue->setMinimumWidth(300);
    //lbValue->setAlignment()
    statusBar()->addPermanentWidget(lbValue);

    // Selection label
    lbSelection = new QLabel();
    lbSelection->setFrameShape(QFrame::Panel);
    lbSelection->setFrameShadow(QFrame::Sunken);
    lbSelection->setMinimumWidth(200);
    statusBar()->addPermanentWidget(lbSelection);
    connect(hexEdit, SIGNAL(selectionChanged(qint64, qint64)), this, SLOT(setSelection(qint64, qint64)));

    // Address Label
    lbAddressName = new QLabel();
    lbAddressName->setText(tr("Address:"));
    statusBar()->addPermanentWidget(lbAddressName);
    lbAddress = new QLabel();
    lbAddress->setFrameShape(QFrame::Panel);
    lbAddress->setFrameShadow(QFrame::Sunken);
    lbAddress->setMinimumWidth(70);
    statusBar()->addPermanentWidget(lbAddress);
    connect(hexEdit, SIGNAL(currentAddressChanged(qint64)), this, SLOT(setAddress(qint64)));

    // Size Label
    lbSizeName = new QLabel();
    lbSizeName->setText(tr("Size:"));
    statusBar()->addPermanentWidget(lbSizeName);
    lbSize = new QLabel();
    lbSize->setFrameShape(QFrame::Panel);
    lbSize->setFrameShadow(QFrame::Sunken);
    lbSize->setMinimumWidth(70);
    statusBar()->addPermanentWidget(lbSize);
    connect(hexEdit, SIGNAL(currentSizeChanged(qint64)), this, SLOT(setSize(qint64)));

    // Overwrite Mode Label
    lbOverwriteModeName = new QLabel();
    lbOverwriteModeName->setText(tr("Mode:"));
    statusBar()->addPermanentWidget(lbOverwriteModeName);
    lbOverwriteMode = new QPushButton();
    lbOverwriteMode->setFlat(true);
    lbOverwriteMode->setMinimumWidth(70);
    lbOverwriteMode->setMaximumSize(QSize(90, 24));
    statusBar()->addPermanentWidget(lbOverwriteMode);
    connect(lbOverwriteMode, SIGNAL(clicked()), this, SLOT(toggleOverwriteMode()));
    setOverwriteMode(hexEdit->overwriteMode());

    statusBar()->showMessage(tr("Ready"), 2000);
}

void MainWindow::createToolBars()
{
    fileToolBar = addToolBar(tr("File"));
    fileToolBar->setObjectName("fileToolBar");
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
}

void MainWindow::loadFile(const QString &fileName)
{
    if (!maybeSave())
        return;

    file.setFileName(fileName);

    if (!hexEdit->setData(file))
    {
        QMessageBox::warning(this, tr("RTHextion"),
                             tr("Cannot read file %1:\n%2.")
                             .arg(fileName)
                             .arg(file.errorString()));
        return;
    }

    setCurrentFile(fileName);
    statusBar()->showMessage(tr("File loaded"), 2000);
    rememberDirectory(kLastFileDirKey, fileName);

    QSettings settings;

    settings.setValue("RecentFile0", fileName);

/*    auto pos = settings.value("offset").toLongLong();
    hexEdit->setCursorPosition(pos);
    hexEdit->ensureVisible();
*/
}

void MainWindow::readSettings()
{
    QSettings settings;
    QPoint pos = settings.value("pos", QPoint(200, 200)).toPoint();
    QSize size = settings.value("size", QSize(610, 460)).toSize();
    move(pos);
    resize(size);

    hexEdit->setAddressArea(settings.value("AddressArea").toBool());
    hexEdit->setAsciiArea(settings.value("AsciiArea").toBool());
    hexEdit->setHighlighting(settings.value("Highlighting").toBool());
    hexEdit->setOverwriteMode(settings.value("OverwriteMode").toBool());
    hexEdit->setReadOnly(settings.value("ReadOnly").toBool());

    hexEdit->setHighlightingColor(settings.value("HighlightingColor").value<QColor>());
    hexEdit->setPointersColor(settings.value("PointersColor").value<QColor>());
    hexEdit->setPointedColor(settings.value("PointedColor").value<QColor>());
    hexEdit->setAddressAreaColor(settings.value("AddressAreaColor").value<QColor>());
    hexEdit->setSelectionColor(settings.value("SelectionColor").value<QColor>());
    hexEdit->setFont(settings.value("WidgetFont").value<QFont>());
    hexEdit->setAddressFontColor(settings.value("AddressFontColor").value<QColor>());
    hexEdit->setAsciiAreaColor(settings.value("AsciiAreaColor").value<QColor>());
    hexEdit->setAsciiFontColor(settings.value("AsciiFontColor").value<QColor>());
    hexEdit->setHexFontColor(settings.value("HexFontColor").value<QColor>());

    hexEdit->setAddressWidth(settings.value("AddressAreaWidth").toInt());
    hexEdit->setDynamicBytesPerLine(settings.value("Autosize").toBool());
    hexEdit->setBytesPerLine(settings.value("BytesPerLine").toInt());
    hexEdit->setHexCaps(settings.value("HexCaps", true).toBool());

    const QByteArray windowState = settings.value(kMainWindowStateKey).toByteArray();
    if (!windowState.isEmpty())
        restoreState(windowState);

    auto fileName = settings.value("RecentFile0").toString();

    if (!fileName.isEmpty())
    {
        loadFile(fileName);
    }
}

void MainWindow::switchShowPointers()
{
    hexEdit->setShowPointers(showPointersAct->isChecked());
}

bool MainWindow::saveFile(const QString &fileName)
{
    QString tmpFileName = fileName + ".~tmp";

    QApplication::setOverrideCursor(Qt::WaitCursor);

    QFile file(tmpFileName);

    bool ok = hexEdit->write(file);
    if (QFile::exists(fileName))
        ok = QFile::remove(fileName);
    if (ok)
    {
        file.setFileName(tmpFileName);
        ok = file.copy(fileName);
        if (ok)
            ok = QFile::remove(tmpFileName);
    }

    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::warning(this, tr("RTHextion"),
                             tr("Cannot write file %1.")
                             .arg(fileName));
        return false;
    }

    setCurrentFile(fileName);
    rememberDirectory(kLastFileDirKey, fileName);
    statusBar()->showMessage(tr("File saved"), 2000);
    return true;
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    curFile = QFileInfo(fileName).canonicalFilePath();
    if (curFile.isEmpty())
        curFile = fileName;
    isUntitled = fileName.isEmpty();
    setWindowModified(false);

    if (fileName.isEmpty())
        setWindowTitle(QString("RTHextion"));
    else
        setWindowTitle(QString("%1[*] - RTHextion").arg(strippedName(curFile)));

    updateActionStates();
}

bool MainWindow::maybeSave()
{
    if (!hexEdit->isModified())
        return true;

    QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        tr("RTHextion"),
        tr("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (result == QMessageBox::Save)
        return save();

    return result != QMessageBox::Cancel;
}

void MainWindow::updateActionStates()
{
    // Save is always enabled (for new files it will trigger Save As)
    saveAct->setEnabled(true);
    closeAct->setEnabled(!isUntitled);
    undoAct->setEnabled(hexEdit->canUndo());
    redoAct->setEnabled(hexEdit->canRedo());
}

QString MainWindow::strippedName(const QString &fullFileName)
{
    return QFileInfo(fullFileName).fileName();
}

void MainWindow::writeSettings()
{
    QSettings settings;

    settings.setValue("pos", pos());
    settings.setValue("size", size());
    settings.setValue(kMainWindowStateKey, saveState());

    //settings.setValue("offset", curOffset);
}

QString MainWindow::lastDirectory(const QString &settingsKey) const
{
    QSettings settings;
    const QString dir = settings.value(settingsKey).toString();
    return dir.isEmpty() ? QDir::homePath() : dir;
}

void MainWindow::rememberDirectory(const QString &settingsKey, const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    const QString dirPath = QFileInfo(filePath).absolutePath();
    if (dirPath.isEmpty())
        return;

    QSettings settings;
    settings.setValue(settingsKey, dirPath);
}

void MainWindow::updateEndiannes()
{
    hexEdit->bigEndian = !hexEdit->bigEndian;

    lbEndiannes->setText(hexEdit->bigEndian ? tr("Big-endian") : tr("Little-endian"));

    setAddress(hexEdit->getCurrentOffset());
}
