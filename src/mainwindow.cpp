#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QStatusBar>
#include <QLabel>
#include <QPixmap>
#include <QAction>
#include <QActionGroup>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QColorDialog>
#include <QFontDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QSettings>
#include <QScrollBar>
#include <QDir>
#include <algorithm>

#include "QtWidgets/qpushbutton.h"
#include "appinfo.h"
#include "langtranslator.h"
#include "mainwindow.h"
#include "romdetect.h"

namespace
{
    const char *kLastFileDirKey = "Paths/LastFileDir";
    const char *kLastTableDirKey = "Paths/LastTableDir";
    const char *kLastDumpDirKey = "Paths/LastDumpDir";
    const char *kMainWindowStateKey = "MainWindow/State";
    const char *kRecentFilesKey = "RecentFiles";
    const char *kRecentTablesKey = "RecentTables";
    const int kMaxRecentFiles = 10;
    const int kMaxRecentTables = 10;

    QChar readSingleCharSetting(const QSettings &settings, const char *key, const QChar &fallback)
    {
        const QString value = settings.value(key, QString(fallback)).toString();
        return value.isEmpty() ? fallback : value.at(0);
    }
}

/*****************************************************************************/
/* Public methods */
/*****************************************************************************/
MainWindow::MainWindow()
    : hexEdit(nullptr), optionsDialog(nullptr), searchDialog(nullptr), jumpToDialog(nullptr), pointersDialog(nullptr), tableEditDialog(nullptr), semiAutoTableDialog(nullptr), dumpScriptDialog(nullptr), insertScriptDialog(nullptr)
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
    QString descriptionLine = tr("This is a hex editor for retro game translation/ROM hacking.\nA tribute to Translhextion16c editor made by Januschan in early 00's.");
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
    resetNavigationHistory();
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
    resetNavigationHistory();
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
        if (QMessageBox::warning(this, tr("Clear data"), tr("Clear all data and changes?"), QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
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
        if (QMessageBox::warning(this, tr("Revert"), tr("Reload file from disk and discard all changes?"), QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
        {
            // Reload file directly without triggering maybeSave()
            file.setFileName(curFile);
            if (hexEdit->setData(file))
            {
                resetNavigationHistory();
                statusBar()->showMessage(tr("File reverted"), 2000);
            }
            else
            {
                QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                                     tr("Cannot read file %1:\n%2.")
                                     .arg(curFile)
                                     .arg(file.errorString()));
            }
        }
    }
}

void MainWindow::optionsAccepted()
{
    updateHexEditorSettings();
}

void MainWindow::pointersUpdated()
{
    showPointersAct->setEnabled(!hexEdit->pointers()->empty());
}

void MainWindow::findNext()
{
    if (!searchDialog)
        searchDialog = new SearchDialog(hexEdit, this);
    searchDialog->findNext();
}

void MainWindow::showJumpToDialog()
{
    if (!jumpToDialog)
        jumpToDialog = new JumpToDialog(hexEdit, this);
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

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As..."), initialPath);

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
            QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
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
            QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
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
    if (!lbAddress)
        return;

    const int digits = (hexEdit ? qMax(1, hexEdit->addressWidth()) : 1);
    lbAddress->setText(QString("0x%1").arg(address, digits, 16, QChar('0')));
    curOffset = address;
    pushNavigationPosition(address);

    updateValuePanels();
}

void MainWindow::updateValuePanels()
{
    auto value = hexEdit->getValue(curOffset);

    const quint8 byteValue = value.uByte;
    const quint16 wordValue = readWord(value, hexEdit->byteOrder);
    const quint32 dwordValue = readDword(value, hexEdit->byteOrder);

    if (showSignedValuesAct && showSignedValuesAct->isChecked())
    {
        lbValueByte->setText(QString("B: %1").arg(static_cast<qint8>(byteValue)));
        lbValueWord->setText(QString("W: %1").arg(static_cast<qint16>(wordValue)));
        lbValueDword->setText(QString("D: %1").arg(static_cast<qint32>(dwordValue)));
    }
    else
    {
        lbValueByte->setText(QString("B: %1").arg(byteValue));
        lbValueWord->setText(QString("W: %1").arg(wordValue));
        lbValueDword->setText(QString("D: %1").arg(dwordValue));
    }
}

void MainWindow::setSelection(qint64 start, qint64 end)
{
    auto len = end - start;
    auto text = len ?
        QString("0x%1-0x%2: %3")
            .arg(start, 2, 16, QChar('0'))
            .arg(end, 2, 16, QChar('0'))
            .arg(end - start + 1)
        : tr("No selection");

    lbSelection->setText(text);
    saveSelectionReadable->setEnabled(len > 0);
    copyAct->setEnabled(len > 0);
    cutAct->setEnabled(len > 0 && !hexEdit->overwriteMode() && !hexEdit->isReadOnly());
    pasteAct->setEnabled(!QApplication::clipboard()->text().isEmpty());

    updateScriptMenuState(len > 0);
}

void MainWindow::setOverwriteMode(bool mode)
{
    lbOverwriteMode->setText(mode ? tr("REPLACE") : tr("INSERT"));
}

void MainWindow::setSize(qint64 size)
{
    lbSize->setText(QString("%1").arg(size));
}

void MainWindow::updateStatusBarVisibility()
{
    if (lbEndiannes)
        lbEndiannes->setVisible(showStatusEndianAct->isChecked());

    if (lbValueByte)
        lbValueByte->setVisible(showStatusByteAct->isChecked());
    if (lbValueWord)
        lbValueWord->setVisible(showStatusWordAct->isChecked());
    if (lbValueDword)
        lbValueDword->setVisible(showStatusDwordAct->isChecked());

    if (lbSelection)
        lbSelection->setVisible(showStatusSelectionAct->isChecked());

    const bool showAddress = showStatusAddressAct->isChecked();
    if (lbAddressName)
        lbAddressName->setVisible(showAddress);
    if (lbAddress)
        lbAddress->setVisible(showAddress);

    const bool showSize = showStatusSizeAct->isChecked();
    if (lbSizeName)
        lbSizeName->setVisible(showSize);
    if (lbSize)
        lbSize->setVisible(showSize);

    const bool showMode = showStatusModeAct->isChecked();
    if (lbOverwriteModeName)
        lbOverwriteModeName->setVisible(showMode);
    if (lbOverwriteMode)
        lbOverwriteMode->setVisible(showMode);
}

void MainWindow::showOptionsDialog()
{
    if (!optionsDialog)
    {
        optionsDialog = new OptionsDialog(this);
        connect(optionsDialog, SIGNAL(accepted()), this, SLOT(optionsAccepted()));
    }
    optionsDialog->show();
}

void MainWindow::showSearchDialog()
{
    if (!searchDialog)
        searchDialog = new SearchDialog(hexEdit, this);
    searchDialog->show();
}

void MainWindow::showPointersDialog()
{
    if (!pointersDialog)
    {
        pointersDialog = new PointersDialog(hexEdit, this);
        connect(pointersDialog, SIGNAL(accepted()), this, SLOT(pointersUpdated()));
        connect(pointersDialog, &PointersDialog::searchCompleted, this, &MainWindow::onQuickSearchCompleted);
    }
    pointersDialog->show();
}

void MainWindow::hexEditContextMenu(const QPoint &globalPos, qint64 bytePos)
{
    PointerListModel *model = hexEdit->pointers();
    const qint64 pointerStart = hexEdit->pointerStartAt(bytePos, 4);
    const qint64 fileSize = hexEdit->data().size();

    const bool hasSelection  = hexEdit->getSelectionBegin() != hexEdit->getSelectionEnd();
    const bool isOverwrite   = hexEdit->overwriteMode();
    const bool isReadOnly    = hexEdit->isReadOnly();
    const bool clickedAscii  = hexEdit->editAreaIsAscii();

    bool canAddAsPointer = false;
    qint64 addAsPointerTarget = -1;

    if (bytePos >= 0 && bytePos + 4 <= fileSize)
    {
        const QByteArray rawPointer = hexEdit->dataAt(bytePos, 4);
        if (rawPointer.size() == 4)
        {
            const quint64 decodedPointer = decodePointer(reinterpret_cast<const uchar *>(rawPointer.constData()), 4, hexEdit->byteOrder);
            if (decodedPointer <= static_cast<quint64>(fileSize))
            {
                canAddAsPointer = true;
                addAsPointerTarget = static_cast<qint64>(decodedPointer);
            }
        }
    }

    auto refreshPointersUi = [this]()
    {
        if (pointersDialog)
            pointersDialog->refreshFromTable();
        pointersUpdated();
        hexEdit->viewport()->update();
    };

    // --- Helper: clipboard has pasteable hex data ---
    const QClipboard *clipboard = QApplication::clipboard();
    const bool canPaste = !isReadOnly && !clipboard->text().isEmpty();

    // --- Helper: add common clipboard + address actions to a menu ---
    // Returns pointers to created actions via output params (nullptr when skipped).
    struct ClipboardActions {
        QAction *copyAddress = nullptr;
        QAction *cut = nullptr;
        QAction *copy = nullptr;
        QAction *paste = nullptr;
    };

    auto addClipboardActions = [&](QMenu &menu, bool isHexArea = false) -> ClipboardActions
    {
        ClipboardActions acts;

        menu.addSeparator();

        // Copy address is always available
        acts.copyAddress = menu.addAction(tr("Copy address"));

        menu.addSeparator();
        acts.cut  = menu.addAction(tr("Cut"));
        acts.cut->setShortcut(QKeySequence::Cut);
        acts.cut->setEnabled(hasSelection && !isReadOnly && !isOverwrite);

        if (isHexArea)
            acts.copy = menu.addAction(tr("Copy hex values"));
        else
            acts.copy = menu.addAction(tr("Copy"));
        acts.copy->setShortcut(QKeySequence::Copy);
        acts.copy->setEnabled(hasSelection);

        if (isHexArea)
            acts.paste = menu.addAction(tr("Paste hex values"));
        else
            acts.paste = menu.addAction(tr("Paste"));
        acts.paste->setShortcut(QKeySequence::Paste);
        acts.paste->setEnabled(!isReadOnly && canPaste);

        return acts;
    };

    // --- Helper: handle clipboard action result ---
    auto handleClipboardAction = [&](QAction *chosen, const ClipboardActions &acts)
    {
        if (!chosen)
            return false;

        if (chosen == acts.copyAddress)
        {
            // Keep "0x" prefix lowercase, uppercase only the hex digits
            const QString addrText = QStringLiteral("0x") + QString("%1").arg(bytePos, 8, 16, QChar('0')).toUpper();
            QApplication::clipboard()->setText(addrText);
            return true;
        }

        if (chosen == acts.cut && acts.cut)
        {
            // Cut: copy selection to clipboard, remove bytes (INSERT) or zero-fill (REPLACE)
            const qint64 selBegin = hexEdit->getSelectionBegin();
            const qint64 selEnd   = hexEdit->getSelectionEnd();
            const QByteArray raw = hexEdit->dataAt(selBegin, selEnd - selBegin);

            if (clickedAscii)
            {
                // ASCII area: copy raw bytes as Latin-1 text
                QApplication::clipboard()->setText(QString::fromLatin1(raw.constData(), raw.size()));
            }
            else
            {
                // Hex area: space-separated uppercase hex pairs
                QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
            }

            if (isOverwrite)
            {
                // In overwrite mode cut is disabled, but just in case:
                hexEdit->replace(selBegin, selEnd - selBegin, QByteArray(static_cast<int>(selEnd - selBegin), char(0)));
            }
            else
            {
                hexEdit->remove(selBegin, selEnd - selBegin);
            }

            hexEdit->setCursorPosition(2 * selBegin);
            hexEdit->ensureVisible();
            return true;
        }

        if (chosen == acts.copy && acts.copy)
        {
            const qint64 selBegin = hexEdit->getSelectionBegin();
            const qint64 selEnd   = hexEdit->getSelectionEnd();
            const QByteArray raw = hexEdit->dataAt(selBegin, selEnd - selBegin);

            if (clickedAscii)
            {
                // ASCII area: copy raw bytes as Latin-1 text
                QApplication::clipboard()->setText(QString::fromLatin1(raw.constData(), raw.size()));
            }
            else
            {
                // Hex area: space-separated uppercase hex pairs
                QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
            }
            return true;
        }

        if (chosen == acts.paste && acts.paste)
        {
            // Decode clipboard depending on the area that was right-clicked
            QByteArray ba;
            if (clickedAscii)
            {
                // ASCII area: paste raw bytes (Latin-1 encoding)
                ba = QApplication::clipboard()->text().toLatin1();
            }
            else
            {
                // Hex area: strip whitespace then decode hex pairs (e.g. "AF 00 91" or "AF0091")
                const QString stripped = QApplication::clipboard()->text()
                                             .remove(' ').remove('\t').remove('\n').remove('\r');
                ba = QByteArray::fromHex(stripped.toLatin1());
            }

            if (ba.isEmpty())
                return true;

            if (isOverwrite)
            {
                // REPLACE mode
                if (hasSelection)
                {
                    // REPLACE with selection: truncate paste to selection size, paste at selection beginning
                    const qint64 selBegin = hexEdit->getSelectionBegin();
                    const qint64 selLen   = hexEdit->getSelectionEnd() - selBegin;
                    ba = ba.left(static_cast<int>(selLen));
                    hexEdit->replace(selBegin, static_cast<int>(ba.size()), ba);
                    hexEdit->setCursorPosition(2 * (selBegin + ba.size()));
                }
                else
                {
                    // REPLACE without selection: paste at clicked position
                    ba = ba.left(static_cast<int>(std::min<qint64>(ba.size(), fileSize - bytePos)));
                    hexEdit->replace(bytePos, static_cast<int>(ba.size()), ba);
                    hexEdit->setCursorPosition(2 * (bytePos + ba.size()));
                }
            }
            else
            {
                // INSERT mode
                if (hasSelection)
                {
                    // INSERT with selection: delete entire selection, then insert paste at selection beginning
                    const qint64 selBegin = hexEdit->getSelectionBegin();
                    const qint64 selLen = hexEdit->getSelectionEnd() - selBegin;
                    hexEdit->remove(selBegin, static_cast<int>(selLen));
                    hexEdit->insert(selBegin, ba);
                    hexEdit->setCursorPosition(2 * (selBegin + ba.size()));
                }
                else
                {
                    // INSERT without selection: insert at clicked position
                    hexEdit->insert(bytePos, ba);
                    hexEdit->setCursorPosition(2 * (bytePos + ba.size()));
                }
            }
            hexEdit->ensureVisible();
            return true;
        }

        return false;
    };

    // 1) Right click on a pointer entry (full pointer length is clickable).
    if (pointerStart >= 0)
    {
        QMenu menu(this);
        QAction *jumpAct = menu.addAction(tr("Jump to offset"));
        QAction *dropAct = menu.addAction(tr("Drop pointer"));
        menu.addSeparator();
        QAction *addAsPointerAct = menu.addAction(tr("Add as pointer"));
        addAsPointerAct->setEnabled(canAddAsPointer);

        auto clipActs = addClipboardActions(menu);

        QAction *chosen = menu.exec(globalPos);
        if (handleClipboardAction(chosen, clipActs))
            return;

        if (chosen == addAsPointerAct)
        {
            if (canAddAsPointer && hexEdit->addPointerUndoable(bytePos, addAsPointerTarget))
                refreshPointersUi();
        }
        else if (chosen == jumpAct)
        {
            const qint64 targetOffset = model->getOffset(pointerStart);
            if (targetOffset >= 0)
            {
                hexEdit->setCursorPosition(targetOffset * 2);
                hexEdit->ensureVisible();
            }
        }
        else if (chosen == dropAct)
        {
            if (hexEdit->removePointerUndoable(pointerStart))
                refreshPointersUi();
        }
        return;
    }

    // 2) Right click on offset that has incoming pointers.
    if (model->hasOffset(bytePos))
    {
        QMenu menu(this);
        QAction *titleAct = menu.addAction(tr("Pointers") + ":");
        titleAct->setEnabled(false);

        const QList<qint64> ptrs = model->getPointers(bytePos);
        QList<QAction *> ptrActs;
        ptrActs.reserve(ptrs.size());

        for (const qint64 ptr : ptrs)
        {
            QAction *ptrAct = menu.addAction(QStringLiteral("0x%1").arg(ptr, 8, 16, QChar('0')).toUpper());
            ptrActs.append(ptrAct);
        }

        menu.addSeparator();
        QAction *dropAllAct = menu.addAction(tr("Drop all"));
        QAction *addAsPointerAct = menu.addAction(tr("Add as pointer"));
        addAsPointerAct->setEnabled(canAddAsPointer);

        auto clipActs = addClipboardActions(menu);

        QAction *chosen = menu.exec(globalPos);
        if (!chosen)
            return;

        if (handleClipboardAction(chosen, clipActs))
            return;

        if (chosen == addAsPointerAct)
        {
            if (canAddAsPointer && hexEdit->addPointerUndoable(bytePos, addAsPointerTarget))
                refreshPointersUi();
            return;
        }

        const int ptrIdx = ptrActs.indexOf(chosen);
        if (ptrIdx >= 0)
        {
            const qint64 ptrOffset = ptrs[ptrIdx];
            hexEdit->setCursorPosition(ptrOffset * 2);
            hexEdit->ensureVisible();
            return;
        }

        if (chosen == dropAllAct)
        {
            QMessageBox confirm(QMessageBox::Question,
                                QString(),
                                tr("Drop all pointers to this offset?"),
                                QMessageBox::Yes | QMessageBox::Cancel,
                                this);
            if (confirm.exec() == QMessageBox::Yes)
            {
                if (hexEdit->removePointersToOffsetUndoable(bytePos) > 0)
                    refreshPointersUi();
            }
        }
        return;
    }

    // 3) Default hex-area context menu.
    QMenu menu(this);

    QAction *quickSearchAct = menu.addAction(tr("Quick pointer search"));
    QAction *findPtrAct     = menu.addAction(tr("Find pointers") + QString("..."));
    menu.addSeparator();
    QAction *addAsPointerAct = menu.addAction(tr("Add as pointer"));
    addAsPointerAct->setEnabled(canAddAsPointer);

    auto clipActs = addClipboardActions(menu, !clickedAscii);  // Show hex labels only in hex area

    QAction *chosen = menu.exec(globalPos);
    if (handleClipboardAction(chosen, clipActs))
        return;

    if (chosen == addAsPointerAct)
    {
        if (canAddAsPointer && hexEdit->addPointerUndoable(bytePos, addAsPointerTarget))
            refreshPointersUi();
    }
    else if (chosen == quickSearchAct)
    {
        if (!pointersDialog)
        {
            pointersDialog = new PointersDialog(hexEdit, this);
            connect(pointersDialog, SIGNAL(accepted()), this, SLOT(pointersUpdated()));
            connect(pointersDialog, &PointersDialog::searchCompleted, this, &MainWindow::onQuickSearchCompleted);
        }
        pointersDialog->quickSearch(bytePos);
    }
    else if (chosen == findPtrAct)
    {
        showPointersDialog();
    }
}

void MainWindow::onQuickSearchCompleted(int found)
{
    if (found == 0)
    {
        QMessageBox::information(this, QString(), tr("No pointers found"));
    }
    else
    {
        hexEdit->viewport()->update();
    }
}

void MainWindow::goToPreviousPosition()
{
    navigateToHistoryIndex(navigationHistoryIndex - 1);
}

void MainWindow::goToNextPosition()
{
    navigateToHistoryIndex(navigationHistoryIndex + 1);
}

void MainWindow::goToFirstPosition()
{
    navigateToHistoryIndex(0);
}

void MainWindow::goToLastPosition()
{
    navigateToHistoryIndex(navigationHistory.size() - 1);
}

void MainWindow::goToFileBeginning()
{
    hexEdit->jumpTo(0);
}

void MainWindow::goToFileEnd()
{
    hexEdit->jumpTo(hexEdit->data().size());
}

bool MainWindow::loadTable()
{
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open translation table"), lastDirectory(kLastTableDirKey), "Tables (*.tbl *.tab *.table);;Text files (*.txt)");

    if (!fileName.isEmpty())
    {
        if (tb)
            delete tb;

        // todo catch exceptions
        tb = new TranslationTable(fileName);
        hexEdit->setTranslationTable(tb);
        useTableAct->setDisabled(false);
        editTableAct->setDisabled(false);
        saveTableAct->setDisabled(false);
        saveTableAsAct->setDisabled(false);
        updateActionStates();
        rememberDirectory(kLastTableDirKey, fileName);
        tableFilePath = fileName;
        addToRecentTables(fileName);
        statusBar()->showMessage(tr("Table loaded"), 2000);
    }

    return true;
}

void MainWindow::switchUseTable()
{
    useTableAct->isChecked() ? hexEdit->setTranslationTable(tb) : hexEdit->removeTranslationTable();
    updateActionStates();
}

void MainWindow::updateScriptMenuState(bool enabled)
{
    dumpScriptAct->setEnabled(enabled);
    toolbarDumpScriptAct->setEnabled(enabled);
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
    QStringList candidates;
    candidates << language;
    if (!languageShort.isEmpty() && languageShort != language)
        candidates << languageShort;

    bool loaded = false;
    for (const QString &candidate : candidates)
    {
        const QString path = QStringLiteral(":/translations/") + candidate + QStringLiteral(".lang");
        if (!QFile::exists(path))
            continue;
        if (translator->load(path))
        {
            loaded = true;
            break;
        }
    }

    if (loaded)
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
    saveAct->setStatusTip(tr("Save the file to disk"));
    closeAct->setText(tr("Close"));
    closeAct->setStatusTip(tr("Close the current file"));
    saveAsAct->setText(tr("Save As..."));
    saveAsAct->setStatusTip(tr("Save the file under a new name"));
    saveReadable->setText(tr("Save Dump..."));
    saveReadable->setStatusTip(tr("Save the file as dump"));
    revertAct->setText(tr("Revert"));
    revertAct->setStatusTip(tr("Revert file to last saved version"));
    exitAct->setText(tr("Exit"));
    exitAct->setStatusTip(tr("Exit the application"));

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
    loadTableAct->setText(tr("Load table"));
    loadTableAct->setStatusTip(tr("Load translation table"));
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
    dumpScriptAct->setText(tr("Dump script"));
    dumpScriptAct->setStatusTip(tr("Dump text script"));
    insertScriptAct->setText(tr("Insert script"));
    insertScriptAct->setStatusTip(tr("Insert text script"));

    // Actions - Pointers
    findPointersAct->setText(tr("Find pointers"));
    findPointersAct->setStatusTip(tr("Find pointers for selected text"));
    showPointersAct->setText(tr("Show pointers"));
    showPointersAct->setStatusTip(tr("Show the pointers dialog"));

    // Actions - Search
    findAct->setText(tr("Find/Replace"));
    findAct->setStatusTip(tr("Show the dialog for finding and replacing"));
    findNextAct->setText(tr("Find next"));
    findNextAct->setStatusTip(tr("Find next occurrence of the searched pattern"));
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

    // Actions - Help/Options
    aboutAct->setText(tr("About %1").arg(AppInfo::Name));
    aboutAct->setStatusTip(tr("Show the application's About box"));
    optionsAct->setText(tr("Preferences"));
    optionsAct->setStatusTip(tr("Show the application options dialog"));

    // Menu titles
    fileMenu->setTitle(tr("File"));
    recentFileMenu->setTitle(tr("Recent"));
    editMenu->setTitle(tr("Edit"));
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
    helpMenu->setTitle(tr("Help"));

    showStatusEndianAct->setText(tr("Endianness"));
    showStatusByteAct->setText(tr("Byte"));
    showStatusWordAct->setText(tr("Word"));
    showStatusDwordAct->setText(tr("DWord"));
    showStatusSelectionAct->setText(tr("Selection"));
    showStatusAddressAct->setText(tr("Address"));
    showStatusSizeAct->setText(tr("Size"));
    showStatusModeAct->setText(tr("Mode"));
    showSignedValuesAct->setText(tr("Signed"));
    showAddressAreaAct->setText(tr("Address area"));
    showAsciiAreaAct->setText(tr("ASCII area"));
    showAddressGridAct->setText(tr("Show grid"));

    showMapPointersAct->setText(tr("Pointer map"));
    showMapPointersAct->setStatusTip(tr("Show pointer locations on the side map"));
    showMapTargetsAct->setText(tr("Target map"));
    showMapTargetsAct->setStatusTip(tr("Show target locations on the side map"));

    mapsMenu->setTitle(tr("Maps"));

    // Toolbar names and their View menu entries
    fileToolBar->setWindowTitle(tr("File"));
    editToolBar->setWindowTitle(tr("Edit"));
    searchToolBar->setWindowTitle(tr("Search"));
    navigationToolBar->setWindowTitle(tr("Navigation"));
    scriptToolBar->setWindowTitle(tr("Script"));
    fileToolBar->toggleViewAction()->setText(tr("File"));
    editToolBar->toggleViewAction()->setText(tr("Actions"));
    searchToolBar->toggleViewAction()->setText(tr("Search"));
    navigationToolBar->toggleViewAction()->setText(tr("Navigation"));
    scriptToolBar->toggleViewAction()->setText(tr("Script"));
    resetToolbarsAct->setText(tr("Reset"));

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
    toolbarDumpScriptAct->setText(tr("Dump script"));
    toolbarDumpScriptAct->setToolTip(tr("Dump script"));
    toolbarDumpScriptAct->setStatusTip(tr("Dump text script"));
    toolbarInsertScriptAct->setText(tr("Insert script"));
    toolbarInsertScriptAct->setToolTip(tr("Insert script"));
    toolbarInsertScriptAct->setStatusTip(tr("Insert text script"));

    // Status bar labels
    lbAddressName->setText(tr("Address") + QString(":"));
    lbSizeName->setText(tr("Size") + QString(":"));
    lbOverwriteModeName->setText(tr("Mode") + QString(":"));
    updateEndiannesLabel();
    setOverwriteMode(hexEdit->overwriteMode());
    updateValuePanels();
    setSelection(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
}

void MainWindow::editTable()
{
    if (!tableEditDialog)
    {
        tableEditDialog = new TableEditDialog(&tb, this);
        connect(tableEditDialog, SIGNAL(tableChanged()), this, SLOT(onTranslationTableChanged()));
    }
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

void MainWindow::createEmptyTable()
{
    if (tb)
        delete tb;

    tb = new TranslationTable();
    hexEdit->setTranslationTable(tb);
    useTableAct->setDisabled(false);
    editTableAct->setDisabled(false);
    saveTableAct->setDisabled(false);
    saveTableAsAct->setDisabled(false);
    updateActionStates();

    editTable();
}

void MainWindow::showSemiAutoTableDialog()
{
    if (!semiAutoTableDialog)
    {
        semiAutoTableDialog = new SemiAutoTableDialog(hexEdit, &tb, this);
        connect(semiAutoTableDialog, &SemiAutoTableDialog::tableGenerated, this, &MainWindow::onSemiAutoTableGenerated);
    }
    semiAutoTableDialog->show();
}

void MainWindow::onSemiAutoTableGenerated()
{
    useTableAct->setDisabled(false);
    editTableAct->setDisabled(false);
    saveTableAct->setDisabled(false);
    saveTableAsAct->setDisabled(false);
    useTableAct->setChecked(true);
    hexEdit->setTranslationTable(tb);
    updateActionStates();

    editTable();
}

void MainWindow::saveTable()
{
    if (!tb || tb->size() == 0)
        return;

    if (tableFilePath.isEmpty())
    {
        saveTableAs();
        return;
    }

    if (!tb->save(tableFilePath))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not save the table file"));
    }
    else
    {
        addToRecentTables(tableFilePath);
        statusBar()->showMessage(tr("Table saved"), 2000);
    }
}

void MainWindow::saveTableAs()
{
    if (!tb || tb->size() == 0)
        return;

    auto fileName = QFileDialog::getSaveFileName(this, tr("Save translation table"),
        lastDirectory(kLastTableDirKey), "Tables (*.tbl);;Text files (*.txt)");

    if (!fileName.isEmpty())
    {
        if (!tb->save(fileName))
        {
            QMessageBox::warning(this, tr("Error"), tr("Could not save the table file"));
            return;
        }

        rememberDirectory(kLastTableDirKey, fileName);
        tableFilePath = fileName;
        addToRecentTables(fileName);
        statusBar()->showMessage(tr("Table saved"), 2000);
    }
}

void MainWindow::dumpScript()
{
    if (!dumpScriptDialog)
        dumpScriptDialog = new DumpScriptDialog(hexEdit, this);
    dumpScriptDialog->show();
}

void MainWindow::insertScript()
{
    if (!insertScriptDialog)
        insertScriptDialog = new InsertScriptDialog(hexEdit, this);
    insertScriptDialog->show();
}

/*****************************************************************************/
/* Private Methods */
/*****************************************************************************/
void MainWindow::init()
{
    setAttribute(Qt::WA_DeleteOnClose);
    isUntitled = true;

    hexEdit = new QHexEdit;
    setCentralWidget(hexEdit);

    connect(hexEdit, SIGNAL(overwriteModeChanged(bool)), this, SLOT(setOverwriteMode(bool)));
    connect(hexEdit, SIGNAL(dataChanged()), this, SLOT(dataChanged()));
    connect(hexEdit, &QHexEdit::contextMenuRequested, this, &MainWindow::hexEditContextMenu);

    createActions();
    createToolBars();
    createMenus();

    createStatusBar();

    defaultWindowState = saveState();

    setCurrentFile("");

    readSettings();

    updateActionStates();

    setUnifiedTitleAndToolBarOnMac(true);
}

void MainWindow::createActions()
{
    newAct = new QAction(tr("New"), this);
    newAct->setStatusTip(tr("Create a new file"));
    connect(newAct, SIGNAL(triggered()), this, SLOT(newFile()));
    openAct = new QAction(QIcon(":/images/open.png"), tr("Open..."), this);
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
    connect(closeAct, SIGNAL(triggered()), this, SLOT(closeFile()));

    saveAsAct = new QAction(tr("Save As..."), this);
    saveAsAct->setShortcuts(QKeySequence::SaveAs);
    saveAsAct->setStatusTip(tr("Save file under a new name"));
    connect(saveAsAct, SIGNAL(triggered()), this, SLOT(saveAs()));

    saveReadable = new QAction(tr("Save Dump..."), this);
    saveReadable->setStatusTip(tr("Save file as dump"));
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
            QApplication::clipboard()->setText(QString::fromLatin1(raw.constData(), raw.size()));
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
        
        if (hexEdit->editAreaIsAscii())
            QApplication::clipboard()->setText(QString::fromLatin1(raw.constData(), raw.size()));
        else
            QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
    });

    pasteAct = new QAction(tr("Paste"), this);
    pasteAct->setShortcut(QKeySequence::Paste);
    pasteAct->setEnabled(false);
    connect(pasteAct, &QAction::triggered, this, [this]()
    {
        const QString clipText = QApplication::clipboard()->text();
        if (clipText.isEmpty()) return;

        QByteArray ba;
        if (hexEdit->editAreaIsAscii())
            ba = clipText.toLatin1();
        else
        {
            QString stripped = clipText;
            stripped.remove(' ').remove('\t').remove('\n').remove('\r');
            ba = QByteArray::fromHex(stripped.toLatin1());
        }

        if (ba.isEmpty()) return;

        const qint64 selBegin = hexEdit->getSelectionBegin();
        const qint64 selEnd   = hexEdit->getSelectionEnd();
        const bool hasSelection = (selBegin != selEnd);

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
                ba = ba.left(static_cast<int>(std::min<qint64>(ba.size(), hexEdit->data().size() - pos)));
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

    loadTableAct = new QAction(tr("Open..."), this);
    loadTableAct->setStatusTip(tr("Open translation table"));
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

    dumpScriptAct = new QAction(QIcon(":/images/dump.png"), tr("Dump script"), this);
    dumpScriptAct->setStatusTip(tr("Dump text script"));
    dumpScriptAct->setEnabled(false);
    connect(dumpScriptAct, SIGNAL(triggered()), this, SLOT(dumpScript()));

    insertScriptAct = new QAction(QIcon(":/images/insert_script.png"), tr("Insert script"), this);
    insertScriptAct->setStatusTip(tr("Insert text script"));
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

    findAct = new QAction(QIcon(":/images/find.png"), tr("Find/Replace"), this);
    findAct->setShortcuts(QKeySequence::Find);
    findAct->setStatusTip(tr("Show the dialog for finding and replacing"));
    connect(findAct, SIGNAL(triggered()), this, SLOT(showSearchDialog()));

    findNextAct = new QAction(tr("Find next"), this);
    findNextAct->setStatusTip(tr("Find next occurrence of the searched pattern"));
    connect(findNextAct, SIGNAL(triggered()), this, SLOT(findNext()));

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

    QSettings settings;
    const QString language = settings.value("Language", QStringLiteral("en")).toString();
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

    showSignedValuesAct = new QAction(tr("Signed"), this);
    showSignedValuesAct->setCheckable(true);
    showSignedValuesAct->setChecked(false);

    showAddressAreaAct = new QAction(tr("Address area"), this);
    showAddressAreaAct->setCheckable(true);
    showAddressAreaAct->setChecked(true);

    showAsciiAreaAct = new QAction(tr("ASCII area"), this);
    showAsciiAreaAct->setCheckable(true);
    showAsciiAreaAct->setChecked(true);

    showAddressGridAct = new QAction(tr("Show grid"), this);
    showAddressGridAct->setCheckable(true);
    showAddressGridAct->setChecked(true);

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
    connect(showSignedValuesAct, &QAction::toggled, this, [this](bool)
            { updateValuePanels(); });
    connect(showAddressAreaAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setAddressArea(checked); });
    connect(showAsciiAreaAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setAsciiArea(checked); });
    connect(showAddressGridAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setShowHexGrid(checked); });

    showMapPointersAct = new QAction(tr("Pointer map"), this);
    showMapPointersAct->setCheckable(true);
    showMapPointersAct->setChecked(true);
    showMapPointersAct->setStatusTip(tr("Show pointer storage locations on the side map"));
    connect(showMapPointersAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setScrollMapPtrVisible(checked); });

    showMapTargetsAct = new QAction(tr("Target map"), this);
    showMapTargetsAct->setCheckable(true);
    showMapTargetsAct->setChecked(true);
    showMapTargetsAct->setStatusTip(tr("Show pointer target locations on the side map"));
    connect(showMapTargetsAct, &QAction::toggled, this, [this](bool checked)
            { hexEdit->setScrollMapTargetVisible(checked); });

    updateNavigationActions();

}

void MainWindow::createMenus()
{

    fileMenu = menuBar()->addMenu(tr("File"));

    fileMenu->addAction(newAct);
    fileMenu->addAction(openAct);
    recentFileMenu = fileMenu->addMenu(tr("Recent"));
    recentFileMenu->setEnabled(false);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAct);
    fileMenu->addAction(saveAsAct);
    fileMenu->addAction(saveReadable);
    fileMenu->addSeparator();
    fileMenu->addAction(revertAct);
    fileMenu->addAction(closeAct);
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
    panelsMenu->addAction(showAsciiAreaAct);
    panelsMenu->addAction(showAddressGridAct);

    mapsMenu = viewMenu->addMenu(tr("Maps"));
    mapsMenu->addAction(showMapPointersAct);
    mapsMenu->addAction(showMapTargetsAct);

    toolbarMenu = viewMenu->addMenu(tr("Toolbar"));

    QAction *fileToolbarAct = fileToolBar->toggleViewAction();
    QAction *actionsToolbarAct = editToolBar->toggleViewAction();
    QAction *searchToolbarAct = searchToolBar->toggleViewAction();
    QAction *navigationToolbarAct = navigationToolBar->toggleViewAction();
    QAction *scriptToolbarAct = scriptToolBar->toggleViewAction();
    fileToolbarAct->setText(tr("File"));
    actionsToolbarAct->setText(tr("Actions"));
    searchToolbarAct->setText(tr("Search"));
    navigationToolbarAct->setText(tr("Navigation"));
    scriptToolbarAct->setText(tr("Script"));
    toolbarMenu->addAction(fileToolbarAct);
    toolbarMenu->addAction(actionsToolbarAct);
    toolbarMenu->addAction(searchToolbarAct);
    toolbarMenu->addAction(navigationToolbarAct);
    toolbarMenu->addAction(scriptToolbarAct);
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
        scriptToolBar->show(); });

    statusBarMenu = viewMenu->addMenu(tr("Status bar"));
    statusBarMenu->addAction(showStatusEndianAct);
    statusBarMenu->addAction(showStatusByteAct);
    statusBarMenu->addAction(showStatusWordAct);
    statusBarMenu->addAction(showStatusDwordAct);
    statusBarMenu->addAction(showStatusSelectionAct);
    statusBarMenu->addAction(showStatusAddressAct);
    statusBarMenu->addAction(showStatusSizeAct);
    statusBarMenu->addAction(showStatusModeAct);
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

    viewMenu->addSeparator();

    helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(aboutAct);
}

void MainWindow::createStatusBar()
{
 //   QGridLayout *myGridLayout = new QGridLayout();
 //   statusBar()->setLayout(myGridLayout);

    // Endiannes label
    lbEndiannes = new QPushButton();
    lbEndiannes->setFlat(true);
    lbEndiannes->setText(tr("Little-endian"));
    lbEndiannes->setMaximumSize(QSize(120, 24));
    connect(lbEndiannes, SIGNAL(clicked()), this, SLOT(updateEndiannes()));
    statusBar()->addWidget(lbEndiannes);

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
    connect(hexEdit, SIGNAL(selectionChanged(qint64, qint64)), this, SLOT(setSelection(qint64, qint64)));

    // Address Label
    lbAddressName = new QLabel();
    lbAddressName->setText(tr("Address") + QString(":"));
    statusBar()->addPermanentWidget(lbAddressName);
    lbAddress = new QLabel();
    lbAddress->setFrameShape(QFrame::Panel);
    lbAddress->setFrameShadow(QFrame::Sunken);
    lbAddress->setAlignment(Qt::AlignCenter);
    lbAddress->setMinimumWidth(120);
    statusBar()->addPermanentWidget(lbAddress);
    connect(hexEdit, SIGNAL(currentAddressChanged(qint64)), this, SLOT(setAddress(qint64)));
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
    connect(hexEdit, SIGNAL(currentSizeChanged(qint64)), this, SLOT(setSize(qint64)));

    // Overwrite Mode Label
    lbOverwriteModeName = new QLabel();
    lbOverwriteModeName->setText(tr("Mode") + QString(":"));
    statusBar()->addPermanentWidget(lbOverwriteModeName);
    lbOverwriteMode = new QPushButton();
    lbOverwriteMode->setFlat(true);
    lbOverwriteMode->setMinimumWidth(70);
    lbOverwriteMode->setMaximumSize(QSize(90, 24));
    statusBar()->addPermanentWidget(lbOverwriteMode);
    connect(lbOverwriteMode, SIGNAL(clicked()), this, SLOT(toggleOverwriteMode()));
    setOverwriteMode(hexEdit->overwriteMode());

    updateValuePanels();
    updateStatusBarVisibility();

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
}

void MainWindow::loadFile(const QString &fileName)
{
    if (!maybeSave())
        return;
    
    const QString canonicalIncoming = QFileInfo(fileName).canonicalFilePath();
    const QString incomingPath = canonicalIncoming.isEmpty() ? fileName : canonicalIncoming;
    const bool loadingAnotherFile = !curFile.isEmpty() && !incomingPath.isEmpty() && curFile != incomingPath;
    if (loadingAnotherFile && !hexEdit->pointers()->empty())
    {
        QMessageBox confirm(QMessageBox::Warning,
                            QString::fromLatin1(AppInfo::Name),
                            tr("You are about to load another file. Clear pointer list?"),
                            QMessageBox::Yes | QMessageBox::Cancel,
                            this);
        if (confirm.exec() != QMessageBox::Yes)
            return;
        hexEdit->clearPointers();
        pointersUpdated();
    }

    file.setFileName(fileName);

    if (!hexEdit->setData(file))
    {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Cannot read file %1:\n%2.")
                                 .arg(fileName)
                                 .arg(file.errorString()));
        return;
    }

    resetNavigationHistory();
    setCurrentFile(fileName);
    statusBar()->showMessage(tr("File loaded"), 2000);
    rememberDirectory(kLastFileDirKey, fileName);

    // Auto-detect endianness based on ROM type
    QSettings settings;
    if (settings.value("DetectEndianness", true).toBool())
    {
        const QByteArray header = hexEdit->dataAt(0, 512);
        const RomType rom = detectRomType(fileName, header);
        if (rom != RomType::Unknown)
        {
            hexEdit->byteOrder = defaultByteOrder(rom);
            updateEndiannesLabel();
        }
    }

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


    hexEdit->setAddressArea(settings.value("AddressArea", true).toBool());
    hexEdit->setAsciiArea(settings.value("AsciiArea", true).toBool());
    hexEdit->setHighlighting(settings.value("Highlighting", true).toBool());
    hexEdit->setOverwriteMode(settings.value("OverwriteMode", true).toBool());


    // Set color values with proper defaults for first-launch initialization
    hexEdit->setHighlightingColor(settings.value("HighlightingColor", QColor(0xff, 0xff, 0x99, 0xff)).value<QColor>());
    hexEdit->setPointedColor(settings.value("PointedColor", QColor(0xc0, 0x80, 0x00, 0xff)).value<QColor>());
    hexEdit->setPointedFontColor(settings.value("PointedFontColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setPointerFontColor(settings.value("PointerFontColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setPointerFrameColor(settings.value("PointerFrameColor", QColor(0x00, 0x00, 0xFF)).value<QColor>());
    hexEdit->setPointerFrameBackgroundColor(settings.value("PointerFrameBgColor", QColor(0x00, 0xFF, 0x00, 0x80)).value<QColor>());
    hexEdit->setAddressAreaColor(settings.value("AddressAreaColor", palette().alternateBase().color()).value<QColor>());
    hexEdit->setSelectionColor(settings.value("SelectionColor", palette().highlight().color()).value<QColor>());
    hexEdit->setFont(settings.value("WidgetFont", QFont("Courier New", 14)).value<QFont>());
    hexEdit->setAddressFontColor(settings.value("AddressFontColor", palette().color(QPalette::WindowText)).value<QColor>());
    hexEdit->setAsciiAreaColor(settings.value("AsciiAreaColor", palette().alternateBase().color()).value<QColor>());
    hexEdit->setAsciiFontColor(settings.value("AsciiFontColor", palette().color(QPalette::WindowText)).value<QColor>());
    hexEdit->setHexFontColor(settings.value("HexFontColor", palette().color(QPalette::WindowText)).value<QColor>());
    hexEdit->setNonPrintableNoTableChar(readSingleCharSetting(settings, "NonPrintableNoTableChar", QChar(0x25AA)));
    hexEdit->setNotInTableChar(readSingleCharSetting(settings, "NotInTableChar", QChar(0x25A1)));


    hexEdit->setAddressWidth(settings.value("AddressAreaWidth", 8).toInt());
    hexEdit->setBytesPerLine(settings.value("BytesPerLine", 32).toInt());
    hexEdit->setDynamicBytesPerLine(settings.value("Autosize", true).toBool());
    hexEdit->setHexCaps(settings.value("HexCaps", true).toBool());
    hexEdit->setShowHexGrid(settings.value("ShowHexGrid", true).toBool());
    hexEdit->setHexAreaBackgroundColor(settings.value("HexAreaBackgroundColor", QColor(Qt::white)).value<QColor>());
    hexEdit->setHexAreaGridColor(settings.value("HexAreaGridColor", QColor(0x99, 0x99, 0x99)).value<QColor>());
    hexEdit->setCursorCharColor(settings.value("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80)).value<QColor>());
    hexEdit->setCursorFrameColor(settings.value("CursorFrameColor", QColor(Qt::black)).value<QColor>());

    if (showAddressAreaAct)
        showAddressAreaAct->setChecked(hexEdit->addressArea());
    if (showAsciiAreaAct)
        showAsciiAreaAct->setChecked(hexEdit->asciiArea());
    if (showAddressGridAct)
        showAddressGridAct->setChecked(hexEdit->showHexGrid());


    const QByteArray windowState = settings.value(kMainWindowStateKey).toByteArray();
    if (!windowState.isEmpty())
        restoreState(windowState);

    updateRecentFileMenu();

    updateRecentTableMenu();

    const bool autoLoadRecentFile = settings.value("AutoLoadRecentFile", true).toBool();
    const QString fileName = settings.value("RecentFile0").toString();

    if (autoLoadRecentFile && !fileName.isEmpty())
    {
        if (QFile::exists(fileName))
        {
            loadFile(fileName);
        }
    }
}

void MainWindow::switchShowPointers()
{
    hexEdit->setShowPointers(showPointersAct->isChecked());
}

void MainWindow::pushNavigationPosition(qint64 position)
{
    if (navigationJumpInProgress || position < 0)
        return;

    if (navigationHistoryIndex >= 0
        && navigationHistoryIndex < navigationHistory.size()
        && navigationHistory[navigationHistoryIndex] == position)
    {
        return;
    }

    if (navigationHistoryIndex + 1 < navigationHistory.size())
        navigationHistory.resize(navigationHistoryIndex + 1);

    navigationHistory.append(position);

    if (navigationHistory.size() > 1024)
        navigationHistory.remove(0, navigationHistory.size() - 1024);

    navigationHistoryIndex = navigationHistory.size() - 1;
    updateNavigationActions();
}

void MainWindow::resetNavigationHistory()
{
    navigationHistory.clear();
    navigationHistoryIndex = -1;
    pushNavigationPosition(hexEdit ? hexEdit->getCurrentOffset() : 0);
}

void MainWindow::navigateToHistoryIndex(int index)
{
    if (index < 0 || index >= navigationHistory.size())
        return;

    navigationHistoryIndex = index;
    navigationJumpInProgress = true;
    hexEdit->jumpTo(navigationHistory[index]);
    navigationJumpInProgress = false;
    updateNavigationActions();
}

void MainWindow::updateNavigationActions()
{
    const bool hasHistory = !navigationHistory.isEmpty();
    const bool hasPrev = hasHistory && navigationHistoryIndex > 0;
    const bool hasNext = hasHistory && navigationHistoryIndex >= 0 && navigationHistoryIndex < navigationHistory.size() - 1;

    if (previousPositionAct)
        previousPositionAct->setEnabled(hasPrev);
    if (nextPositionAct)
        nextPositionAct->setEnabled(hasNext);
    if (firstPositionAct)
        firstPositionAct->setEnabled(hasPrev);
    if (lastPositionAct)
        lastPositionAct->setEnabled(hasNext);
    if (toolbarPreviousPositionAct)
        toolbarPreviousPositionAct->setEnabled(hasPrev);
    if (toolbarNextPositionAct)
        toolbarNextPositionAct->setEnabled(hasNext);
    if (toolbarFirstPositionAct)
        toolbarFirstPositionAct->setEnabled(hasPrev);
    if (toolbarLastPositionAct)
        toolbarLastPositionAct->setEnabled(hasNext);
    if (toFileBeginningAct)
        toFileBeginningAct->setEnabled(hexEdit && hexEdit->data().size() > 0);
    if (toFileEndAct)
        toFileEndAct->setEnabled(hexEdit && hexEdit->data().size() > 0);
}

void MainWindow::updateHexEditorSettings()
{
    if (!hexEdit)
        return;

    const qint64 savedCursorPos = hexEdit->cursorPosition();
    const int savedBytesPerLine = qMax(1, hexEdit->bytesPerLine());
    const qint64 savedTopByte = static_cast<qint64>(hexEdit->verticalScrollBar()->value()) * savedBytesPerLine;
    const int savedHorizontal = hexEdit->horizontalScrollBar()->value();

    // Apply settings from QSettings to hex editor without reloading file
    QSettings settings;

    hexEdit->setAddressArea(settings.value("AddressArea", true).toBool());
    hexEdit->setAsciiArea(settings.value("AsciiArea", true).toBool());
    hexEdit->setHighlighting(settings.value("Highlighting").toBool());
    hexEdit->setHighlightingColor(settings.value("HighlightingColor").value<QColor>());
    hexEdit->setPointedColor(settings.value("PointedColor").value<QColor>());
    hexEdit->setPointedFontColor(settings.value("PointedFontColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setPointerFontColor(settings.value("PointerFontColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setPointerFrameColor(settings.value("PointerFrameColor", QColor(0x00, 0x00, 0xFF)).value<QColor>());
    hexEdit->setPointerFrameBackgroundColor(settings.value("PointerFrameBgColor", QColor(0x00, 0xFF, 0x00, 0x80)).value<QColor>());
    hexEdit->setAddressAreaColor(settings.value("AddressAreaColor").value<QColor>());
    hexEdit->setSelectionColor(settings.value("SelectionColor").value<QColor>());
    hexEdit->setFont(settings.value("WidgetFont").value<QFont>());
    hexEdit->setAddressFontColor(settings.value("AddressFontColor").value<QColor>());
    hexEdit->setAsciiAreaColor(settings.value("AsciiAreaColor").value<QColor>());
    hexEdit->setAsciiFontColor(settings.value("AsciiFontColor").value<QColor>());
    hexEdit->setHexFontColor(settings.value("HexFontColor").value<QColor>());
    hexEdit->setNonPrintableNoTableChar(readSingleCharSetting(settings, "NonPrintableNoTableChar", QChar(0x25AA)));
    hexEdit->setNotInTableChar(readSingleCharSetting(settings, "NotInTableChar", QChar(0x25A1)));
    hexEdit->setAddressWidth(settings.value("AddressAreaWidth").toInt());
    hexEdit->setBytesPerLine(settings.value("BytesPerLine", 32).toInt());
    hexEdit->setDynamicBytesPerLine(settings.value("Autosize", true).toBool());
    hexEdit->setShowHexGrid(settings.value("ShowHexGrid", true).toBool());
    hexEdit->setHexAreaBackgroundColor(settings.value("HexAreaBackgroundColor", QColor(Qt::white)).value<QColor>());
    hexEdit->setHexAreaGridColor(settings.value("HexAreaGridColor", QColor(0x99, 0x99, 0x99)).value<QColor>());
    hexEdit->setCursorCharColor(settings.value("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80)).value<QColor>());
    hexEdit->setCursorFrameColor(settings.value("CursorFrameColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setScrollMapPtrBgColor(settings.value("ScrollMapPtrBgColor", QColor(0xd0, 0xd0, 0xd0)).value<QColor>());
    hexEdit->setScrollMapTargetBgColor(settings.value("ScrollMapTargetBgColor", QColor(0xd0, 0xd0, 0xd0)).value<QColor>());

    const int newBytesPerLine = qMax(1, hexEdit->bytesPerLine());
    hexEdit->verticalScrollBar()->setValue(static_cast<int>(savedTopByte / newBytesPerLine));
    hexEdit->horizontalScrollBar()->setValue(savedHorizontal);
    hexEdit->setCursorPosition(savedCursorPos);
    hexEdit->viewport()->update();

    if (showAddressAreaAct)
        showAddressAreaAct->setChecked(hexEdit->addressArea());
    if (showAsciiAreaAct)
        showAsciiAreaAct->setChecked(hexEdit->asciiArea());
    if (showAddressGridAct)
        showAddressGridAct->setChecked(hexEdit->showHexGrid());
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
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
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
    {
        setWindowTitle(QString("%1[*] - RTHextion").arg(strippedName(curFile)));
        addToRecentFiles(fileName);
    }

    updateActionStates();
}

bool MainWindow::maybeSave()
{
    if (!hexEdit->isModified())
        return true;

    QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        QString::fromLatin1(AppInfo::Name),
        tr("File has been modified.\nDo you want to save your changes?"),
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
    
    const bool hasSelection = hexEdit && hexEdit->getRawSelection().size() > 1;
    const bool dumpEnabled = hasSelection;
    const bool insertEnabled = true;

    if (scriptMenu)
        scriptMenu->setEnabled(dumpEnabled || insertEnabled);
    if (dumpScriptAct)
        dumpScriptAct->setEnabled(dumpEnabled);
    if (insertScriptAct)
        insertScriptAct->setEnabled(insertEnabled);
    if (toolbarDumpScriptAct)
        toolbarDumpScriptAct->setEnabled(dumpEnabled);
    if (toolbarInsertScriptAct)
        toolbarInsertScriptAct->setEnabled(insertEnabled);
}

QString MainWindow::strippedName(const QString &fullFileName)
{
    return QFileInfo(fullFileName).fileName();
}

void MainWindow::writeSettings()
{
    QSettings settings;

    // Save window geometry
    settings.setValue("pos", pos());
    settings.setValue("size", size());
    settings.setValue(kMainWindowStateKey, saveState());

    // Save hex editor settings to ensure persisted state
    if (hexEdit)
    {
        settings.setValue("AddressArea", hexEdit->addressArea());
        settings.setValue("AsciiArea", hexEdit->asciiArea());
        settings.setValue("Highlighting", hexEdit->highlighting());
        settings.setValue("OverwriteMode", hexEdit->overwriteMode());
        settings.setValue("ShowHexGrid", hexEdit->showHexGrid());
        settings.setValue("Autosize", hexEdit->dynamicBytesPerLine());
        settings.setValue("HexCaps", hexEdit->hexCaps());
        settings.setValue("AddressAreaWidth", hexEdit->addressWidth());
        settings.setValue("BytesPerLine", hexEdit->bytesPerLine());
    }
    
    settings.sync();

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

void MainWindow::addToRecentFiles(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    QSettings settings;
    QStringList files = settings.value(kRecentFilesKey).toStringList();

    files.removeAll(fileName);
    files.prepend(fileName);

    while (files.size() > kMaxRecentFiles)
        files.removeLast();

    settings.setValue(kRecentFilesKey, files);
    updateRecentFileMenu();
}

void MainWindow::addToRecentTables(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    QSettings settings;
    QStringList files = settings.value(kRecentTablesKey).toStringList();

    files.removeAll(fileName);
    files.prepend(fileName);

    while (files.size() > kMaxRecentTables)
        files.removeLast();

    settings.setValue(kRecentTablesKey, files);
    updateRecentTableMenu();
}

void MainWindow::updateRecentFileMenu()
{
    QSettings settings;
    QStringList files = settings.value(kRecentFilesKey).toStringList();

    recentFileMenu->clear();

    if (files.isEmpty())
    {
        recentFileMenu->setEnabled(false);
        return;
    }

    recentFileMenu->setEnabled(true);

    for (int i = 0; i < files.size(); ++i)
    {
        const QString fileName = files[i];
        const QString text = QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(fileName).fileName());

        QAction *action = recentFileMenu->addAction(text);
        action->setData(fileName);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentFile);
    }
}

void MainWindow::updateRecentTableMenu()
{
    QSettings settings;
    QStringList files = settings.value(kRecentTablesKey).toStringList();

    recentTableMenu->clear();

    if (files.isEmpty())
    {
        recentTableMenu->setEnabled(false);
        return;
    }

    recentTableMenu->setEnabled(true);

    for (int i = 0; i < files.size(); ++i)
    {
        const QString fileName = files[i];
        const QString text = QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(fileName).fileName());

        QAction *action = recentTableMenu->addAction(text);
        action->setData(fileName);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentTable);
    }
}

void MainWindow::openRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString fileName = action->data().toString();
    if (!fileName.isEmpty())
        loadFile(fileName);
}

void MainWindow::openRecentTable()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString fileName = action->data().toString();
    if (!fileName.isEmpty())
    {
        // Check if file still exists
        if (!QFile::exists(fileName))
        {
            QMessageBox::warning(this, tr("Error"), tr("File not found: %1").arg(fileName));
            return;
        }

        if (tb)
            delete tb;

        try
        {
            tb = new TranslationTable(fileName);
            useTableAct->setEnabled(true);
            editTableAct->setEnabled(true);
            saveTableAct->setEnabled(true);
            saveTableAsAct->setEnabled(true);
            hexEdit->setTranslationTable(tb);
            tableFilePath = fileName;
            statusBar()->showMessage(tr("Table loaded"), 2000);
        }
        catch (const std::exception &e)
        {
            QMessageBox::warning(this, tr("Error"), tr("Failed to load table: %1").arg(QString::fromStdString(e.what())));
        }
    }
}

void MainWindow::updateEndiannes()
{
    // Cycle: Little-endian → Big-endian → Swapped bytes → Little-endian
    switch (hexEdit->byteOrder) {
    case ByteOrder::LittleEndian:
        hexEdit->byteOrder = ByteOrder::BigEndian;
        break;
    case ByteOrder::BigEndian:
        hexEdit->byteOrder = ByteOrder::SwappedBytes;
        break;
    case ByteOrder::SwappedBytes:
    default:
        hexEdit->byteOrder = ByteOrder::LittleEndian;
        break;
    }

    updateEndiannesLabel();
    setAddress(hexEdit->getCurrentOffset());
}

void MainWindow::updateEndiannesLabel()
{
    switch (hexEdit->byteOrder) {
    case ByteOrder::BigEndian:
        lbEndiannes->setText(tr("Big-endian"));
        break;
    case ByteOrder::SwappedBytes:
        lbEndiannes->setText(tr("Byte-swapped"));
        break;
    default:
        lbEndiannes->setText(tr("Little-endian"));
        break;
    }
}
