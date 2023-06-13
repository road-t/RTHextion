#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QStatusBar>
#include <QLabel>
#include <QAction>
#include <QMenuBar>
#include <QToolBar>
#include <QColorDialog>
#include <QFontDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>

#include "QtWidgets/qpushbutton.h"
#include "mainwindow.h"

/*****************************************************************************/
/* Public methods */
/*****************************************************************************/
MainWindow::MainWindow()
{
    setAcceptDrops( true );
    init();
    setCurrentFile("");
}

/*****************************************************************************/
/* Protected methods */
/*****************************************************************************/
void MainWindow::closeEvent(QCloseEvent *)
{
    writeSettings();
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
   QMessageBox::about(this, tr("About RTHextion"),
            tr("RTHextion is a tribute to Translhextion hexeditor for ROM hacking based on QHexEdit widget."));
}

void MainWindow::dataChanged()
{
    setWindowModified(hexEdit->isModified());
}

void MainWindow::open()
{
    QString fileName = QFileDialog::getOpenFileName(this);

    if (!fileName.isEmpty()) {
        loadFile(fileName);
    }
}

void MainWindow::revert()
{
    auto msg = QMessageBox(QMessageBox::Warning, nullptr, tr("Are you sure want to load last saved version and lose all the changes?"), QMessageBox::Yes | QMessageBox::Cancel, this);
    auto result = msg.exec();

    if (result == QMessageBox::Yes)
    {
        QSettings settings;

        loadFile(settings.value("RecentFile0").toString());
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
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As"), curFile);

    if (fileName.isEmpty())
        return false;

    return saveFile(fileName);
}

void MainWindow::saveSelectionToReadableFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save To Readable File"));

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
    }
}

void MainWindow::saveToReadableFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save To Readable File"));
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
}

void MainWindow::setOverwriteMode(bool mode)
{
    lbOverwriteMode->setText(mode ? tr("OVERWRITE") : ("INSERT"));
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
    auto fileName = QFileDialog::getOpenFileName(this, tr("Load translation table"), nullptr, "Tables (*.tbl *.tab *.table);;Text files (*.txt)");

    if (!fileName.isEmpty())
    {
        if (tb)
            delete tb;

        // todo catch exceptions
        tb = new TranslationTable(fileName);
        hexEdit->setTranslationTable(tb);
        useTableAct->setDisabled(false);
        editTableAct->setDisabled(false);
    }

    return true;
}

void MainWindow::switchUseTable()
{
    useTableAct->isChecked() ? hexEdit->setTranslationTable(tb) : hexEdit->removeTranslationTable();
}

void MainWindow::editTable()
{
    tableEditDialog->show();
}

void MainWindow::dumpScript()
{
    dumpScriptDialog->show();
}

void MainWindow::insertScript()
{
    insertScriptDialog->show();
}

void MainWindow::findPointers()
{
    auto found = hexEdit->findPointers();

    if (found)
        showPointersAct->setEnabled(true);

    auto msg = QMessageBox(QMessageBox::Information, nullptr, tr("Pointers found: %1").arg(found), QMessageBox::Ok, this);
    msg.exec();
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
    searchDialog = new SearchDialog(hexEdit, this);

    jumpToDialog = new JumpToDialog(hexEdit, this);

    pointersDialog = new PointersDialog(hexEdit, this);
    connect(pointersDialog, SIGNAL(accepted()), this, SLOT(pointersUpdated()));

    tableEditDialog = new TableEditDialog(&tb, this);

    dumpScriptDialog = new DumpScriptDialog(hexEdit, this);
    insertScriptDialog = new InsertScriptDialog(hexEdit, this);

    createActions();
    createMenus();
    createToolBars();
    createStatusBar();

    readSettings();

    setUnifiedTitleAndToolBarOnMac(true);
}

void MainWindow::createActions()
{
    openAct = new QAction(QIcon(":/images/open.png"), tr("&Open..."), this);
    openAct->setShortcuts(QKeySequence::Open);
    openAct->setStatusTip(tr("Open an existing file"));
    connect(openAct, SIGNAL(triggered()), this, SLOT(open()));

    saveAct = new QAction(QIcon(":/images/save.png"), tr("&Save"), this);
    saveAct->setShortcuts(QKeySequence::Save);
    saveAct->setStatusTip(tr("Save the document to disk"));
    connect(saveAct, SIGNAL(triggered()), this, SLOT(save()));

    saveAsAct = new QAction(tr("Save &As..."), this);
    saveAsAct->setShortcuts(QKeySequence::SaveAs);
    saveAsAct->setStatusTip(tr("Save the document under a new name"));
    connect(saveAsAct, SIGNAL(triggered()), this, SLOT(saveAs()));

    saveReadable = new QAction(tr("Save &Readable..."), this);
    saveReadable->setStatusTip(tr("Save document in readable form"));
    connect(saveReadable, SIGNAL(triggered()), this, SLOT(saveToReadableFile()));

    revertAct = new QAction(tr("&Revert"), this);
    revertAct->setStatusTip(tr("Revert file to last saved version"));
    connect(revertAct, SIGNAL(triggered()), this, SLOT(revert()));

    exitAct = new QAction(tr("E&xit"), this);
    exitAct->setShortcuts(QKeySequence::Quit);
    exitAct->setStatusTip(tr("Exit the application"));
    connect(exitAct, SIGNAL(triggered()), qApp, SLOT(closeAllWindows()));

    undoAct = new QAction(QIcon(":/images/undo.png"), tr("&Undo"), this);
    undoAct->setShortcuts(QKeySequence::Undo);
    connect(undoAct, SIGNAL(triggered()), hexEdit, SLOT(undo()));

    redoAct = new QAction(QIcon(":/images/redo.png"), tr("&Redo"), this);
    redoAct->setShortcuts(QKeySequence::Redo);
    connect(redoAct, SIGNAL(triggered()), hexEdit, SLOT(redo()));

    saveSelectionReadable = new QAction(tr("&Save Selection Readable..."), this);
    saveSelectionReadable->setStatusTip(tr("Save selection in readable form"));
    connect(saveSelectionReadable, SIGNAL(triggered()), this, SLOT(saveSelectionToReadableFile()));

    loadTableAct = new QAction(tr("Load table"), this);
    loadTableAct->setStatusTip(tr("Load translation table"));
    connect(loadTableAct, SIGNAL(triggered()), this, SLOT(loadTable()));

    useTableAct = new QAction(tr("Use &table"), this);
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

    dumpScriptAct = new QAction(tr("&Dump script"), this);
    dumpScriptAct->setStatusTip(tr("Dump text script"));
    connect(dumpScriptAct, SIGNAL(triggered()), this, SLOT(dumpScript()));

    insertScriptAct = new QAction(tr("Insert script"), this);
    insertScriptAct->setStatusTip(tr("Insert text script"));
    connect(insertScriptAct, SIGNAL(triggered()), this, SLOT(insertScript()));

    findPointersAct = new QAction(tr("Fi&nd pointers"), this);
    findPointersAct->setShortcuts(QKeySequence::New);
    findPointersAct->setStatusTip(tr("Find pointers for selected text"));
    connect(findPointersAct, SIGNAL(triggered()), this, SLOT(showPointersDialog()));

    //listPointersAct = new QAction(tr("&List pointers"), this);

    showPointersAct = new QAction(tr("Show pointers"), this);
    showPointersAct->setEnabled(false);
    showPointersAct->setCheckable(true);
    showPointersAct->setChecked(true);
    connect(showPointersAct, SIGNAL(triggered()), this, SLOT(switchShowPointers()));

    aboutAct = new QAction(tr("&About"), this);
    aboutAct->setStatusTip(tr("Show the application's About box"));
    connect(aboutAct, SIGNAL(triggered()), this, SLOT(about()));

    aboutQtAct = new QAction(tr("About &Qt"), this);
    aboutQtAct->setStatusTip(tr("Show the Qt library's About box"));
    connect(aboutQtAct, SIGNAL(triggered()), qApp, SLOT(aboutQt()));

    findAct = new QAction(QIcon(":/images/find.png"), tr("&Find/Replace"), this);
    findAct->setShortcuts(QKeySequence::Find);
    findAct->setStatusTip(tr("Show the Dialog for finding and replacing"));
    connect(findAct, SIGNAL(triggered()), this, SLOT(showSearchDialog()));

    findNextAct = new QAction(tr("Find &next"), this);
    findNextAct->setStatusTip(tr("Find next occurrence of the searched pattern"));
    connect(findNextAct, SIGNAL(triggered()), this, SLOT(findNext()));

    gotoAct = new QAction(tr("Jump to offset..."), this);
    gotoAct->setShortcut(QKeySequence::FindNext); // ctrl/cmd + G
    gotoAct->setStatusTip(tr("Go to specified offset"));
    connect(gotoAct, SIGNAL(triggered()), this, SLOT(showJumpToDialog()));

    optionsAct = new QAction(tr("&Options"), this);
    optionsAct->setStatusTip(tr("Show the Dialog to select applications options"));
    connect(optionsAct, SIGNAL(triggered()), this, SLOT(showOptionsDialog()));
}

void MainWindow::createMenus()
{
    fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openAct);
    fileMenu->addAction(saveAct);
    fileMenu->addAction(saveAsAct);
    fileMenu->addAction(saveReadable);
    fileMenu->addSeparator();
    fileMenu->addAction(revertAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAct);

    editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);
    editMenu->addAction(saveSelectionReadable);
    editMenu->addSeparator();
    editMenu->addAction(findAct);
    editMenu->addAction(findNextAct);
    editMenu->addSeparator();
    editMenu->addAction(gotoAct);
    editMenu->addSeparator();
    editMenu->addAction(optionsAct);

    tableMenu = menuBar()->addMenu(tr("&Table"));
    tableMenu->addAction(loadTableAct);
    tableMenu->addAction(useTableAct);
    tableMenu->addAction(editTableAct);

    scriptMenu = menuBar()->addMenu(tr("Script"));
    scriptMenu->addAction(dumpScriptAct);
    scriptMenu->addAction(insertScriptAct);

    pointersMenu = menuBar()->addMenu(tr("&Pointers"));
    pointersMenu->addAction(findPointersAct);
    pointersMenu->addAction(showPointersAct);

    helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(aboutAct);
    helpMenu->addAction(aboutQtAct);
}

void MainWindow::createStatusBar()
{
 //   QGridLayout *myGridLayout = new QGridLayout();
 //   statusBar()->setLayout(myGridLayout);

    // Endiannes label
    lbEndiannes = new QPushButton();
    lbEndiannes->setFlat(true);
    lbEndiannes->setText("Little-endian");
    lbEndiannes->setMaximumSize(QSize(90, 24));
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
    lbOverwriteMode = new QLabel();
    lbOverwriteMode->setFrameShape(QFrame::Panel);
    lbOverwriteMode->setFrameShadow(QFrame::Sunken);
    lbOverwriteMode->setMinimumWidth(70);
    statusBar()->addPermanentWidget(lbOverwriteMode);
    setOverwriteMode(hexEdit->overwriteMode());

    statusBar()->showMessage(tr("Ready"), 2000);
}

void MainWindow::createToolBars()
{
    fileToolBar = addToolBar(tr("File"));
    fileToolBar->addAction(openAct);
    fileToolBar->addAction(saveAct);
    editToolBar = addToolBar(tr("Edit"));
    editToolBar->addAction(undoAct);
    editToolBar->addAction(redoAct);
    editToolBar->addAction(findAct);
}

void MainWindow::loadFile(const QString &fileName)
{
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
    statusBar()->showMessage(tr("File saved"), 2000);
    return true;
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    curFile = QFileInfo(fileName).canonicalFilePath();
    isUntitled = fileName.isEmpty();
    setWindowModified(false);

    if (fileName.isEmpty())
        setWindowFilePath("RTHextion");
    else
    {
        setWindowFilePath(curFile + " - RTHextion");
    }
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

    //settings.setValue("offset", curOffset);
}

void MainWindow::updateEndiannes()
{
    hexEdit->bigEndian = !hexEdit->bigEndian;

    lbEndiannes->setText(hexEdit->bigEndian ? "Big-endian" : "Little-endian");

    setAddress(hexEdit->getCurrentOffset());
}
