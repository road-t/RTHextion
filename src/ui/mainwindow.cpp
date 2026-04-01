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
#include <QUrl>
#include <QShortcut>
#include <QSplitter>
#include <QDir>
#include <QComboBox>
#include <QLocale>
#include <QTimer>
#include <QTabWidget>
#include <QStyleFactory>
#include <QMouseEvent>
#include <QChildEvent>
#include <QInputDialog>
#include <QFile>
#ifdef Q_OS_MAC
#include "macostheme.h"
#endif
#include <algorithm>

#include "QtWidgets/qpushbutton.h"
#include "appinfo.h"
#include "langtranslator.h"
#include "mainwindow.h"
#include "DockTitleBar.h"
#include "TablesDockWidget.h"
#include "ChangesDockWidget.h"
#include "romdetect.h"
#include "romchecksum.h"
#include "encodingdetect.h"

namespace
{
    const char *kLastFileDirKey = "Paths/LastFileDir";
    const char *kLastTableDirKey = "Paths/LastTableDir";
    const char *kLastDumpDirKey = "Paths/LastDumpDir";
    const char *kMainWindowStateKey = "MainWindow/State";
    const char *kRecentFilesKey = "RecentFiles";
    const char *kRecentTablesKey = "RecentTables";
    const char *kRecentProjectsKey = "RecentProjects";
    const int kMaxRecentFiles = 10;
    const int kMaxRecentTables = 10;
    const int kMaxRecentProjects = 10;

    bool isTableLikeFilePath(const QString &path)
    {
        const QString ext = QFileInfo(path).suffix().toLower();
        return ext == QStringLiteral("tbl")
            || ext == QStringLiteral("tab")
            || ext == QStringLiteral("table");
    }

    QString chooseTableImportEncoding(QWidget *parent, const QString &fileName, bool *accepted = nullptr)
    {
        if (accepted)
            *accepted = true;

        QFile rawFile(fileName);
        if (!rawFile.open(QIODevice::ReadOnly))
            return QString();

        const QByteArray raw = rawFile.readAll();
        rawFile.close();

        if (!TranslationTable::hasNonAsciiValueBytes(raw))
            return QString();

        const QStringList encodings = TranslationTable::supportedImportEncodings();
        if (encodings.isEmpty())
            return QString();

        const QString guessed = TranslationTable::guessImportEncoding(raw);
        const int defaultIndex = qMax(0, encodings.indexOf(guessed));

        bool ok = false;
        const QString selected = QInputDialog::getItem(
            parent,
            MainWindow::tr("Table encoding"),
            MainWindow::tr("Select encoding for imported table:"),
            encodings,
            defaultIndex,
            false,
            &ok);

        if (accepted)
            *accepted = ok;

        return ok ? selected : QString();
    }

    QChar readSingleCharSetting(const QSettings &settings, const char *key, const QChar &fallback)
    {
        const QString value = settings.value(key, QString(fallback)).toString();
        return value.isEmpty() ? fallback : value.at(0);
    }

    void mergeRunsAt(QVector<QPair<qint64, QByteArray>> &runs, int idx)
    {
        if (idx < 0 || idx >= runs.size())
            return;

        if (idx > 0) {
            const qint64 prevEnd = runs[idx - 1].first + runs[idx - 1].second.size();
            if (prevEnd == runs[idx].first) {
                runs[idx - 1].second.append(runs[idx].second);
                runs.removeAt(idx);
                idx -= 1;
            }
        }

        if (idx >= 0 && idx + 1 < runs.size()) {
            const qint64 curEnd = runs[idx].first + runs[idx].second.size();
            if (curEnd == runs[idx + 1].first) {
                runs[idx].second.append(runs[idx + 1].second);
                runs.removeAt(idx + 1);
            }
        }
    }

    void removeByteFromRun(QVector<QPair<qint64, QByteArray>> &runs, int runIdx, int byteIdx)
    {
        if (runIdx < 0 || runIdx >= runs.size())
            return;
        QByteArray &bytes = runs[runIdx].second;
        if (byteIdx < 0 || byteIdx >= bytes.size())
            return;

        if (bytes.size() == 1) {
            runs.removeAt(runIdx);
            return;
        }

        if (byteIdx == 0) {
            bytes.remove(0, 1);
            runs[runIdx].first += 1;
            return;
        }

        if (byteIdx == bytes.size() - 1) {
            bytes.chop(1);
            return;
        }

        const qint64 rightStart = runs[runIdx].first + byteIdx + 1;
        QByteArray right = bytes.mid(byteIdx + 1);
        bytes.truncate(byteIdx);
        runs.insert(runIdx + 1, {rightStart, right});
    }

    // Adjust originalBytes run offsets when bytes are inserted or deleted.
    // `changeOffset` is the position where the size change occurred and
    // `delta` is positive for insertion (new bytes added) or negative for
    // deletion (bytes removed).
    void adjustOriginalBytesForSizeChange(QVector<QPair<qint64, QByteArray>> &runs,
                                          qint64 changeOffset, qint64 delta)
    {
        if (delta == 0 || runs.isEmpty())
            return;

        if (delta > 0) {
            // Insertion: shift entries at/after changeOffset by +delta.
            // An entry that spans the insert point is split: the left part
            // stays in place, the right part is shifted.
            for (int i = runs.size() - 1; i >= 0; --i) {
                auto &entry = runs[i];
                if (entry.first >= changeOffset) {
                    entry.first += delta;
                } else if (entry.first + entry.second.size() > changeOffset) {
                    const int splitPos = static_cast<int>(changeOffset - entry.first);
                    QByteArray rightPart = entry.second.mid(splitPos);
                    entry.second.truncate(splitPos);
                    runs.insert(i + 1, {changeOffset + delta, rightPart});
                    if (entry.second.isEmpty())
                        runs.removeAt(i);
                }
            }
        } else {
            const qint64 absDelta = -delta;
            const qint64 delEnd = changeOffset + absDelta;

            for (int i = runs.size() - 1; i >= 0; --i) {
                auto &entry = runs[i];
                const qint64 entryEnd = entry.first + entry.second.size();

                if (entry.first >= delEnd) {
                    // Entirely after deleted range: shift left.
                    entry.first -= absDelta;
                } else if (entry.first >= changeOffset) {
                    if (entryEnd <= delEnd) {
                        // Entirely inside deleted range: remove.
                        runs.removeAt(i);
                    } else {
                        // Starts inside deleted range, extends past it: trim front.
                        const int trimFront = static_cast<int>(delEnd - entry.first);
                        entry.second.remove(0, trimFront);
                        entry.first = changeOffset;
                    }
                } else if (entryEnd > changeOffset) {
                    if (entryEnd <= delEnd) {
                        // Starts before, ends inside deleted range: trim tail.
                        entry.second.truncate(static_cast<int>(changeOffset - entry.first));
                    } else {
                        // Spans entire deleted range: remove middle.
                        const int keepLeft = static_cast<int>(changeOffset - entry.first);
                        const int removeLen = static_cast<int>(absDelta);
                        entry.second.remove(keepLeft, removeLen);
                    }
                    if (entry.second.isEmpty())
                        runs.removeAt(i);
                }
            }
        }
    }

    void applyIncrementalOriginalByteChange(QVector<QPair<qint64, QByteArray>> &runs,
                                            qint64 offset,
                                            char oldByte,
                                            char newByte)
    {
        int nearestIdx = -1;
        for (int i = 0; i < runs.size(); ++i) {
            if (runs[i].first <= offset)
                nearestIdx = i;
            else
                break;
        }

        if (nearestIdx >= 0) {
            const qint64 start = runs[nearestIdx].first;
            const qint64 rel = offset - start;
            QByteArray &bytes = runs[nearestIdx].second;
            if (rel >= 0 && rel < bytes.size()) {
                const char originalByte = bytes.at(static_cast<int>(rel));
                // Byte returned to its original value: remove it from tracked runs.
                if (newByte == originalByte)
                    removeByteFromRun(runs, nearestIdx, static_cast<int>(rel));
                return;
            }
        }

        // No existing tracked run contains this byte.
        // If value did not effectively change, nothing to track.
        if (newByte == oldByte)
            return;

        const int insertPos = nearestIdx + 1;
        const bool appendToNearest = (nearestIdx >= 0) &&
                                     (runs[nearestIdx].first + runs[nearestIdx].second.size() == offset);
        if (appendToNearest) {
            runs[nearestIdx].second.append(oldByte);
            mergeRunsAt(runs, nearestIdx);
            return;
        }

        runs.insert(insertPos, {offset, QByteArray(1, oldByte)});
        mergeRunsAt(runs, insertPos);
    }
}

/*****************************************************************************/
/* Public methods */
/*****************************************************************************/
MainWindow::MainWindow()
    : hexEdit(nullptr), optionsDialog(nullptr), searchDialog(nullptr), jumpToDialog(nullptr), pointersDialog(nullptr), semiAutoTableDialog(nullptr), dumpScriptDialog(nullptr), insertScriptDialog(nullptr)
{
    setAcceptDrops( true );
    init();
}

/*****************************************************************************/
/* Protected methods */
/*****************************************************************************/
void MainWindow::closeEvent(QCloseEvent *event)
{
    // Check each session for unsaved changes
    for (int i = 0; i < m_sessions.size(); ++i) {
        m_tabWidget->setCurrentIndex(i);
        if (!maybeSave()) {
            event->ignore();
            return;
        }
        if (!maybeSaveProject()) {
            event->ignore();
            return;
        }
    }

    // Silently persist cursor position for each project that was not
    // otherwise saved (e.g. when only the cursor moved but nothing else changed).
    for (int i = 0; i < m_sessions.size(); ++i) {
        m_tabWidget->setCurrentIndex(i);
        if (m_document && !m_document->projectFilePath.isEmpty())
            saveProjectImpl(m_document->projectFilePath);
    }

    m_closing = true;
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
        for (int i = 0; i < urls.size(); ++i) {
            const QString filePath = urls.at(i).toLocalFile();
            if (i == 0 && !m_sessions.isEmpty() && isUntitled && !hexEdit->isModified())
                loadFile(filePath);
            else
                loadFileInNewTab(filePath);
        }
        event->accept();
    }
}

bool MainWindow::event(QEvent *e)
{
    // Track child widgets added to the main window (including internal separators)
    // and install event filters on potential dock area separators.
    if (e->type() == QEvent::ChildAdded) {
        auto *ce = static_cast<QChildEvent *>(e);
        if (QWidget *w = qobject_cast<QWidget *>(ce->child())) {
            // Skip known widget types — only separators remain
            if (!qobject_cast<QDockWidget *>(w) && !qobject_cast<QToolBar *>(w)
                && !qobject_cast<QMenuBar *>(w) && !qobject_cast<QStatusBar *>(w)
                && w != centralWidget() && w != m_tabWidget) {
                w->installEventFilter(this);
            }
        }
    }
    return QMainWindow::event(e);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == lbEncoding && event && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent && mouseEvent->button() == Qt::LeftButton) {
            openEncodingSelectionDialog();
            return true;
        }
    }

    // Double-click on dock area separator → toggle area collapse
    if (event && event->type() == QEvent::MouseButtonDblClick) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            QWidget *w = qobject_cast<QWidget *>(watched);
            if (w && w->parentWidget() == this
                && !qobject_cast<QDockWidget *>(w)
                && !qobject_cast<QToolBar *>(w)
                && !qobject_cast<QMenuBar *>(w)
                && !qobject_cast<QStatusBar *>(w)
                && w != centralWidget() && w != m_tabWidget) {
                Qt::DockWidgetArea area = separatorDockArea(w);
                if (area != Qt::NoDockWidgetArea) {
                    setDockAreaCollapsed(area, !isDockAreaCollapsed(area));
                    return true;
                }
            }
        }
    }

    // Single click on dock area separator when already collapsed → expand
    if (event && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            QWidget *w = qobject_cast<QWidget *>(watched);
            if (w && w->parentWidget() == this
                && !qobject_cast<QDockWidget *>(w)
                && !qobject_cast<QToolBar *>(w)
                && !qobject_cast<QMenuBar *>(w)
                && !qobject_cast<QStatusBar *>(w)
                && w != centralWidget() && w != m_tabWidget) {
                Qt::DockWidgetArea area = separatorDockArea(w);
                if (area != Qt::NoDockWidgetArea && isDockAreaCollapsed(area)) {
                    setDockAreaCollapsed(area, false);
                    return true;
                }
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
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
    if (m_closing)
        return;
    if (!hexEdit)
        return;
    setWindowModified(hexEdit->isModified());
    updateActionStates();
    updateTabTitle(m_tabWidget->currentIndex());
}

void MainWindow::onHexDataChangedAt(qint64 offset)
{
    if (m_closing || !m_document)
        return;

    const qint64 newSize = hexEdit->dataSize();

    if (m_changeTrackingSnapshot.isNull()) {
        if (newSize <= 256 * 1024 * 1024)
            m_changeTrackingSnapshot = hexEdit->data();
        else
            m_changeTrackingSnapshot = QByteArray("");
        return;
    }

    const qint64 oldSize = m_changeTrackingSnapshot.size();

    if (oldSize == newSize && offset >= 0 && offset < newSize) {
        // Same size: only a single overwrite. Read just the changed byte(s)
        // from the already-buffered visible data or chunks (not the whole file).
        const char oldByte = m_changeTrackingSnapshot.at(static_cast<int>(offset));
        const QByteArray oneByte = hexEdit->dataAt(offset, 1);
        const char newByte = oneByte.isEmpty() ? oldByte : oneByte.at(0);
        if (oldByte != newByte) {
            applyIncrementalOriginalByteChange(m_document->originalBytes, offset, oldByte, newByte);
            m_changeTrackingSnapshot[static_cast<int>(offset)] = newByte;
        }
        // A macro (e.g. multi-byte overwrite from script insert) fires
        // dataChangedAt only once with the last byte's offset.  Detect nearby
        // bytes that may also have changed (scan a small window, not the whole file).
        {
            const qint64 scanStart = qMax<qint64>(0, offset - 64);
            const qint64 scanEnd = qMin(newSize, offset + 65);
            const QByteArray region = hexEdit->dataAt(scanStart, scanEnd - scanStart);
            for (qint64 i = scanStart; i < scanEnd && (i - scanStart) < region.size(); ++i) {
                const char ob = m_changeTrackingSnapshot.at(static_cast<int>(i));
                const char nb = region.at(static_cast<int>(i - scanStart));
                if (ob != nb) {
                    applyIncrementalOriginalByteChange(m_document->originalBytes, i, ob, nb);
                    m_changeTrackingSnapshot[static_cast<int>(i)] = nb;
                }
            }
        }
    } else {
        // Insert/delete: adjust tracked original-byte offsets so they stay
        // aligned with the shifted data, then resync snapshot.
        const qint64 delta = newSize - oldSize;
        if (delta != 0 && offset >= 0 && !m_document->originalBytes.isEmpty())
            adjustOriginalBytesForSizeChange(m_document->originalBytes, offset, delta);
        if (newSize <= 256 * 1024 * 1024)
            m_changeTrackingSnapshot = hexEdit->data();
        else
            m_changeTrackingSnapshot = QByteArray("");
    }

    if (m_changesUiUpdateTimer)
        m_changesUiUpdateTimer->start();
}

void MainWindow::flushChangesUiUpdate()
{
    if (m_closing)
        return;

    if (m_changesDock && m_changesDock->isVisible())
        refreshChangesView();

    if (showChangesAct && showChangesAct->isChecked())
        updateChangedBytesHighlight();
}

void MainWindow::closeFile()
{
    if (!maybeSave())
        return;
    if (!maybeSaveProject())
        return;

    const bool hadProject = m_document && !m_document->projectFilePath.isEmpty();
    m_document->projectFilePath.clear();
    m_document->projectName.clear();
    m_projectModified = false;
    m_document->originalBytes.clear();
    m_document->originalFileSize = -1;
    hexEdit->setData(QByteArray());
    m_changeTrackingSnapshot = QByteArray();
    hexEdit->clearPointers();
    resetNavigationHistory();
    showPointersAct->setEnabled(false);

    // Remove table: always when a project existed, otherwise respect settings
    {
        QSettings s;
        if (hadProject || s.value("ResetTableOnClose", false).toBool()) {
            hexEdit->removeTranslationTable();
            tb = nullptr;
            m_tablesDock->clearAll();
            useTableAct->setChecked(false);
            useTableAct->setEnabled(false);
            editTableAct->setEnabled(false);
            saveTableAct->setEnabled(false);
            saveTableAsAct->setEnabled(false);
        }
        if (s.value("ResetEncodingOnClose", false).toBool()) {
            const QString defEnc = s.value("DefaultEncoding", QStringLiteral("ASCII")).toString();
            m_currentEncoding = defEnc;
            hexEdit->setCurrentEncoding(defEnc);
            if (lbEncoding)
                lbEncoding->setText(defEnc);
            syncEncodingMenu();
        }
    }

    setCurrentFile("");
    statusBar()->showMessage(tr("File closed"), 2000);
}

void MainWindow::newFile()
{
    // In multi-tab mode: create a new tab if current has content, else reuse current empty tab
    if (m_sessions.isEmpty() || !isUntitled || (hexEdit && hexEdit->isModified())) {
        createSession();
    }
    setCurrentFile("");
    statusBar()->showMessage(tr("New file created"), 2000);
}

void MainWindow::open()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Open file"), lastDirectory(kLastFileDirKey));

    if (!fileName.isEmpty()) {
        // Open in new tab if current has content; handle no-tabs state safely
        if (m_sessions.isEmpty() || !isUntitled || hexEdit->isModified()) {
            loadFileInNewTab(fileName);
        } else {
            loadFile(fileName);
        }
    }
}

void MainWindow::revert()
{
    if (isUntitled)
    {
        // For new files, just clear the data
        if (QMessageBox::warning(this, tr("Clear data"), tr("Clear all data and changes?"), QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
        {
            m_document->originalBytes.clear();
            m_document->originalFileSize = -1;
            hexEdit->setData(QByteArray());
            m_changeTrackingSnapshot = QByteArray();
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
            m_currentSession->file.setFileName(curFile);
            if (hexEdit->setData(m_currentSession->file))
            {
                if (hexEdit->dataSize() <= 256 * 1024 * 1024)
                    m_changeTrackingSnapshot = hexEdit->data();
                else
                    m_changeTrackingSnapshot = QByteArray("");
                resetNavigationHistory();
                statusBar()->showMessage(tr("File reverted"), 2000);
            }
            else
            {
                QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                                     tr("Cannot read file %1:\n%2.")
                                     .arg(curFile)
                                     .arg(m_currentSession->file.errorString()));
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
    if (m_document && !m_document->projectFilePath.isEmpty())
        m_projectModified = true;
}

void MainWindow::findNext()
{
    if (!searchDialog)
        searchDialog = new SearchDialog(hexEdit, this);
    else
        searchDialog->setHexEdit(hexEdit);

    searchDialog->setAvailableTables(m_tablesDock->allTables(),
                                     m_tablesDock->currentIndex(),
                                     useTableAct && useTableAct->isChecked());
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
    bool ok;
    if (isUntitled)
        ok = saveAs();
    else
        ok = saveFile(curFile);

    // Also save project if one is open and named
    if (ok && m_document && !m_document->projectFilePath.isEmpty())
        saveProjectImpl(m_document->projectFilePath);

    return ok;
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

        const QString data = hexEdit->decodeTextForCurrentEncoding(hexEdit->getRawSelection());
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
        file.write(hexEdit->toReadableString().toUtf8());
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
    if (!hexEdit)
        return;
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
    if (!lbSelection)
        return;

    // end is now exclusive (half-open range); real byte count = end - start.
    // A selection of exactly 1 means only the cursor byte = no real selection.
    auto len = end - start;
    auto selBytes = len > 1 ? len : 0;   // bytes actually selected (excluding mere cursor byte)

    auto text = selBytes ?
        QString("0x%1-0x%2: %3")
            .arg(start, 2, 16, QChar('0'))
            .arg(end - 1, 2, 16, QChar('0'))   // end-1 = last included byte
            .arg(selBytes)
        : tr("No selection");

    lbSelection->setText(text);
    const bool canModify = hexEdit && !hexEdit->isReadOnly() && !hexEdit->showOriginal();
    saveSelectionReadable->setEnabled(selBytes > 0);
    copyAct->setEnabled(selBytes > 0);
    cutAct->setEnabled(selBytes > 0 && !hexEdit->overwriteMode() && canModify);
    pasteAct->setEnabled(canModify && !QApplication::clipboard()->text().isEmpty());

    updateScriptMenuState(selBytes > 0);
}

void MainWindow::setOverwriteMode(bool mode)
{
    lbOverwriteMode->setText(mode ? tr("REPLACE") : tr("INSERT"));
}

void MainWindow::setSize(qint64 size)
{
    if (lbSize)
        lbSize->setText(QString("%1").arg(size));
}

void MainWindow::updateStatusBarVisibility()
{
    if (lbEncoding)
        lbEncoding->setVisible(showStatusEncodingAct->isChecked());
    if (lbValueByte)
        lbValueByte->setVisible(showStatusByteAct->isChecked());
    if (lbValueWord)
        lbValueWord->setVisible(showStatusWordAct->isChecked());
    if (lbValueDword)
        lbValueDword->setVisible(showStatusDwordAct->isChecked());

    if (lbSelection)
        lbSelection->setVisible(showStatusSelectionAct->isChecked());

    const bool showAddress = showStatusAddressAct->isChecked();
    if (lbAddress)
        lbAddress->setVisible(showAddress);

    const bool showSize = showStatusSizeAct->isChecked();
    if (lbSizeName)
        lbSizeName->setVisible(showSize);
    if (lbSize)
        lbSize->setVisible(showSize);

    const bool showMode = showStatusModeAct->isChecked();

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
    else
        searchDialog->setHexEdit(hexEdit);

    searchDialog->setAvailableTables(m_tablesDock->allTables(),
                                     m_tablesDock->currentIndex(),
                                     useTableAct && useTableAct->isChecked());

    // Restore per-tab search state
    if (m_currentSession) {
        SearchDialog::State s;
        s.findText    = m_currentSession->searchFindText;
        s.findFormat  = m_currentSession->searchFindFormat;
        s.replaceText = m_currentSession->searchReplaceText;
        s.replaceFormat = m_currentSession->searchReplaceFormat;
        s.relative    = m_currentSession->searchRelative;
        searchDialog->setDialogState(s);
    }

    searchDialog->show();
}

void MainWindow::showPointersDialog()
{
    if (!pointersDialog)
    {
        pointersDialog = new PointersDialog(hexEdit, this);
        connect(pointersDialog, SIGNAL(accepted()), this, SLOT(pointersUpdated()));
        connect(pointersDialog, &PointersDialog::searchCompleted, this, &MainWindow::onQuickSearchCompleted);
        pointersDialog->setDock(m_pointersDock);
    }
    // Always sync to the active tab's editor and ROM profile before showing
    pointersDialog->setHexEdit(hexEdit);
    m_pointersDock->show();
    m_pointersDock->raise();
    pointersDialog->show();
    // Override profile AFTER show so it takes effect regardless of _profileInitialized
    pointersDialog->setRomProfile(m_detectedRomType, m_pointerOffset);

    // Restore per-tab "where" and "text optimization" options
    if (m_currentSession) {
        PointersDialog::State ps;
        ps.searchDir        = m_currentSession->ptrSearchDir;
        ps.excludeSelection = m_currentSession->ptrExcludeSelection;
        ps.alignedOnly      = m_currentSession->ptrAlignedOnly;
        ps.optimize         = m_currentSession->ptrOptimize;
        pointersDialog->setDialogState(ps);
    }
}

void MainWindow::hexEditContextMenu(const QPoint &globalPos, qint64 bytePos)
{
    PointerListModel *model = hexEdit->pointers();
    const qint64 pointerStart = hexEdit->pointerStartAt(bytePos, currentPointerSize());
    const qint64 fileSize = hexEdit->dataSize();

    const bool hasSelection  = hexEdit->getSelectionEnd() - hexEdit->getSelectionBegin() > 1;
    const bool isOverwrite   = hexEdit->overwriteMode();
    const bool isReadOnly    = hexEdit->isReadOnly();
    const bool clickedAscii  = hexEdit->editAreaIsAscii();

    const qint64 ptrOffset = currentPointerOffset();

    struct PointerLengthOption {
        int size = 0;
        qint64 target = -1;
    };

    QVector<PointerLengthOption> addPointerOptions;
    addPointerOptions.reserve(2);
    for (int size : {2, 4})
    {
        if (bytePos < 0 || bytePos + size > fileSize)
            continue;

        const QByteArray rawPointer = hexEdit->dataAt(bytePos, size);
        if (rawPointer.size() != size)
            continue;

        const quint64 decodedPointer = decodePointer(reinterpret_cast<const uchar *>(rawPointer.constData()), size, hexEdit->byteOrder);
        const qint64 fileTarget = static_cast<qint64>(decodedPointer) + ptrOffset;
        addPointerOptions.append({size, fileTarget});
    }

    auto addPointerLengthMenu = [&](QMenu &menu) -> QMap<QAction *, PointerLengthOption>
    {
        QMap<QAction *, PointerLengthOption> actions;
        QMenu *sub = menu.addMenu(tr("Add as pointer"));
        for (const PointerLengthOption &opt : addPointerOptions)
        {
            const bool valid = (opt.target >= 0 && opt.target <= fileSize);
            const QString targetStr = valid
                ? QStringLiteral("0x") + QString::number(opt.target, 16).toUpper()
                : tr("out of range");
            QAction *act = sub->addAction(tr("%1-byte \u2192 %2").arg(opt.size).arg(targetStr));
            act->setEnabled(valid);
            actions.insert(act, opt);
        }
        sub->setEnabled(!addPointerOptions.isEmpty());
        return actions;
    };

    auto refreshPointersUi = [this]()
    {
        if (pointersDialog)
            pointersDialog->refreshFromTable();
        pointersUpdated();
        hexEdit->viewport()->update();
    };

    auto selectedPointerOffsetsForDrop = [&]() -> QVector<qint64>
    {
        QVector<qint64> result;
        if (!hasSelection || !model)
            return result;

        const qint64 selBegin = hexEdit->getSelectionBegin();
        const qint64 selEnd = hexEdit->getSelectionEnd();
        if (selEnd <= selBegin)
            return result;

        QSet<qint64> unique;
        const QList<qint64> keys = model->pointerKeys();
        unique.reserve(keys.size());

        for (qint64 ptrOfs : keys)
        {
            const qint64 targetOfs = model->getOffset(ptrOfs);
            const bool pointerInSelection = (ptrOfs >= selBegin && ptrOfs < selEnd);
            const bool targetInSelection = (targetOfs >= selBegin && targetOfs < selEnd);
            if (pointerInSelection || targetInSelection)
                unique.insert(ptrOfs);
        }

        result.reserve(unique.size());
        for (qint64 ptrOfs : unique)
            result.append(ptrOfs);
        return result;
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
                // ASCII area: use active table, otherwise current text encoding
                QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
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
                // ASCII area: use active table, otherwise current text encoding
                QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
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
                // ASCII area: use active table, otherwise current text encoding
                ba = hexEdit->encodeTextForCurrentEncoding(QApplication::clipboard()->text());
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

    // 0) Original-view mode: minimal read-only context menu.
    if (hexEdit->showOriginal())
    {
        QMenu menu(this);
        const QString addrText = QStringLiteral("0x") + QString("%1").arg(bytePos, 8, 16, QChar('0')).toUpper();

        QAction *copyAddrAct = menu.addAction(tr("Copy address"));
        QAction *saveAsDumpAct = nullptr;
        if (clickedAscii)
            saveAsDumpAct = menu.addAction(tr("Save as dump"));

        // Revert to original is still useful even in original view —
        // it writes the original byte back into the current (edited) data.
        QAction *revertOrigAct = nullptr;
        if (bytePos >= 0 && m_document && !m_document->originalBytes.isEmpty())
        {
            for (const auto &entry : m_document->originalBytes)
            {
                const qint64 off = entry.first;
                const QByteArray &orig = entry.second;
                if (bytePos >= off && bytePos < off + orig.size())
                {
                    const uint8_t origByte = static_cast<uint8_t>(orig.at(bytePos - off));
                    menu.addSeparator();
                    revertOrigAct = menu.addAction(
                        tr("Revert to original: %1")
                            .arg(QString::number(origByte, 16).toUpper().rightJustified(2, QLatin1Char('0'))));
                    break;
                }
            }
        }

        QAction *chosen = menu.exec(globalPos);
        if (chosen == copyAddrAct)
        {
            QApplication::clipboard()->setText(addrText);
        }
        else if (saveAsDumpAct && chosen == saveAsDumpAct)
        {
            dumpScript();
        }
        else if (revertOrigAct && chosen == revertOrigAct)
        {
            for (int ei = 0; ei < m_document->originalBytes.size(); ++ei)
            {
                auto &entry = m_document->originalBytes[ei];
                const qint64 off = entry.first;
                QByteArray &orig = entry.second;
                if (bytePos >= off && bytePos < off + orig.size())
                {
                    const char origByte = orig.at(static_cast<int>(bytePos - off));
                    hexEdit->replace(bytePos, 1, QByteArray(1, origByte));
                    const int localIdx = static_cast<int>(bytePos - off);
                    if (orig.size() == 1) {
                        m_document->originalBytes.removeAt(ei);
                    } else if (localIdx == 0) {
                        entry.first += 1;
                        orig.remove(0, 1);
                    } else if (localIdx == orig.size() - 1) {
                        orig.chop(1);
                    } else {
                        const QByteArray tail = orig.mid(localIdx + 1);
                        orig.truncate(localIdx);
                        m_document->originalBytes.insert(ei + 1, {bytePos + 1, tail});
                    }
                    break;
                }
            }
            if (showChangesAct->isChecked())
                updateChangedBytesHighlight();
            updateActionStates();
        }
        return;
    }

    // 1) Right click on a pointer entry (full pointer length is clickable).
    if (pointerStart >= 0 && hexEdit->showPointers())
    {
        QMenu menu(this);
        QAction *jumpAct = menu.addAction(tr("Jump to offset"));
        QAction *dropAct = menu.addAction(tr("Drop pointer"));
        QAction *dropSelectionPtrsAct = nullptr;
        QVector<qint64> dropSelectionPtrs;
        if (hasSelection)
        {
            dropSelectionPtrs = selectedPointerOffsetsForDrop();
            dropSelectionPtrsAct = menu.addAction(tr("Drop pointers"));
            dropSelectionPtrsAct->setEnabled(!dropSelectionPtrs.isEmpty());
        }
        menu.addSeparator();
        QAction *editScriptAct1 = nullptr;
        QMap<QAction *, PointerLengthOption> addPointerActs;
        if (!hasSelection)
            addPointerActs = addPointerLengthMenu(menu);

        auto clipActs = addClipboardActions(menu);

        if (hasSelection) {
            menu.addSeparator();
            editScriptAct1 = menu.addAction(tr("Edit script..."));
        }

        QAction *chosen = menu.exec(globalPos);
        if (handleClipboardAction(chosen, clipActs))
            return;

        if (editScriptAct1 && chosen == editScriptAct1)
        {
            dumpScript();
        }
        else if (addPointerActs.contains(chosen))
        {
            const PointerLengthOption opt = addPointerActs.value(chosen);
            if (hexEdit->addPointerUndoable(bytePos, opt.target, opt.size))
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
        else if (dropSelectionPtrsAct && chosen == dropSelectionPtrsAct)
        {
            if (hexEdit->removePointersUndoable(dropSelectionPtrs) > 0)
                refreshPointersUi();
        }
        return;
    }

    // 2) Right click on offset that has incoming pointers.
    if (model->hasOffset(bytePos) && hexEdit->showPointers())
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
        QAction *dropSelectionPtrsAct = nullptr;
        QVector<qint64> dropSelectionPtrs;
        if (hasSelection)
        {
            dropSelectionPtrs = selectedPointerOffsetsForDrop();
            dropSelectionPtrsAct = menu.addAction(tr("Drop pointers"));
            dropSelectionPtrsAct->setEnabled(!dropSelectionPtrs.isEmpty());
        }
        QAction *editScriptAct2 = nullptr;
        QMap<QAction *, PointerLengthOption> addPointerActs;
        if (!hasSelection)
            addPointerActs = addPointerLengthMenu(menu);

        auto clipActs = addClipboardActions(menu);

        if (hasSelection) {
            menu.addSeparator();
            editScriptAct2 = menu.addAction(tr("Edit script..."));
        }

        QAction *chosen = menu.exec(globalPos);
        if (!chosen)
            return;

        if (handleClipboardAction(chosen, clipActs))
            return;

        if (editScriptAct2 && chosen == editScriptAct2)
        {
            dumpScript();
            return;
        }

        if (addPointerActs.contains(chosen))
        {
            const PointerLengthOption opt = addPointerActs.value(chosen);
            if (hexEdit->addPointerUndoable(bytePos, opt.target, opt.size))
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
        else if (dropSelectionPtrsAct && chosen == dropSelectionPtrsAct)
        {
            if (hexEdit->removePointersUndoable(dropSelectionPtrs) > 0)
                refreshPointersUi();
        }
        return;
    }

    // 3) Default hex-area context menu.
    QMenu menu(this);

    QAction *quickSearchAct = menu.addAction(tr("Quick pointer search"));
    QAction *findPtrAct     = menu.addAction(tr("Find pointers") + QString("..."));
    QMap<QAction *, PointerLengthOption> addPointerActs;
    if (!hasSelection)
        addPointerActs = addPointerLengthMenu(menu);
    QAction *dropSelectionPtrsAct = nullptr;
    QVector<qint64> dropSelectionPtrs;
    if (hasSelection)
    {
        dropSelectionPtrs = selectedPointerOffsetsForDrop();
        dropSelectionPtrsAct = menu.addAction(tr("Drop pointers"));
        dropSelectionPtrsAct->setEnabled(!dropSelectionPtrs.isEmpty());
    }

    QAction *saveAsDumpAct = nullptr;
    if (clickedAscii)
    {
        menu.addSeparator();
        saveAsDumpAct = menu.addAction(tr("Save as dump"));
    }

    auto clipActs = addClipboardActions(menu, !clickedAscii);  // Show hex labels only in hex area

    // "Revert to original" — shown when bytePos is in project originalBytes
    QAction *revertOrigAct = nullptr;
    if (bytePos >= 0 && m_document && !m_document->originalBytes.isEmpty())
    {
        for (const auto &entry : m_document->originalBytes)
        {
            const qint64 off = entry.first;
            const QByteArray &orig = entry.second;
            if (bytePos >= off && bytePos < off + orig.size())
            {
                const uint8_t origByte = static_cast<uint8_t>(orig.at(bytePos - off));
                menu.addSeparator();
                revertOrigAct = menu.addAction(
                    tr("Revert to original: %1")
                        .arg(QString::number(origByte, 16).toUpper().rightJustified(2, QLatin1Char('0'))));
                break;
            }
        }
    }

    QAction *editScriptAct3 = nullptr;
    if (hasSelection) {
        menu.addSeparator();
        editScriptAct3 = menu.addAction(tr("Edit script..."));
    }

    QAction *chosen = menu.exec(globalPos);
    if (handleClipboardAction(chosen, clipActs))
        return;

    if (revertOrigAct && chosen == revertOrigAct)
    {
        // Find the entry, write back original byte, then remove it from tracking
        for (int ei = 0; ei < m_document->originalBytes.size(); ++ei)
        {
            auto &entry = m_document->originalBytes[ei];
            const qint64 off = entry.first;
            QByteArray &orig = entry.second;
            if (bytePos >= off && bytePos < off + orig.size())
            {
                const char origByte = orig.at(static_cast<int>(bytePos - off));
                hexEdit->replace(bytePos, 1, QByteArray(1, origByte));

                // Remove this byte from the tracked range, splitting if needed
                const int localIdx = static_cast<int>(bytePos - off);
                if (orig.size() == 1) {
                    // Only byte in group — remove entire entry
                    m_document->originalBytes.removeAt(ei);
                } else if (localIdx == 0) {
                    // First byte — trim from front
                    entry.first += 1;
                    orig.remove(0, 1);
                } else if (localIdx == orig.size() - 1) {
                    // Last byte — trim from back
                    orig.chop(1);
                } else {
                    // Middle byte — split into two entries
                    const QByteArray tail = orig.mid(localIdx + 1);
                    orig.truncate(localIdx);
                    m_document->originalBytes.insert(ei + 1, {bytePos + 1, tail});
                }
                break;
            }
        }
        if (showChangesAct->isChecked())
            updateChangedBytesHighlight();
        updateActionStates();
    }
    else if (addPointerActs.contains(chosen))
    {
        const PointerLengthOption opt = addPointerActs.value(chosen);
        if (hexEdit->addPointerUndoable(bytePos, opt.target, opt.size))
            refreshPointersUi();
    }
    else if (dropSelectionPtrsAct && chosen == dropSelectionPtrsAct)
    {
        if (hexEdit->removePointersUndoable(dropSelectionPtrs) > 0)
            refreshPointersUi();
    }
    else if (saveAsDumpAct && chosen == saveAsDumpAct)
    {
        dumpScript();
    }
    else if (editScriptAct3 && chosen == editScriptAct3)
    {
        dumpScript();
    }
    else if (chosen == quickSearchAct)
    {
        if (!pointersDialog)
        {
            pointersDialog = new PointersDialog(hexEdit, this);
            connect(pointersDialog, SIGNAL(accepted()), this, SLOT(pointersUpdated()));
            connect(pointersDialog, &PointersDialog::searchCompleted, this, &MainWindow::onQuickSearchCompleted);
            pointersDialog->setDock(m_pointersDock);
        }
        pointersDialog->setHexEdit(hexEdit);
        if (m_currentSession) {
            PointersDialog::State ps;
            ps.searchDir        = m_currentSession->ptrSearchDir;
            ps.excludeSelection = m_currentSession->ptrExcludeSelection;
            ps.alignedOnly      = m_currentSession->ptrAlignedOnly;
            ps.optimize         = m_currentSession->ptrOptimize;
            pointersDialog->setDialogState(ps);
        }
        m_pointersDock->show();
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
    hexEdit->jumpTo(hexEdit->dataSize());
}

bool MainWindow::loadTable()
{
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open translation table"), lastDirectory(kLastTableDirKey), "Tables (*.tbl *.tab *.table);;Text files (*.txt)");

    if (!fileName.isEmpty())
    {
        bool encodingAccepted = true;
        const QString importEncoding = chooseTableImportEncoding(this, fileName, &encodingAccepted);
        if (!encodingAccepted)
            return true;

        const TranslationTable newTable(fileName, importEncoding);

        // Add to dock widget
        m_tablesDock->addTable(QFileInfo(fileName).completeBaseName(), &newTable);
        m_tablesDock->show();

        // Also keep tb in sync for backward compat
        tb = m_tablesDock->currentTable();
        hexEdit->setTranslationTable(tb);
        useTableAct->setDisabled(false);
        useTableAct->setChecked(true);
        editTableAct->setDisabled(false);
        saveTableAct->setDisabled(false);
        saveTableAsAct->setDisabled(false);
        updateActionStates();
        rememberDirectory(kLastTableDirKey, fileName);
        tableFilePath = fileName;
        addToRecentTables(fileName);
        if (m_document && !m_document->projectFilePath.isEmpty())
            m_projectModified = true;
        statusBar()->showMessage(tr("Table loaded"), 2000);
    }

    return true;
}

void MainWindow::switchUseTable()
{
    tb = m_tablesDock->currentTable();
    applySelectedTable();
    updateActionStates();
    refreshChangesView();
    
    if (m_pointersDock)
        m_pointersDock->refreshView();
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
    
    // Apply language
    applyLanguage(language);

    // Save language preference
    QSettings settings;
    settings.setValue("Language", language);
}

QString MainWindow::detectSystemLanguage()
{
    // Get system locale
    const QLocale systemLocale = QLocale::system();
    const QString uiLanguage = systemLocale.uiLanguages().isEmpty() ? 
        systemLocale.name() : systemLocale.uiLanguages().first();

    // Map system language to our supported languages
    const QString langLower = uiLanguage.toLower();
    
    if (langLower.startsWith("ru"))
        return QStringLiteral("ru");
    else if (langLower.startsWith("de"))
        return QStringLiteral("de");
    else if (langLower.startsWith("fr"))
        return QStringLiteral("fr");
    else if (langLower.startsWith("es"))
        return QStringLiteral("es");
    else if (langLower.startsWith("pt"))
        return QStringLiteral("pt");
    else if (langLower.startsWith("ja"))
        return QStringLiteral("ja");
    else if (langLower.startsWith("zh"))
        return QStringLiteral("zh_CN");
    
    // Default to English if no match
    return QStringLiteral("en");
}

void MainWindow::applyLanguage(const QString &language)
{
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
    
    const QString languageShort = language.left(2);
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
    showAsciiAreaAct->setText(tr("ASCII area"));
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

void MainWindow::editTable()
{
    // Show and raise the dock widget instead of a separate dialog
    m_tablesDock->show();
    m_tablesDock->raise();
}

void MainWindow::onTranslationTableChanged()
{
    tb = m_tablesDock->currentTable();
    applySelectedTable();

    hexEdit->viewport()->update();
    hexEdit->update();

    if (pointersDialog)
        pointersDialog->refreshFromTable();
    else
        m_pointersDock->refreshView();
    
    refreshChangesView();
}

void MainWindow::onDockTableChanged(TranslationTable *table)
{
    if (m_closing) return;
    tb = table;
    applySelectedTable();

    const bool hasTables = m_tablesDock->count() > 0;
    useTableAct->setEnabled(hasTables);
    editTableAct->setEnabled(hasTables);
    saveTableAct->setEnabled(hasTables);
    saveTableAsAct->setEnabled(hasTables);

    hexEdit->viewport()->update();
    hexEdit->update();

    if (pointersDialog)
        pointersDialog->refreshFromTable();
    else
        m_pointersDock->refreshView();
    
    refreshChangesView();
}

void MainWindow::onDockTableContentChanged()
{
    if (m_closing) return;
    tb = m_tablesDock->currentTable();
    applySelectedTable();

    hexEdit->viewport()->update();
    hexEdit->update();

    if (!m_restoringTableDockState && m_document && !m_document->projectFilePath.isEmpty())
        m_projectModified = true;

    if (pointersDialog)
        pointersDialog->refreshFromTable();
    else
        m_pointersDock->refreshView();
    
    refreshChangesView();
}

void MainWindow::createEmptyTable()
{
    m_tablesDock->addTable();
    m_tablesDock->show();
    tb = m_tablesDock->currentTable();
    hexEdit->setTranslationTable(tb);
    useTableAct->setDisabled(false);
    useTableAct->setChecked(true);
    editTableAct->setDisabled(false);
    saveTableAct->setDisabled(false);
    saveTableAsAct->setDisabled(false);
    updateActionStates();
}

void MainWindow::showSemiAutoTableDialog()
{
    if (!semiAutoTableDialog)
    {
        semiAutoTableDialog = new SemiAutoTableDialog(hexEdit, this);
        connect(semiAutoTableDialog, &SemiAutoTableDialog::tableGenerated, this, &MainWindow::onSemiAutoTableGenerated);
    }
    semiAutoTableDialog->show();
}

void MainWindow::onSemiAutoTableGenerated()
{
    if (semiAutoTableDialog && semiAutoTableDialog->hasGeneratedTable()) {
        const TranslationTable &generated = semiAutoTableDialog->generatedTable();
        m_tablesDock->addTable(QString(), &generated);
        tb = m_tablesDock->currentTable();
        m_tablesDock->show();
    }
    useTableAct->setDisabled(false);
    editTableAct->setDisabled(false);
    saveTableAct->setDisabled(false);
    saveTableAsAct->setDisabled(false);
    useTableAct->setChecked(true);
    hexEdit->setTranslationTable(tb);
    updateActionStates();
}

void MainWindow::saveTable()
{
    tb = m_tablesDock->currentTable();
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
    tb = m_tablesDock->currentTable();
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

    dumpScriptDialog->setRomProfile(currentPointerSize(), currentPointerOffset());
    dumpScriptDialog->setAvailableTables(m_tablesDock->allTables(),
                                         m_tablesDock->currentIndex(),
                                         useTableAct->isChecked());
    dumpScriptDialog->show();
}

void MainWindow::insertScript()
{
    if (!hexEdit || hexEdit->isReadOnly() || hexEdit->showOriginal())
        return;

    if (!insertScriptDialog)
        insertScriptDialog = new InsertScriptDialog(hexEdit, this);

    insertScriptDialog->setRomProfile(currentPointerSize(), currentPointerOffset());
    insertScriptDialog->show();
}

/*****************************************************************************/
/* Private Methods */
/*****************************************************************************/
void MainWindow::init()
{
    setAttribute(Qt::WA_DeleteOnClose);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    setCentralWidget(m_tabWidget);

    // Create the first session
    EditorSession *firstSession = createSession();
    m_currentSession = firstSession;
    hexEdit = firstSession->editor;
    m_document = firstSession->document;
    isUntitled = true;

    m_changesUiUpdateTimer = new QTimer(this);
    m_changesUiUpdateTimer->setSingleShot(true);
    m_changesUiUpdateTimer->setInterval(40);
    connect(m_changesUiUpdateTimer, &QTimer::timeout, this, &MainWindow::flushChangesUiUpdate);

    // Tables dock widget (right side)
    m_tablesDock = new TablesDockWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, m_tablesDock);
    m_tablesDock->show();  // visible by default
    connect(m_tablesDock, &TablesDockWidget::activeTableChanged,
            this, &MainWindow::onDockTableChanged);
    connect(m_tablesDock, &TablesDockWidget::tableContentChanged,
            this, &MainWindow::onDockTableContentChanged);
    connect(m_tablesDock, &TablesDockWidget::generateTableRequested,
            this, &MainWindow::showSemiAutoTableDialog);
    connect(m_tablesDock, &TablesDockWidget::useTableToggled, this, [this](bool checked) {
        if (useTableAct->isEnabled()) {
            useTableAct->setChecked(checked);
            switchUseTable();
        }
    });

    // Pointers dock widget (bottom)
    m_pointersDock = new PointersDockWidget(this);
    m_pointersDock->setHexEdit(hexEdit);
    addDockWidget(Qt::BottomDockWidgetArea, m_pointersDock);
    m_pointersDock->show();
    connect(m_pointersDock, &PointersDockWidget::findPointersRequested,
            this, &MainWindow::showPointersDialog);

    // Changes dock widget (to the right of pointers dock, bottom)
    m_changesDock = new ChangesDockWidget(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_changesDock);
    splitDockWidget(m_pointersDock, m_changesDock, Qt::Horizontal);
    // Set 50/50 width split and matching stretch factors
    for (auto *splitter : findChildren<QSplitter*>())
    {
        const int pointersIdx = splitter->indexOf(m_pointersDock);
        const int changesIdx = splitter->indexOf(m_changesDock);
        if (pointersIdx >= 0 && changesIdx >= 0)
        {
            splitter->setStretchFactor(pointersIdx, 1);
            splitter->setStretchFactor(changesIdx, 1);
            splitter->setSizes({400, 400});
            break;
        }
    }
    m_changesDock->show();

    // Ctrl+1..9 shortcuts for switching table tabs
    for (int i = 1; i <= 9; ++i) {
        auto *shortcut = new QShortcut(QKeySequence(static_cast<int>(Qt::CTRL) | (Qt::Key_0 + i)), this);
        connect(shortcut, &QShortcut::activated, this, [this, i]() {
            if (m_tablesDock && m_tablesDock->count() >= i)
                m_tablesDock->setCurrentIndex(i - 1);
        });
    }

    createActions();
    // Give the pointers dock the show-pointers toggle action after it's created
    m_pointersDock->addShowPointersAction(showPointersAct);
    connect(m_pointersDock, &PointersDockWidget::showPointersToggled, this, [this](bool checked) {
        showPointersAct->setChecked(checked);
        switchShowPointers();
    });
    connect(showPointersAct, &QAction::toggled,
            m_pointersDock, &PointersDockWidget::setShowPointersChecked);
    connect(showPointersAct, &QAction::changed, this, [this]() {
        m_pointersDock->setShowPointersEnabled(showPointersAct->isEnabled());
    });

    connect(m_changesDock, &ChangesDockWidget::showChangesToggled, this, [this](bool checked) {
        showChangesAct->setChecked(checked);
        toggleShowChanges();
    });
    connect(m_changesDock, &ChangesDockWidget::showOriginalToggled, this, [this](bool show) {
        if (!hexEdit || !m_document)
            return;
        if (show) {
            // Reconstruct original file from current data + sparse originalBytes records
            QByteArray original = hexEdit->data();
            for (const auto &entry : m_document->originalBytes) {
                const int off = static_cast<int>(entry.first);
                const QByteArray &origBytes = entry.second;
                if (off >= 0 && off + origBytes.size() <= original.size())
                    original.replace(off, origBytes.size(), origBytes);
            }
            hexEdit->setOriginalData(original);
        }
        hexEdit->setShowOriginal(show);
        
        // Only switch table if there are tables with different isOriginal values
        if (shouldSwitchTableOnViewModeChange()) {
            const int idx = tableIndexForViewMode(show);
            if (idx >= 0 && m_tablesDock && m_tablesDock->currentIndex() != idx)
                m_tablesDock->setCurrentIndex(idx);
            else
                applyTranslationTableForViewMode();
        }
        
        refreshChangesView();
        updateActionStates();
    });
    connect(showChangesAct, &QAction::toggled,
            m_changesDock, &ChangesDockWidget::setShowChangesChecked);
    connect(showChangesAct, &QAction::changed, this, [this]() {
        m_changesDock->setShowChangesEnabled(showChangesAct->isEnabled());
    });
    connect(m_changesDock, &ChangesDockWidget::jumpToOffset, this, [this](qint64 offset) {
        hexEdit->setCursorPosition(offset * 2);
        hexEdit->ensureVisible();
        hexEdit->setFocus();
    });
    connect(m_changesDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_document)
            refreshChangesView();
    });

    // Wire up dock title bar collapse callbacks and separator event filters
    setupDockTitleBarCallbacks();
    installSeparatorEventFilters();
    
    createToolBars();
    createMenus();

    createStatusBar();

    defaultWindowState = saveState();

    setCurrentFile("");

    readSettings();

    updateActionStates();
    enforceBottomDockEqualWidth();

    setUnifiedTitleAndToolBarOnMac(true);
}

// ---------- Session / tab management ----------

EditorSession *MainWindow::createSession()
{
    auto *session = new EditorSession;
    session->document = new HexDocument;
    session->editor = new HexEditor;
    session->isUntitled = true;

    if (m_tabWidget->isHidden())
        m_tabWidget->show();

    int idx = m_tabWidget->addTab(session->editor, tr("New file"));
    m_sessions.append(session);
    m_tabWidget->setCurrentIndex(idx);  // triggers onTabChanged → connectEditorSignals

    // Apply current visual settings to the new editor
    updateHexEditorSettings();

    // Guarantee signals are connected for the new editor.
    // QTabWidget::addTab may fire currentChanged BEFORE m_sessions.append() runs
    // (for the very first tab), causing onTabChanged to return early without
    // connecting signals. Disconnect + reconnect is idempotent and ensures
    // exactly one connection regardless of Qt's internal signal-emission order.
    disconnectEditorSignals(session->editor);
    connectEditorSignals(session->editor);

    // If Qt already selected this tab during addTab(), setCurrentIndex(idx)
    // may not emit currentChanged again. In that case onTabChanged is skipped,
    // so force the MainWindow active session pointers to this tab.
    // Guard against early startup: during init() the first createSession()
    // runs before dock widgets are created, so restoreSession is not safe yet.
    if (m_tablesDock && m_pointersDock
            && m_currentSession != session
            && m_tabWidget->currentIndex() == idx) {
        if (m_currentSession && m_currentSession->editor)
            disconnectEditorSignals(m_currentSession->editor);
        saveCurrentSession();
        restoreSession(session);
    }

    return session;
}

void MainWindow::connectEditorSignals(HexEditor *editor)
{
    connect(editor, SIGNAL(overwriteModeChanged(bool)), this, SLOT(setOverwriteMode(bool)));
    connect(editor, SIGNAL(dataChanged()), this, SLOT(dataChanged()));
    connect(editor, &HexEditor::dataChangedAt, this, &MainWindow::onHexDataChangedAt);
    connect(editor, &HexEditor::contextMenuRequested, this, &MainWindow::hexEditContextMenu);
    connect(editor, SIGNAL(selectionChanged(qint64, qint64)), this, SLOT(setSelection(qint64, qint64)));
    connect(editor, SIGNAL(currentAddressChanged(qint64)), this, SLOT(setAddress(qint64)));
    connect(editor, SIGNAL(currentSizeChanged(qint64)), this, SLOT(setSize(qint64)));
}

void MainWindow::disconnectEditorSignals(HexEditor *editor)
{
    disconnect(editor, SIGNAL(overwriteModeChanged(bool)), this, SLOT(setOverwriteMode(bool)));
    disconnect(editor, SIGNAL(dataChanged()), this, SLOT(dataChanged()));
    disconnect(editor, &HexEditor::dataChangedAt, this, &MainWindow::onHexDataChangedAt);
    disconnect(editor, &HexEditor::contextMenuRequested, this, &MainWindow::hexEditContextMenu);
    disconnect(editor, SIGNAL(selectionChanged(qint64, qint64)), this, SLOT(setSelection(qint64, qint64)));
    disconnect(editor, SIGNAL(currentAddressChanged(qint64)), this, SLOT(setAddress(qint64)));
    disconnect(editor, SIGNAL(currentSizeChanged(qint64)), this, SLOT(setSize(qint64)));
}

void MainWindow::saveCurrentSession()
{
    if (!m_currentSession)
        return;

    m_currentSession->editor = hexEdit;
    m_currentSession->document = m_document;
    m_currentSession->curFile = curFile;
    m_currentSession->tableFilePath = tableFilePath;
    m_currentSession->isUntitled = isUntitled;
    m_currentSession->curOffset = curOffset;
    m_currentSession->table = tb;
    m_currentSession->projectModified = m_projectModified;
    m_currentSession->changeTrackingSnapshot = m_changeTrackingSnapshot;
    m_currentSession->detectedRomType = m_detectedRomType;
    m_currentSession->pointerOffset = m_pointerOffset;
    m_currentSession->pointerSize = m_pointerSize;
    m_currentSession->currentEncoding = m_currentEncoding;
    m_currentSession->navigationHistory = navigationHistory;
    m_currentSession->navigationHistoryIndex = navigationHistoryIndex;
    m_currentSession->navigationJumpInProgress = navigationJumpInProgress;
    m_currentSession->tableSnapshot = m_tablesDock->takeSnapshot();
    m_currentSession->tableActiveIndex = m_tablesDock->currentIndex();
    m_currentSession->tablesDockVisible = m_tablesDock->isVisible();
    m_currentSession->tablesDockVisibilityInitialized = true;
    if (m_document)
        m_document->useTable = useTableAct && useTableAct->isChecked();

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
    m_currentSession = session;
    hexEdit = session->editor;
    m_document = session->document;
    curFile = session->curFile;
    tableFilePath = session->tableFilePath;
    isUntitled = session->isUntitled;
    curOffset = session->curOffset;
    m_projectModified = session->projectModified;
    m_changeTrackingSnapshot = session->changeTrackingSnapshot;
    m_detectedRomType = session->detectedRomType;
    m_pointerOffset = session->pointerOffset;
    m_pointerSize = session->pointerSize;
    m_currentEncoding = session->currentEncoding;
    navigationHistory = session->navigationHistory;
    navigationHistoryIndex = session->navigationHistoryIndex;
    navigationJumpInProgress = session->navigationJumpInProgress;

    m_pointersDock->setHexEdit(hexEdit);

    // Restore the per-session table dock content. applySnapshot rebuilds the
    // dock's tab list and emits activeTableChanged/tableContentChanged.
    // Suppress "project modified" side effects while this is a pure restore.

    // Null out translation table pointers on ALL editors before applySnapshot
    // clears m_tables. Any editor's _tb pointing into the old m_tables becomes
    // a dangling pointer after clear(); setting it to nullptr here prevents a
    // use-after-free crash in ensureTableDisplayCache(). applySelectedTable()
    // at the end of this function will restore the correct pointer for the
    // active editor once the new snapshot has been applied.
    for (EditorSession *s : m_sessions) {
        if (s->editor)
            s->editor->setTranslationTable(nullptr);
    }

    m_restoringTableDockState = true;
    m_tablesDock->applySnapshot(session->tableSnapshot, session->tableActiveIndex);
    m_restoringTableDockState = false;
    tb = m_tablesDock->currentTable();
    session->table = tb;  // keep in sync after pointer recreation
    const bool hasTables = m_tablesDock->count() > 0;
    
    // Always show the tables dock regardless of previous session state
    // The tables dock should only be hidden by explicit user action, never automatically
    m_tablesDock->show();
    m_tablesDock->setCollapsed(false);

    useTableAct->setEnabled(hasTables);
    editTableAct->setEnabled(hasTables);
    saveTableAct->setEnabled(hasTables);
    saveTableAsAct->setEnabled(hasTables);
    useTableAct->setChecked(hasTables && m_document && m_document->useTable);

    // Keep the persisted project-modified state exactly as it was for this tab.
    m_projectModified = session->projectModified;

    if (m_pointersDock && m_pointersDock->isVisible())
        m_pointersDock->refreshView();

    m_restoringProjectUi = true;

    // Sync changes-dock supplementary buttons with restored session
    if (m_changesDock) {
        if (showChangesAct)
            showChangesAct->setChecked(m_document && m_document->showChanges);
        m_changesDock->setShowChangesChecked(showChangesAct && showChangesAct->isChecked());
        m_changesDock->setHexMode(m_document && m_document->changesHexMode);
        m_changesDock->setShowOriginalChecked(hexEdit && hexEdit->showOriginal());
    }

    // Sync UI with restored session
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
    if (s->editor && s->editor->isModified())
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

    if (!maybeSave())
        return;
    if (!maybeSaveProject())
        return;

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

// ---------- End session / tab management ----------

void MainWindow::createActions()
{
    newAct = new QAction(tr("New"), this);
    newAct->setStatusTip(tr("Create a new file"));
    connect(newAct, SIGNAL(triggered()), this, SLOT(newFile()));
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
    newTabAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
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
        
        if (hexEdit->editAreaIsAscii())
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
    useTableAct->setShortcuts(QKeySequence::AddTab);
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

    showAsciiAreaAct = new QAction(tr("ASCII area"), this);
    showAsciiAreaAct->setCheckable(true);
    showAsciiAreaAct->setChecked(true);

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
    connect(showAsciiAreaAct, &QAction::toggled, this, [this](bool checked)
            {
                hexEdit->setAsciiArea(checked);
                QSettings s;
                s.setValue("AsciiArea", checked);
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
    viewMenu->addAction(showDarkThemeAct);
    viewMenu->addSeparator();

    panelsMenu = viewMenu->addMenu(tr("Panels"));
    panelsMenu->addAction(showAddressAreaAct);
    panelsMenu->addAction(showAsciiAreaAct);
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

    setDockAreaCollapsed(Qt::LeftDockWidgetArea, false);
    setDockAreaCollapsed(Qt::RightDockWidgetArea, false);
    setDockAreaCollapsed(Qt::BottomDockWidgetArea, false);

    if (m_pointersDock && m_changesDock)
        resizeDocks({m_pointersDock, m_changesDock}, {220, 220}, Qt::Vertical);

    if (m_currentSession) {
        m_currentSession->tablesDockVisible = true;
        m_currentSession->tablesDockVisibilityInitialized = true;
    }

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
            Qt::DockWidgetArea area = dockWidgetArea(dock);
            if (area != Qt::NoDockWidgetArea)
                setDockAreaCollapsed(area, !isDockAreaCollapsed(area));
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
        m_projectModified = false;
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

    // 1. Load the data file
    if (!doc.filePath.isEmpty() && QFile::exists(doc.filePath)) {
        loadFile(doc.filePath);
    }

    // 2. Load translation tables into dock widget
    tb = nullptr;
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

    // 3. Encoding
    m_currentEncoding = doc.currentEncoding;
    hexEdit->setCurrentEncoding(doc.currentEncoding);
    if (lbEncoding)
        lbEncoding->setText(doc.currentEncoding);
    syncEncodingMenu();

    // 4. ROM type + byte order
    m_detectedRomType = doc.romType;
    m_pointerOffset = doc.pointerOffset;
    m_pointerSize = doc.pointerSize;
    {
        const QSignalBlocker blocker(cbRomType);
        cbRomType->setCurrentIndex(static_cast<int>(doc.romType));
        syncRomTypeMenu(static_cast<int>(doc.romType));
    }
    hexEdit->byteOrder = doc.byteOrder;
    updateEndiannesLabel();

    // 5. Pointers
    doc.restorePointers(hexEdit->pointers());
    if (!m_restoringProjectUi)
        pointersUpdated();

    // 6. Cursor position
    if (doc.cursorPosition > 0) {
        hexEdit->setCursorPosition(doc.cursorPosition);
        hexEdit->ensureVisible();
    }

    // 7. Store project association
    *m_document = doc;
    m_document->translationTable = nullptr; // MainWindow owns tb

    // 8. Remember project as last opened
    QSettings settings;
    settings.setValue(QStringLiteral("LastProjectFile"), path);
    addToRecentProjects(path);
    rememberDirectory(QStringLiteral("kLastProjectDirKey"), path);
    m_tablesDock->setProjectName(m_document->projectName);
    statusBar()->showMessage(tr("Project loaded"), 2000);
    m_projectModified = false;
    updateWindowTitle();
    updateActionStates();

    // Restore display settings
    m_restoringProjectUi = true;
    showPointersAct->setChecked(doc.showPointers);
    m_pointersDock->setShowPointersChecked(doc.showPointers);
    switchShowPointers();
    showChangesAct->setChecked(doc.showChanges);
    m_changesDock->setShowChangesChecked(doc.showChanges);
    m_changesDock->setHexMode(doc.changesHexMode);
    toggleShowChanges();

    // Restore dock layout (positions/sizes) from project, then ensure all docks are always visible.
    if (!doc.dockLayoutState.isEmpty())
        restoreState(doc.dockLayoutState);
    m_tablesDock->show();
    m_pointersDock->show();
    m_changesDock->show();
    if (!doc.tablesColumnsState.isEmpty())
        m_tablesDock->restoreColumnsState(doc.tablesColumnsState);
    updateDockAreaActions();

    m_restoringProjectUi = false;
    m_projectModified = false;
    if (!m_document->originalBytes.isEmpty()) {
        refreshChangesView();
        enforceBottomDockEqualWidth();
    }

    // Keep the current tab session snapshot in sync with the just-loaded project.
    // This prevents stale default values (like hidden tables dock) from being restored.
    saveCurrentSession();
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
    m_document->currentEncoding = m_currentEncoding;
    m_document->romType = m_detectedRomType;
    m_document->pointerOffset = m_pointerOffset;
    m_document->pointerSize = m_pointerSize;
    m_document->byteOrder = hexEdit->byteOrder;
    m_document->snapshotPointers(hexEdit->pointers());
    m_document->showPointers = showPointersAct && showPointersAct->isChecked();
    m_document->showChanges  = showChangesAct  && showChangesAct->isChecked();
    m_document->changesHexMode = m_changesDock && m_changesDock->hexMode();
    m_document->dockLayoutState = saveState();
    m_document->tablesColumnsState = m_tablesDock ? m_tablesDock->saveColumnsState() : QByteArray();
    m_document->cursorPosition = hexEdit->cursorPosition();

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
    m_projectModified = false;
    updateWindowTitle();
    updateActionStates();

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
    m_projectModified = false;

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

    // Restore last known cursor position for this file
    {
        settings.beginGroup(QStringLiteral("CursorPositions"));
        const QString key = QString::fromUtf8(QUrl::toPercentEncoding(fileName));
        const QVariant savedPos = settings.value(key);
        settings.endGroup();
        if (savedPos.isValid()) {
            const qint64 fileSize = hexEdit->dataSize();
            const qint64 bytePos = qBound(0LL, savedPos.toLongLong(), fileSize > 0 ? fileSize - 1 : 0LL);
            if (bytePos > 0) {
                hexEdit->setCursorPosition(bytePos * 2);
                hexEdit->ensureVisible();
            }
        }
    }
}

void MainWindow::readSettings()
{
    QSettings settings;
    QPoint pos = settings.value("pos", QPoint(200, 200)).toPoint();
    QSize size = settings.value("size", QSize(610, 460)).toSize();
    move(pos);
    resize(size);

    const bool darkTheme = settings.value("DarkTheme", false).toBool();
    if (showDarkThemeAct)
        showDarkThemeAct->setChecked(darkTheme);
    else
        applyDarkTheme(darkTheme);


    hexEdit->setAddressArea(settings.value("AddressArea", true).toBool());
    hexEdit->setAsciiArea(settings.value("AsciiArea", true).toBool());
    hexEdit->setHighlighting(true);
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
    hexEdit->setAddressZeroByteFontColor(settings.value("AddressZeroByteFontColor", settings.value("AddressFontColor", palette().color(QPalette::WindowText)).value<QColor>()).value<QColor>());
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
    hexEdit->setShowMultibyteFrame(settings.value("ShowMultibyteFrame", true).toBool());
    hexEdit->setHexAreaBackgroundColor(settings.value("HexAreaBackgroundColor", QColor(Qt::white)).value<QColor>());
    hexEdit->setHexAreaGridColor(settings.value("HexAreaGridColor", QColor(0x99, 0x99, 0x99)).value<QColor>());
    hexEdit->setMultibyteFrameColor(settings.value("MultibyteFrameColor", QColor(0x20, 0x20, 0x20)).value<QColor>());
    hexEdit->setCursorCharColor(settings.value("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80)).value<QColor>());
    hexEdit->setCursorFrameColor(settings.value("CursorFrameColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setZeroByteFontColor(settings.value("ZeroByteFontColor", QColor(0xCC, 0xCC, 0xCC)).value<QColor>());
    hexEdit->setChangesColor(settings.value("ChangesColor", QColor(0x99, 0xff, 0x99, 0xff)).value<QColor>());

    if (showAddressAreaAct)
        showAddressAreaAct->setChecked(hexEdit->addressArea());
    if (showAsciiAreaAct)
        showAsciiAreaAct->setChecked(hexEdit->asciiArea());
    if (showAddressGridAct)
        showAddressGridAct->setChecked(hexEdit->showHexGrid());


    const QByteArray windowState = settings.value(kMainWindowStateKey).toByteArray();
    if (!windowState.isEmpty())
        restoreState(windowState);

    // Always show all dock panels regardless of saved state
    if (m_tablesDock) m_tablesDock->show();
    if (m_pointersDock) m_pointersDock->show();
    if (m_changesDock) m_changesDock->show();

    // Restore dock column states
    if (m_pointersDock)
        m_pointersDock->restoreColumnsState(settings.value(QStringLiteral("PointersDockColumns")).toByteArray());
    if (m_tablesDock)
        m_tablesDock->restoreColumnsState(settings.value(QStringLiteral("TablesDockColumns")).toByteArray());
    if (m_changesDock)
        m_changesDock->restoreColumnsState(settings.value(QStringLiteral("ChangesDockColumns")).toByteArray());

    updateRecentFileMenu();

    updateRecentTableMenu();

    updateRecentProjectMenu();

    const bool autoLoadRecentFile = settings.value("AutoLoadRecentFile", true).toBool();
    const QString sessionTabsKey = QStringLiteral("Session/Tabs");
    const bool hasSavedSessionTabs = settings.contains(sessionTabsKey);

    if (autoLoadRecentFile)
    {
        // Restore the previous session (all open tabs) if saved.
        const QStringList sessionTabs = settings.value(sessionTabsKey).toStringList();
        const int activeTab = settings.value(QStringLiteral("Session/ActiveTab"), 0).toInt();

        if (!sessionTabs.isEmpty()) {
            bool anyOpened = false;
            for (const QString &entry : sessionTabs) {
                if (entry.startsWith(QStringLiteral("project:"))) {
                    const QString path = entry.mid(8);
                    if (!path.isEmpty() && QFile::exists(path)) {
                        if (!anyOpened) {
                            // Reuse the initial empty tab for the first entry
                            openProjectFile(path);
                        } else {
                            createSession();
                            openProjectFile(path);
                        }
                        anyOpened = true;
                    }
                } else if (entry.startsWith(QStringLiteral("file:"))) {
                    const QString path = entry.mid(5);
                    if (!path.isEmpty() && QFile::exists(path)) {
                        if (!anyOpened) {
                            loadFile(path);
                        } else {
                            loadFileInNewTab(path);
                        }
                        anyOpened = true;
                    }
                }
            }
            // Restore active tab
            if (anyOpened && activeTab >= 0 && activeTab < m_tabWidget->count())
                m_tabWidget->setCurrentIndex(activeTab);
        } else if (!hasSavedSessionTabs) {
            // Legacy fallback: restore the single most-recently-used file/project
            const QString lastProjectFile = settings.value("LastProjectFile").toString();
            const QString fileName = settings.value("RecentFile0").toString();
            if (!lastProjectFile.isEmpty() && QFile::exists(lastProjectFile)) {
                openProjectFile(lastProjectFile);
            } else if (!fileName.isEmpty() && QFile::exists(fileName)) {
                loadFile(fileName);
            }
        }
    }

    applyShortcutsFromSettings();
    
    // Load and apply the language translator
    QSettings settingsForLang;
    const QString language = settingsForLang.value("Language", QStringLiteral("en")).toString();
    applyLanguage(language);
}

void MainWindow::applyShortcutsFromSettings()
{
    QSettings s;
    openAct->setShortcut(s.value("hotkey_Open",         QKeySequence(QKeySequence::Open)).value<QKeySequence>());
    saveAct->setShortcut(s.value("hotkey_Save",          QKeySequence(QKeySequence::Save)).value<QKeySequence>());
    saveAsAct->setShortcut(s.value("hotkey_SaveAs",      QKeySequence(QKeySequence::SaveAs)).value<QKeySequence>());
    closeAct->setShortcut(s.value("hotkey_Close",        QKeySequence(QKeySequence::Close)).value<QKeySequence>());
    undoAct->setShortcut(s.value("hotkey_Undo",          QKeySequence(QKeySequence::Undo)).value<QKeySequence>());
    redoAct->setShortcut(s.value("hotkey_Redo",          QKeySequence(QKeySequence::Redo)).value<QKeySequence>());
    cutAct->setShortcut(s.value("hotkey_Cut",            QKeySequence(QKeySequence::Cut)).value<QKeySequence>());
    copyAct->setShortcut(s.value("hotkey_Copy",          QKeySequence(QKeySequence::Copy)).value<QKeySequence>());
    pasteAct->setShortcut(s.value("hotkey_Paste",        QKeySequence(QKeySequence::Paste)).value<QKeySequence>());
    findAct->setShortcut(s.value("hotkey_Find",          QKeySequence(QKeySequence::Find)).value<QKeySequence>());
    gotoAct->setShortcut(s.value("hotkey_Goto",          QKeySequence(QKeySequence::FindNext)).value<QKeySequence>());
    useTableAct->setShortcut(s.value("hotkey_UseTable",  QKeySequence(QKeySequence::AddTab)).value<QKeySequence>());
    findPointersAct->setShortcut(s.value("hotkey_FindPointers", QKeySequence(QKeySequence::New)).value<QKeySequence>());
    dumpScriptAct->setShortcut(s.value("hotkey_EditScript", QKeySequence(Qt::CTRL | Qt::Key_E)).value<QKeySequence>());
    if (previousPositionAct)
        previousPositionAct->setShortcut(s.value("hotkey_PrevPos",
            QKeySequence(Qt::CTRL | Qt::Key_BracketLeft)).value<QKeySequence>());
    if (nextPositionAct)
        nextPositionAct->setShortcut(s.value("hotkey_NextPos",
            QKeySequence(Qt::CTRL | Qt::Key_BracketRight)).value<QKeySequence>());
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
        toFileBeginningAct->setEnabled(hexEdit && hexEdit->dataSize() > 0);
    if (toFileEndAct)
        toFileEndAct->setEnabled(hexEdit && hexEdit->dataSize() > 0);
}

void MainWindow::toggleDarkTheme(bool enabled)
{
    applyDarkTheme(enabled);
    QSettings settings;
    settings.setValue("DarkTheme", enabled);
}

void MainWindow::applyDarkTheme(bool enabled)
{
#ifdef Q_OS_MAC
    setMacOSDarkMode(enabled);
#else
    if (!m_lightPaletteCaptured) {
        m_lightPalette = qApp->palette();
        m_lightStyleName = qApp->style()->objectName();
        m_lightPaletteCaptured = true;
    }

    if (enabled) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(53, 53, 53));
        dark.setColor(QPalette::WindowText, Qt::white);
        dark.setColor(QPalette::Base, QColor(35, 35, 35));
        dark.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        dark.setColor(QPalette::ToolTipBase, Qt::white);
        dark.setColor(QPalette::ToolTipText, Qt::white);
        dark.setColor(QPalette::Text, Qt::white);
        dark.setColor(QPalette::Button, QColor(53, 53, 53));
        dark.setColor(QPalette::ButtonText, Qt::white);
        dark.setColor(QPalette::BrightText, Qt::red);
        dark.setColor(QPalette::Highlight, QColor(42, 130, 218));
        dark.setColor(QPalette::HighlightedText, Qt::black);
        qApp->setPalette(dark);
    } else {
        if (!m_lightStyleName.isEmpty())
            QApplication::setStyle(QStyleFactory::create(m_lightStyleName));
        qApp->setPalette(m_lightPalette);
    }
#endif
    if (m_tabWidget)
        m_tabWidget->update();
    if (statusBar())
        statusBar()->update();
    if (hexEdit)
        hexEdit->update();
}

void MainWindow::updateHexEditorSettings()
{
    // Apply settings to all open editors
    for (EditorSession *session : m_sessions) {
        HexEditor *editor = session->editor;
        if (!editor)
            continue;

        const qint64 savedCursorPos = editor->cursorPosition();
        const int savedBytesPerLine = qMax(1, editor->bytesPerLine());
        const qint64 savedTopByte = static_cast<qint64>(editor->verticalScrollBar()->value()) * savedBytesPerLine;
        const int savedHorizontal = editor->horizontalScrollBar()->value();

        QSettings settings;

        editor->setAddressArea(settings.value("AddressArea", true).toBool());
        editor->setAsciiArea(settings.value("AsciiArea", true).toBool());
        editor->setHighlighting(true);
        editor->setHighlightingColor(settings.value("HighlightingColor").value<QColor>());
        editor->setPointedColor(settings.value("PointedColor").value<QColor>());
        editor->setPointedFontColor(settings.value("PointedFontColor", QColor(Qt::black)).value<QColor>());
        editor->setPointerFontColor(settings.value("PointerFontColor", QColor(Qt::black)).value<QColor>());
        editor->setPointerFrameColor(settings.value("PointerFrameColor", QColor(0x00, 0x00, 0xFF)).value<QColor>());
        editor->setPointerFrameBackgroundColor(settings.value("PointerFrameBgColor", QColor(0x00, 0xFF, 0x00, 0x80)).value<QColor>());
        editor->setAddressAreaColor(settings.value("AddressAreaColor").value<QColor>());
        editor->setSelectionColor(settings.value("SelectionColor").value<QColor>());
        editor->setFont(settings.value("WidgetFont").value<QFont>());
        editor->setAddressFontColor(settings.value("AddressFontColor").value<QColor>());
        editor->setAddressZeroByteFontColor(settings.value("AddressZeroByteFontColor", settings.value("AddressFontColor").value<QColor>()).value<QColor>());
        editor->setAsciiAreaColor(settings.value("AsciiAreaColor").value<QColor>());
        editor->setAsciiFontColor(settings.value("AsciiFontColor").value<QColor>());
        editor->setHexFontColor(settings.value("HexFontColor").value<QColor>());
        editor->setNonPrintableNoTableChar(readSingleCharSetting(settings, "NonPrintableNoTableChar", QChar(0x25AA)));
        editor->setNotInTableChar(readSingleCharSetting(settings, "NotInTableChar", QChar(0x25A1)));
        editor->setAddressWidth(settings.value("AddressAreaWidth").toInt());
        editor->setBytesPerLine(settings.value("BytesPerLine", 32).toInt());
        editor->setDynamicBytesPerLine(settings.value("Autosize", true).toBool());
        editor->setShowHexGrid(settings.value("ShowHexGrid", true).toBool());
        editor->setShowMultibyteFrame(settings.value("ShowMultibyteFrame", true).toBool());
        editor->setHexAreaBackgroundColor(settings.value("HexAreaBackgroundColor", QColor(Qt::white)).value<QColor>());
        editor->setHexAreaGridColor(settings.value("HexAreaGridColor", QColor(0x99, 0x99, 0x99)).value<QColor>());
        editor->setMultibyteFrameColor(settings.value("MultibyteFrameColor", QColor(0x20, 0x20, 0x20)).value<QColor>());
        editor->setCursorCharColor(settings.value("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80)).value<QColor>());
        editor->setCursorFrameColor(settings.value("CursorFrameColor", QColor(Qt::black)).value<QColor>());
        editor->setZeroByteFontColor(settings.value("ZeroByteFontColor", QColor(0xCC, 0xCC, 0xCC)).value<QColor>());
        editor->setChangesColor(settings.value("ChangesColor", QColor(0x99, 0xff, 0x99, 0xff)).value<QColor>());
        editor->setScrollMapChangesBgColor(settings.value("ScrollMapPtrBgColor", QColor(0xd0, 0xd0, 0xd0)).value<QColor>());
        editor->setScrollMapTargetBgColor(settings.value("ScrollMapTargetBgColor", QColor(0xd0, 0xd0, 0xd0)).value<QColor>());

        const int newBytesPerLine = qMax(1, editor->bytesPerLine());
        editor->verticalScrollBar()->setValue(static_cast<int>(savedTopByte / newBytesPerLine));
        editor->horizontalScrollBar()->setValue(savedHorizontal);
        editor->setCursorPosition(savedCursorPos);
        editor->viewport()->update();
    }

    if (showAddressAreaAct && hexEdit)
        showAddressAreaAct->setChecked(hexEdit->addressArea());
    if (showAsciiAreaAct && hexEdit)
        showAsciiAreaAct->setChecked(hexEdit->asciiArea());
    if (showAddressGridAct && hexEdit)
        showAddressGridAct->setChecked(hexEdit->showHexGrid());
}

bool MainWindow::saveFile(const QString &fileName)
{
    QSettings settings;
    const bool autoFixChecksums = settings.value("AutoFixChecksums", false).toBool();

    QString tmpFileName = fileName + ".~tmp";

    QApplication::setOverrideCursor(Qt::WaitCursor);

    // If auto-fix is requested, apply checksum correction to a copy of the data
    // before writing so both the file and the editor reflect the corrected state.
    bool checksumFixed = false;
    if (autoFixChecksums) {
        QByteArray data = hexEdit->data();
        const ChecksumFixResult csResult = tryFixChecksum(data, m_detectedRomType);
        if (csResult.status == ChecksumFixStatus::Fixed) {
            checksumFixed = true;
            // Update the editor bytes that changed (minimal replace, adds to undo stack).
            const QByteArray oldData = hexEdit->data();
            for (int i = 0; i < data.size() && i < oldData.size(); ++i) {
                if (data[i] != oldData[i])
                    hexEdit->replace(i, data[i]);
            }
            // Refresh changes view to show recalculated checksums
            if (showChangesAct && showChangesAct->isChecked())
                refreshChangesView();
        }
    }

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
    if (checksumFixed)
        statusBar()->showMessage(tr("File saved (checksums fixed)"), 3000);
    else
        statusBar()->showMessage(tr("File saved"), 2000);
    return true;
}

void MainWindow::updateWindowTitle()
{
    const bool hasProject = m_document && !m_document->projectName.isEmpty();
    const bool hasFile = !isUntitled && !curFile.isEmpty();

    if (hasProject && hasFile)
        setWindowTitle(QString("%1 — %2[*] - RTHextion")
                           .arg(m_document->projectName, strippedName(curFile)));
    else if (hasProject)
        setWindowTitle(QString("%1[*] - RTHextion").arg(m_document->projectName));
    else if (hasFile)
        setWindowTitle(QString("%1[*] - RTHextion").arg(strippedName(curFile)));
    else
        setWindowTitle(QStringLiteral("RTHextion"));

    // Refresh recent projects menu since filter depends on current project path
    updateRecentProjectMenu();
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    curFile = QFileInfo(fileName).canonicalFilePath();
    if (curFile.isEmpty())
        curFile = fileName;
    isUntitled = fileName.isEmpty();
    setWindowModified(false);

    if (!fileName.isEmpty())
        addToRecentFiles(fileName);

    updateWindowTitle();
    updateActionStates();

    // Sync session and tab title
    if (m_currentSession) {
        m_currentSession->curFile = curFile;
        m_currentSession->isUntitled = isUntitled;
        updateTabTitle(m_tabWidget->currentIndex());
    }
}

bool MainWindow::maybeSaveProject()
{
    if (!m_document || m_document->projectFilePath.isEmpty() || !m_projectModified)
        return true;

    QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        QString::fromLatin1(AppInfo::Name),
        tr("The project has unsaved changes.\nDo you want to save the project?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (result == QMessageBox::Save)
        return saveProjectImpl(m_document->projectFilePath);

    return result != QMessageBox::Cancel;
}

bool MainWindow::maybeSave()
{
    if (!isWindowModified())
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
    const bool canModify = hexEdit && !hexEdit->isReadOnly() && !hexEdit->showOriginal();

    // Save is always enabled (for new files it will trigger Save As)
    saveAct->setEnabled(!m_sessions.isEmpty());
    closeAct->setEnabled(!m_sessions.isEmpty());
    undoAct->setEnabled(canModify && hexEdit->canUndo());
    redoAct->setEnabled(canModify && hexEdit->canRedo());

    // Save Project is only enabled after the project has been given a path
    if (saveProjectAct)
        saveProjectAct->setEnabled(m_document && !m_document->projectFilePath.isEmpty());

    // Create IPS patch only when project has tracked original bytes
    if (createIpsPatchAct)
        createIpsPatchAct->setEnabled(m_document && !m_document->originalBytes.isEmpty());

    // Show-original button enabled whenever the project has recorded original bytes
    if (m_changesDock)
        m_changesDock->setShowOriginalEnabled(m_document && !m_document->originalBytes.isEmpty());

    // Load original is available whenever a file is open
    if (loadOriginalAct)
        loadOriginalAct->setEnabled(!isUntitled && !curFile.isEmpty());
    
    const bool hasSelection = hexEdit && hexEdit->getRawSelection().size() > 1;
    const bool dumpEnabled = hasSelection;
    const bool insertEnabled = canModify;

    if (cutAct)
        cutAct->setEnabled(hasSelection && hexEdit && !hexEdit->overwriteMode() && canModify);
    if (pasteAct)
        pasteAct->setEnabled(canModify && !QApplication::clipboard()->text().isEmpty());
    if (editMenu)
        editMenu->setEnabled(canModify);

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

    // Save dock column states
    if (m_pointersDock)
        settings.setValue(QStringLiteral("PointersDockColumns"), m_pointersDock->saveColumnsState());
    if (m_tablesDock)
        settings.setValue(QStringLiteral("TablesDockColumns"), m_tablesDock->saveColumnsState());
    if (m_changesDock)
        settings.setValue(QStringLiteral("ChangesDockColumns"), m_changesDock->saveColumnsState());

    // Save hex editor settings to ensure persisted state
    if (hexEdit)
    {
        settings.setValue("AddressArea", hexEdit->addressArea());
        settings.setValue("AsciiArea", hexEdit->asciiArea());
        settings.setValue("OverwriteMode", hexEdit->overwriteMode());
        settings.setValue("ShowHexGrid", hexEdit->showHexGrid());
        settings.setValue("Autosize", hexEdit->dynamicBytesPerLine());
        settings.setValue("HexCaps", hexEdit->hexCaps());
        settings.setValue("AddressAreaWidth", hexEdit->addressWidth());
        settings.setValue("BytesPerLine", hexEdit->bytesPerLine());
    }

    // Save all open tabs for session restore on next launch.
    // Make sure the current session's state is flushed first.
    saveCurrentSession();
    QStringList sessionPaths;
    for (int i = 0; i < m_sessions.size(); ++i) {
        const EditorSession *s = m_sessions[i];
        // Project file takes priority over standalone file
        const QString project = s->document ? s->document->projectFilePath : QString();
        if (!project.isEmpty() && QFile::exists(project)) {
            sessionPaths << QStringLiteral("project:") + project;
        } else if (!s->curFile.isEmpty() && !s->isUntitled && QFile::exists(s->curFile)) {
            sessionPaths << QStringLiteral("file:") + s->curFile;
        }
        // Skip untitled / empty tabs
    }
    settings.setValue(QStringLiteral("Session/Tabs"), sessionPaths);
    settings.setValue(QStringLiteral("Session/ActiveTab"),
                      m_tabWidget->isVisible() ? m_tabWidget->currentIndex() : -1);

    // Save last cursor position for each open file so it survives app restarts
    settings.beginGroup(QStringLiteral("CursorPositions"));
    for (const EditorSession *s : std::as_const(m_sessions)) {
        if (!s->curFile.isEmpty() && !s->isUntitled) {
            const QString key = QString::fromUtf8(QUrl::toPercentEncoding(s->curFile));
            settings.setValue(key, s->curOffset);
        }
    }
    settings.endGroup();

    settings.sync();
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
    if (!fileName.isEmpty()) {
        if (m_sessions.isEmpty() || !isUntitled || hexEdit->isModified()) {
            loadFileInNewTab(fileName);
        } else {
            loadFile(fileName);
        }
    }
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

        try
        {
            bool encodingAccepted = true;
            const QString importEncoding = chooseTableImportEncoding(this, fileName, &encodingAccepted);
            if (!encodingAccepted)
                return;

            const TranslationTable newTable(fileName, importEncoding);
            m_tablesDock->addTable(QFileInfo(fileName).completeBaseName(), &newTable);
            m_tablesDock->show();
            tb = m_tablesDock->currentTable();
            useTableAct->setEnabled(true);
            useTableAct->setChecked(true);
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

void MainWindow::addToRecentProjects(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    QSettings settings;
    QStringList files = settings.value(kRecentProjectsKey).toStringList();

    files.removeAll(fileName);
    files.prepend(fileName);

    while (files.size() > kMaxRecentProjects)
        files.removeLast();

    settings.setValue(kRecentProjectsKey, files);
    updateRecentProjectMenu();
}

void MainWindow::updateRecentProjectMenu()
{
    QSettings settings;
    QStringList files = settings.value(kRecentProjectsKey).toStringList();

    // Filter out the currently open project
    const QString currentProject = m_document ? m_document->projectFilePath : QString();

    recentProjectMenu->clear();

    // Build filtered list (exclude current project)
    QStringList displayFiles;
    for (const QString &f : files) {
        if (!f.isEmpty() && f != currentProject)
            displayFiles.append(f);
    }

    if (displayFiles.isEmpty())
    {
        recentProjectMenu->setEnabled(false);
        return;
    }

    recentProjectMenu->setEnabled(true);

    for (int i = 0; i < displayFiles.size(); ++i)
    {
        const QString fileName = displayFiles[i];
        const QString text = QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(fileName).fileName());

        QAction *action = recentProjectMenu->addAction(text);
        action->setData(fileName);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentProject);
    }
}

void MainWindow::openRecentProject()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString fileName = action->data().toString();
    if (!fileName.isEmpty())
    {
        if (!QFile::exists(fileName))
        {
            QMessageBox::warning(this, tr("Error"), tr("File not found: %1").arg(fileName));
            return;
        }
        // Open in new tab if current has content
        if (m_sessions.isEmpty() || !isUntitled || (hexEdit && hexEdit->isModified()))
            createSession();
        openProjectFile(fileName);
    }
}

void MainWindow::toggleShowChanges()
{
    const bool show = showChangesAct->isChecked();
    hexEdit->setShowChanges(show);
    if (show)
        updateChangedBytesHighlight();
    else
        hexEdit->clearChangedPositions();

    if (m_document)
        m_document->showChanges = show;

    if (!m_restoringProjectUi && m_document && !m_document->projectFilePath.isEmpty())
        m_projectModified = true;
}

void MainWindow::updateChangedBytesHighlight()
{
    QSet<qint64> positions;
    if (!m_document || m_document->originalBytes.isEmpty()) {
        hexEdit->clearChangedRange();
        hexEdit->setChangedPositions(positions);
        return;
    }

    const QByteArray currentData = hexEdit->data();

    // Mark the expansion tail as changed only when the original file size is
    // explicitly known (set by IPS patch / Load Original).  For regular edits
    // originalFileSize stays -1 and no range highlight is needed.
    if (m_document->originalFileSize >= 0 && currentData.size() > m_document->originalFileSize) {
        hexEdit->setChangedRange(m_document->originalFileSize, currentData.size());
    } else {
        hexEdit->clearChangedRange();
    }

    // Compare original bytes with current data for the overlapping portion
    for (const auto &entry : m_document->originalBytes) {
        const qint64 offset = entry.first;
        const QByteArray &origBytes = entry.second;
        for (int i = 0; i < origBytes.size(); ++i) {
            const qint64 pos = offset + i;
            if (pos < currentData.size()) {
                if (currentData.at(pos) != origBytes.at(i))
                    positions.insert(pos);
            }
        }
    }
    hexEdit->setChangedPositions(positions);
}

void MainWindow::refreshChangesView()
{
    if (!m_document || !m_changesDock)
        return;

    TranslationTable *origTable = tableForViewMode(true);
    TranslationTable *activeTable = selectedTable();
    if (!activeTable)
        activeTable = tableForViewMode(false);

    m_changesDock->refresh(m_document->originalBytes, hexEdit->data(), origTable, activeTable,
                           useTableAct && useTableAct->isChecked(), m_currentEncoding);
}

TranslationTable *MainWindow::tableForViewMode(bool showOriginal) const
{
    if (!m_tablesDock)
        return nullptr;

    const auto &tabs = m_tablesDock->allTables();
    if (tabs.isEmpty())
        return nullptr;

    const int curIdx = m_tablesDock->currentIndex();
    TranslationTable *currentTable = nullptr;
    if (curIdx >= 0 && curIdx < tabs.size())
        currentTable = const_cast<TranslationTable *>(&tabs[curIdx].table);

    const int modeIdx = tableIndexForViewMode(showOriginal);
    if (modeIdx >= 0)
        return const_cast<TranslationTable *>(&tabs[modeIdx].table);

    if (showOriginal)
        return currentTable ? currentTable : const_cast<TranslationTable *>(&tabs.first().table);

    if (curIdx >= 0 && curIdx < tabs.size() && !tabs[curIdx].isOriginal)
        return const_cast<TranslationTable *>(&tabs[curIdx].table);

    for (const auto &tt : tabs) {
        if (!tt.isOriginal)
            return const_cast<TranslationTable *>(&tt.table);
    }

    return currentTable ? currentTable : const_cast<TranslationTable *>(&tabs.first().table);
}

int MainWindow::tableIndexForViewMode(bool showOriginal) const
{
    if (!m_tablesDock)
        return -1;

    const auto &tabs = m_tablesDock->allTables();
    if (tabs.isEmpty())
        return -1;

    const int curIdx = m_tablesDock->currentIndex();
    if (curIdx >= 0 && curIdx < tabs.size() && tabs[curIdx].isOriginal == showOriginal)
        return curIdx;

    for (int i = 0; i < tabs.size(); ++i) {
        if (tabs[i].isOriginal == showOriginal)
            return i;
    }

    return -1;
}

TranslationTable *MainWindow::selectedTable() const
{
    if (!m_tablesDock)
        return nullptr;

    const auto &tabs = m_tablesDock->allTables();
    if (tabs.isEmpty())
        return nullptr;

    const int curIdx = m_tablesDock->currentIndex();
    if (curIdx >= 0 && curIdx < tabs.size())
        return const_cast<TranslationTable *>(&tabs[curIdx].table);

    return const_cast<TranslationTable *>(&tabs.first().table);
}

void MainWindow::applySelectedTable()
{
    if (!hexEdit)
        return;

    if (!(useTableAct && useTableAct->isChecked())) {
        hexEdit->removeTranslationTable();
        return;
    }

    TranslationTable *table = selectedTable();
    if (table)
        hexEdit->setTranslationTable(table);
    else
        hexEdit->removeTranslationTable();
}

void MainWindow::applyTranslationTableForViewMode()
{
    if (!hexEdit)
        return;

    if (!(useTableAct && useTableAct->isChecked())) {
        hexEdit->removeTranslationTable();
        return;
    }

    TranslationTable *table = tableForViewMode(hexEdit->showOriginal());
    if (table)
        hexEdit->setTranslationTable(table);
    else
        hexEdit->removeTranslationTable();
}

bool MainWindow::shouldSwitchTableOnViewModeChange() const
{
    if (!m_tablesDock)
        return false;

    const auto &tabs = m_tablesDock->allTables();
    
    // If there's only one table or no tables, no need to switch
    if (tabs.size() <= 1)
        return false;

    // Check if there are tables with different isOriginal values
    bool hasOriginal = false;
    bool hasNonOriginal = false;
    
    for (const auto &tt : tabs) {
        if (tt.isOriginal)
            hasOriginal = true;
        else
            hasNonOriginal = true;
            
        if (hasOriginal && hasNonOriginal)
            return true;
    }
    
    // If we don't have both types, no need to switch
    return false;
}

void MainWindow::enforceBottomDockEqualWidth()
{
    if (!m_pointersDock || !m_changesDock)
        return;
    if (!m_pointersDock->isVisible() || !m_changesDock->isVisible())
        return;
    if (m_pointersDock->isFloating() || m_changesDock->isFloating())
        return;
    if (dockWidgetArea(m_pointersDock) != Qt::BottomDockWidgetArea ||
        dockWidgetArea(m_changesDock) != Qt::BottomDockWidgetArea)
        return;

    const int halfWidth = qMax(200, width() / 2);
    resizeDocks({m_pointersDock, m_changesDock}, {halfWidth, halfWidth}, Qt::Horizontal);
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

void MainWindow::onRomTypeChanged(int index)
{
    m_detectedRomType = static_cast<RomType>(cbRomType->itemData(index).toInt());
    m_pointerOffset = defaultPointerOffset(m_detectedRomType);
    m_pointerSize = defaultPointerSize(m_detectedRomType);
    if (m_document) {
        m_document->pointerOffset = m_pointerOffset;
        m_document->pointerSize = m_pointerSize;
    }

    // Update byte order to match the new ROM type
    hexEdit->byteOrder = defaultByteOrder(m_detectedRomType);
    updateEndiannesLabel();
    setAddress(hexEdit->getCurrentOffset());
    syncRomTypeMenu(index);
}

void MainWindow::repopulateRomTypeCombo()
{
    if (!cbRomType) return;
    const int savedIndex = cbRomType->currentIndex();
    const QSignalBlocker blocker(cbRomType);
    cbRomType->clear();
    cbRomType->addItem(tr("Unknown"), 0);
    for (int i = 1; i < kRomTypeCount; ++i)
        cbRomType->addItem(tr(romTypeName(static_cast<RomType>(i))), i);
    cbRomType->setCurrentIndex(savedIndex >= 0 ? savedIndex : 0);
}

void MainWindow::onMenuRomTypeTriggered(QAction *action)
{
    int index = action->data().toInt();
    const QSignalBlocker blocker(cbRomType);
    cbRomType->setCurrentIndex(index);
    m_detectedRomType = static_cast<RomType>(index);
    m_pointerOffset = defaultPointerOffset(m_detectedRomType);
    m_pointerSize = defaultPointerSize(m_detectedRomType);
    if (m_document) {
        m_document->pointerOffset = m_pointerOffset;
        m_document->pointerSize = m_pointerSize;
    }
    hexEdit->byteOrder = defaultByteOrder(m_detectedRomType);
    updateEndiannesLabel();
    setAddress(hexEdit->getCurrentOffset());
}

void MainWindow::setCurrentPointerOffset(qint64 offset)
{
    m_pointerOffset = offset;
    if (m_document)
        m_document->pointerOffset = m_pointerOffset;
    if (!m_restoringProjectUi && m_document && !m_document->projectFilePath.isEmpty())
        m_projectModified = true;
}

void MainWindow::setCurrentPointerSize(int size)
{
    if (size != 2 && size != 4)
        return;
    m_pointerSize = size;
    if (m_document)
        m_document->pointerSize = m_pointerSize;
    if (!m_restoringProjectUi && m_document && !m_document->projectFilePath.isEmpty())
        m_projectModified = true;
}

void MainWindow::syncRomTypeMenu(int index)
{
    if (!romTypeMenuGroup) return;
    const auto actions = romTypeMenuGroup->actions();
    if (index >= 0 && index < actions.size())
        actions[index]->setChecked(true);
}

void MainWindow::onEncodingTriggered(QAction *action)
{
    m_currentEncoding = action->data().toString();
    hexEdit->setCurrentEncoding(m_currentEncoding);
    if (lbEncoding)
        lbEncoding->setText(m_currentEncoding);
}

void MainWindow::syncEncodingMenu()
{
    if (!encodingGroup) return;
    for (QAction *a : encodingGroup->actions()) {
        if (a->data().toString() == m_currentEncoding) {
            a->setChecked(true);
            break;
        }
    }
}

void MainWindow::openEncodingSelectionDialog()
{
    if (!encodingGroup || !hexEdit)
        return;

    const QList<QAction *> actions = encodingGroup->actions();
    if (actions.isEmpty())
        return;

    QStringList values;
    values.reserve(actions.size());
    int currentIndex = 0;

    for (int i = 0; i < actions.size(); ++i) {
        const QString value = actions[i]->data().toString();
        values.append(value);
        if (value == m_currentEncoding)
            currentIndex = i;
    }

    bool ok = false;
    const QString selected = QInputDialog::getItem(
        this,
        tr("Encoding"),
        tr("Select current encoding:"),
        values,
        currentIndex,
        false,
        &ok);

    if (!ok || selected.isEmpty() || selected == m_currentEncoding)
        return;

    for (QAction *action : actions) {
        if (action->data().toString() == selected) {
            onEncodingTriggered(action);
            syncEncodingMenu();
            break;
        }
    }
}
